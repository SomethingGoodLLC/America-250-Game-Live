"""Negotiation providers."""

from .base import Provider, ProviderEvent
from .types import ProviderConfig, VideoSourceConfig, ProcessingContext, IntentEvent, SubtitleEvent, AnalysisEvent, SafetyEvent
from .mock_local import MockLocalProvider
from .gemini_provider import GeminiProvider
from .gemini_veo3 import Veo3Provider
from .openai_provider import OpenAIProvider
from .claude_provider import ClaudeProvider
from .grok_provider import GrokProvider

# Video sources
from .video_sources.base import BaseVideoSource, AvatarVideoTrack, VideoFrame
from .video_sources.placeholder_loop import PlaceholderLoopVideoSource
from .video_sources.veo3_stream import Veo3StreamVideoSource

# Utility modules
from ._scoring import score_intent, calculate_overall_score
from ._safety import screen_text, screen_intent, create_safety_event
from ._backpressure import BoundedAIO

__all__ = [
    # Core provider classes
    "Provider",
    "ProviderEvent",

    # Type definitions
    "ProviderConfig",
    "VideoSourceConfig",
    "ProcessingContext",
    "IntentEvent",
    "SubtitleEvent",
    "AnalysisEvent",
    "SafetyEvent",

    # Provider implementations
    "MockLocalProvider",
    "GeminiProvider",
    "Veo3Provider",
    "OpenAIProvider",
    "ClaudeProvider",
    "GrokProvider",

    # Video source classes
    "BaseVideoSource",
    "AvatarVideoTrack",
    "VideoFrame",
    "PlaceholderLoopVideoSource",
    "Veo3StreamVideoSource",

    # Utility functions
    "score_intent",
    "calculate_overall_score",
    "screen_text",
    "screen_intent",
    "create_safety_event",
    "BoundedAIO"
]
