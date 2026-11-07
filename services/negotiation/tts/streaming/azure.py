"""Azure TTS streaming adapter.

Azure Cognitive Services Speech SDK provides high-quality TTS with
viseme events and blendshape support for facial animation.

Documentation: https://learn.microsoft.com/en-us/azure/cognitive-services/speech-service/
"""

import asyncio
import base64
from collections.abc import AsyncIterator
from typing import Any

import structlog

from app.schemas import StreamEvent, VisemeEvent

from .base import TTSStreamer

logger = structlog.get_logger(__name__)


# Try to import Azure Speech SDK
try:
    import azure.cognitiveservices.speech as speechsdk
    AZURE_AVAILABLE = True
except ImportError:
    AZURE_AVAILABLE = False
    logger.warning(
        "Azure Speech SDK not installed. Install with: pip install azure-cognitiveservices-speech"
    )


class AzureTTSStreamer(TTSStreamer):
    """Azure TTS streaming implementation.

    Features:
        - High-quality neural voices
        - Viseme events for lip-sync (SAPI/Azure 0-21 IDs)
        - Blendshape support via SSML
        - Multiple language support
        - Prosody control (rate, pitch, volume)

    Configuration:
        subscription_key: Azure Speech subscription key (required)
        region: Azure region (required, e.g., "eastus")
        endpoint: Custom endpoint (optional)

    Example:
        streamer = AzureTTSStreamer(
            subscription_key="abc123...",
            region="eastus"
        )
        await streamer.start(
            voice_id="en-US-JennyNeural",
            enable_blendshapes=True
        )

        async for event in streamer.aiter_events():
            if event.kind == "viseme":
                update_lipsync(event.viseme.viseme_id)
            elif event.kind == "blendshapes":
                apply_blendshapes(event.blendshapes)

        await streamer.send_text("Hello world!")
        await streamer.stop()
    """

    SAMPLE_RATE = 24000

    def __init__(
        self,
        subscription_key: str,
        region: str,
        endpoint: str | None = None,
        **kwargs
    ):
        """Initialize Azure TTS streamer.

        Args:
            subscription_key: Azure Speech subscription key
            region: Azure region (e.g., "eastus", "westus2")
            endpoint: Custom endpoint URL (optional)
            **kwargs: Additional configuration
        """
        super().__init__(
            subscription_key=subscription_key,
            region=region,
            endpoint=endpoint,
            **kwargs
        )

        if not AZURE_AVAILABLE:
            raise ImportError(
                "Azure Speech SDK required. Install with: "
                "pip install azure-cognitiveservices-speech"
            )

        if not subscription_key or not region:
            raise ValueError("subscription_key and region are required")

        self.subscription_key = subscription_key
        self.region = region
        self.endpoint = endpoint

        self._speech_config: speechsdk.SpeechConfig | None = None
        self._synthesizer: speechsdk.SpeechSynthesizer | None = None
        self._event_queue: asyncio.Queue = asyncio.Queue()
        self._audio_buffer: list[bytes] = []
        self._enable_blendshapes = False

    async def start(self, *, voice_id: str | None = None, **kwargs) -> None:
        """Start Azure TTS streaming.

        Args:
            voice_id: Azure voice name (e.g., "en-US-JennyNeural")
            **kwargs: Additional options
                - enable_blendshapes: Enable blendshape output (default: False)
                - sample_rate: Sample rate (default: 24000)
                - language: Language override (optional)
                - rate: Speech rate (0.5-2.0, default: 1.0)
                - pitch: Pitch adjustment (-50 to +50, default: 0)
        """
        self._validate_not_stopped()

        if not voice_id:
            raise ValueError("voice_id is required for Azure")

        self.voice_id = voice_id
        self._enable_blendshapes = kwargs.get("enable_blendshapes", False)
        self.sample_rate = kwargs.get("sample_rate", self.SAMPLE_RATE)
        self.rate = kwargs.get("rate", 1.0)
        self.pitch = kwargs.get("pitch", 0)

        try:
            # Create speech config
            if self.endpoint:
                self._speech_config = speechsdk.SpeechConfig(
                    subscription=self.subscription_key,
                    endpoint=self.endpoint
                )
            else:
                self._speech_config = speechsdk.SpeechConfig(
                    subscription=self.subscription_key,
                    region=self.region
                )

            # Set voice
            self._speech_config.speech_synthesis_voice_name = voice_id

            # Set output format (PCM 24kHz 16-bit mono)
            self._speech_config.set_speech_synthesis_output_format(
                speechsdk.SpeechSynthesisOutputFormat.Raw24Khz16BitMonoPcm
            )

            # Create synthesizer with push stream
            stream_config = speechsdk.audio.AudioOutputConfig(
                use_default_speaker=False
            )

            self._synthesizer = speechsdk.SpeechSynthesizer(
                speech_config=self._speech_config,
                audio_config=stream_config
            )

            # Connect event handlers
            self._synthesizer.synthesizing.connect(self._on_synthesizing)
            self._synthesizer.synthesis_completed.connect(self._on_synthesis_completed)
            self._synthesizer.synthesis_canceled.connect(self._on_synthesis_canceled)

            # Connect viseme event handler
            self._synthesizer.viseme_received.connect(self._on_viseme_received)

            # Note: Blendshape support requires special SSML and may not be
            # available in all regions/voices. Check Azure docs for details.

            self.logger.info(
                "Azure TTS initialized",
                voice_id=voice_id,
                region=self.region,
                blendshapes=self._enable_blendshapes
            )

            self._started = True

        except Exception as e:
            self.logger.error("Failed to initialize Azure TTS", error=str(e))
            raise ConnectionError(f"Azure TTS initialization failed: {str(e)}") from e

    async def send_text(self, text: str, *, final: bool = False) -> None:
        """Send text to Azure for synthesis.

        Args:
            text: Text to synthesize
            final: Whether this is the final chunk (triggers synthesis)

        Note:
            Azure's SDK doesn't support true incremental streaming from the
            client side. We accumulate text and synthesize when final=True.
        """
        self._validate_started()
        self._validate_not_stopped()

        if not text:
            raise ValueError("Text cannot be empty")

        # Accumulate text in buffer
        if not hasattr(self, '_text_buffer'):
            self._text_buffer = []

        self._text_buffer.append(text)

        # Only synthesize when final
        if final:
            full_text = " ".join(self._text_buffer)
            self._text_buffer = []

            # Build SSML
            ssml = self._build_ssml(full_text)

            self.logger.debug(
                "Starting Azure synthesis",
                text_length=len(full_text),
                voice_id=self.voice_id
            )

            try:
                # Run synthesis in executor (blocking call)
                loop = asyncio.get_event_loop()
                await loop.run_in_executor(
                    None,
                    self._synthesizer.speak_ssml,
                    ssml
                )

            except Exception as e:
                self.logger.error("Azure synthesis failed", error=str(e))
                await self._event_queue.put(
                    StreamEvent(kind="error", error=f"Synthesis failed: {str(e)}")
                )

    async def stop(self) -> None:
        """Stop Azure TTS and clean up."""
        if self._stopped:
            return

        self.logger.info("Stopping Azure TTS streamer")

        # Clean up synthesizer
        if self._synthesizer:
            try:
                # Note: Azure SDK doesn't have explicit close method
                self._synthesizer = None
            except Exception as e:
                self.logger.error("Error cleaning up synthesizer", error=str(e))

        self._stopped = True

        # Put final done event
        try:
            await self._event_queue.put(StreamEvent(kind="done"))
        except Exception:
            pass

    async def aiter_events(self) -> AsyncIterator[StreamEvent]:
        """Async generator yielding StreamEvents from Azure.

        Yields:
            StreamEvent: Normalized events (audio, viseme, blendshapes, done, error)
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

    def _build_ssml(self, text: str) -> str:
        """Build SSML for Azure TTS.

        Args:
            text: Text to synthesize

        Returns:
            SSML string with viseme/blendshape configuration
        """
        # Escape XML special characters
        text = text.replace("&", "&amp;")
        text = text.replace("<", "&lt;")
        text = text.replace(">", "&gt;")

        # Build SSML with viseme support
        ssml = f"""
        <speak version="1.0" xmlns="http://www.w3.org/2001/10/synthesis"
               xmlns:mstts="https://www.w3.org/2001/mstts" xml:lang="en-US">
            <voice name="{self.voice_id}">
                <mstts:viseme type="FacialExpression"/>
        """

        # Add blendshape support if enabled
        if self._enable_blendshapes:
            ssml += '<mstts:viseme type="blendShapes"/>'

        # Add prosody controls
        if self.rate != 1.0 or self.pitch != 0:
            rate_str = f"{self.rate:.1f}"
            pitch_str = f"{self.pitch:+d}%"
            ssml += f'<prosody rate="{rate_str}" pitch="{pitch_str}">{text}</prosody>'
        else:
            ssml += text

        ssml += """
            </voice>
        </speak>
        """

        return ssml

    def _on_synthesizing(self, evt: Any) -> None:
        """Handle synthesizing event (audio chunks).

        This is called from Azure SDK thread, so we need to use thread-safe queue.
        """
        try:
            audio_data = evt.result.audio_data

            if audio_data and len(audio_data) > 0:
                # Encode as base64
                pcm16_b64 = base64.b64encode(audio_data).decode('utf-8')

                event = StreamEvent(
                    kind="audio",
                    pcm16_b64=pcm16_b64,
                    sample_rate=self.sample_rate
                )

                # Put in queue (thread-safe)
                asyncio.run_coroutine_threadsafe(
                    self._event_queue.put(event),
                    asyncio.get_event_loop()
                )

                self.logger.debug("Azure audio chunk", size=len(audio_data))

        except Exception as e:
            self.logger.error("Error in synthesizing callback", error=str(e))

    def _on_viseme_received(self, evt: Any) -> None:
        """Handle viseme event from Azure.

        Azure provides viseme IDs (0-21) with timing information.
        """
        try:
            viseme_id = evt.viseme_id
            audio_offset = evt.audio_offset / 10000  # Convert to milliseconds

            # Convert Azure viseme to OVR if needed
            # For now, pass through Azure viseme ID
            # Client can convert using azure_to_ovr() from mapping.py

            event = StreamEvent(
                kind="viseme",
                viseme=VisemeEvent(
                    viseme_id=viseme_id,
                    audio_offset_ms=audio_offset
                )
            )

            asyncio.run_coroutine_threadsafe(
                self._event_queue.put(event),
                asyncio.get_event_loop()
            )

            self.logger.debug(
                "Azure viseme",
                viseme_id=viseme_id,
                offset_ms=audio_offset
            )

        except Exception as e:
            self.logger.error("Error in viseme callback", error=str(e))

    def _on_synthesis_completed(self, evt: Any) -> None:
        """Handle synthesis completed event."""
        try:
            self.logger.info("Azure synthesis completed")

            # Note: Don't send "done" here yet - wait for stop()
            # This allows multiple send_text() calls

        except Exception as e:
            self.logger.error("Error in completion callback", error=str(e))

    def _on_synthesis_canceled(self, evt: Any) -> None:
        """Handle synthesis canceled event."""
        try:
            reason = evt.cancellation_details.reason
            error_details = evt.cancellation_details.error_details

            self.logger.error(
                "Azure synthesis canceled",
                reason=str(reason),
                details=error_details
            )

            event = StreamEvent(
                kind="error",
                error=f"Azure synthesis canceled: {error_details}"
            )

            asyncio.run_coroutine_threadsafe(
                self._event_queue.put(event),
                asyncio.get_event_loop()
            )

        except Exception as e:
            self.logger.error("Error in cancellation callback", error=str(e))


# Note: Azure blendshape support
# ---------------------------------
# Azure's blendshape feature (ARKit-compatible) is in preview and requires:
# 1. Specific neural voices that support it
# 2. Using <mstts:viseme type="blendShapes"/> in SSML
# 3. Handling the blendshape events (currently not fully exposed in Python SDK)
#
# For full blendshape support, you may need to:
# - Use the REST API directly
# - Use the JavaScript/C# SDK
# - Or wait for Python SDK updates
#
# For now, we provide viseme events (0-21) which can be mapped to basic
# mouth shapes. Clients can use third-party solutions (e.g., ARKit, Live Link)
# for full facial animation.

