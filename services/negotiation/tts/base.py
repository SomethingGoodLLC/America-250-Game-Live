"""Base TTS interface."""

import asyncio
from abc import ABC, abstractmethod
from typing import AsyncGenerator, Dict, Any

from aiortc.mediastreams import MediaStreamTrack


class TTSProvider(ABC):
    """Base class for Text-to-Speech providers."""

    def __init__(self, config: Dict[str, Any]):
        self.config = config

    @abstractmethod
    async def synthesize_speech(
        self,
        text: str,
        speaker_id: str = "default"
    ) -> AsyncGenerator[bytes, None]:
        """
        Synthesize speech from text.

        Args:
            text: Text to synthesize
            speaker_id: ID of the speaker voice to use

        Yields:
            bytes: Audio chunks
        """
        pass

    @abstractmethod
    async def get_audio_track(self, text: str, speaker_id: str = "default") -> MediaStreamTrack:
        """Get a MediaStreamTrack for the synthesized audio."""
        pass

    @abstractmethod
    async def close(self):
        """Clean up resources."""
        pass
