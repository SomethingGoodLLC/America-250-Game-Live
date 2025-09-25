"""Base listener interface for real-time audio processing."""
from __future__ import annotations
import asyncio
import os
from abc import ABC, abstractmethod
from typing import AsyncIterator, Dict, Any


class Listener(ABC):
    """Base class for audio listener adapters.

    Listeners process incoming audio from WebRTC and produce:
    - Real-time subtitles (partial and final)
    - Final text for intent analysis
    """

    def __init__(self, config: Dict[str, Any]):
        self.config = config

    @abstractmethod
    async def start(self) -> None:
        """Start the listener (e.g., open WebSocket connections)."""
        pass

    @abstractmethod
    async def stop(self) -> None:
        """Stop the listener and clean up resources."""
        pass

    @abstractmethod
    async def feed_pcm(self, pcm_bytes: bytes, ts_ms: int) -> None:
        """Feed PCM audio data to the listener.

        Args:
            pcm_bytes: Raw PCM audio bytes (16-bit signed, mono, 16kHz)
            ts_ms: Timestamp in milliseconds
        """
        pass

    @abstractmethod
    async def final_text(self) -> str:
        """Get the final recognized text when user stops speaking.

        Returns:
            str: Final transcript for intent analysis
        """
        pass

    @abstractmethod
    async def stream_events(self) -> AsyncIterator[Dict[str, Any]]:
        """Stream real-time events from the listener.

        Yields:
            dict: Events like {"subtitle": "...", "final": false}
        """
        pass


def make_listener_from_env() -> Listener:
    """Factory function to create listener from environment configuration.

    Returns:
        Listener: Configured listener instance
    """
    listener_type = os.getenv("LISTENER_TYPE", "local_stt")

    if listener_type == "local_stt":
        from .local_stt import LocalSTTListener
        return LocalSTTListener({})
    elif listener_type == "real_llm":
        from .real_llm import RealLLMListener
        return RealLLMListener({
            "api_key": os.getenv("GEMINI_API_KEY") or os.getenv("OPENAI_API_KEY"),
            "model": "gemini-1.5-flash",
            "use_real_llm": True
        })
    elif listener_type == "gemini_realtime":
        from .gemini_realtime import GeminiRealtimeListener
        return GeminiRealtimeListener({
            "api_key": os.getenv("GEMINI_API_KEY"),
            "model": "models/gemini-1.5-flash"
        })
    elif listener_type == "openai_realtime":
        from .openai_realtime import OpenAIRealtimeListener
        return OpenAIRealtimeListener({
            "api_key": os.getenv("OPENAI_API_KEY"),
            "model": "gpt-4o-realtime-preview-2024-10-01"
        })
    elif listener_type == "grok_realtime":
        from .grok_realtime import GrokRealtimeListener
        return GrokRealtimeListener({
            "api_key": os.getenv("GROK_API_KEY"),
            "model": "grok-beta"
        })
    else:
        raise ValueError(f"Unknown listener type: {listener_type}")


# Re-export for convenience
__all__ = ["Listener", "make_listener_from_env"]
