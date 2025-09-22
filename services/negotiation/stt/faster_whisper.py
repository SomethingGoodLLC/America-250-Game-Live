"""Faster-Whisper STT provider."""

import asyncio
from typing import AsyncGenerator, Dict, Any

from aiortc.mediastreams import MediaStreamTrack

from .base import STTProvider


class FasterWhisperProvider(STTProvider):
    """STT provider using faster-whisper."""

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        # TODO: Initialize faster-whisper model
        self.model_size = config.get("model_size", "base")
        self.device = config.get("device", "cpu")

    async def transcribe_audio(
        self,
        audio_track: MediaStreamTrack
    ) -> AsyncGenerator[str, None]:
        """Transcribe audio using faster-whisper."""
        # TODO: Implement actual faster-whisper integration
        # For now, this is a stub

        await asyncio.sleep(0.1)  # Simulate processing

        # Mock transcription
        yield "Faster-whisper integration not yet implemented"

    async def close(self):
        """Clean up resources."""
        # TODO: Clean up model resources
        pass
