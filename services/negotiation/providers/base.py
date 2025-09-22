"""Base provider interface for negotiation providers."""

from __future__ import annotations
from typing import AsyncIterator, Protocol, Any, Dict, Iterable
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
