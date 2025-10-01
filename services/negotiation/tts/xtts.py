"""XTTS TTS provider."""

import asyncio
import numpy as np
import struct
from typing import AsyncGenerator, Dict, Any

from aiortc.mediastreams import MediaStreamTrack, AudioStreamTrack
import av

from .base import TTSProvider


class XTTSProvider(TTSProvider):
    """TTS provider using Coqui XTTS with a simple text-to-audio implementation."""

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        # Simple implementation for now - no actual XTTS model
        self.model_path = config.get("model_path", "models/xtts")
        self.device = config.get("device", "cpu")
        self.sample_rate = 16000
        self.channels = 1

    async def synthesize_speech(
        self,
        text: str,
        speaker_id: str = "default"
    ) -> AsyncGenerator[bytes, None]:
        """Synthesize speech using a simple waveform generation approach."""
        # Simple text-to-audio implementation
        # In a real implementation, this would use XTTS or similar model

        # Generate a simple sine wave modulated by text characteristics
        duration = len(text) * 0.1  # 100ms per character
        frequency = 220  # Base frequency for speech-like sound

        samples_per_second = self.sample_rate
        total_samples = int(duration * samples_per_second)

        # Generate waveform based on text content
        audio_data = []
        for i in range(total_samples):
            # Create a more complex waveform that sounds speech-like
            t = i / samples_per_second

            # Base tone
            sample = np.sin(2 * np.pi * frequency * t)

            # Add some harmonics and modulation based on text
            for j, char in enumerate(text):
                char_freq = 100 + (ord(char) % 200)
                modulation = np.sin(2 * np.pi * char_freq * t + j * 0.5)
                sample += 0.1 * modulation * np.sin(2 * np.pi * (frequency + char_freq) * t)

            # Add some noise for realism
            noise = np.random.normal(0, 0.01)
            sample += noise

            # Normalize
            sample = np.clip(sample, -0.9, 0.9)

            # Convert to 16-bit PCM
            sample_int = int(sample * 32767)
            audio_data.extend(struct.pack('<h', sample_int))

        # Yield the complete audio data
        yield bytes(audio_data)

    async def get_audio_track(self, text: str, speaker_id: str = "default") -> MediaStreamTrack:
        """Get audio track for synthesized speech."""
        import structlog
        logger = structlog.get_logger(__name__)
        logger.info("Creating TTS audio track", text_length=len(text), speaker_id=speaker_id)
        track = TTSGeneratedAudioTrack(text, self)
        logger.info("TTS audio track created successfully")
        return track

    async def close(self):
        """Clean up resources."""
        pass


class TTSGeneratedAudioTrack(AudioStreamTrack):
    """Audio track that plays pre-generated TTS audio with proper timing."""

    def __init__(self, text: str, tts_provider: XTTSProvider):
        super().__init__()
        self.text = text
        self.tts_provider = tts_provider
        self.audio_data = None
        self.position = 0
        self.sample_rate = tts_provider.sample_rate
        self.frame_size = 1024  # samples per frame
        self.frame_duration = 1.0 / 30.0  # ~33ms per frame for smooth playback
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
                logger.info("Starting audio data generation for TTS", text_length=len(self.text))
                # Generate audio data from text
                async for chunk in self.tts_provider.synthesize_speech(self.text):
                    self.audio_data = chunk
                    logger.info("Audio data generated", size=len(self.audio_data))
                    break
            finally:
                self._generating = False

    async def recv(self):
        """Receive the next audio frame with proper timing."""
        import time
        import structlog
        logger = structlog.get_logger(__name__)

        logger.info("Audio track recv() called", frame_count=self._frame_count, text_length=len(self.text), has_audio_data=self.audio_data is not None)

        await self._ensure_audio_data()

        current_time = time.time()

        # Ensure proper frame timing - be more lenient for the first few frames
        if self._last_frame_time > 0:
            elapsed = current_time - self._last_frame_time
            if elapsed < self.frame_duration:
                await asyncio.sleep(self.frame_duration - elapsed)

        self._last_frame_time = time.time()
        self._frame_count += 1

        logger.debug("Generating audio frame", frame_count=self._frame_count, position=self.position, audio_data_size=len(self.audio_data) if self.audio_data else 0, has_audio_data=self.audio_data is not None)

        if self.audio_data is None:
            # Return silence if no audio data
            return self._create_silence_frame()

        # Calculate frame size in bytes (16-bit samples)
        frame_size_bytes = self.frame_size * 2  # 16-bit samples

        if self.position >= len(self.audio_data):
            # End of audio - return silence to indicate end of stream
            logger.info("End of audio data reached")
            return self._create_silence_frame()

        # Get chunk of audio data for this frame
        end_pos = min(self.position + frame_size_bytes, len(self.audio_data))
        chunk = self.audio_data[self.position:end_pos]
        self.position = end_pos

        # Create audio frame from the chunk
        return self._create_audio_frame(chunk)

    def _create_audio_frame(self, audio_bytes: bytes):
        """Create an AV audio frame from raw PCM bytes."""
        # Convert bytes back to numpy array
        samples = np.frombuffer(audio_bytes, dtype=np.int16)

        # Ensure we have the right number of samples
        if len(samples) < self.frame_size:
            # Pad with zeros if needed
            padding = np.zeros(self.frame_size - len(samples), dtype=np.int16)
            samples = np.concatenate([samples, padding])

        # Create audio frame
        audio_frame = av.AudioFrame.from_ndarray(
            samples.reshape(1, -1),
            format="s16",
            layout="mono"
        )
        audio_frame.sample_rate = self.sample_rate
        # Set time base properly for PyAV
        from fractions import Fraction
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
        # Set time base properly for PyAV
        from fractions import Fraction
        audio_frame.time_base = Fraction(1, self.sample_rate)
        audio_frame.pts = self._frame_count

        return audio_frame
