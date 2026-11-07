"""Cartesia TTS streaming adapter.

Cartesia provides high-quality, low-latency TTS with phoneme timestamps.
This adapter connects to Cartesia's WebSocket API and converts their
native events to our normalized StreamEvent format.

Documentation: https://docs.cartesia.ai/
"""

import asyncio
import base64
import json
from collections.abc import AsyncIterator
from typing import Any

import structlog
import websockets
from websockets.client import WebSocketClientProtocol

from app.schemas import PhonemeTimestamp, StreamEvent

from .base import TTSStreamer

logger = structlog.get_logger(__name__)


class CartesiaTTSStreamer(TTSStreamer):
    """Cartesia TTS streaming implementation.

    Features:
        - 24kHz PCM16LE audio output
        - Phoneme timestamps with timing
        - Low-latency streaming (~200ms)
        - WebSocket-based real-time synthesis

    Configuration:
        api_key: Cartesia API key (required)
        base_url: WebSocket URL (default: wss://api.cartesia.ai/tts/websocket)
        model_id: Voice model ID (default: "sonic-english")

    Example:
        streamer = CartesiaTTSStreamer(api_key="cart_...")
        await streamer.start(voice_id="a0e99841-...")

        # Consume events
        async for event in streamer.aiter_events():
            if event.kind == "audio":
                play_audio(event.pcm16_b64)

        await streamer.send_text("Hello!")
        await streamer.stop()
    """

    WEBSOCKET_URL = "wss://api.cartesia.ai/tts/websocket"
    DEFAULT_MODEL = "sonic-english"
    SAMPLE_RATE = 24000

    def __init__(self, api_key: str, **kwargs):
        """Initialize Cartesia streamer.

        Args:
            api_key: Cartesia API key
            **kwargs: Additional configuration
        """
        super().__init__(api_key=api_key, **kwargs)

        if not api_key:
            raise ValueError("Cartesia API key is required")

        self.api_key = api_key
        self.base_url = kwargs.get("base_url", self.WEBSOCKET_URL)
        self.model_id = kwargs.get("model_id", self.DEFAULT_MODEL)

        self._ws: WebSocketClientProtocol | None = None
        self._event_queue: asyncio.Queue = asyncio.Queue()
        self._receive_task: asyncio.Task | None = None
        self._context_id: str | None = None

    async def start(self, *, voice_id: str | None = None, **kwargs) -> None:
        """Start Cartesia streaming connection.

        Args:
            voice_id: Cartesia voice ID (required)
            **kwargs: Additional options
                - language: Language code (default: "en")
                - sample_rate: Sample rate (default: 24000)
                - encoding: Audio encoding (default: "pcm_s16le")
        """
        self._validate_not_stopped()

        if not voice_id:
            raise ValueError("voice_id is required for Cartesia")

        self.voice_id = voice_id
        self.language = kwargs.get("language", "en")
        self.sample_rate = kwargs.get("sample_rate", self.SAMPLE_RATE)
        self.encoding = kwargs.get("encoding", "pcm_s16le")

        try:
            # Connect to WebSocket with API key
            headers = {
                "X-API-Key": self.api_key,
                "Cartesia-Version": "2024-06-10"
            }

            self.logger.info(
                "Connecting to Cartesia WebSocket",
                url=self.base_url,
                voice_id=voice_id
            )

            self._ws = await websockets.connect(
                self.base_url,
                extra_headers=headers,
                ping_interval=20,
                ping_timeout=10
            )

            # Send initial configuration
            config_msg = {
                "type": "configure",
                "model_id": self.model_id,
                "voice": {
                    "mode": "id",
                    "id": voice_id
                },
                "output_format": {
                    "container": "raw",
                    "encoding": self.encoding,
                    "sample_rate": self.sample_rate
                },
                "language": self.language,
                "enable_phonemes": True  # Enable phoneme timestamps
            }

            await self._ws.send(json.dumps(config_msg))

            # Wait for configuration acknowledgment
            ack = await self._ws.recv()
            ack_data = json.loads(ack)

            if ack_data.get("type") != "config_ack":
                raise ConnectionError(f"Unexpected response: {ack_data}")

            self._context_id = ack_data.get("context_id")

            self.logger.info(
                "Cartesia connection established",
                context_id=self._context_id
            )

            # Start receiving messages
            self._receive_task = asyncio.create_task(self._receive_loop())

            self._started = True

        except Exception as e:
            self.logger.error("Failed to connect to Cartesia", error=str(e))
            if self._ws:
                await self._ws.close()
            raise ConnectionError(f"Cartesia connection failed: {str(e)}") from e

    async def send_text(self, text: str, *, final: bool = False) -> None:
        """Send text to Cartesia for synthesis.

        Args:
            text: Text chunk to synthesize
            final: Whether this is the final chunk
        """
        self._validate_started()
        self._validate_not_stopped()

        if not text:
            raise ValueError("Text cannot be empty")

        try:
            # Send text chunk
            msg = {
                "type": "chunk",
                "context_id": self._context_id,
                "text": text,
                "continue": not final
            }

            await self._ws.send(json.dumps(msg))

            self.logger.debug(
                "Sent text to Cartesia",
                text_length=len(text),
                final=final
            )

            # If final, signal end of input
            if final:
                end_msg = {
                    "type": "input_end",
                    "context_id": self._context_id
                }
                await self._ws.send(json.dumps(end_msg))

        except Exception as e:
            self.logger.error("Failed to send text to Cartesia", error=str(e))
            await self._event_queue.put(
                StreamEvent(kind="error", error=f"Send failed: {str(e)}")
            )

    async def stop(self) -> None:
        """Stop Cartesia streaming and clean up."""
        if self._stopped:
            return

        self.logger.info("Stopping Cartesia streamer")

        # Cancel receive task
        if self._receive_task and not self._receive_task.done():
            self._receive_task.cancel()
            try:
                await self._receive_task
            except asyncio.CancelledError:
                pass

        # Close WebSocket
        if self._ws and not self._ws.closed:
            try:
                await self._ws.close()
            except Exception as e:
                self.logger.error("Error closing WebSocket", error=str(e))

        self._stopped = True

        # Put final done event
        try:
            await self._event_queue.put(StreamEvent(kind="done"))
        except Exception:
            pass

    async def aiter_events(self) -> AsyncIterator[StreamEvent]:
        """Async generator yielding StreamEvents from Cartesia.

        Yields:
            StreamEvent: Normalized events (audio, phonemes, done, error)
        """
        self._validate_started()

        while not self._stopped:
            try:
                # Get event from queue with timeout
                event = await asyncio.wait_for(
                    self._event_queue.get(),
                    timeout=0.1
                )

                yield event

                # Stop iteration if done or error
                if event.kind in ("done", "error"):
                    break

            except TimeoutError:
                continue
            except Exception as e:
                self.logger.error("Error in event iterator", error=str(e))
                yield StreamEvent(kind="error", error=str(e))
                break

    async def _receive_loop(self) -> None:
        """Background task to receive and process Cartesia messages."""
        try:
            async for message in self._ws:
                if isinstance(message, bytes):
                    # Binary audio data
                    await self._handle_audio_chunk(message)
                else:
                    # JSON message
                    data = json.loads(message)
                    await self._handle_json_message(data)

        except asyncio.CancelledError:
            self.logger.debug("Receive loop cancelled")
        except Exception as e:
            self.logger.error("Error in receive loop", error=str(e))
            await self._event_queue.put(
                StreamEvent(kind="error", error=f"Receive error: {str(e)}")
            )

    async def _handle_audio_chunk(self, audio_data: bytes) -> None:
        """Process audio chunk from Cartesia.

        Args:
            audio_data: Raw PCM16LE audio bytes
        """
        try:
            # Encode audio as base64
            pcm16_b64 = base64.b64encode(audio_data).decode('utf-8')

            event = StreamEvent(
                kind="audio",
                pcm16_b64=pcm16_b64,
                sample_rate=self.sample_rate
            )

            await self._event_queue.put(event)

            self.logger.debug("Received audio chunk", size=len(audio_data))

        except Exception as e:
            self.logger.error("Error handling audio chunk", error=str(e))

    async def _handle_json_message(self, data: dict[str, Any]) -> None:
        """Process JSON message from Cartesia.

        Args:
            data: Parsed JSON message
        """
        msg_type = data.get("type")

        try:
            if msg_type == "phoneme_timestamps":
                # Convert Cartesia phoneme format to our format
                phonemes = []
                for p in data.get("phonemes", []):
                    phonemes.append(PhonemeTimestamp(
                        phoneme=p["phoneme"],
                        start_ms=p["start_time_ms"],
                        end_ms=p["end_time_ms"]
                    ))

                event = StreamEvent(kind="phonemes", phonemes=phonemes)
                await self._event_queue.put(event)

                self.logger.debug("Received phoneme timestamps", count=len(phonemes))

            elif msg_type == "done":
                # Synthesis complete
                event = StreamEvent(kind="done")
                await self._event_queue.put(event)

                self.logger.info("Cartesia synthesis complete")

            elif msg_type == "error":
                # Error from Cartesia
                error_msg = data.get("message", "Unknown error")
                event = StreamEvent(kind="error", error=f"Cartesia error: {error_msg}")
                await self._event_queue.put(event)

                self.logger.error("Cartesia error", message=error_msg)

            elif msg_type == "chunk_complete":
                # Chunk processed (informational, no event needed)
                self.logger.debug("Chunk complete", chunk_id=data.get("chunk_id"))

            else:
                self.logger.warning("Unknown Cartesia message type", type=msg_type)

        except Exception as e:
            self.logger.error("Error handling JSON message", error=str(e), data=data)

