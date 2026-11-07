"""Negotiation providers."""

from ._backpressure import BoundedAIO
from ._safety import create_safety_event, screen_intent, screen_text

# Utility modules
from ._scoring import calculate_overall_score, score_intent
from .base import Analysis, LiveSubtitle, NewIntent, Provider, ProviderEvent, Safety
from .claude_provider import ClaudeProvider
from .gemini_provider import GeminiProvider
from .gemini_veo3 import Veo3Provider
from .grok_provider import GrokProvider
from .mock_local import MockLocalProvider
from .openai_provider import OpenAIProvider
from .types import (
    AnalysisEvent,
    IntentEvent,
    ProcessingContext,
    ProviderConfig,
    SafetyEvent,
    SubtitleEvent,
    VideoSourceConfig,
)
from .video_sources import create_video_source

# Video sources
from .video_sources.base import AvatarVideoTrack, BaseVideoSource, VideoFrame
from .video_sources.placeholder_loop import PlaceholderLoopVideoSource
from .video_sources.veo3_stream import Veo3StreamVideoSource

__all__ = [
    # Core provider classes
    "Provider",
    "ProviderEvent",
    "NewIntent",
    "LiveSubtitle",
    "Analysis",
    "Safety",

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
    "create_video_source",

    # Utility functions
    "score_intent",
    "calculate_overall_score",
    "screen_text",
    "screen_intent",
    "create_safety_event",
    "BoundedAIO"
]
