"""File-per-line avatar pipeline for the Unreal client (no WebRTC).

TTS -> WAV on disk -> video: MuseTalk, else D-ID if keyed, else audio-only.
Result is announced by the caller as a ``sadtalker_video_ready`` control event.

Every outcome is logged: INFO for the backend, duration and path that were produced, WARNING for each
failure with its reason (ElevenLabs HTTP status + body, ``say``/``afconvert`` exit code + stderr).
"""

from __future__ import annotations

import asyncio
import base64
import os
import shutil
import time
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import httpx
import structlog
from ruamel.yaml import YAML

from diplomacy.visemes import approximate_alignment, curves_from_alignment
from providers.video_sources.musetalk_source import MuseTalkVideoSource

logger = structlog.get_logger(__name__)

SERVICE_DIR = Path(__file__).resolve().parent.parent
AUDIO_DIR = SERVICE_DIR / "generated_audio"
VIDEO_DIR = SERVICE_DIR / "generated_video"
DEFAULT_PORTRAIT = SERVICE_DIR / "assets" / "avatars" / "british_general_frame.jpg"
VOICES_FILE = Path(__file__).resolve().parent / "envoy_voices.yaml"

ELEVENLABS_URL = "https://api.elevenlabs.io/v1/text-to-speech/{voice_id}/with-timestamps"  # JSON: audio + per-character times
ELEVENLABS_RATE = (
    24000  # output_format=pcm_24000: raw 16-bit little-endian mono PCM at 24 kHz
)
DEFAULT_ELEVENLABS_VOICE = (
    "JBFqnCBsd6RMkjVDRZzb"  # George (premade) when ELEVENLABS_VOICE_ID is unset
)
DEFAULT_ELEVENLABS_MODEL = "eleven_multilingual_v2"
# This account allows 3 parallel requests; a reply streamed as 6 sentences got a 429 on the overflow and that
# sentence fell back to macOS `say` (a different voice mid-reply). Queue here instead, and retry a 429.
_ELEVEN_SLOTS: dict[Any, asyncio.Semaphore] = {}
TAIL_SILENCE_S = 0.08  # ElevenLabs pads each clip with ~0.35 s of silence; with the room's 0.25 s drain wait
# that left ~0.6 s holes between the sentences of one reply.


def _eleven_slot() -> asyncio.Semaphore:
    loop = asyncio.get_running_loop()  # one semaphore per event loop (tests run several)
    if loop not in _ELEVEN_SLOTS:
        _ELEVEN_SLOTS[loop] = asyncio.Semaphore(max(1, int(os.getenv("ELEVENLABS_CONCURRENCY", "2"))))
    return _ELEVEN_SLOTS[loop]


def trim_tail_silence(pcm: bytes, rate: int, keep_s: float = TAIL_SILENCE_S) -> bytes:
    """Cut trailing near-silence from 16-bit mono PCM, keeping `keep_s`. The start is untouched, so the
    character timings used for lip-sync stay valid."""
    import array

    a = array.array("h")
    a.frombytes(pcm[: len(pcm) & ~1])
    peak = max((abs(v) for v in a[:: max(1, len(a) // 20000)]), default=0)
    floor = max(200, int(peak * 0.02))
    end = len(a)
    while end > 0 and abs(a[end - 1]) <= floor:
        end -= 1
    if end == 0:
        return pcm
    return a[: min(len(a), end + int(keep_s * rate))].tobytes()


@dataclass
class AvatarLine:
    audio_file: str | None
    video_file: str | None
    backend: str  # "musetalk" | "did" | "audio_only" | "none"
    notes: list[str]
    tts_backend: str = "none"  # "elevenlabs" | "say" | "none"
    audio_seconds: float = 0.0
    visemes: dict[str, Any] | None = None  # 30 Hz jaw/wide/round/press curves for the painting (display only)


@dataclass
class EnvoyVoice:
    envoy_id: str
    voice_id: str
    model: str
    settings: dict[str, Any] = field(default_factory=dict)
    say_voice: str | None = None
    source: str = "default"  # "table" | "default"


def voice_for(envoy_id: str) -> EnvoyVoice:
    """Envoy id -> voice from ``envoy_voices.yaml``; ELEVENLABS_VOICE_ID is only the default."""
    default = EnvoyVoice(
        envoy_id,
        os.getenv("ELEVENLABS_VOICE_ID") or DEFAULT_ELEVENLABS_VOICE,
        os.getenv("ELEVENLABS_MODEL") or DEFAULT_ELEVENLABS_MODEL,
    )
    try:
        table = YAML(typ="safe").load(VOICES_FILE.read_text()) or {}
    except Exception as e:
        logger.warning(
            "Envoy voice table unreadable; using default voice",
            path=str(VOICES_FILE),
            error=str(e),
        )
        return default
    row = (table.get("envoys") or {}).get(envoy_id)
    if not isinstance(row, dict) or not row.get("voice_id"):
        return default
    return EnvoyVoice(
        envoy_id,
        str(row["voice_id"]),
        str(row.get("model") or default.model),
        dict(row.get("settings") or {}),
        row.get("say_voice"),
        "table",
    )


def wav_seconds(path: str | Path) -> float:
    try:
        with wave.open(str(path), "rb") as w:
            return w.getnframes() / float(w.getframerate() or 1)
    except Exception:
        return 0.0


async def _run(*cmd: str) -> tuple[int, str]:
    """Run a child process; returns (exit code, stderr tail). Kills the child if cancelled."""
    proc = await asyncio.create_subprocess_exec(
        *cmd, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE
    )
    try:
        _, err = await proc.communicate()
        return proc.returncode or 0, err.decode(errors="replace").strip()[-400:]
    except (
        asyncio.CancelledError
    ):  # only at service shutdown: do not leave orphan children
        proc.kill()
        raise


async def _tts_say(text: str, wav_path: Path, voice: EnvoyVoice, previous_text: str | None = None) -> dict[str, Any] | None:
    """Offline macOS TTS backup: `say` -> AIFF -> `afconvert` -> 16-bit WAV."""
    if not shutil.which("say") or not shutil.which("afconvert"):
        logger.warning("say TTS unavailable", reason="say or afconvert not on PATH")
        return None
    aiff = wav_path.with_suffix(".aiff")
    try:
        cmd = (
            ["say", "-o", str(aiff)]
            + (["-v", voice.say_voice] if voice.say_voice else [])
            + [text]
        )
        rc, err = await _run(*cmd)
        if rc != 0:
            logger.warning(
                "say TTS failed", exit_code=rc, stderr=err, say_voice=voice.say_voice
            )
            return None
        rc, err = await _run(
            "afconvert", "-f", "WAVE", "-d", "LEI16@22050", str(aiff), str(wav_path)
        )
        if rc != 0:
            logger.warning("afconvert failed", exit_code=rc, stderr=err)
            return None
        return {}
    finally:
        aiff.unlink(missing_ok=True)


async def _tts_elevenlabs(text: str, wav_path: Path, voice: EnvoyVoice, previous_text: str | None = None) -> dict[str, Any] | None:
    """ElevenLabs `/with-timestamps`, output_format=pcm_24000 -> 24 kHz mono 16-bit WAV (no decode).

    Returns {"alignment": ...} (per-character start/end times for lip-sync) or None on failure.
    `previous_text` keeps the intonation continuous when a reply is spoken sentence by sentence.
    """
    key = os.getenv("ELEVENLABS_API_KEY", "")
    if not key:
        logger.warning("ElevenLabs TTS skipped", reason="ELEVENLABS_API_KEY not set")
        return None
    body: dict[str, Any] = {"text": text, "model_id": voice.model}
    if voice.settings:
        body["voice_settings"] = voice.settings
    if previous_text:
        body["previous_text"] = previous_text
    timeout = float(os.getenv("ELEVENLABS_TIMEOUT", "30"))
    try:
        async with _eleven_slot(), httpx.AsyncClient(timeout=timeout) as client:
            for attempt in range(4):
                r = await client.post(
                    ELEVENLABS_URL.format(voice_id=voice.voice_id),
                    params={"output_format": f"pcm_{ELEVENLABS_RATE}"},
                    headers={"xi-api-key": key},
                    json=body,
                )
                if r.status_code != 429 or attempt == 3:
                    break
                logger.info("ElevenLabs busy (429); retrying", attempt=attempt + 1, envoy=voice.envoy_id)
                await asyncio.sleep(0.4 * (attempt + 1))
    except httpx.HTTPError as e:
        logger.warning(
            "ElevenLabs TTS failed",
            reason="request error",
            error=f"{type(e).__name__}: {e}",
            voice_id=voice.voice_id,
            envoy=voice.envoy_id,
        )
        return None
    if r.status_code != 200:
        logger.warning(
            "ElevenLabs TTS failed",
            http_status=r.status_code,
            body=r.text[:600],
            voice_id=voice.voice_id,
            voice_source=voice.source,
            envoy=voice.envoy_id,
        )
        return None
    try:
        data = r.json()
        pcm = base64.b64decode(data.get("audio_base64") or "")
    except (ValueError, TypeError) as e:
        logger.warning("ElevenLabs TTS failed", reason="unreadable with-timestamps body", error=str(e), body=r.text[:300])
        return None
    if len(pcm) < 2:
        logger.warning("ElevenLabs TTS failed", reason="empty audio", body=r.text[:300])
        return None
    pcm = trim_tail_silence(pcm, ELEVENLABS_RATE)
    with wave.open(str(wav_path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(ELEVENLABS_RATE)
        w.writeframes(pcm)
    return {"alignment": data.get("alignment")}


async def synthesize_line_audio(
    text: str, job_id: str, envoy_id: str = "default", previous_text: str | None = None
) -> tuple[str, str, dict[str, Any] | None] | None:
    """PA250_TTS: 'auto' (ElevenLabs if keyed, else say), 'say', 'elevenlabs', 'off'.

    Returns (wav path, backend, ElevenLabs alignment or None) or None. Logs the backend, duration and path,
    or why nothing was made.
    """
    mode = os.getenv("PA250_TTS", "auto").lower()
    if mode == "off" or not text.strip():
        logger.info(
            "TTS skipped",
            job_id=job_id,
            reason="PA250_TTS=off" if mode == "off" else "empty line",
        )
        return None
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    wav = AUDIO_DIR / f"{job_id}.wav"
    voice = voice_for(envoy_id)
    for backend, fn in (("elevenlabs", _tts_elevenlabs), ("say", _tts_say)):
        if mode not in ("auto", backend):
            continue
        t0 = time.monotonic()
        meta = await fn(text, wav, voice, previous_text)
        if meta is not None:
            logger.info(
                "TTS done",
                job_id=job_id,
                tts_backend=backend,
                audio_seconds=round(wav_seconds(wav), 2),
                path=str(wav),
                render_s=round(time.monotonic() - t0, 2),
                envoy=envoy_id,
                voice_id=voice.voice_id if backend == "elevenlabs" else voice.say_voice,
            )
            return str(wav), backend, meta.get("alignment")
    logger.warning("TTS produced no audio", job_id=job_id, mode=mode, envoy=envoy_id)
    return None


def _did_unavailable_reason() -> str | None:
    if os.getenv("PA250_DID", "1") == "0":
        return "PA250_DID=0"
    if not os.getenv("DID_API_KEY"):
        return "DID_API_KEY not set"
    return None


async def _did_video(audio_file: str, portrait: Path, out_mp4: Path) -> str | None:
    """D-ID lip-syncs OUR WAV (uploaded via POST /audios, script type "audio"); never D-ID's built-in voice."""
    if _did_unavailable_reason():
        return None
    try:
        from video_providers.did_video import DIDVideoProvider

        prov = DIDVideoProvider()
        audio_url = await prov.upload_audio(Path(audio_file))
        path = await prov.generate_video(
            audio_url=audio_url, image_url=str(portrait), output_path=out_mp4
        )
        return str(path) if path else None
    except Exception as e:
        logger.warning("D-ID failed", error=f"{type(e).__name__}: {e}"[:600])
        return None


def _musetalk_remote_reason() -> str:
    return "not built yet" + (
        " (MUSETALK_REMOTE_URL is set but ignored)"
        if os.getenv("MUSETALK_REMOTE_URL")
        else ""
    )


def backend_status() -> list[dict[str, Any]]:
    """Each video backend in chain order with availability and reason (no secrets)."""
    from providers.video_sources.fal_talking_source import FalTalkingVideoSource

    fal = FalTalkingVideoSource()
    rows: list[dict[str, Any]] = [
        {
            "name": "musetalk_local",
            "reason": MuseTalkVideoSource().unavailable_reason(),
        },
        {"name": "musetalk_remote", "reason": _musetalk_remote_reason()},
        {
            "name": "fal_talking",
            "reason": fal.unavailable_reason(),
            "model": fal.model_id,
            "resolution": fal.resolution or None,
        },
        {"name": "did", "reason": _did_unavailable_reason()},
        {"name": "audio_only", "reason": None},
    ]
    mode = os.getenv("PA250_AVATAR", "auto").lower()
    for r in rows:
        r["available"] = r["reason"] is None
        r["enabled"] = (
            r["name"] == "audio_only" or mode == "auto" or r["name"].startswith(mode)
        )
    return rows


async def render_avatar_line(
    text: str, job_id: str, portrait: Path | None = None, envoy_id: str = "default", previous_text: str | None = None
) -> AvatarLine:
    """TTS, then video: musetalk_local -> musetalk_remote (not built) -> fal_talking -> did -> audio_only.

    PA250_AVATAR selects one backend (musetalk | fal | did) or 'auto' for the chain; 'none' is voice only.
    The whole video stage runs under PA250_AVATAR_TIMEOUT (default 90 s); when it runs out the line goes
    out audio-only. Never raises (except CancelledError).
    """
    notes: list[str] = []
    portrait = portrait or DEFAULT_PORTRAIT
    tts = await synthesize_line_audio(text, job_id, envoy_id=envoy_id, previous_text=previous_text)
    if tts is None:
        notes.append("no audio (TTS off or unavailable)")
        return AvatarLine(None, None, "none", notes)
    audio, tts_backend, *rest = tts
    alignment = rest[0] if rest else None
    seconds = wav_seconds(audio)
    visemes = curves_from_alignment(alignment, seconds)
    if visemes is None:  # `say` backup or no timestamps: spread the letters over the clip
        visemes = curves_from_alignment(approximate_alignment(text, seconds), seconds)
        if visemes is not None:
            visemes["approx"] = True
    VIDEO_DIR.mkdir(parents=True, exist_ok=True)
    out_mp4 = VIDEO_DIR / f"{job_id}.mp4"
    mode = os.getenv("PA250_AVATAR", "auto").lower()

    def done(video: str | None, backend: str) -> AvatarLine:
        return AvatarLine(audio, video, backend, notes, tts_backend, seconds, visemes)

    async def chain() -> AvatarLine:
        if mode in ("auto", "musetalk"):
            mt = MuseTalkVideoSource()
            reason = mt.unavailable_reason()
            if reason is None:
                video = await mt.render_line(audio, str(portrait), str(out_mp4))
                if video:
                    return done(video, "musetalk")
                notes.append("musetalk_local render failed")
            else:
                notes.append(f"musetalk_local skipped: {reason}")
            notes.append(f"musetalk_remote skipped: {_musetalk_remote_reason()}")
        if mode in ("auto", "fal"):
            from providers.video_sources.fal_talking_source import FalTalkingVideoSource

            fal = FalTalkingVideoSource()
            reason = fal.unavailable_reason()
            if reason is None:
                r = await fal.render(audio, str(portrait), str(out_mp4))
                if r.path:
                    notes.append(
                        f"fal_talking {r.model}: {r.seconds:.2f}s video, est ${r.est_cost_usd:.2f}, "
                        f"render {r.render_s:.1f}s{' (cache hit)' if r.cache_hit else ''}"
                    )
                    return done(r.path, "fal_talking")
                notes.append(f"fal_talking failed: {r.error}")
            else:
                notes.append(f"fal_talking skipped: {reason}")
        if mode in ("auto", "did"):
            reason = _did_unavailable_reason()
            if reason is None:
                video = await _did_video(audio, portrait, out_mp4)
                if video:
                    return done(video, "did")
                notes.append("did failed")
            else:
                notes.append(f"did skipped: {reason}")
        return done(None, "audio_only")

    if mode in ("none", "off", "audio_only"):
        return done(None, "audio_only")
    if os.getenv("PA250_VIDEO_WAIT", "0") != "1":
        # Voice first: renders take 35 s+ (fal) or minutes (MuseTalk on this Mac), far longer than a reply may
        # wait. Use a talking-portrait video only when this exact line is already rendered; otherwise speak now.
        if mode in ("auto", "fal"):
            from providers.video_sources.fal_talking_source import FalTalkingVideoSource

            hit = FalTalkingVideoSource().cached_path(audio, str(portrait))
            if hit:
                shutil.copyfile(hit, out_mp4)
                notes.append("fal_talking cache hit")
                return done(str(out_mp4), "fal_talking")
        notes.append(
            "voice first: no cached video (PA250_VIDEO_WAIT=1 waits for a render)"
        )
        return done(None, "audio_only")
    budget = float(os.getenv("PA250_AVATAR_TIMEOUT", "90"))
    t0 = time.monotonic()
    try:
        line = await asyncio.wait_for(chain(), timeout=budget)
    except TimeoutError:
        notes.append(f"video timed out after {budget:.0f}s (PA250_AVATAR_TIMEOUT)")
        logger.warning(
            "Avatar video timed out; sending audio-only",
            job_id=job_id,
            timeout_s=budget,
            notes=notes,
        )
        return done(None, "audio_only")
    logger.info(
        "Avatar video",
        job_id=job_id,
        backend=line.backend,
        video_file=line.video_file,
        elapsed_s=round(time.monotonic() - t0, 2),
        notes=notes,
    )
    return line
