"""Placeholder video source that loops MP4 video files."""

import asyncio
import os
import math
from pathlib import Path
from typing import AsyncGenerator, Optional, AsyncIterator
try:
    import av
    import numpy as np
except ImportError:
    av = None
    np = None
import structlog

from .base import BaseVideoSource, VideoFrame
from ..types import VideoSourceConfig


class PlaceholderLoopVideoSource(BaseVideoSource):
    """Video source that loops MP4 video files for avatar display.

    Reads a short looping MP4 from /assets/avatars/loop.mp4 and decodes
    frames using av/PyAV, yielding frames at the configured framerate.

    Environment Variables:
        AVATAR_VIDEO_PATH: Custom path to avatar video file
        ASSETS_DIR: Directory containing avatar assets (default: /assets/avatars/)
    """

    def __init__(self, config: VideoSourceConfig):
        super().__init__(config)

        # Video file configuration
        self.assets_dir = Path(os.getenv("ASSETS_DIR", "/tmp/avatars/"))
        self.video_path = os.getenv("AVATAR_VIDEO_PATH") or str(self.assets_dir / "loop.mp4")

        # Video decoding state
        self._container = None
        self._video_stream = None
        self._frame_iter = None
        self._current_frame_index = 0
        self._total_frames = 0

        # Ensure assets directory exists
        try:
            self.assets_dir.mkdir(parents=True, exist_ok=True)
        except (OSError, PermissionError):
            # Fallback to current directory if we can't create the assets dir
            self.assets_dir = Path.cwd() / "assets" / "avatars"
            self.assets_dir.mkdir(parents=True, exist_ok=True)
            self.video_path = str(self.assets_dir / "loop.mp4")

    async def start(self) -> None:
        """Start the placeholder video source."""
        self.logger.info(
            "Starting placeholder video source",
            video_path=self.video_path,
            style=self.config.avatar_style
        )
        self._is_running = True

        if av is None:
            self.logger.error("PyAV not available, cannot decode video file")
            return

        try:
            # Open video container
            self._container = av.open(self.video_path)
            self._video_stream = self._container.streams.video[0]

            # Get total frame count
            self._total_frames = self._video_stream.frames if self._video_stream.frames > 0 else 0

            if self._total_frames == 0:
                self.logger.warning("Video file has no frames or frame count unknown")

            self.logger.info(
                "Opened video file",
                duration=self._container.duration / av.time_base if self._container.duration else 0,
                frames=self._total_frames,
                framerate=self._video_stream.average_rate
            )

        except Exception as e:
            self.logger.error("Failed to open video file", error=str(e), path=self.video_path)
            # Fall back to synthetic frame generation
            self.logger.info("Falling back to synthetic frame generation")
            await self._initialize_synthetic_frames()

    async def stop(self) -> None:
        """Stop the placeholder video source."""
        self.logger.info("Stopping placeholder video source")
        self._is_running = False

        # Clean up video resources
        if self._container:
            self._container.close()
            self._container = None
            self._video_stream = None
            self._frame_iter = None

    async def get_frame(self) -> Optional[VideoFrame]:
        """Get the next frame from video file or synthetic generation."""
        if not self._is_running:
            return None

        try:
            if self._container and self._video_stream:
                # Read from video file
                return await self._get_video_frame()
            else:
                # Fall back to synthetic generation
                return await self._get_synthetic_frame()
        except Exception as e:
            self.logger.error("Error getting frame", error=str(e))
            return None

    async def _get_video_frame(self) -> Optional[VideoFrame]:
        """Get next frame from video file."""
        if not self._container or not self._video_stream:
            return None

        try:
            # Decode next frame
            frame = next(self._container.decode(video=0))

            # Convert to RGB format if needed
            if frame.format.name != 'rgb24':
                frame = frame.reformat(format='rgb24')

            # Create VideoFrame object
            video_frame = VideoFrame(
                data=frame.to_ndarray(format='rgb24').tobytes(),
                timestamp=frame.time,
                width=frame.width,
                height=frame.height,
                format="rgb24"
            )

            self._frame_count += 1
            return video_frame

        except (StopIteration, av.error.EOFError):
            # End of file reached, restart from beginning
            self._container.seek(0)
            return await self._get_video_frame()
        except Exception as e:
            self.logger.error("Error decoding video frame", error=str(e))
            return None

    async def frames(self) -> AsyncIterator[np.ndarray]:
        """Stream video frames as numpy arrays."""
        while self._is_running:
            frame = await self.get_frame()
            if frame:
                # Convert bytes to numpy array
                if np is not None:
                    frame_array = np.frombuffer(frame.data, dtype=np.uint8).reshape(
                        (frame.height, frame.width, 3)
                    )
                    yield frame_array
                else:
                    # Fallback if numpy not available
                    yield None

            # Control frame rate
            await asyncio.sleep(1.0 / self.config.framerate)

    async def stream_frames(self) -> AsyncGenerator[VideoFrame, None]:
        """Stream frames continuously."""
        while self._is_running:
            frame = await self.get_frame()
            if frame:
                yield frame

            # Control frame rate
            await asyncio.sleep(1.0 / self.config.framerate)

    async def _get_synthetic_frame(self) -> Optional[VideoFrame]:
        """Get synthetic frame when video file is not available."""
        width, height = self.config.resolution

        if np is None:
            # Fallback to basic array if numpy not available
            frame_data = bytes([(50, 100, 150) for _ in range(width * height * 3)])
        else:
            # Create simple animated frame
            frame_array = await self._generate_synthetic_frame_data(width, height)
            frame_data = frame_array.tobytes()

        # Create frame object
        frame = VideoFrame(
            data=frame_data,
            timestamp=asyncio.get_event_loop().time(),
            width=width,
            height=height,
            format="rgb24"
        )

        self._frame_count += 1
        return frame

    async def _generate_synthetic_frame_data(self, width: int, height: int):
        """Generate synthetic frame pixel data."""
        if np is None:
            # Fallback to basic array if numpy not available
            return np.array([[(50, 100, 150) for _ in range(width)] for _ in range(height)])

        # Create base frame with simple animation
        frame = np.zeros((height, width, 3), dtype=np.uint8)

        # Add simple pulsing background
        phase = self._frame_count * 0.1
        pulse = (math.sin(phase) + 1) / 2  # 0 to 1
        base_color = (
            int(50 + 50 * pulse),
            int(100 + 50 * pulse),
            int(150 + 50 * pulse)
        )
        frame[:, :] = base_color

        # Add simple avatar representation
        center_x, center_y = width // 2, height // 2
        radius = min(width, height) // 4

        # Simple face circle
        for y in range(max(0, center_y - radius), min(height, center_y + radius + 1)):
            for x in range(max(0, center_x - radius), min(width, center_x + radius + 1)):
                dx, dy = x - center_x, y - center_y
                if dx*dx + dy*dy <= radius*radius:
                    frame[y, x] = (200, 180, 160)  # Skin tone

        return frame

    async def _initialize_synthetic_frames(self) -> None:
        """Initialize synthetic frame generation fallback."""
        self.logger.info("Initialized synthetic frame generation fallback")
