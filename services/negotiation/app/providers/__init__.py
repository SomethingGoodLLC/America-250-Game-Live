"""WebSocket proxy providers for TTS services.

This package contains WebSocket proxies that bridge browser clients to TTS provider APIs:
- Cartesia: Real-time streaming TTS with phoneme timestamps
- ElevenLabs: Real-time streaming with alignment data for lip-sync
- Azure: Token endpoint for browser-side SDK usage with visemes/blendshapes

These proxies keep API keys server-side and handle rate limiting.
"""

from .azure_token import router as azure_router
from .cartesia_ws import router as cartesia_router
from .elevenlabs_ws import router as elevenlabs_router

__all__ = ["cartesia_router", "elevenlabs_router", "azure_router"]

