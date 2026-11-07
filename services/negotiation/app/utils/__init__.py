"""Utility modules for phoneme-to-viseme mapping and G2P conversion.

This package provides utilities for lip-sync animation:
- mapping: Convert phonemes to visemes for facial animation
- g2p: Convert text to phonemes with timing information
"""

from .g2p import (
    text_to_phonemes,
    text_to_phonemes_with_timing,
    words_to_phoneme_timestamps,
)
from .mapping import (
    ARPABET_TO_OVR,
    arpabet_to_ovr_viseme,
    collapse_visemes,
    phonemes_to_visemes,
)

__all__ = [
    "phonemes_to_visemes",
    "arpabet_to_ovr_viseme",
    "collapse_visemes",
    "ARPABET_TO_OVR",
    "text_to_phonemes",
    "text_to_phonemes_with_timing",
    "words_to_phoneme_timestamps",
]

