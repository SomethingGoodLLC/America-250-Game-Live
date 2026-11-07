"""
Audio Feature Extraction for Portrait Animation

Extracts phonemes, visemes, and audio features from PCM16 audio chunks
for driving portrait animation in real-time.
"""

import logging

import numpy as np

logger = logging.getLogger(__name__)

# Import existing phoneme/viseme utilities
try:
    from app.utils.g2p import text_to_phonemes_with_timing
    from app.utils.mapping import phonemes_to_visemes
    UTILS_AVAILABLE = True
except ImportError:
    logger.warning("app.utils not available - phoneme/viseme features disabled")
    UTILS_AVAILABLE = False


class AudioFeatureExtractor:
    """
    Extracts features from audio for portrait animation.

    Supports:
    - Phoneme extraction from text (via TTS providers)
    - Viseme mapping for lip-sync
    - Mel-spectrogram features (for audio-driven models)
    - Energy/amplitude envelope
    """

    def __init__(self, sample_rate: int = 24000):
        """
        Initialize audio feature extractor.

        Args:
            sample_rate: Audio sample rate in Hz
        """
        self.sample_rate = sample_rate
        self.frame_size = int(0.02 * sample_rate)  # 20ms frames

        logger.info(f"AudioFeatureExtractor initialized: sr={sample_rate}, frame={self.frame_size}")

    def extract_energy(self, pcm16_bytes: bytes) -> float:
        """
        Extract RMS energy from PCM16 audio chunk.

        Useful for detecting speech activity and mouth openness.

        Args:
            pcm16_bytes: PCM16 audio data (little-endian)

        Returns:
            RMS energy (normalized 0-1)
        """
        # Convert PCM16 bytes to numpy array
        samples = np.frombuffer(pcm16_bytes, dtype=np.int16).astype(np.float32)

        if len(samples) == 0:
            return 0.0

        # Normalize to [-1, 1]
        samples = samples / 32768.0

        # Calculate RMS
        rms = np.sqrt(np.mean(samples ** 2))

        return float(rms)

    def extract_envelope(self, pcm16_bytes: bytes, window_ms: int = 20) -> list[float]:
        """
        Extract amplitude envelope from audio.

        Returns a smoothed amplitude curve useful for mouth openness animation.

        Args:
            pcm16_bytes: PCM16 audio data
            window_ms: Smoothing window in milliseconds

        Returns:
            List of amplitude values (one per window)
        """
        samples = np.frombuffer(pcm16_bytes, dtype=np.int16).astype(np.float32)
        samples = samples / 32768.0

        if len(samples) == 0:
            return []

        # Calculate window size
        window_size = int(window_ms * self.sample_rate / 1000)

        # Split into windows and calculate RMS per window
        envelope = []
        for i in range(0, len(samples), window_size):
            window = samples[i:i + window_size]
            if len(window) > 0:
                rms = np.sqrt(np.mean(window ** 2))
                envelope.append(float(rms))

        return envelope

    def text_to_viseme_events(
        self,
        text: str,
        duration_ms: float | None = None
    ) -> list[dict]:
        """
        Convert text to viseme events for lip-sync.

        Uses existing phoneme-to-viseme pipeline.

        Args:
            text: Input text
            duration_ms: Total duration to distribute phonemes across

        Returns:
            List of viseme events with timing:
            [{"viseme": "AA", "start_ms": 0, "end_ms": 100}, ...]
        """
        if not UTILS_AVAILABLE:
            logger.warning("Utils not available - cannot generate visemes")
            return []

        try:
            # Convert text to phonemes with timing
            if duration_ms:
                phoneme_timestamps = text_to_phonemes_with_timing(
                    text,
                    total_duration_ms=duration_ms
                )
            else:
                # Use default timing
                phoneme_timestamps = text_to_phonemes_with_timing(text)

            # Convert phonemes to visemes
            viseme_events = phonemes_to_visemes(phoneme_timestamps)

            return viseme_events

        except Exception as e:
            logger.error(f"Failed to generate visemes: {e}")
            return []

    def audio_to_mouth_openness(
        self,
        pcm16_bytes: bytes,
        threshold: float = 0.02,
        max_openness: float = 1.0
    ) -> float:
        """
        Estimate mouth openness from audio energy.

        Simple heuristic: louder = more open mouth.

        Args:
            pcm16_bytes: PCM16 audio data
            threshold: Minimum energy to open mouth
            max_openness: Maximum mouth openness (0-1)

        Returns:
            Mouth openness value (0-1)
        """
        energy = self.extract_energy(pcm16_bytes)

        # Map energy to mouth openness
        if energy < threshold:
            return 0.0

        # Simple linear mapping
        openness = min((energy - threshold) * 10.0, max_openness)
        return float(openness)

    def extract_mel_spectrogram(
        self,
        pcm16_bytes: bytes,
        n_mels: int = 80,
        n_fft: int = 2048,
        hop_length: int = 512
    ) -> np.ndarray | None:
        """
        Extract mel-spectrogram features for audio-driven animation.

        Many talking head models (LivePortrait, SadTalker) use mel-spectrograms
        as audio features.

        Args:
            pcm16_bytes: PCM16 audio data
            n_mels: Number of mel bands
            n_fft: FFT size
            hop_length: Hop length for STFT

        Returns:
            Mel-spectrogram array (n_mels, n_frames) or None if librosa unavailable
        """
        try:
            import librosa
        except ImportError:
            logger.warning("librosa not available - cannot extract mel-spectrogram")
            return None

        # Convert PCM16 to float32
        samples = np.frombuffer(pcm16_bytes, dtype=np.int16).astype(np.float32)
        samples = samples / 32768.0

        if len(samples) == 0:
            return None

        # Extract mel-spectrogram
        mel_spec = librosa.feature.melspectrogram(
            y=samples,
            sr=self.sample_rate,
            n_mels=n_mels,
            n_fft=n_fft,
            hop_length=hop_length
        )

        # Convert to log scale
        mel_spec_db = librosa.power_to_db(mel_spec, ref=np.max)

        return mel_spec_db

    def extract_features_for_animation(
        self,
        pcm16_bytes: bytes,
        text: str | None = None
    ) -> dict:
        """
        Extract all available features for portrait animation.

        Returns a comprehensive feature dict that can be used by various
        animation models.

        Args:
            pcm16_bytes: PCM16 audio data
            text: Optional text for phoneme/viseme extraction

        Returns:
            Dict with keys:
            - energy: RMS energy
            - mouth_openness: Estimated mouth openness (0-1)
            - envelope: Amplitude envelope
            - mel_spectrogram: Mel-spectrogram (if librosa available)
            - viseme_events: Viseme events (if text provided)
        """
        features = {}

        # Basic energy features
        features['energy'] = self.extract_energy(pcm16_bytes)
        features['mouth_openness'] = self.audio_to_mouth_openness(pcm16_bytes)
        features['envelope'] = self.extract_envelope(pcm16_bytes)

        # Advanced features
        mel_spec = self.extract_mel_spectrogram(pcm16_bytes)
        if mel_spec is not None:
            features['mel_spectrogram'] = mel_spec

        # Text-based features
        if text:
            # Calculate approximate duration from audio length
            duration_ms = len(pcm16_bytes) / 2 / self.sample_rate * 1000
            visemes = self.text_to_viseme_events(text, duration_ms)
            features['viseme_events'] = visemes

        return features


# Convenience functions

def pcm16_to_numpy(pcm16_bytes: bytes, normalize: bool = True) -> np.ndarray:
    """
    Convert PCM16 bytes to numpy array.

    Args:
        pcm16_bytes: PCM16 audio data (little-endian)
        normalize: If True, normalize to [-1, 1]

    Returns:
        Numpy array of samples
    """
    samples = np.frombuffer(pcm16_bytes, dtype=np.int16)

    if normalize:
        samples = samples.astype(np.float32) / 32768.0

    return samples


def numpy_to_pcm16(samples: np.ndarray) -> bytes:
    """
    Convert numpy array to PCM16 bytes.

    Args:
        samples: Numpy array (float32 in range [-1, 1] or int16)

    Returns:
        PCM16 bytes (little-endian)
    """
    if samples.dtype == np.float32 or samples.dtype == np.float64:
        # Assume normalized to [-1, 1]
        samples = (samples * 32767).astype(np.int16)
    elif samples.dtype != np.int16:
        samples = samples.astype(np.int16)

    return samples.tobytes()


def estimate_speaking_rate(text: str, audio_duration_ms: float) -> float:
    """
    Estimate speaking rate in words per minute.

    Args:
        text: Spoken text
        audio_duration_ms: Audio duration in milliseconds

    Returns:
        Words per minute
    """
    word_count = len(text.split())
    duration_min = audio_duration_ms / 60000

    if duration_min == 0:
        return 0.0

    return word_count / duration_min

