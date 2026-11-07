"""Grapheme-to-phoneme (G2P) conversion for TTS systems.

This module provides utilities to convert text into ARPAbet phonemes
for use with TTS systems that don't provide native phoneme output.

It uses a simple dictionary-based lookup with fallback rules for common
English words and patterns.
"""

import re

import structlog

logger = structlog.get_logger(__name__)


# Common English words to ARPAbet phonemes
# This is a small subset for demonstration. In production, use:
# - CMU Pronouncing Dictionary (http://www.speech.cs.cmu.edu/cgi-bin/cmudict)
# - phonemizer library with espeak backend
# - g2p_en library
WORD_TO_PHONEMES: dict[str, list[str]] = {
    # Common words
    "hello": ["HH", "AH0", "L", "OW1"],
    "hi": ["HH", "AY1"],
    "world": ["W", "ER1", "L", "D"],
    "the": ["DH", "AH0"],
    "a": ["AH0"],
    "an": ["AE1", "N"],
    "and": ["AH0", "N", "D"],
    "or": ["AO1", "R"],
    "but": ["B", "AH1", "T"],
    "is": ["IH1", "Z"],
    "are": ["AA1", "R"],
    "was": ["W", "AA1", "Z"],
    "were": ["W", "ER1"],
    "be": ["B", "IY1"],
    "been": ["B", "IH1", "N"],
    "have": ["HH", "AE1", "V"],
    "has": ["HH", "AE1", "Z"],
    "had": ["HH", "AE1", "D"],
    "do": ["D", "UW1"],
    "does": ["D", "AH1", "Z"],
    "did": ["D", "IH1", "D"],
    "will": ["W", "IH1", "L"],
    "would": ["W", "UH1", "D"],
    "could": ["K", "UH1", "D"],
    "should": ["SH", "UH1", "D"],
    "can": ["K", "AE1", "N"],
    "may": ["M", "EY1"],
    "must": ["M", "AH1", "S", "T"],
    "this": ["DH", "IH1", "S"],
    "that": ["DH", "AE1", "T"],
    "these": ["DH", "IY1", "Z"],
    "those": ["DH", "OW1", "Z"],
    "i": ["AY1"],
    "you": ["Y", "UW1"],
    "he": ["HH", "IY1"],
    "she": ["SH", "IY1"],
    "we": ["W", "IY1"],
    "they": ["DH", "EY1"],
    "it": ["IH1", "T"],
    "me": ["M", "IY1"],
    "my": ["M", "AY1"],
    "your": ["Y", "AO1", "R"],
    "his": ["HH", "IH1", "Z"],
    "her": ["HH", "ER1"],
    "their": ["DH", "EH1", "R"],
    "our": ["AW1", "R"],
    "yes": ["Y", "EH1", "S"],
    "no": ["N", "OW1"],
    "not": ["N", "AA1", "T"],
    "what": ["W", "AH1", "T"],
    "when": ["W", "EH1", "N"],
    "where": ["W", "EH1", "R"],
    "who": ["HH", "UW1"],
    "why": ["W", "AY1"],
    "how": ["HH", "AW1"],
    "all": ["AO1", "L"],
    "each": ["IY1", "CH"],
    "every": ["EH1", "V", "R", "IY0"],
    "some": ["S", "AH1", "M"],
    "any": ["EH1", "N", "IY0"],
    "many": ["M", "EH1", "N", "IY0"],
    "much": ["M", "AH1", "CH"],
    "more": ["M", "AO1", "R"],
    "most": ["M", "OW1", "S", "T"],
    "other": ["AH1", "DH", "ER0"],
    "another": ["AH0", "N", "AH1", "DH", "ER0"],
    "such": ["S", "AH1", "CH"],
    "one": ["W", "AH1", "N"],
    "two": ["T", "UW1"],
    "three": ["TH", "R", "IY1"],
    "four": ["F", "AO1", "R"],
    "five": ["F", "AY1", "V"],
}


def text_to_phonemes(text: str) -> list[str]:
    """Convert text to ARPAbet phonemes using dictionary lookup.

    This is a simple implementation for demonstration. For production use,
    consider using:
    - phonemizer library with espeak-ng backend
    - g2p_en library
    - CMU Pronouncing Dictionary

    Args:
        text: Input text to convert

    Returns:
        List of ARPAbet phonemes

    Example:
        >>> text_to_phonemes("hello world")
        ["HH", "AH0", "L", "OW1", "SP", "W", "ER1", "L", "D"]
    """
    # Normalize text: lowercase, remove punctuation except spaces
    text = text.lower()
    text = re.sub(r'[^\w\s]', '', text)

    # Split into words
    words = text.split()

    phonemes = []
    for i, word in enumerate(words):
        if word in WORD_TO_PHONEMES:
            phonemes.extend(WORD_TO_PHONEMES[word])
        else:
            # Fallback: use simple letter-to-sound rules
            phonemes.extend(_guess_phonemes(word))

        # Add silence marker between words (except after last word)
        if i < len(words) - 1:
            phonemes.append("SP")

    return phonemes


def _guess_phonemes(word: str) -> list[str]:
    """Guess phonemes for unknown words using simple rules.

    This is a very basic implementation. In production, use a proper G2P system.

    Args:
        word: Word to convert

    Returns:
        List of guessed phonemes
    """
    if not word:
        return []

    # Very basic letter-to-phoneme mapping
    letter_map = {
        'a': 'AE1',
        'b': 'B',
        'c': 'K',
        'd': 'D',
        'e': 'EH1',
        'f': 'F',
        'g': 'G',
        'h': 'HH',
        'i': 'IH1',
        'j': 'JH',
        'k': 'K',
        'l': 'L',
        'm': 'M',
        'n': 'N',
        'o': 'OW1',
        'p': 'P',
        'q': 'K',
        'r': 'R',
        's': 'S',
        't': 'T',
        'u': 'UW1',
        'v': 'V',
        'w': 'W',
        'x': 'K S',
        'y': 'Y',
        'z': 'Z',
    }

    phonemes = []
    for char in word:
        if char in letter_map:
            phoneme = letter_map[char]
            if ' ' in phoneme:
                phonemes.extend(phoneme.split())
            else:
                phonemes.append(phoneme)

    return phonemes


def words_to_phoneme_timestamps(
    words: list[str],
    word_timings: list[dict[str, float]],
) -> list[dict[str, any]]:
    """Convert word-level timing to phoneme-level timing.

    This distributes the word duration evenly across its phonemes.
    In production, use a forced aligner like Montreal Forced Aligner
    for accurate phoneme timing.

    Args:
        words: List of words
        word_timings: List of dicts with 'start_ms' and 'end_ms' for each word

    Returns:
        List of dicts with 'phoneme', 'start_ms', 'end_ms'

    Example:
        >>> words_to_phoneme_timestamps(
        ...     ["hello", "world"],
        ...     [{"start_ms": 0, "end_ms": 500}, {"start_ms": 600, "end_ms": 1000}]
        ... )
        [
            {"phoneme": "HH", "start_ms": 0, "end_ms": 125},
            {"phoneme": "AH0", "start_ms": 125, "end_ms": 250},
            ...
        ]
    """
    if len(words) != len(word_timings):
        logger.warning(
            "Words and timings length mismatch",
            words_len=len(words),
            timings_len=len(word_timings)
        )
        return []

    phoneme_timings = []

    for word, timing in zip(words, word_timings, strict=False):
        word_lower = word.lower()
        word_lower = re.sub(r'[^\w]', '', word_lower)

        # Get phonemes for this word
        if word_lower in WORD_TO_PHONEMES:
            phonemes = WORD_TO_PHONEMES[word_lower]
        else:
            phonemes = _guess_phonemes(word_lower)

        if not phonemes:
            continue

        # Distribute duration evenly across phonemes
        start_ms = timing["start_ms"]
        end_ms = timing["end_ms"]
        duration_ms = end_ms - start_ms
        phoneme_duration = duration_ms / len(phonemes)

        for i, phoneme in enumerate(phonemes):
            phoneme_start = start_ms + (i * phoneme_duration)
            phoneme_end = phoneme_start + phoneme_duration

            phoneme_timings.append({
                "phoneme": phoneme,
                "start_ms": round(phoneme_start, 2),
                "end_ms": round(phoneme_end, 2)
            })

    return phoneme_timings


def load_cmu_dict(dict_path: str | None = None) -> None:
    """Load CMU Pronouncing Dictionary for better phoneme accuracy.

    Downloads and parses the CMU dict if not already loaded.
    This significantly improves phoneme accuracy over the basic dictionary.

    Args:
        dict_path: Optional path to CMU dict file. If None, downloads from web.

    Note:
        This is a placeholder for production implementation.
        Use the `cmudict` library or similar for real implementation.
    """
    # TODO: Implement CMU dict loading
    # For now, just log that we'd load it
    logger.info("CMU dict loading not yet implemented", dict_path=dict_path)
    pass


# Optional: Integration point for external phonemizer libraries
try:
    from phonemizer import phonemize
    from phonemizer.backend import EspeakBackend

    PHONEMIZER_AVAILABLE = True
    logger.info("Phonemizer library available, will use espeak-ng for G2P")

    def text_to_phonemes_espeak(text: str, language: str = "en-us") -> list[str]:
        """Convert text to phonemes using espeak-ng backend.

        This requires the phonemizer library and espeak-ng to be installed:
            pip install phonemizer
            apt-get install espeak-ng (Linux)
            brew install espeak-ng (macOS)

        Args:
            text: Input text
            language: Language code (default "en-us")

        Returns:
            List of IPA or ARPAbet phonemes
        """
        try:
            EspeakBackend(language)
            phonemes_str = phonemize(
                text,
                language=language,
                backend='espeak',
                separator={'word': ' ', 'phone': ' '},
                strip=True
            )
            return phonemes_str.split()
        except Exception as e:
            logger.error("Espeak phonemization failed", error=str(e))
            # Fallback to dictionary
            return text_to_phonemes(text)

except ImportError:
    PHONEMIZER_AVAILABLE = False
    logger.info("Phonemizer library not available, using basic dictionary")

    def text_to_phonemes_espeak(text: str, language: str = "en-us") -> list[str]:
        """Fallback when phonemizer not available."""
        logger.warning("Phonemizer not available, using basic dictionary")
        return text_to_phonemes(text)

