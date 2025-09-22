"""Video source implementations for avatar generation."""

from .base import BaseVideoSource, AvatarVideoTrack, VideoFrame
from .placeholder_loop import PlaceholderLoopVideoSource
from .veo3_stream import Veo3StreamVideoSource

__all__ = [
    "BaseVideoSource",
    "AvatarVideoTrack", 
    "VideoFrame",
    "PlaceholderLoopVideoSource",
    "Veo3StreamVideoSource"
]
