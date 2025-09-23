"""Video source implementations for avatar generation."""

import os
from typing import Optional
import structlog

from .base import BaseVideoSource, AvatarVideoTrack, VideoFrame
from .placeholder_loop import PlaceholderLoopVideoSource
from .veo3_stream import Veo3StreamVideoSource
from ..types import VideoSourceConfig

__all__ = [
    "BaseVideoSource",
    "AvatarVideoTrack",
    "VideoFrame",
    "PlaceholderLoopVideoSource",
    "Veo3StreamVideoSource",
    "create_video_source"
]


def create_video_source(config: VideoSourceConfig) -> BaseVideoSource:
    """Factory function to create video sources based on configuration.

    Args:
        config: Video source configuration

    Returns:
        BaseVideoSource: Configured video source instance

    Environment Variables:
        DEFAULT_VIDEO_SOURCE: Override source type ("placeholder", "veo3")
        USE_VEO3: Set to "1" to use Veo3 API mode instead of mock mode
    """
    logger = structlog.get_logger(__name__)

    # Determine source type from config or environment
    source_type = os.getenv("DEFAULT_VIDEO_SOURCE", config.source_type).lower()

    # Special handling for Veo3: check USE_VEO3 flag
    if source_type == "veo3":
        use_veo3 = os.getenv("USE_VEO3", "0") == "1"
        if not use_veo3:
            logger.info("USE_VEO3=0, falling back to placeholder source")
            source_type = "placeholder"

    # Create the appropriate video source
    if source_type == "veo3":
        logger.info("Creating Veo3 video source", style=config.avatar_style)
        return Veo3StreamVideoSource(config)
    elif source_type == "placeholder":
        logger.info("Creating placeholder video source", style=config.avatar_style)
        return PlaceholderLoopVideoSource(config)
    else:
        logger.warning("Unknown video source type, falling back to placeholder", source_type=source_type)
        return PlaceholderLoopVideoSource(config)
