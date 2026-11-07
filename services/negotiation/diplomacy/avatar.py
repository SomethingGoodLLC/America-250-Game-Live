"""File-per-line avatar pipeline for the Unreal client (no WebRTC).

TTS -> WAV on disk -> video: MuseTalk, else D-ID if keyed, else audio-only.
Result is announced by the caller as a ``sadtalker_video_ready`` control event.
"""

from __future__ import annotations

import asyncio
import os
import shutil
import wave
from dataclasses import dataclass
from pathlib import Path

import structlog

from providers.video_sources.musetalk_source import MuseTalkVideoSource

logger = structlog.get_logger(__name__)

SERVICE_DIR = Path(__file__).resolve().parent.parent
AUDIO_DIR = SERVICE_DIR / "generated_audio"
VIDEO_DIR = SERVICE_DIR / "generated_video"
DEFAULT_PORTRAIT = SERVICE_DIR / "assets" / "avatars" / "british_general_frame.jpg"


@dataclass
class AvatarLine:
    audio_file: str | None
    video_file: str | None
    backend: str  # "musetalk" | "did" | "audio_only" | "none"
    notes: list[str]


async def _run(*cmd: str) -> bool:
    proc = await asyncio.create_subprocess_exec(
        *cmd, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL
    )
    try:
        return (await proc.wait()) == 0
    except asyncio.CancelledError:  # client went away mid-render: do not leave orphan children
        proc.kill()
        raise


async def _tts_say(text: str, wav_path: Path) -> bool:
    """Offline macOS TTS fallback: `say` -> AIFF -> `afconvert` -> 16-bit WAV."""
    if not shutil.which("say") or not shutil.which("afconvert"):
        return False
    aiff = wav_path.with_suffix(".aiff")
    try:
        if not await _run("say", "-o", str(aiff), text):
            return False
        return await _run("afconvert", "-f", "WAVE", "-d", "LEI16@22050", str(aiff), str(wav_path))
    finally:
        aiff.unlink(missing_ok=True)


async def _tts_elevenlabs(text: str, wav_path: Path) -> bool:
    key = os.getenv("ELEVENLABS_API_KEY", "")
    if not key:
        return False
    try:
        from tts.elevenlabs import ElevenLabsProvider

        prov = ElevenLabsProvider({"api_key": key, "voice_id": os.getenv("ELEVENLABS_VOICE_ID", "JBFqnCBsd6RMkjVDRZzb"),
                                   "model": os.getenv("ELEVENLABS_MODEL", "eleven_multilingual_v2")})
        pcm = b""
        async for chunk in prov.synthesize_speech(text):
            pcm += chunk
        if not pcm:
            return False
        with wave.open(str(wav_path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(int(getattr(prov, "sample_rate", 44100)))
            w.writeframes(pcm)
        return True
    except Exception as e:
        logger.warning("ElevenLabs TTS failed", error=str(e))
        return False


async def synthesize_line_audio(text: str, job_id: str) -> str | None:
    """PA250_TTS: 'auto' (ElevenLabs if keyed, else say), 'say', 'elevenlabs', 'off'."""
    mode = os.getenv("PA250_TTS", "auto").lower()
    if mode == "off" or not text.strip():
        return None
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    wav = AUDIO_DIR / f"{job_id}.wav"
    if mode in ("auto", "elevenlabs") and await _tts_elevenlabs(text, wav):
        return str(wav)
    if mode in ("auto", "say") and await _tts_say(text, wav):
        return str(wav)
    return None


async def _did_video(text: str, portrait: Path, out_mp4: Path) -> str | None:
    if not os.getenv("DID_API_KEY") or os.getenv("PA250_DID", "1") == "0":
        return None
    try:
        from video_providers.did_video import DIDVideoProvider

        path = await DIDVideoProvider().generate_video(text=text, image_url=str(portrait), output_path=out_mp4)
        return str(path) if path else None
    except Exception as e:
        logger.warning("D-ID failed; falling back to audio-only", error=str(e))
        return None


async def render_avatar_line(text: str, job_id: str, portrait: Path | None = None) -> AvatarLine:
    """Run the preference chain. Never raises."""
    notes: list[str] = []
    portrait = portrait or DEFAULT_PORTRAIT
    audio = await synthesize_line_audio(text, job_id)
    if audio is None:
        notes.append("no audio (TTS off or unavailable)")
        return AvatarLine(None, None, "none", notes)
    VIDEO_DIR.mkdir(parents=True, exist_ok=True)
    out_mp4 = VIDEO_DIR / f"{job_id}.mp4"

    if os.getenv("PA250_AVATAR", "auto").lower() in ("auto", "musetalk"):
        mt = MuseTalkVideoSource()
        reason = mt.unavailable_reason()
        if reason is None:
            video = await mt.render_line(audio, str(portrait), str(out_mp4))
            if video:
                return AvatarLine(audio, video, "musetalk", notes)
            notes.append("MuseTalk render failed")
        else:
            notes.append(f"MuseTalk skipped: {reason}")
    if os.getenv("PA250_AVATAR", "auto").lower() in ("auto", "did"):
        video = await _did_video(text, portrait, out_mp4)
        if video:
            return AvatarLine(audio, video, "did", notes)
        notes.append("D-ID skipped or failed")
    return AvatarLine(audio, None, "audio_only", notes)
