"""Gemini realtime listener adapter for bidirectional audio streaming."""

import asyncio
import json
import base64
import io
import wave
from typing import AsyncIterator, Dict, Any, Optional
import structlog
from .base import Listener

# Import Google GenAI library
try:
    from google import genai
    GENAI_AVAILABLE = True
except ImportError:
    genai = None
    GENAI_AVAILABLE = False


class GeminiRealtimeListener(Listener):
    """Gemini realtime listener using Google GenAI Live API with WebSocket streaming.
    
    This implementation uses the official Google GenAI Python SDK for real-time
    multimodal conversations with Gemini 2.0 models.
    """

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.logger = structlog.get_logger(__name__)
        self.api_key = config.get("api_key")
        self.model = config.get("model", "gemini-2.0-flash-exp")
        self.client: Optional['genai.Client'] = None
        self.session: Optional[Any] = None
        self.event_queue = asyncio.Queue()
        self.audio_buffer = bytearray()
        self.running = False
        self.session_config = {
            "generation_config": {
                "speech_config": {
                    "voice_config": {
                        "prebuilt_voice_config": {
                            "voice_name": "Aoede"  # Professional diplomatic voice
                        }
                    }
                }
            },
            "system_instruction": (
                "You are a diplomatic AI assistant helping with negotiations. "
                "Provide thoughtful, measured responses appropriate for diplomatic contexts. "
                "Be concise but informative. Speak in a formal but approachable tone."
            )
        }

    async def start(self) -> None:
        """Start the Gemini Live API connection."""
        if not self.api_key:
            self.logger.warning("GEMINI_API_KEY not set. Using mock mode.")
            self.running = True
            asyncio.create_task(self._mock_stream())
            return

        if not GENAI_AVAILABLE:
            self.logger.error("google-genai library not installed. Install with: pip install google-genai")
            self.running = True
            asyncio.create_task(self._mock_stream())
            return

        try:
            # Initialize the Google GenAI client
            self.client = genai.Client(api_key=self.api_key)
            
            # Start live session with multimodal model
            self.session = await self.client.aio.live.connect(
                model=self.model,
                config=self.session_config
            )
            
            self.running = True
            
            # Start the event processing loop
            asyncio.create_task(self._process_session_events())
            
            self.logger.info("Gemini Live API connection established", model=self.model)
            
        except Exception as e:
            self.logger.error("Failed to start Gemini Live API", error=str(e))
            # Fall back to mock mode
            self.running = True
            asyncio.create_task(self._mock_stream())

    async def stop(self) -> None:
        """Stop the Gemini Live API connection."""
        self.running = False
        
        if self.session:
            try:
                await self.session.close()
                self.logger.info("Gemini Live API session closed")
            except Exception as e:
                self.logger.error("Error closing Gemini session", error=str(e))
        
        # Signal stop to event stream
        try:
            await self.event_queue.put({"type": "stop"})
        except:
            pass

    async def feed_pcm(self, pcm_bytes: bytes, ts_ms: int) -> None:
        """Feed PCM audio data to Gemini Live API.
        
        Args:
            pcm_bytes: Raw PCM audio bytes (16-bit signed, mono, 16kHz)
            ts_ms: Timestamp in milliseconds
        """
        if not self.running or not self.session:
            return
        
        try:
            # Accumulate audio in buffer
            self.audio_buffer.extend(pcm_bytes)
            
            # Send audio chunks to Gemini when we have enough data
            # (send ~100ms chunks = 1600 samples = 3200 bytes for 16kHz mono 16-bit)
            chunk_size = 3200
            if len(self.audio_buffer) >= chunk_size:
                chunk_data = bytes(self.audio_buffer[:chunk_size])
                self.audio_buffer = self.audio_buffer[chunk_size:]
                
                # Convert PCM to base64 for transmission
                audio_b64 = base64.b64encode(chunk_data).decode('utf-8')
                
                # Send realtime audio input
                await self.session.send({
                    "realtimeInput": {
                        "mediaChunks": [{
                            "mimeType": "audio/pcm",
                            "data": audio_b64
                        }]
                    }
                })
                
        except Exception as e:
            self.logger.error("Error feeding PCM to Gemini", error=str(e))

    async def final_text(self) -> str:
        """Get final recognized text from the last conversation turn.
        
        Returns:
            str: Final transcript for intent analysis
        """
        # This would typically be captured from the session events
        # For now, return a placeholder
        return getattr(self, '_last_transcript', "")

    async def stream_events(self) -> AsyncIterator[Dict[str, Any]]:
        """Stream real-time events from Gemini Live API.
        
        Yields:
            dict: Events like {"type": "subtitle", "text": "...", "final": True/False}
        """
        while self.running:
            try:
                event = await asyncio.wait_for(self.event_queue.get(), timeout=0.1)
                if event.get("type") == "stop":
                    break
                yield event
            except asyncio.TimeoutError:
                continue
            except Exception as e:
                self.logger.error("Error in stream_events", error=str(e))
                break

    async def _process_session_events(self) -> None:
        """Process events from the Gemini Live API session."""
        if not self.session:
            return
            
        try:
            async for event in self.session:
                if not self.running:
                    break
                    
                await self._handle_session_event(event)
                
        except Exception as e:
            self.logger.error("Error processing session events", error=str(e))

    async def _handle_session_event(self, event: Dict[str, Any]) -> None:
        """Handle individual events from Gemini Live API session.
        
        Args:
            event: Event data from Gemini Live API
        """
        try:
            # Handle speech-to-text transcript events
            if "serverContent" in event:
                content = event["serverContent"]
                if "turnComplete" in content:
                    # Final transcript
                    self._last_transcript = content.get("text", "")
                    await self.event_queue.put({
                        "type": "subtitle",
                        "text": self._last_transcript,
                        "final": True,
                        "confidence": 1.0
                    })
                elif "modelTurn" in content:
                    # Partial or complete model response
                    model_turn = content["modelTurn"]
                    for part in model_turn.get("parts", []):
                        if "text" in part:
                            await self.event_queue.put({
                                "type": "subtitle", 
                                "text": part["text"],
                                "final": False,
                                "confidence": 0.9
                            })
                            
            # Handle audio output from Gemini
            elif "toolCall" in event:
                # Handle any tool calls if configured
                pass
                
        except Exception as e:
            self.logger.error("Error handling session event", error=str(e), event=event)

    async def _mock_stream(self) -> None:
        """Mock streaming for testing when API is not available."""
        self.logger.info("Running Gemini listener in mock mode")
        
        mock_responses = [
            "I understand you're interested in diplomatic negotiations.",
            "Let me analyze the current situation carefully.",
            "Based on the context, I recommend a measured approach.",
            "The proposed terms seem reasonable for both parties.",
            "I suggest we consider the long-term implications."
        ]
        
        response_index = 0
        
        while self.running:
            try:
                await asyncio.sleep(3.0)  # Simulate processing time
                
                # Simulate receiving audio transcription
                await self.event_queue.put({
                    "type": "subtitle",
                    "text": "Processing diplomatic audio input...",
                    "final": False,
                    "confidence": 0.8
                })
                
                await asyncio.sleep(1.0)
                
                # Simulate final response
                response = mock_responses[response_index % len(mock_responses)]
                response_index += 1
                
                await self.event_queue.put({
                    "type": "subtitle",
                    "text": response,
                    "final": True,
                    "confidence": 0.95
                })
                
                # Store for final_text()
                self._last_transcript = response
                
            except Exception as e:
                self.logger.error("Error in mock stream", error=str(e))


# Re-export for convenience
__all__ = ["GeminiRealtimeListener"]
