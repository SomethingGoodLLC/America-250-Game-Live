"""Base provider interface for negotiation providers."""

from __future__ import annotations
from typing import AsyncIterator, Protocol, Any, Dict, Iterable, Union, List
from dataclasses import dataclass
from datetime import datetime


@dataclass
class ProviderEvent:
    """Event emitted by negotiation providers."""
    type: str
    payload: Dict[str, Any]
    timestamp: datetime = None

    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = datetime.now()


# Event type classes
@dataclass
class NewIntent:
    """New diplomatic intent detected."""
    intent: Dict[str, Any]
    confidence: float
    justification: str


@dataclass
class LiveSubtitle:
    """Live subtitle with timing information."""
    text: str
    start_time: float
    end_time: float
    speaker_id: str
    is_final: bool = False


@dataclass
class Analysis:
    """Analysis result with metadata."""
    analysis_type: str
    result: Dict[str, Any]
    confidence: float


@dataclass
class Safety:
    """Safety check result."""
    is_safe: bool
    flags: List[str]
    severity: str
    reason: str


class Provider(Protocol):
    """Protocol for negotiation providers."""

    async def stream_dialogue(
        self,
        turns: Iterable[Dict[str, Any]],        # sequence of speaker_turn.v1 YAML objects
        world_context: Dict[str, Any],          # world_context.v1 YAML object
        system_guidelines: str,                 # safety/tone/system text
    ) -> AsyncIterator[ProviderEvent]:
        """Stream dialogue processing and emit events."""
        ...

    async def validate_and_score_intent(
        self,
        intent: Any,
        world_context: Dict[str, Any]
    ) -> tuple[Any, float, str]:
        """Validate and score an intent against schemas and context.

        Args:
            intent: Intent object to validate and score
            world_context: World context for scoring

        Returns:
            Tuple of (validated_intent, confidence_score, justification)
        """
        ...
