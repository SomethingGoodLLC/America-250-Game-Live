"""fal.ai talking-portrait video source (file-per-line, behind BaseVideoSource).

Per line: portrait still + the envoy's WAV -> talking-head MP4 at generated_video/{job_id}.mp4,
H.264 + AAC with the original voice muxed in. No live frame streaming (same shape as MuseTalk).

Configuration (environment):
  FAL_KEY               fal API key (required; never logged)
  FAL_TALKING_MODEL     "veed/fabric-1.0" (default) | "fal-ai/bytedance/omnihuman"
  FAL_RESOLUTION        Fabric only: "480p" (default) | "720p"
  FAL_MAX_CLIP_SECONDS  longer lines are split at sentence pauses (default 30; OmniHuman caps below 30)
  PA250_FAL=0           disable this backend

Uploads are short-lived: the portrait expires after 1 day on fal storage (its URL is cached by file hash in
generated_video/source_cache.json and reused for 23 h), per-line audio after 1 hour.
Render cache: generated_video/cache/{sha256(model, resolution, portrait bytes, WAV bytes)}.mp4 -> no fal call.
Prices (fal model pages, 2026-11-09): Fabric 1.0 $0.08/s at 480p, $0.15/s at 720p; OmniHuman $0.14/s.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import os
import shutil
import tempfile
import time
import wave
from collections.abc import AsyncGenerator, AsyncIterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Final

import httpx
import numpy as np
import structlog

from ..types import VideoSourceConfig
from .base import BaseVideoSource, VideoFrame

logger = structlog.get_logger(__name__)

SERVICE_DIR = Path(__file__).resolve().parents[2]
VIDEO_DIR = SERVICE_DIR / "generated_video"
SOURCE_CACHE = VIDEO_DIR / "source_cache.json"
RENDER_CACHE = VIDEO_DIR / "cache"
PORTRAIT_EXPIRES: Final = "1d"
PORTRAIT_REUSE_S = 23 * 3600  # re-upload before fal's 1-day expiry
AUDIO_EXPIRES: Final = "1h"


@dataclass(frozen=True)
class FalModel:
    app: str
    usd_per_s: dict[
        str, float
    ]  # resolution ("" if the model has none) -> USD per output second
    max_clip_s: float | None = None  # model's own audio limit
    args: dict[str, Any] = field(default_factory=dict)

    def rate(self, resolution: str) -> float:
        return self.usd_per_s.get(resolution, self.usd_per_s.get("", 0.0))


MODELS: dict[str, FalModel] = {
    "veed/fabric-1.0": FalModel("veed/fabric-1.0", {"480p": 0.08, "720p": 0.15}),
    "fal-ai/bytedance/omnihuman": FalModel(
        "fal-ai/bytedance/omnihuman", {"": 0.14}, max_clip_s=29.5
    ),
}


@dataclass
class FalRender:
    path: str | None
    model: str
    seconds: float = 0.0
    est_cost_usd: float = 0.0
    render_s: float = 0.0
    parts: int = 0
    cache_hit: bool = False
    error: str | None = None


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _read_wav(path: str) -> tuple[np.ndarray, int, int]:
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"need 16-bit WAV, got {8 * w.getsampwidth()}-bit")
        ch, rate = w.getnchannels(), w.getframerate()
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, ch)
    return pcm, rate, ch


def _write_wav(path: Path, pcm: np.ndarray, rate: int) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.astype("<i2").tobytes())


def split_points(
    pcm: np.ndarray, rate: int, max_s: float, min_pause_s: float = 0.18
) -> list[int]:
    """Sample indices to cut at so no part exceeds max_s, preferring the middle of the latest pause.

    A pause is a run of 20 ms frames whose RMS is under 4 % of the line's peak frame RMS (sentence gaps
    in TTS output). With no pause in a window the cut is made at the limit.
    """
    mono = pcm.astype(np.float64).mean(axis=1)
    n, limit = len(mono), int(max_s * rate)
    if n <= limit:
        return []
    hop = max(1, int(0.02 * rate))
    frames = len(mono) // hop
    rms = (
        np.sqrt((mono[: frames * hop].reshape(frames, hop) ** 2).mean(axis=1))
        if frames
        else np.zeros(0)
    )
    quiet = rms < 0.04 * (rms.max() if len(rms) else 1.0)
    pauses: list[int] = []  # centre sample of each long-enough quiet run
    run_start = None
    for i, q in enumerate(np.append(quiet, False)):
        if q and run_start is None:
            run_start = i
        elif not q and run_start is not None:
            if (i - run_start) * hop >= min_pause_s * rate:
                pauses.append(((run_start + i) // 2) * hop)
            run_start = None
    cuts, start = [], 0
    while n - start > limit:
        window = [p for p in pauses if start + int(0.5 * rate) < p <= start + limit]
        cut = window[-1] if window else start + limit
        cuts.append(cut)
        start = cut
    return cuts


async def _ffmpeg(*args: str) -> tuple[int, str]:
    proc = await asyncio.create_subprocess_exec(
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        *args,
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        _, err = await proc.communicate()
    except asyncio.CancelledError:
        proc.kill()
        raise
    return proc.returncode or 0, err.decode(errors="replace").strip()[-400:]


async def probe_streams(path: str) -> dict[str, Any]:
    """ffprobe -> {"video": [codecs], "audio": [codecs], "duration": seconds}."""
    proc = await asyncio.create_subprocess_exec(
        "ffprobe",
        "-v",
        "error",
        "-show_entries",
        "stream=codec_type,codec_name:format=duration",
        "-of",
        "json",
        path,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL,
    )
    out, _ = await proc.communicate()
    info = json.loads(out or b"{}")
    streams = info.get("streams", [])
    return {
        "video": [s["codec_name"] for s in streams if s.get("codec_type") == "video"],
        "audio": [s["codec_name"] for s in streams if s.get("codec_type") == "audio"],
        "duration": float(info.get("format", {}).get("duration", 0) or 0),
    }


class FalTalkingVideoSource(BaseVideoSource):
    def __init__(
        self, config: VideoSourceConfig | None = None, model: str | None = None
    ) -> None:
        super().__init__(config or VideoSourceConfig(source_type="fal_talking"))
        self.model_id = model or os.getenv("FAL_TALKING_MODEL") or "veed/fabric-1.0"
        self.resolution = (
            os.getenv("FAL_RESOLUTION", "480p")
            if self.model_id == "veed/fabric-1.0"
            else ""
        )
        self.max_clip_s = float(os.getenv("FAL_MAX_CLIP_SECONDS", "30"))

    # -- availability ------------------------------------------------------
    def unavailable_reason(self) -> str | None:
        if os.getenv("PA250_FAL") == "0":
            return "PA250_FAL=0"
        if not os.getenv("FAL_KEY"):
            return "FAL_KEY not set"
        if self.model_id not in MODELS:
            return f"unknown FAL_TALKING_MODEL {self.model_id!r} (known: {', '.join(MODELS)})"
        if self.resolution and self.resolution not in MODELS[self.model_id].usd_per_s:
            return f"FAL_RESOLUTION {self.resolution!r} not offered by {self.model_id}"
        try:
            import fal_client  # noqa: F401
        except ImportError:
            return "fal-client not installed in the service venv"
        if not shutil.which("ffmpeg") or not shutil.which("ffprobe"):
            return "ffmpeg/ffprobe not on PATH"
        return None

    def available(self) -> bool:
        return self.unavailable_reason() is None

    # -- caches --------------------------------------------------------------
    def cache_key(self, portrait_bytes: bytes, wav_bytes: bytes) -> str:
        return _sha256(
            b"|".join(
                [
                    self.model_id.encode(),
                    self.resolution.encode(),
                    _sha256(portrait_bytes).encode(),
                    _sha256(wav_bytes).encode(),
                ]
            )
        )

    async def _portrait_url(
        self, client: Any, portrait: Path, digest: str, force: bool = False
    ) -> str:
        cache: dict[str, Any] = {}
        try:
            cache = json.loads(SOURCE_CACHE.read_text())
        except (FileNotFoundError, ValueError):
            pass
        hit = cache.get(digest)
        if (
            not force
            and hit
            and time.time() - float(hit.get("uploaded_at", 0)) < PORTRAIT_REUSE_S
        ):
            return str(hit["url"])
        import fal_client

        url = await client.upload_file(
            portrait, lifecycle=fal_client.StorageSettings(expires_in=PORTRAIT_EXPIRES)
        )
        cache[digest] = {
            "url": url,
            "uploaded_at": time.time(),
            "expires_in": PORTRAIT_EXPIRES,
            "file": portrait.name,
        }
        VIDEO_DIR.mkdir(parents=True, exist_ok=True)
        SOURCE_CACHE.write_text(json.dumps(cache, indent=1))
        logger.info(
            "fal portrait uploaded", file=portrait.name, expires_in=PORTRAIT_EXPIRES
        )
        return str(url)

    # -- rendering -------------------------------------------------------------
    def cached_path(self, audio_file: str, portrait: str) -> Path | None:
        """The render cache entry for this exact (model, portrait, WAV), or None. Makes no network call."""
        try:
            key = self.cache_key(
                Path(portrait).read_bytes(), Path(audio_file).read_bytes()
            )
        except OSError:
            return None
        hit = RENDER_CACHE / f"{key}.mp4"
        return hit if hit.exists() else None

    async def render_line(
        self, audio_file: str, portrait: str, out_mp4: str
    ) -> str | None:
        return (await self.render(audio_file, portrait, out_mp4)).path

    async def render(self, audio_file: str, portrait: str, out_mp4: str) -> FalRender:
        """Never raises (except CancelledError). The caller owns the time budget (asyncio.wait_for)."""
        t0 = time.monotonic()
        res = FalRender(None, self.model_id)
        reason = self.unavailable_reason()
        if reason:
            res.error = reason
            return res
        spec = MODELS[self.model_id]
        portrait_p = Path(portrait)
        try:
            portrait_bytes, wav_bytes = (
                portrait_p.read_bytes(),
                Path(audio_file).read_bytes(),
            )
        except OSError as e:
            res.error = f"cannot read input: {e}"
            logger.warning("fal render failed", model=self.model_id, error=res.error)
            return res
        key = self.cache_key(portrait_bytes, wav_bytes)
        cached = RENDER_CACHE / f"{key}.mp4"
        if cached.exists():
            shutil.copyfile(cached, out_mp4)
            res.path, res.cache_hit = out_mp4, True
            res.seconds = (await probe_streams(out_mp4))["duration"]
            res.render_s = time.monotonic() - t0
            logger.info(
                "fal render",
                model=self.model_id,
                cache_hit=True,
                seconds=round(res.seconds, 2),
                est_cost_usd=0.0,
                render_s=round(res.render_s, 2),
                path=out_mp4,
            )
            return res

        import fal_client

        client = fal_client.AsyncClient(key=os.environ["FAL_KEY"])
        handles: list[Any] = []
        try:
            pcm, rate, _ = _read_wav(audio_file)
            max_s = min(self.max_clip_s, spec.max_clip_s or self.max_clip_s)
            cuts = split_points(pcm, rate, max_s)
            bounds = list(zip([0, *cuts], [*cuts, len(pcm)], strict=True))
            res.parts = len(bounds)
            with tempfile.TemporaryDirectory(prefix="fal_") as tmp_s:
                tmp = Path(tmp_s)
                image_url = await self._portrait_url(
                    client, portrait_p, _sha256(portrait_bytes)
                )
                part_mp4s: list[Path] = []
                billed = 0.0
                for i, (a, b) in enumerate(bounds):
                    part_wav = tmp / f"part{i}.wav"
                    _write_wav(part_wav, pcm[a:b], rate)
                    audio_url = await client.upload_file(
                        part_wav,
                        lifecycle=fal_client.StorageSettings(expires_in=AUDIO_EXPIRES),
                    )
                    args = {"image_url": image_url, "audio_url": audio_url, **spec.args}
                    if self.resolution:
                        args["resolution"] = self.resolution
                    try:
                        out = await self._run_model(client, args, handles)
                    except fal_client.FalClientHTTPError as e:
                        if (
                            i == 0 and "image" in str(e).lower()
                        ):  # cached portrait URL gone: re-upload once
                            logger.warning(
                                "fal could not use the cached portrait URL; re-uploading",
                                error=str(e)[:300],
                            )
                            image_url = await self._portrait_url(
                                client, portrait_p, _sha256(portrait_bytes), force=True
                            )
                            out = await self._run_model(
                                client, dict(args, image_url=image_url), handles
                            )
                        else:
                            raise
                    url = (out.get("video") or {}).get("url")
                    if not url:
                        raise RuntimeError(
                            f"fal returned no video url (keys: {sorted(out)})"
                        )
                    part_mp4 = tmp / f"part{i}.mp4"
                    async with httpx.AsyncClient(
                        timeout=60, follow_redirects=True
                    ) as http:
                        r = await http.get(url)
                        r.raise_for_status()
                        part_mp4.write_bytes(r.content)
                    part_mp4s.append(part_mp4)
                    billed += float(
                        out.get("duration")
                        or (await probe_streams(str(part_mp4)))["duration"]
                    )

                # Concatenate the fal parts (video only) and mux the original voice as AAC: one H.264 + AAC file.
                listing = tmp / "parts.txt"
                listing.write_text("".join(f"file '{p}'\n" for p in part_mp4s))
                rc, err = await _ffmpeg(
                    "-f",
                    "concat",
                    "-safe",
                    "0",
                    "-i",
                    str(listing),
                    "-i",
                    audio_file,
                    "-map",
                    "0:v:0",
                    "-map",
                    "1:a:0",
                    "-c:v",
                    "libx264",
                    "-preset",
                    "veryfast",
                    "-crf",
                    "20",
                    "-pix_fmt",
                    "yuv420p",
                    "-c:a",
                    "aac",
                    "-b:a",
                    "128k",
                    "-ar",
                    "48000",
                    "-shortest",
                    "-movflags",
                    "+faststart",
                    out_mp4,
                )
                if rc != 0:
                    raise RuntimeError(f"ffmpeg mux failed (exit {rc}): {err}")
            probe = await probe_streams(out_mp4)
            if probe["video"] != ["h264"] or probe["audio"] != ["aac"]:
                raise RuntimeError(f"output streams not h264+aac: {probe}")
            RENDER_CACHE.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(out_mp4, cached)
            res.path, res.seconds = out_mp4, probe["duration"]
            res.est_cost_usd = round(billed * spec.rate(self.resolution), 4)
            res.render_s = time.monotonic() - t0
            logger.info(
                "fal render",
                model=self.model_id,
                resolution=self.resolution or None,
                cache_hit=False,
                seconds=round(res.seconds, 2),
                billed_seconds=round(billed, 2),
                parts=res.parts,
                est_cost_usd=res.est_cost_usd,
                usd_per_s=spec.rate(self.resolution),
                render_s=round(res.render_s, 2),
                path=out_mp4,
            )
            return res
        except asyncio.CancelledError:
            for h in (
                handles
            ):  # time budget ran out: stop paying for renders nobody will watch
                try:
                    await h.cancel()
                except Exception:  # noqa: BLE001 - best effort
                    pass
            logger.warning(
                "fal render cancelled",
                model=self.model_id,
                render_s=round(time.monotonic() - t0, 2),
            )
            raise
        except Exception as e:
            res.error = f"{type(e).__name__}: {e}"[:600]
            res.render_s = time.monotonic() - t0
            logger.warning(
                "fal render failed",
                model=self.model_id,
                error=res.error,
                render_s=round(res.render_s, 2),
            )
            return res

    async def _run_model(
        self, client: Any, args: dict[str, Any], handles: list[Any]
    ) -> dict[str, Any]:
        handle = await client.submit(self.model_id, arguments=args)
        handles.append(handle)
        out = await handle.get(interval=1.0)  # fal's default polls every 0.1 s
        return out if isinstance(out, dict) else {}

    # -- BaseVideoSource (no live frames: one MP4 per line) ---------------------
    async def start(self) -> None:
        self._is_running = True

    async def stop(self) -> None:
        self._is_running = False

    async def get_frame(self) -> VideoFrame | None:
        return None

    async def frames(self) -> AsyncIterator[np.ndarray]:
        raise NotImplementedError(
            "fal_talking renders one MP4 per line; use render_line()"
        )

    async def stream_frames(self) -> AsyncGenerator[VideoFrame, None]:
        raise NotImplementedError(
            "fal_talking renders one MP4 per line; use render_line()"
        )
