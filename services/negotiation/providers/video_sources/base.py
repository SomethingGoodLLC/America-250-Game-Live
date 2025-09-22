"""Base video source interface for avatar generation."""

import asyncio
from abc import ABC, abstractmethod
from typing import AsyncGenerator, Optional, Dict, Any
from dataclasses import dataclass
import logging

from aiortc.mediastreams import VideoStreamTrack
import structlog

from ..types import VideoSourceConfig


@dataclass
class VideoFrame:
    """Represents a single video frame with metadata."""
    data: bytes
    timestamp: float
    width: int
    height: int
    format: str = "rgb24"


class BaseVideoSource(ABC):
    """Base class for video sources used in avatar generation.

    This provides the interface for generating video frames for avatar
    streaming in negotiation sessions.
    """

    def __init__(self, config: VideoSourceConfig):
        self.config = config
        self.logger = structlog.get_logger(__name__)
        self._is_running = False
        self._frame_count = 0

    @abstractmethod
    async def start(self) -> None:
        """Start the video source."""
        self._is_running = True

    @abstractmethod
    async def stop(self) -> None:
        """Stop the video source."""
        self._is_running = False

    @abstractmethod
    async def get_frame(self) -> Optional[VideoFrame]:
        """Get the next video frame.

        Returns:
            VideoFrame or None if no frame is available
        """
        pass

    @abstractmethod
    async def stream_frames(self) -> AsyncGenerator[VideoFrame, None]:
        """Stream video frames continuously.

        Yields:
            VideoFrame: Next frame in the stream
        """
        pass

    def is_running(self) -> bool:
        """Check if the video source is currently running."""
        return self._is_running

    def get_frame_count(self) -> int:
        """Get the total number of frames generated."""
        return self._frame_count

    async def wait_for_frame(self, timeout_seconds: float = 1.0) -> Optional[VideoFrame]:
        """Wait for the next frame with timeout.

        Args:
            timeout_seconds: Maximum time to wait for a frame

        Returns:
            VideoFrame or None if timeout occurred
        """
        try:
            frame = await asyncio.wait_for(self.get_frame(), timeout=timeout_seconds)
            return frame
        except asyncio.TimeoutError:
            self.logger.warning("Timeout waiting for video frame")
            return None


class AvatarVideoTrack(VideoStreamTrack):
    """WebRTC-compatible video track that wraps a BaseVideoSource."""

    def __init__(self, video_source: BaseVideoSource):
        super().__init__()
        self.video_source = video_source
        self.logger = structlog.get_logger(__name__)

    async def recv(self):
        """Receive the next video frame for WebRTC streaming."""
        try:
            frame = await self.video_source.get_frame()
            if frame is None:
                # Return a blank frame if no data is available
                try:
                    import av
                    import numpy as np

                    blank_frame = av.VideoFrame.from_ndarray(
                        np.zeros((self.video_source.config.resolution[1],
                                 self.video_source.config.resolution[0], 3),
                                dtype=np.uint8),
                        format="rgb24"
                    )
                except ImportError:
                    import av
                    blank_frame = av.VideoFrame(
                        width=self.video_source.config.resolution[0],
                        height=self.video_source.config.resolution[1],
                        format="rgb24"
                    )
                blank_frame.pts = self.video_source.get_frame_count()
                blank_frame.time_base = av.Fraction(1, self.video_source.config.framerate)
                return blank_frame

            # Convert VideoFrame to av.VideoFrame
            try:
                import av
                import numpy as np

                # Assuming frame.data is in the right format
                # This is a simplified conversion - real implementation would handle format conversion
                av_frame = av.VideoFrame.from_ndarray(
                    np.frombuffer(frame.data, dtype=np.uint8).reshape(
                        (frame.height, frame.width, 3)
                    ),
                    format=frame.format
                )
            except ImportError:
                # Fallback if av or numpy not available
                import av
                av_frame = av.VideoFrame(width=frame.width, height=frame.height, format="rgb24")
                av_frame.pts = self.video_source.get_frame_count()
                av_frame.time_base = av.Fraction(1, self.video_source.config.framerate)
                return av_frame
            av_frame.pts = self.video_source.get_frame_count()
            av_frame.time_base = av.Fraction(1, self.video_source.config.framerate)

            return av_frame

        except Exception as e:
            self.logger.error("Error receiving video frame", error=str(e))
            # Return a minimal frame on error
            import av
            error_frame = av.VideoFrame(width=1, height=1, format="rgb24")
            error_frame.pts = self.video_source.get_frame_count()
            return error_frame
