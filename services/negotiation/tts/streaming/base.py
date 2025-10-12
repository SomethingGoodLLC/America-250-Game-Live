"""Base interface for streaming TTS providers.

This module defines the abstract TTSStreamer class that all streaming TTS
providers must implement. It supports real-time streaming of audio along with
timing metadata (phonemes, visemes, blendshapes) for lip-sync and facial animation.
"""

from abc import ABC, abstractmethod
from typing import AsyncIterator, Dict, Any, Optional
import structlog

from app.schemas import StreamEvent


logger = structlog.get_logger(__name__)


class TTSStreamer(ABC):
    """Abstract base class for streaming TTS providers.
    
    All TTS providers (Cartesia, Azure, ElevenLabs) must implement this interface
    to stream audio and timing metadata in a normalized format.
    
    Lifecycle:
        1. start() - Initialize connection and configure voice
        2. send_text() - Send text chunks to synthesize (can be called multiple times)
        3. aiter_events() - Async generator yielding StreamEvent objects
        4. stop() - Clean up and close connection
        
    Example:
        streamer = CartesiaTTSStreamer(api_key="...")
        await streamer.start(voice_id="voice-123")
        
        # Start consuming events in a task
        async for event in streamer.aiter_events():
            if event.kind == "audio":
                play_audio(event.pcm16_b64)
            elif event.kind == "phonemes":
                update_lipsync(event.phonemes)
            elif event.kind == "done":
                break
        
        # Send text from another task
        await streamer.send_text("Hello, world!")
        await streamer.send_text("This is streaming.", final=True)
        
        await streamer.stop()
    """
    
    def __init__(self, **kwargs):
        """Initialize the streamer with provider-specific configuration.
        
        Args:
            **kwargs: Provider-specific configuration (api_key, etc.)
        """
        self.config = kwargs
        self.logger = logger.bind(provider=self.__class__.__name__)
        self._started = False
        self._stopped = False
    
    @abstractmethod
    async def start(self, *, voice_id: Optional[str] = None, **kwargs) -> None:
        """Initialize the streaming connection.
        
        This must be called before send_text() or aiter_events().
        
        Args:
            voice_id: Voice identifier (provider-specific)
            **kwargs: Additional provider-specific parameters
                - sample_rate: Audio sample rate (default 24000)
                - language: Language code (e.g., "en-US")
                - emotion: Emotional style hint
                - speed: Speech rate multiplier
        
        Raises:
            ConnectionError: If unable to connect to provider
            ValueError: If invalid voice_id or configuration
        """
        ...
    
    @abstractmethod
    async def send_text(self, text: str, *, final: bool = False) -> None:
        """Send text to be synthesized.
        
        Can be called multiple times for incremental streaming.
        The provider will start synthesizing immediately if possible.
        
        Args:
            text: Text chunk to synthesize
            final: Whether this is the final text chunk
        
        Raises:
            RuntimeError: If called before start() or after stop()
            ValueError: If text is empty or invalid
        """
        ...
    
    @abstractmethod
    async def stop(self) -> None:
        """Stop streaming and clean up resources.
        
        After calling stop(), the streamer cannot be reused.
        Create a new instance for subsequent requests.
        """
        ...
    
    @abstractmethod
    async def aiter_events(self) -> AsyncIterator[StreamEvent]:
        """Async generator yielding normalized StreamEvent objects.
        
        This should be consumed in a separate task from send_text().
        Events are yielded as they become available from the provider.
        
        Yields:
            StreamEvent: Normalized events with one of these kinds:
                - "audio": PCM16LE audio chunk (base64-encoded)
                - "phonemes": List of phonemes with timing
                - "viseme": Single viseme event for lip-sync
                - "blendshapes": ARKit blendshape frames
                - "done": Synthesis complete
                - "error": Error occurred
        
        The generator completes when:
            - A "done" event is yielded
            - An error occurs and is yielded as "error"
            - stop() is called
        
        Example events:
            StreamEvent(kind="audio", pcm16_b64="UklGRi4...", sample_rate=24000)
            StreamEvent(kind="viseme", viseme=VisemeEvent(viseme_id=8, audio_offset_ms=125.5))
            StreamEvent(kind="phonemes", phonemes=[PhonemeTimestamp(...), ...])
            StreamEvent(kind="done")
        """
        ...
    
    def _validate_started(self) -> None:
        """Raise if start() hasn't been called yet."""
        if not self._started:
            raise RuntimeError(f"{self.__class__.__name__}.start() must be called before this operation")
    
    def _validate_not_stopped(self) -> None:
        """Raise if stop() has been called."""
        if self._stopped:
            raise RuntimeError(f"{self.__class__.__name__} has been stopped and cannot be used")

