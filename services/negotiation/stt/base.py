"""Base STT interface."""

import asyncio
from abc import ABC, abstractmethod
from typing import AsyncGenerator, Dict, Any

from aiortc.mediastreams import MediaStreamTrack


class STTProvider(ABC):
    """Base class for Speech-to-Text providers."""

    def __init__(self, config: Dict[str, Any]):
        self.config = config

    @abstractmethod
    async def transcribe_audio(
        self,
        audio_track: MediaStreamTrack
    ) -> AsyncGenerator[str, None]:
        """
        Transcribe audio stream.

        Args:
            audio_track: WebRTC audio track

        Yields:
            str: Transcribed text segments
        """
        pass

    @abstractmethod
    async def close(self):
        """Clean up resources."""
        pass
