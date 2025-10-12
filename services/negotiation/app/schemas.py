"""Normalized streaming event schemas for TTS providers.

This module defines the normalized event format that all TTS providers
must stream to clients, enabling consistent frontend handling regardless
of the underlying provider (Azure, ElevenLabs, Cartesia, etc.).
"""

from pydantic import BaseModel, Field
from typing import List, Literal, Optional


class PhonemeTimestamp(BaseModel):
    """Phoneme with timing information."""
    phoneme: str = Field(..., description="Phoneme symbol (IPA or ARPABET)")
    start_ms: float = Field(..., description="Start time in milliseconds")
    end_ms: float = Field(..., description="End time in milliseconds")


class VisemeEvent(BaseModel):
    """Viseme (visual phoneme) event for lip sync.
    
    Viseme IDs typically follow SAPI/Azure conventions (0-21)
    or custom mappings depending on the avatar system.
    """
    viseme_id: int = Field(..., description="Viseme identifier (0-21 for SAPI/Azure)")
    audio_offset_ms: float = Field(..., description="Time offset in audio stream (ms)")


class BlendshapeFrame(BaseModel):
    """ARKit-compatible blendshape frame for facial animation.
    
    Blendshapes follow ARKit's 55 blendshape model for facial expressions.
    Weight values are normalized [0.0, 1.0].
    """
    t_ms: float = Field(..., description="Timestamp in milliseconds")
    weights: List[float] = Field(
        ...,
        description="55 blendshape weights in ARKit order",
        min_length=55,
        max_length=55
    )


class StreamEvent(BaseModel):
    """Normalized event streamed from TTS providers to clients.
    
    All TTS providers (Azure, ElevenLabs, Cartesia) must convert their
    native events to this normalized format before sending to the client.
    """
    kind: Literal["audio", "phonemes", "viseme", "blendshapes", "done", "error"] = Field(
        ...,
        description="Event type identifier"
    )
    
    # Audio data (kind="audio")
    pcm16_b64: Optional[str] = Field(
        None,
        description="Base64-encoded PCM16LE audio chunk"
    )
    sample_rate: Optional[int] = Field(
        None,
        description="Audio sample rate in Hz (typically 24000)"
    )
    
    # Phoneme data (kind="phonemes")
    phonemes: Optional[List[PhonemeTimestamp]] = Field(
        None,
        description="List of phonemes with timing for the current chunk"
    )
    
    # Viseme data (kind="viseme")
    viseme: Optional[VisemeEvent] = Field(
        None,
        description="Single viseme event for lip sync"
    )
    
    # Blendshape data (kind="blendshapes")
    blendshapes: Optional[List[BlendshapeFrame]] = Field(
        None,
        description="Blendshape animation frames for facial expressions"
    )
    
    # Error data (kind="error")
    error: Optional[str] = Field(
        None,
        description="Error message if kind='error'"
    )

    class Config:
        """Pydantic configuration."""
        json_schema_extra = {
            "examples": [
                {
                    "kind": "audio",
                    "pcm16_b64": "UklGRi4AAABXQVZFZm10...",
                    "sample_rate": 24000
                },
                {
                    "kind": "viseme",
                    "viseme": {
                        "viseme_id": 8,
                        "audio_offset_ms": 125.5
                    }
                },
                {
                    "kind": "done"
                }
            ]
        }


class SpeakRequest(BaseModel):
    """Request for the /speak endpoint (diagnostic full synthesis)."""
    provider: Literal["azure", "elevenlabs", "cartesia"] = Field(
        ...,
        description="TTS provider to use"
    )
    voice_id: str = Field(
        ...,
        description="Provider-specific voice ID"
    )
    text: str = Field(
        ...,
        description="Text to synthesize",
        min_length=1,
        max_length=5000
    )
    sample_rate: int = Field(
        24000,
        description="Desired output sample rate in Hz"
    )


class StreamRequest(BaseModel):
    """Initial configuration for the /ws/stream WebSocket endpoint."""
    provider: Literal["azure", "elevenlabs", "cartesia"] = Field(
        ...,
        description="TTS provider to use for streaming"
    )
    voice_id: str = Field(
        ...,
        description="Provider-specific voice ID"
    )
    mode: Literal["full", "incremental"] = Field(
        "full",
        description="Streaming mode: 'full' waits for complete text, 'incremental' streams as text arrives"
    )
    sample_rate: int = Field(
        24000,
        description="Desired output sample rate in Hz"
    )


class TextChunk(BaseModel):
    """Text chunk sent over WebSocket for streaming synthesis."""
    text: str = Field(
        ...,
        description="Text chunk to synthesize",
        min_length=1
    )
    is_final: bool = Field(
        False,
        description="Whether this is the final chunk in the stream"
    )

