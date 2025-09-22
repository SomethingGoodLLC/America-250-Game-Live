"""Speech-to-Text providers."""

from .base import STTProvider
from .faster_whisper import FasterWhisperProvider

__all__ = ["STTProvider", "FasterWhisperProvider"]
