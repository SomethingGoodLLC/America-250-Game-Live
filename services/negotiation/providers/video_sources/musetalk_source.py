"""MuseTalk 1.5 file-per-line video source.

One MP4 per reply: audio WAV + portrait still -> talking-head MP4. No live frame streaming, so the
frame APIs required by BaseVideoSource are deliberately empty. The service announces the result
with the existing ``sadtalker_video_ready`` control event.

Configuration (environment):
  MUSETALK_HOME      checkout of TMElyralab/MuseTalk (see scripts/install_musetalk.sh)
  MUSETALK_PYTHON    interpreter of the MuseTalk venv (default: $MUSETALK_HOME/.venv/bin/python)
  MUSETALK_VERSION   "v15" (default)
  MUSETALK_TIMEOUT   seconds (default 300)
"""

from __future__ import annotations

import asyncio
import os
import shutil
import tempfile
from collections.abc import AsyncGenerator, AsyncIterator
from pathlib import Path

import numpy as np
import structlog

from ..types import VideoSourceConfig
from .base import BaseVideoSource, VideoFrame

logger = structlog.get_logger(__name__)

UNET_V15 = "models/musetalkV15/unet.pth"
UNET_V15_CONFIG = "models/musetalkV15/musetalk.json"


class MuseTalkVideoSource(BaseVideoSource):
    def __init__(self, config: VideoSourceConfig | None = None, home: str | None = None,
                 python: str | None = None) -> None:
        super().__init__(config or VideoSourceConfig(source_type="musetalk"))
        home_str = home or os.getenv("MUSETALK_HOME")
        self.home: Path | None = Path(home_str).expanduser() if home_str else None
        py = python or os.getenv("MUSETALK_PYTHON")
        if not py and self.home:
            py = str(self.home / ".venv" / "bin" / "python")
        self.python = py
        self.timeout = float(os.getenv("MUSETALK_TIMEOUT", "300"))

    # -- availability ------------------------------------------------------
    def unavailable_reason(self) -> str | None:
        if os.getenv("MUSETALK_DISABLE") == "1":
            return "MUSETALK_DISABLE=1"
        if not self.home or not self.home.is_dir():
            return "MUSETALK_HOME not set or missing (run scripts/install_musetalk.sh)"
        if not (self.home / "scripts" / "inference.py").exists():
            return "MuseTalk checkout lacks scripts/inference.py"
        if not (self.home / UNET_V15).exists():
            return f"MuseTalk weights missing: {UNET_V15}"
        if not self.python or not Path(self.python).exists():
            return "MuseTalk python interpreter missing"
        return None

    def available(self) -> bool:
        return self.unavailable_reason() is None

    # -- file-per-line rendering ------------------------------------------
    async def render_line(self, audio_file: str, portrait: str, out_mp4: str) -> str | None:
        """Render one talking-head MP4. Returns the output path, or None on any failure."""
        reason = self.unavailable_reason()
        if reason:
            logger.info("MuseTalk unavailable", reason=reason)
            return None
        assert self.home is not None and self.python is not None
        work = Path(tempfile.mkdtemp(prefix="musetalk_"))
        try:
            cfg = work / "line.yaml"
            cfg.write_text(
                "task_0:\n"
                f'  video_path: "{Path(portrait).resolve()}"\n'
                f'  audio_path: "{Path(audio_file).resolve()}"\n'
            )
            cmd = [
                self.python, "-m", "scripts.inference",
                "--inference_config", str(cfg),
                "--result_dir", str(work / "out"),
                "--unet_model_path", UNET_V15,
                "--unet_config", UNET_V15_CONFIG,
                "--version", os.getenv("MUSETALK_VERSION", "v15"),
            ]
            proc = await asyncio.create_subprocess_exec(
                *cmd, cwd=str(self.home),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT,
            )
            try:
                out, _ = await asyncio.wait_for(proc.communicate(), timeout=self.timeout)
            except TimeoutError:
                proc.kill()
                await proc.wait()
                logger.error("MuseTalk timed out", timeout=self.timeout)
                return None
            except asyncio.CancelledError:  # client left mid-render: do not leave an orphan GPU job
                proc.kill()
                raise
            if proc.returncode != 0:
                logger.error("MuseTalk failed", rc=proc.returncode, tail=out.decode(errors="replace")[-400:])
                return None
            mp4s = sorted((work / "out").rglob("*.mp4"), key=lambda p: p.stat().st_mtime)
            if not mp4s:
                logger.error("MuseTalk produced no MP4")
                return None
            Path(out_mp4).parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(mp4s[-1]), out_mp4)
            return out_mp4
        except Exception as e:  # never let a renderer crash the negotiation
            logger.error("MuseTalk render error", error=str(e))
            return None
        finally:
            shutil.rmtree(work, ignore_errors=True)

    # -- BaseVideoSource (no live frames in this slice) --------------------
    async def start(self) -> None:
        self._is_running = True

    async def stop(self) -> None:
        self._is_running = False

    async def get_frame(self) -> VideoFrame | None:
        return None

    async def frames(self) -> AsyncIterator[np.ndarray]:
        """File-per-line source: no live frames (AvatarVideoTrack falls back on NotImplementedError)."""
        raise NotImplementedError("MuseTalk renders one MP4 per line; use render_line()")

    async def stream_frames(self) -> AsyncGenerator[VideoFrame, None]:
        raise NotImplementedError("MuseTalk renders one MP4 per line; use render_line()")
