"""Provider type definitions and event structures."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Any, Literal, TypedDict

# Event type constants
EventType = Literal["intent", "subtitle", "analysis", "safety"]


class IntentPayload(TypedDict, total=False):
    """YAML-validated diplomatic intent payload."""
    kind: str
    demand: dict[str, Any]
    offer: dict[str, Any]
    rationale: list[str]  # Use List instead of list for compatibility
    confidence: float


@dataclass
class ProviderEvent:
    """Base event class for provider communications.

    This represents structured events emitted by negotiation providers
    during dialogue processing.
    """
    type: EventType
    payload: dict[str, Any] | IntentPayload
    is_final: bool = False
    tag: str | None = None  # for analysis events
    timestamp: datetime = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


# Event type aliases for clarity
IntentEvent = ProviderEvent  # type: "intent"
SubtitleEvent = ProviderEvent  # type: "subtitle"
AnalysisEvent = ProviderEvent  # type: "analysis"
SafetyEvent = ProviderEvent  # type: "safety"


@dataclass
class ProviderConfig:
    """Configuration for provider initialization."""
    api_key: str | None = None
    model_name: str = "default"
    timeout_seconds: int = 30
    max_retries: int = 3
    enable_video: bool = False
    enable_audio: bool = True
    safety_enabled: bool = True
    debug_mode: bool = False


@dataclass
class VideoSourceConfig:
    """Configuration for video sources."""
    source_type: str = "placeholder"  # "placeholder", "veo3", "file"
    avatar_style: str = "diplomatic"
    resolution: tuple = (640, 480)  # Use tuple instead of tuple[int, int] for compatibility
    framerate: int = 30
    quality: str = "medium"  # "low", "medium", "high"


@dataclass
class ProcessingContext:
    """Context information for provider processing."""
    session_id: str
    world_context: dict[str, Any]
    system_guidelines: str | None = None
    processing_deadline: datetime | None = None
    resource_constraints: dict[str, Any] | None = None
