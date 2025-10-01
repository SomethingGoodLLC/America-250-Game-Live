"""ElevenLabs TTS provider using official SDK."""

import asyncio
import struct
from typing import AsyncGenerator, Dict, Any

from aiortc.mediastreams import MediaStreamTrack, AudioStreamTrack
import numpy as np
from fractions import Fraction

from .base import TTSProvider


class ElevenLabsProvider(TTSProvider):
    """TTS provider using ElevenLabs official SDK."""

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.api_key = config.get("api_key", "")
        self.voice_id = config.get("voice_id", "JBFqnCBsd6RMkjVDRZzb")  # George voice (from your example)
        self.model = config.get("model", "eleven_multilingual_v2")
        # CRITICAL: Must match ElevenLabs API output format (mp3_44100_128)
        # Using wrong sample rate causes audio to play slowed down and "creepy"
        self.sample_rate = 44100  # Match ElevenLabs output format
        self.channels = 1

        # Initialize ElevenLabs client
        try:
            from elevenlabs.client import ElevenLabs
            import structlog
            logger = structlog.get_logger(__name__)
            self.client = ElevenLabs(api_key=self.api_key)
            logger.info("ElevenLabs client initialized successfully")
        except ImportError:
            import structlog
            logger = structlog.get_logger(__name__)
            logger.error("ElevenLabs SDK not installed")
            self.client = None
        except Exception as e:
            import structlog
            logger = structlog.get_logger(__name__)
            logger.error("Failed to initialize ElevenLabs client", error=str(e))
            self.client = None

    async def synthesize_speech(
        self,
        text: str,
        speaker_id: str = "default"
    ) -> AsyncGenerator[bytes, None]:
        """Synthesize speech using ElevenLabs official SDK."""
        import structlog
        logger = structlog.get_logger(__name__)

        if not self.client:
            logger.error("ElevenLabs client not available")
            yield self._generate_fallback_audio(text)
            return

        try:
            logger.info("Starting ElevenLabs TTS synthesis", text_length=len(text))

            # Use ElevenLabs SDK to generate audio
            # Request MP3 format for better compatibility
            audio_generator = self.client.text_to_speech.convert(
                text=text,
                voice_id=self.voice_id,
                model_id=self.model,
                output_format="mp3_44100_128",  # MP3 at 44kHz, 128kbps (from your example)
            )

            # Convert the generator to bytes (it's a regular generator, not async)
            audio_data = b""
            for chunk in audio_generator:
                if hasattr(chunk, 'read'):
                    # File-like object
                    audio_data += chunk.read()
                else:
                    # Raw bytes
                    audio_data += chunk

            logger.info("ElevenLabs TTS synthesis completed", audio_size=len(audio_data))

            if audio_data:
                # Convert MP3 to PCM for WebRTC
                pcm_data = await self._convert_mp3_to_pcm(audio_data)
                if pcm_data:
                    yield pcm_data
                else:
                    logger.warning("Failed to convert MP3 to PCM")
                    yield self._generate_fallback_audio(text)
            else:
                logger.error("No audio data received from ElevenLabs")
                yield self._generate_fallback_audio(text)

        except Exception as e:
            logger.error("ElevenLabs TTS synthesis failed", error=str(e))
            # Fallback to simple synthesis
            yield self._generate_fallback_audio(text)

    async def _convert_mp3_to_pcm(self, mp3_data: bytes) -> bytes:
        """Convert MP3 audio data to PCM format for WebRTC."""
        import structlog
        logger = structlog.get_logger(__name__)

        try:
            import io
            import av

            # Use PyAV to decode MP3 to PCM
            with io.BytesIO(mp3_data) as mp3_buffer:
                container = av.open(mp3_buffer)
                pcm_frames = []

                for frame in container.decode(audio=0):
                    # Convert to PCM
                    if hasattr(frame, 'to_ndarray'):
                        import numpy as np
                        frame_array = frame.to_ndarray()
                        # Ensure 16-bit PCM format
                        if frame_array.dtype != np.int16:
                            frame_array = (frame_array * 32767).astype(np.int16)
                        pcm_frames.append(frame_array.tobytes())

                if pcm_frames:
                    combined_pcm = b''.join(pcm_frames)
                    logger.info("Successfully converted MP3 to PCM", size=len(combined_pcm))
                    return combined_pcm
                else:
                    logger.warning("No audio frames found in MP3")
                    return None

        except Exception as e:
            logger.warning("Failed to convert MP3 to PCM", error=str(e))
            return None

    def _generate_fallback_audio(self, text: str) -> bytes:
        """Generate simple fallback audio if ElevenLabs fails."""
        # Simple waveform generation as fallback
        duration = len(text) * 0.1  # 100ms per character
        sample_rate = self.sample_rate  # Use consistent sample rate (44100)
        frequency = 220

        samples_per_second = sample_rate
        total_samples = int(duration * samples_per_second)

        audio_data = []
        for i in range(total_samples):
            t = i / samples_per_second
            sample = np.sin(2 * np.pi * frequency * t)

            # Add harmonics based on text
            for j, char in enumerate(text):
                char_freq = 100 + (ord(char) % 200)
                modulation = np.sin(2 * np.pi * char_freq * t + j * 0.5)
                sample += 0.1 * modulation * np.sin(2 * np.pi * (frequency + char_freq) * t)

            # Normalize
            sample = np.clip(sample, -0.9, 0.9)
            sample_int = int(sample * 32767)
            audio_data.extend(struct.pack('<h', sample_int))

        return bytes(audio_data)

    async def get_audio_track(self, text: str, speaker_id: str = "default") -> MediaStreamTrack:
        """Get audio track for synthesized speech."""
        import structlog
        logger = structlog.get_logger(__name__)
        logger.info("Creating ElevenLabs audio track", text_length=len(text), voice_id=self.voice_id)
        track = ElevenLabsAudioTrack(text, self)
        return track

    async def close(self):
        """Clean up resources."""
        pass


class ElevenLabsAudioTrack(AudioStreamTrack):
    """Audio track that plays ElevenLabs TTS audio with proper timing."""

    def __init__(self, text: str, tts_provider: ElevenLabsProvider):
        super().__init__()
        self.text = text
        self.tts_provider = tts_provider
        self.audio_data = None
        self.position = 0
        self.sample_rate = tts_provider.sample_rate  # Use provider's sample rate (44100)
        self.frame_size = 1024
        self.frame_duration = 1.0 / 30.0
        self._generating = False
        self._last_frame_time = 0
        self._frame_count = 0

    async def _ensure_audio_data(self):
        """Generate audio data if not already available."""
        if self.audio_data is None and not self._generating:
            self._generating = True
            try:
                import structlog
                logger = structlog.get_logger(__name__)
                logger.info("Starting ElevenLabs audio data generation", text_length=len(self.text))
                # Generate audio data from text
                async for chunk in self.tts_provider.synthesize_speech(self.text):
                    self.audio_data = chunk
                    logger.info("ElevenLabs audio data generated", size=len(self.audio_data))
                    break
            finally:
                self._generating = False

    async def recv(self):
        """Receive the next audio frame with proper timing."""
        import time
        import structlog
        logger = structlog.get_logger(__name__)

        await self._ensure_audio_data()

        current_time = time.time()

        # Ensure proper frame timing
        if self._last_frame_time > 0:
            elapsed = current_time - self._last_frame_time
            if elapsed < self.frame_duration:
                await asyncio.sleep(self.frame_duration - elapsed)

        self._last_frame_time = time.time()
        self._frame_count += 1

        if self.audio_data is None:
            return self._create_silence_frame()

        # Calculate frame size in bytes (16-bit samples)
        frame_size_bytes = self.frame_size * 2

        if self.position >= len(self.audio_data):
            logger.info("End of ElevenLabs audio data reached")
            return self._create_silence_frame()

        # Get chunk of audio data for this frame
        end_pos = min(self.position + frame_size_bytes, len(self.audio_data))
        chunk = self.audio_data[self.position:end_pos]
        self.position = end_pos

        return self._create_audio_frame(chunk)

    def _create_audio_frame(self, audio_bytes: bytes):
        """Create an AV audio frame from raw PCM bytes."""
        # Convert bytes back to numpy array
        samples = np.frombuffer(audio_bytes, dtype=np.int16)

        # Ensure we have the right number of samples
        if len(samples) < self.frame_size:
            padding = np.zeros(self.frame_size - len(samples), dtype=np.int16)
            samples = np.concatenate([samples, padding])

        # Create audio frame
        audio_frame = av.AudioFrame.from_ndarray(
            samples.reshape(1, -1),
            format="s16",
            layout="mono"
        )
        audio_frame.sample_rate = self.sample_rate
        audio_frame.time_base = Fraction(1, self.sample_rate)
        audio_frame.pts = self._frame_count

        return audio_frame

    def _create_silence_frame(self):
        """Create a silence audio frame."""
        samples = np.zeros(self.frame_size, dtype=np.int16)
        audio_frame = av.AudioFrame.from_ndarray(
            samples.reshape(1, -1),
            format="s16",
            layout="mono"
        )
        audio_frame.sample_rate = self.sample_rate
        audio_frame.time_base = Fraction(1, self.sample_rate)
        audio_frame.pts = self._frame_count

        return audio_frame
