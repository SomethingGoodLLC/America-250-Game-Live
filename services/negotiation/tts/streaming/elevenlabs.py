"""ElevenLabs TTS streaming adapter.

ElevenLabs provides high-quality voice cloning and synthesis with
word-level alignment. This adapter connects to ElevenLabs WebSocket API
and generates phoneme timestamps using the alignment data.

Documentation: https://elevenlabs.io/docs/
"""

import asyncio
import base64
import json
from collections.abc import AsyncIterator
from typing import Any

import structlog
import websockets
from websockets.client import WebSocketClientProtocol

from app.phonemizer import text_to_phonemes, words_to_phoneme_timestamps
from app.schemas import PhonemeTimestamp, StreamEvent

from .base import TTSStreamer

logger = structlog.get_logger(__name__)


class ElevenLabsTTSStreamer(TTSStreamer):
    """ElevenLabs TTS streaming implementation.

    Features:
        - High-quality voice synthesis and cloning
        - Word-level alignment timestamps
        - Phoneme generation via G2P (grapheme-to-phoneme)
        - Low-latency streaming
        - Multiple language support

    Configuration:
        api_key: ElevenLabs API key (required)
        base_url: WebSocket URL (default: wss://api.elevenlabs.io/v1/text-to-speech)

    Example:
        streamer = ElevenLabsTTSStreamer(api_key="el_...")
        await streamer.start(voice_id="21m00Tcm4TlvDq8ikWAM")

        async for event in streamer.aiter_events():
            if event.kind == "audio":
                play_audio(event.pcm16_b64)
            elif event.kind == "phonemes":
                update_lipsync(event.phonemes)

        await streamer.send_text("Hello!")
        await streamer.stop()
    """

    WEBSOCKET_URL = "wss://api.elevenlabs.io/v1/text-to-speech/{voice_id}/stream-input"
    SAMPLE_RATE = 24000

    def __init__(self, api_key: str, **kwargs):
        """Initialize ElevenLabs streamer.

        Args:
            api_key: ElevenLabs API key
            **kwargs: Additional configuration
        """
        super().__init__(api_key=api_key, **kwargs)

        if not api_key:
            raise ValueError("ElevenLabs API key is required")

        self.api_key = api_key
        self.base_url = kwargs.get("base_url", self.WEBSOCKET_URL)

        self._ws: WebSocketClientProtocol | None = None
        self._event_queue: asyncio.Queue = asyncio.Queue()
        self._receive_task: asyncio.Task | None = None
        self._cumulative_time_ms: float = 0.0
        self._pending_text: list[str] = []

    async def start(self, *, voice_id: str | None = None, **kwargs) -> None:
        """Start ElevenLabs streaming connection.

        Args:
            voice_id: ElevenLabs voice ID (required)
            **kwargs: Additional options
                - model_id: Model ID (default: "eleven_turbo_v2")
                - voice_settings: Dict with stability, similarity_boost, etc.
                - language_code: Language code (optional)
                - enable_ssml_parsing: Enable SSML (default: False)
        """
        self._validate_not_stopped()

        if not voice_id:
            raise ValueError("voice_id is required for ElevenLabs")

        self.voice_id = voice_id
        self.model_id = kwargs.get("model_id", "eleven_turbo_v2")
        self.voice_settings = kwargs.get("voice_settings", {
            "stability": 0.5,
            "similarity_boost": 0.75
        })
        self.language_code = kwargs.get("language_code")
        self.enable_ssml = kwargs.get("enable_ssml_parsing", False)

        try:
            # Build WebSocket URL
            ws_url = self.base_url.format(voice_id=voice_id)

            # Add query parameters
            query_params = []
            query_params.append(f"model_id={self.model_id}")
            query_params.append("output_format=pcm_24000")

            # Enable alignment for phoneme generation
            query_params.append("enable_logging=true")
            query_params.append("optimize_streaming_latency=3")

            if self.language_code:
                query_params.append(f"language_code={self.language_code}")

            ws_url += "?" + "&".join(query_params)

            self.logger.info(
                "Connecting to ElevenLabs WebSocket",
                voice_id=voice_id,
                model=self.model_id
            )

            # Connect with API key header
            self._ws = await websockets.connect(
                ws_url,
                extra_headers={
                    "xi-api-key": self.api_key
                },
                ping_interval=20,
                ping_timeout=10
            )

            # Send initial configuration
            config = {
                "text": " ",  # Initial space to start stream
                "voice_settings": self.voice_settings,
                "generation_config": {
                    "chunk_length_schedule": [120, 160, 250, 290]
                }
            }

            if self.enable_ssml:
                config["enable_ssml_parsing"] = True

            await self._ws.send(json.dumps(config))

            self.logger.info("ElevenLabs connection established")

            # Start receiving messages
            self._receive_task = asyncio.create_task(self._receive_loop())

            self._started = True

        except Exception as e:
            self.logger.error("Failed to connect to ElevenLabs", error=str(e))
            if self._ws:
                await self._ws.close()
            raise ConnectionError(f"ElevenLabs connection failed: {str(e)}") from e

    async def send_text(self, text: str, *, final: bool = False) -> None:
        """Send text to ElevenLabs for synthesis.

        Args:
            text: Text chunk to synthesize
            final: Whether this is the final chunk
        """
        self._validate_started()
        self._validate_not_stopped()

        if not text and not final:
            raise ValueError("Text cannot be empty unless final=True")

        try:
            # Store text for phoneme generation later
            if text:
                self._pending_text.append(text)

            # Send text chunk
            msg = {"text": text if text else " "}

            await self._ws.send(json.dumps(msg))

            self.logger.debug(
                "Sent text to ElevenLabs",
                text_length=len(text) if text else 0,
                final=final
            )

            # If final, close the input stream
            if final:
                # Send empty string to signal end
                await self._ws.send(json.dumps({"text": ""}))

                # Generate phonemes for all text
                await self._generate_phonemes_for_text()

        except Exception as e:
            self.logger.error("Failed to send text to ElevenLabs", error=str(e))
            await self._event_queue.put(
                StreamEvent(kind="error", error=f"Send failed: {str(e)}")
            )

    async def stop(self) -> None:
        """Stop ElevenLabs streaming and clean up."""
        if self._stopped:
            return

        self.logger.info("Stopping ElevenLabs streamer")

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
        """Async generator yielding StreamEvents from ElevenLabs.

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
        """Background task to receive and process ElevenLabs messages."""
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
        """Process audio chunk from ElevenLabs.

        Args:
            audio_data: Raw PCM audio bytes (24kHz, 16-bit, mono)
        """
        try:
            # Encode audio as base64
            pcm16_b64 = base64.b64encode(audio_data).decode('utf-8')

            event = StreamEvent(
                kind="audio",
                pcm16_b64=pcm16_b64,
                sample_rate=self.SAMPLE_RATE
            )

            await self._event_queue.put(event)

            # Track cumulative time for alignment
            num_samples = len(audio_data) // 2  # 16-bit = 2 bytes per sample
            duration_ms = (num_samples / self.SAMPLE_RATE) * 1000
            self._cumulative_time_ms += duration_ms

            self.logger.debug(
                "Received audio chunk",
                size=len(audio_data),
                duration_ms=duration_ms
            )

        except Exception as e:
            self.logger.error("Error handling audio chunk", error=str(e))

    async def _handle_json_message(self, data: dict[str, Any]) -> None:
        """Process JSON message from ElevenLabs.

        Args:
            data: Parsed JSON message
        """
        try:
            # ElevenLabs sends alignment data with word timings
            if "alignment" in data:
                alignment = data["alignment"]
                await self._process_alignment(alignment)

            # Check for completion
            if data.get("isFinal", False):
                self.logger.info("ElevenLabs synthesis complete")
                # Note: Don't send done yet, wait for stop()

            # Check for errors
            if "error" in data:
                error_msg = data["error"]
                self.logger.error("ElevenLabs error", error=error_msg)
                event = StreamEvent(kind="error", error=f"ElevenLabs: {error_msg}")
                await self._event_queue.put(event)

        except Exception as e:
            self.logger.error("Error handling JSON message", error=str(e), data=data)

    async def _process_alignment(self, alignment: dict[str, Any]) -> None:
        """Process alignment data from ElevenLabs.

        ElevenLabs provides word-level alignment. We convert this to
        phoneme-level alignment using G2P.

        Args:
            alignment: Alignment data with characters and character_start_times_seconds
        """
        try:
            # Extract word timings from character alignment
            chars = alignment.get("chars", [])
            char_times = alignment.get("charStartTimesMs", [])

            if not chars or not char_times:
                return

            # Group characters into words
            words = []
            word_timings = []
            current_word = ""
            word_start_ms = None

            for _i, (char, time_ms) in enumerate(zip(chars, char_times, strict=False)):
                if char.isspace():
                    # End of word
                    if current_word:
                        words.append(current_word)
                        word_end_ms = time_ms
                        word_timings.append({
                            "start_ms": word_start_ms,
                            "end_ms": word_end_ms
                        })
                        current_word = ""
                        word_start_ms = None
                else:
                    # Part of word
                    if word_start_ms is None:
                        word_start_ms = time_ms
                    current_word += char

            # Handle last word
            if current_word:
                words.append(current_word)
                word_timings.append({
                    "start_ms": word_start_ms,
                    "end_ms": char_times[-1] if char_times else word_start_ms
                })

            # Convert words to phonemes with timing
            if words and word_timings:
                phoneme_data = words_to_phoneme_timestamps(words, word_timings)

                if phoneme_data:
                    # Create phoneme event
                    phonemes = [
                        PhonemeTimestamp(
                            phoneme=p["phoneme"],
                            start_ms=p["start_ms"],
                            end_ms=p["end_ms"]
                        )
                        for p in phoneme_data
                    ]

                    event = StreamEvent(kind="phonemes", phonemes=phonemes)
                    await self._event_queue.put(event)

                    self.logger.debug(
                        "Generated phoneme alignment",
                        word_count=len(words),
                        phoneme_count=len(phonemes)
                    )

        except Exception as e:
            self.logger.error("Error processing alignment", error=str(e))

    async def _generate_phonemes_for_text(self) -> None:
        """Generate phonemes for all pending text.

        This is called when final=True. It generates phonemes for the entire
        utterance using our G2P helper since ElevenLabs may not provide
        perfect alignment for all cases.
        """
        if not self._pending_text:
            return

        try:
            full_text = " ".join(self._pending_text)

            # Generate phonemes
            phonemes_list = text_to_phonemes(full_text)

            # Estimate timing (divide total duration across phonemes)
            if phonemes_list and self._cumulative_time_ms > 0:
                phoneme_duration = self._cumulative_time_ms / len(phonemes_list)

                phonemes = []
                for i, phoneme in enumerate(phonemes_list):
                    start_ms = i * phoneme_duration
                    end_ms = (i + 1) * phoneme_duration

                    phonemes.append(PhonemeTimestamp(
                        phoneme=phoneme,
                        start_ms=round(start_ms, 2),
                        end_ms=round(end_ms, 2)
                    ))

                # Only send if we have meaningful timing
                if phonemes:
                    event = StreamEvent(kind="phonemes", phonemes=phonemes)
                    await self._event_queue.put(event)

                    self.logger.debug(
                        "Generated fallback phonemes",
                        count=len(phonemes)
                    )

        except Exception as e:
            self.logger.error("Error generating phonemes", error=str(e))

