"""Grapheme-to-phoneme (G2P) conversion for lip-sync timing.

This module provides lightweight G2P conversion for converting text to phonemes
with timing information. It uses a small embedded CMUdict subset for common words
plus rule-based fallbacks for unknown words.

For production use with high accuracy requirements, consider:
- phonemizer library with espeak-ng backend
- g2p_en library
- Montreal Forced Aligner for precise timing

This implementation prioritizes:
1. Speed: No external dependencies, fast lookups
2. Simplicity: Easy to understand and maintain
3. "Good enough": Accuracy sufficient for realtime lip-sync
"""

import re
from typing import List, Dict, Optional, Tuple
import structlog

logger = structlog.get_logger(__name__)


# CMUdict subset for common English words
# Format: word -> list of phonemes (ARPAbet with stress markers)
WORD_TO_PHONEMES: Dict[str, List[str]] = {
    # Pronouns
    "i": ["AY1"], "you": ["Y", "UW1"], "he": ["HH", "IY1"], "she": ["SH", "IY1"],
    "we": ["W", "IY1"], "they": ["DH", "EY1"], "it": ["IH1", "T"],
    "me": ["M", "IY1"], "my": ["M", "AY1"], "your": ["Y", "AO1", "R"],
    "his": ["HH", "IH1", "Z"], "her": ["HH", "ER1"], "their": ["DH", "EH1", "R"],
    "our": ["AW1", "R"], "its": ["IH1", "T", "S"],
    
    # Common verbs
    "is": ["IH1", "Z"], "are": ["AA1", "R"], "was": ["W", "AA1", "Z"],
    "were": ["W", "ER1"], "be": ["B", "IY1"], "been": ["B", "IH1", "N"],
    "have": ["HH", "AE1", "V"], "has": ["HH", "AE1", "Z"], "had": ["HH", "AE1", "D"],
    "do": ["D", "UW1"], "does": ["D", "AH1", "Z"], "did": ["D", "IH1", "D"],
    "will": ["W", "IH1", "L"], "would": ["W", "UH1", "D"], "could": ["K", "UH1", "D"],
    "should": ["SH", "UH1", "D"], "can": ["K", "AE1", "N"], "may": ["M", "EY1"],
    "must": ["M", "AH1", "S", "T"], "go": ["G", "OW1"], "come": ["K", "AH1", "M"],
    "get": ["G", "EH1", "T"], "make": ["M", "EY1", "K"], "see": ["S", "IY1"],
    "know": ["N", "OW1"], "think": ["TH", "IH1", "NG", "K"], "take": ["T", "EY1", "K"],
    "give": ["G", "IH1", "V"], "find": ["F", "AY1", "N", "D"], "tell": ["T", "EH1", "L"],
    "ask": ["AE1", "S", "K"], "work": ["W", "ER1", "K"], "call": ["K", "AO1", "L"],
    "try": ["T", "R", "AY1"], "need": ["N", "IY1", "D"], "feel": ["F", "IY1", "L"],
    "become": ["B", "IH0", "K", "AH1", "M"], "leave": ["L", "IY1", "V"],
    "put": ["P", "UH1", "T"], "mean": ["M", "IY1", "N"], "keep": ["K", "IY1", "P"],
    "let": ["L", "EH1", "T"], "begin": ["B", "IH0", "G", "IH1", "N"],
    "seem": ["S", "IY1", "M"], "help": ["HH", "EH1", "L", "P"], "show": ["SH", "OW1"],
    "hear": ["HH", "IH1", "R"], "play": ["P", "L", "EY1"], "run": ["R", "AH1", "N"],
    "move": ["M", "UW1", "V"], "like": ["L", "AY1", "K"], "live": ["L", "IH1", "V"],
    "believe": ["B", "IH0", "L", "IY1", "V"], "bring": ["B", "R", "IH1", "NG"],
    "happen": ["HH", "AE1", "P", "AH0", "N"], "write": ["R", "AY1", "T"],
    "sit": ["S", "IH1", "T"], "stand": ["S", "T", "AE1", "N", "D"],
    "lose": ["L", "UW1", "Z"], "pay": ["P", "EY1"], "meet": ["M", "IY1", "T"],
    "include": ["IH0", "N", "K", "L", "UW1", "D"], "continue": ["K", "AH0", "N", "T", "IH1", "N", "Y", "UW0"],
    "set": ["S", "EH1", "T"], "learn": ["L", "ER1", "N"], "change": ["CH", "EY1", "N", "JH"],
    "lead": ["L", "IY1", "D"], "understand": ["AH2", "N", "D", "ER0", "S", "T", "AE1", "N", "D"],
    "watch": ["W", "AA1", "CH"], "follow": ["F", "AA1", "L", "OW0"], "stop": ["S", "T", "AA1", "P"],
    "create": ["K", "R", "IY0", "EY1", "T"], "speak": ["S", "P", "IY1", "K"],
    "read": ["R", "IY1", "D"], "spend": ["S", "P", "EH1", "N", "D"], "grow": ["G", "R", "OW1"],
    "open": ["OW1", "P", "AH0", "N"], "walk": ["W", "AO1", "K"], "win": ["W", "IH1", "N"],
    "teach": ["T", "IY1", "CH"], "offer": ["AO1", "F", "ER0"], "remember": ["R", "IH0", "M", "EH1", "M", "B", "ER0"],
    "consider": ["K", "AH0", "N", "S", "IH1", "D", "ER0"], "appear": ["AH0", "P", "IH1", "R"],
    "buy": ["B", "AY1"], "serve": ["S", "ER1", "V"], "die": ["D", "AY1"],
    "send": ["S", "EH1", "N", "D"], "build": ["B", "IH1", "L", "D"], "stay": ["S", "T", "EY1"],
    "fall": ["F", "AO1", "L"], "cut": ["K", "AH1", "T"], "reach": ["R", "IY1", "CH"],
    "kill": ["K", "IH1", "L"], "raise": ["R", "EY1", "Z"], "pass": ["P", "AE1", "S"],
    "sell": ["S", "EH1", "L"], "decide": ["D", "IH0", "S", "AY1", "D"],
    "return": ["R", "IH0", "T", "ER1", "N"], "explain": ["IH0", "K", "S", "P", "L", "EY1", "N"],
    "hope": ["HH", "OW1", "P"], "develop": ["D", "IH0", "V", "EH1", "L", "AH0", "P"],
    "carry": ["K", "AE1", "R", "IY0"], "break": ["B", "R", "EY1", "K"],
    "receive": ["R", "IH0", "S", "IY1", "V"], "agree": ["AH0", "G", "R", "IY1"],
    "support": ["S", "AH0", "P", "AO1", "R", "T"], "hit": ["HH", "IH1", "T"],
    "produce": ["P", "R", "AH0", "D", "UW1", "S"], "eat": ["IY1", "T"],
    "cover": ["K", "AH1", "V", "ER0"], "catch": ["K", "AE1", "CH"], "draw": ["D", "R", "AO1"],
    
    # Articles & determiners
    "the": ["DH", "AH0"], "a": ["AH0"], "an": ["AE1", "N"],
    "this": ["DH", "IH1", "S"], "that": ["DH", "AE1", "T"],
    "these": ["DH", "IY1", "Z"], "those": ["DH", "OW1", "Z"],
    
    # Conjunctions & prepositions
    "and": ["AH0", "N", "D"], "or": ["AO1", "R"], "but": ["B", "AH1", "T"],
    "if": ["IH1", "F"], "because": ["B", "IH0", "K", "AO1", "Z"],
    "as": ["AE1", "Z"], "when": ["W", "EH1", "N"], "where": ["W", "EH1", "R"],
    "while": ["W", "AY1", "L"], "after": ["AE1", "F", "T", "ER0"],
    "before": ["B", "IH0", "F", "AO1", "R"], "since": ["S", "IH1", "N", "S"],
    "until": ["AH0", "N", "T", "IH1", "L"], "of": ["AH1", "V"], "to": ["T", "UW1"],
    "in": ["IH1", "N"], "on": ["AA1", "N"], "at": ["AE1", "T"],
    "by": ["B", "AY1"], "with": ["W", "IH1", "DH"], "from": ["F", "R", "AH1", "M"],
    "up": ["AH1", "P"], "about": ["AH0", "B", "AW1", "T"], "into": ["IH1", "N", "T", "UW0"],
    "through": ["TH", "R", "UW1"], "during": ["D", "UH1", "R", "IH0", "NG"],
    "including": ["IH0", "N", "K", "L", "UW1", "D", "IH0", "NG"],
    "against": ["AH0", "G", "EH1", "N", "S", "T"], "among": ["AH0", "M", "AH1", "NG"],
    "throughout": ["TH", "R", "UW0", "AW1", "T"], "between": ["B", "IH0", "T", "W", "IY1", "N"],
    "under": ["AH1", "N", "D", "ER0"], "within": ["W", "IH0", "DH", "IH1", "N"],
    "along": ["AH0", "L", "AO1", "NG"], "following": ["F", "AA1", "L", "OW0", "IH0", "NG"],
    "across": ["AH0", "K", "R", "AO1", "S"], "behind": ["B", "IH0", "HH", "AY1", "N", "D"],
    "beyond": ["B", "IH0", "AA1", "N", "D"], "over": ["OW1", "V", "ER0"],
    "without": ["W", "IH0", "DH", "AW1", "T"], "toward": ["T", "AO1", "R", "D"],
    "around": ["AH0", "R", "AW1", "N", "D"], "upon": ["AH0", "P", "AA1", "N"],
    
    # Question words
    "what": ["W", "AH1", "T"], "who": ["HH", "UW1"], "why": ["W", "AY1"],
    "how": ["HH", "AW1"], "which": ["W", "IH1", "CH"],
    
    # Common adjectives
    "good": ["G", "UH1", "D"], "new": ["N", "UW1"], "first": ["F", "ER1", "S", "T"],
    "last": ["L", "AE1", "S", "T"], "long": ["L", "AO1", "NG"], "great": ["G", "R", "EY1", "T"],
    "little": ["L", "IH1", "T", "AH0", "L"], "own": ["OW1", "N"], "other": ["AH1", "DH", "ER0"],
    "old": ["OW1", "L", "D"], "right": ["R", "AY1", "T"], "big": ["B", "IH1", "G"],
    "high": ["HH", "AY1"], "different": ["D", "IH1", "F", "ER0", "AH0", "N", "T"],
    "small": ["S", "M", "AO1", "L"], "large": ["L", "AA1", "R", "JH"],
    "next": ["N", "EH1", "K", "S", "T"], "early": ["ER1", "L", "IY0"],
    "young": ["Y", "AH1", "NG"], "important": ["IH0", "M", "P", "AO1", "R", "T", "AH0", "N", "T"],
    "few": ["F", "Y", "UW1"], "public": ["P", "AH1", "B", "L", "IH0", "K"],
    "bad": ["B", "AE1", "D"], "same": ["S", "EY1", "M"], "able": ["EY1", "B", "AH0", "L"],
    
    # Common nouns
    "time": ["T", "AY1", "M"], "year": ["Y", "IH1", "R"], "people": ["P", "IY1", "P", "AH0", "L"],
    "way": ["W", "EY1"], "day": ["D", "EY1"], "man": ["M", "AE1", "N"],
    "thing": ["TH", "IH1", "NG"], "woman": ["W", "UH1", "M", "AH0", "N"],
    "life": ["L", "AY1", "F"], "child": ["CH", "AY1", "L", "D"], "world": ["W", "ER1", "L", "D"],
    "school": ["S", "K", "UW1", "L"], "state": ["S", "T", "EY1", "T"],
    "family": ["F", "AE1", "M", "AH0", "L", "IY0"], "student": ["S", "T", "UW1", "D", "AH0", "N", "T"],
    "group": ["G", "R", "UW1", "P"], "country": ["K", "AH1", "N", "T", "R", "IY0"],
    "problem": ["P", "R", "AA1", "B", "L", "AH0", "M"], "hand": ["HH", "AE1", "N", "D"],
    "part": ["P", "AA1", "R", "T"], "place": ["P", "L", "EY1", "S"],
    "case": ["K", "EY1", "S"], "week": ["W", "IY1", "K"], "company": ["K", "AH1", "M", "P", "AH0", "N", "IY0"],
    "system": ["S", "IH1", "S", "T", "AH0", "M"], "program": ["P", "R", "OW1", "G", "R", "AE2", "M"],
    "question": ["K", "W", "EH1", "S", "CH", "AH0", "N"], "work": ["W", "ER1", "K"],
    "government": ["G", "AH1", "V", "ER0", "N", "M", "AH0", "N", "T"],
    "number": ["N", "AH1", "M", "B", "ER0"], "night": ["N", "AY1", "T"],
    "point": ["P", "OY1", "N", "T"], "home": ["HH", "OW1", "M"], "water": ["W", "AO1", "T", "ER0"],
    "room": ["R", "UW1", "M"], "mother": ["M", "AH1", "DH", "ER0"], "area": ["EH1", "R", "IY0", "AH0"],
    "money": ["M", "AH1", "N", "IY0"], "story": ["S", "T", "AO1", "R", "IY0"],
    "fact": ["F", "AE1", "K", "T"], "month": ["M", "AH1", "N", "TH"], "lot": ["L", "AA1", "T"],
    "right": ["R", "AY1", "T"], "study": ["S", "T", "AH1", "D", "IY0"],
    "book": ["B", "UH1", "K"], "eye": ["AY1"], "job": ["JH", "AA1", "B"],
    "word": ["W", "ER1", "D"], "business": ["B", "IH1", "Z", "N", "AH0", "S"],
    "issue": ["IH1", "SH", "UW0"], "side": ["S", "AY1", "D"], "kind": ["K", "AY1", "N", "D"],
    "head": ["HH", "EH1", "D"], "house": ["HH", "AW1", "S"], "service": ["S", "ER1", "V", "AH0", "S"],
    "friend": ["F", "R", "EH1", "N", "D"], "father": ["F", "AA1", "DH", "ER0"],
    "power": ["P", "AW1", "ER0"], "hour": ["AW1", "R"], "game": ["G", "EY1", "M"],
    "line": ["L", "AY1", "N"], "end": ["EH1", "N", "D"], "member": ["M", "EH1", "M", "B", "ER0"],
    "law": ["L", "AO1"], "car": ["K", "AA1", "R"], "city": ["S", "IH1", "T", "IY0"],
    "name": ["N", "EY1", "M"], "president": ["P", "R", "EH1", "Z", "AH0", "D", "AH0", "N", "T"],
    "team": ["T", "IY1", "M"], "minute": ["M", "IH1", "N", "AH0", "T"],
    "idea": ["AY0", "D", "IY1", "AH0"], "body": ["B", "AA1", "D", "IY0"],
    "information": ["IH2", "N", "F", "ER0", "M", "EY1", "SH", "AH0", "N"],
    "back": ["B", "AE1", "K"], "parent": ["P", "EH1", "R", "AH0", "N", "T"],
    "face": ["F", "EY1", "S"], "others": ["AH1", "DH", "ER0", "Z"],
    "level": ["L", "EH1", "V", "AH0", "L"], "office": ["AO1", "F", "AH0", "S"],
    "door": ["D", "AO1", "R"], "health": ["HH", "EH1", "L", "TH"],
    "person": ["P", "ER1", "S", "AH0", "N"], "art": ["AA1", "R", "T"],
    "war": ["W", "AO1", "R"], "history": ["HH", "IH1", "S", "T", "ER0", "IY0"],
    "party": ["P", "AA1", "R", "T", "IY0"], "result": ["R", "IH0", "Z", "AH1", "L", "T"],
    "change": ["CH", "EY1", "N", "JH"], "morning": ["M", "AO1", "R", "N", "IH0", "NG"],
    "reason": ["R", "IY1", "Z", "AH0", "N"], "research": ["R", "IY0", "S", "ER1", "CH"],
    "girl": ["G", "ER1", "L"], "guy": ["G", "AY1"], "moment": ["M", "OW1", "M", "AH0", "N", "T"],
    "air": ["EH1", "R"], "teacher": ["T", "IY1", "CH", "ER0"], "force": ["F", "AO1", "R", "S"],
    "education": ["EH2", "JH", "AH0", "K", "EY1", "SH", "AH0", "N"],
    
    # Adverbs
    "not": ["N", "AA1", "T"], "all": ["AO1", "L"], "so": ["S", "OW1"],
    "just": ["JH", "AH1", "S", "T"], "very": ["V", "EH1", "R", "IY0"],
    "only": ["OW1", "N", "L", "IY0"], "now": ["N", "AW1"], "well": ["W", "EH1", "L"],
    "also": ["AO1", "L", "S", "OW0"], "then": ["DH", "EH1", "N"], "here": ["HH", "IH1", "R"],
    "there": ["DH", "EH1", "R"], "still": ["S", "T", "IH1", "L"], "back": ["B", "AE1", "K"],
    "even": ["IY1", "V", "AH0", "N"], "more": ["M", "AO1", "R"], "most": ["M", "OW1", "S", "T"],
    "really": ["R", "IY1", "L", "IY0"], "never": ["N", "EH1", "V", "ER0"],
    "always": ["AO1", "L", "W", "EY0", "Z"], "today": ["T", "AH0", "D", "EY1"],
    "together": ["T", "AH0", "G", "EH1", "DH", "ER0"], "often": ["AO1", "F", "AH0", "N"],
    "however": ["HH", "AW0", "EH1", "V", "ER0"], "already": ["AO0", "L", "R", "EH1", "D", "IY0"],
    "yes": ["Y", "EH1", "S"], "no": ["N", "OW1"], "hello": ["HH", "AH0", "L", "OW1"],
    
    # Numbers
    "one": ["W", "AH1", "N"], "two": ["T", "UW1"], "three": ["TH", "R", "IY1"],
    "four": ["F", "AO1", "R"], "five": ["F", "AY1", "V"], "six": ["S", "IH1", "K", "S"],
    "seven": ["S", "EH1", "V", "AH0", "N"], "eight": ["EY1", "T"], "nine": ["N", "AY1", "N"],
    "ten": ["T", "EH1", "N"], "hundred": ["HH", "AH1", "N", "D", "R", "AH0", "D"],
    "thousand": ["TH", "AW1", "Z", "AH0", "N", "D"],
}


def text_to_phonemes(text: str) -> List[str]:
    """Convert text to ARPAbet phonemes using dictionary lookup.
    
    Uses embedded CMUdict subset for common words and rule-based fallback
    for unknown words.
    
    Args:
        text: Input text to convert
    
    Returns:
        List of ARPAbet phonemes with stress markers
    
    Example:
        >>> text_to_phonemes("hello world")
        ["HH", "AH0", "L", "OW1", "SP", "W", "ER1", "L", "D"]
    """
    # Normalize: lowercase, remove punctuation
    text = text.lower()
    text = re.sub(r'[^\w\s]', '', text)
    
    # Split into words
    words = text.split()
    
    phonemes = []
    for i, word in enumerate(words):
        if word in WORD_TO_PHONEMES:
            phonemes.extend(WORD_TO_PHONEMES[word])
        else:
            # Fallback to simple letter-to-sound rules
            phonemes.extend(_guess_phonemes(word))
        
        # Add pause between words (except after last word)
        if i < len(words) - 1:
            phonemes.append("SP")
    
    return phonemes


def _guess_phonemes(word: str) -> List[str]:
    """Guess phonemes for unknown words using simple letter-to-sound rules.
    
    This is a very basic implementation for fallback only.
    Real G2P systems use complex machine learning models.
    
    Args:
        word: Word to convert
    
    Returns:
        List of guessed phonemes
    """
    if not word:
        return []
    
    # Very basic letter-to-phoneme mapping
    letter_map = {
        'a': 'AE1', 'b': 'B', 'c': 'K', 'd': 'D', 'e': 'EH1',
        'f': 'F', 'g': 'G', 'h': 'HH', 'i': 'IH1', 'j': 'JH',
        'k': 'K', 'l': 'L', 'm': 'M', 'n': 'N', 'o': 'OW1',
        'p': 'P', 'q': 'K', 'r': 'R', 's': 'S', 't': 'T',
        'u': 'UW1', 'v': 'V', 'w': 'W', 'x': 'K S',
        'y': 'Y', 'z': 'Z',
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


def text_to_phonemes_with_timing(
    text: str,
    word_timings: Optional[List[Dict[str, float]]] = None,
    total_duration_ms: Optional[float] = None
) -> List[Dict[str, any]]:
    """Convert text to phonemes with timing information.
    
    If word_timings are provided, distributes phonemes across word durations.
    If total_duration_ms is provided (without word_timings), estimates timing.
    
    Args:
        text: Input text
        word_timings: Optional list of dicts with 'start_ms' and 'end_ms' per word
        total_duration_ms: Optional total duration to distribute evenly
    
    Returns:
        List of dicts with 'phoneme', 'start_ms', 'end_ms'
    
    Example:
        >>> text_to_phonemes_with_timing(
        ...     "hello world",
        ...     word_timings=[
        ...         {"start_ms": 0, "end_ms": 400},
        ...         {"start_ms": 500, "end_ms": 900}
        ...     ]
        ... )
        [
            {"phoneme": "HH", "start_ms": 0, "end_ms": 100},
            {"phoneme": "AH0", "start_ms": 100, "end_ms": 200},
            ...
        ]
    """
    # Get words and their phonemes
    words = re.sub(r'[^\w\s]', '', text.lower()).split()
    
    if word_timings:
        return words_to_phoneme_timestamps(words, word_timings)
    
    elif total_duration_ms:
        # Estimate even distribution
        phonemes = text_to_phonemes(text)
        # Remove pauses for timing calculation
        phonemes_no_pause = [p for p in phonemes if p != "SP"]
        
        if not phonemes_no_pause:
            return []
        
        # Distribute duration evenly
        phoneme_duration = total_duration_ms / len(phonemes_no_pause)
        
        result = []
        current_time = 0.0
        for phoneme in phonemes:
            if phoneme == "SP":
                # Short pause (50ms)
                result.append({
                    "phoneme": phoneme,
                    "start_ms": current_time,
                    "end_ms": current_time + 50
                })
                current_time += 50
            else:
                result.append({
                    "phoneme": phoneme,
                    "start_ms": current_time,
                    "end_ms": current_time + phoneme_duration
                })
                current_time += phoneme_duration
        
        return result
    
    else:
        # No timing info, just return phonemes with estimated durations
        phonemes = text_to_phonemes(text)
        result = []
        current_time = 0.0
        
        from .mapping import estimate_phoneme_duration
        
        for phoneme in phonemes:
            if phoneme == "SP":
                duration = 50.0
            else:
                duration = estimate_phoneme_duration(phoneme)
            
            result.append({
                "phoneme": phoneme,
                "start_ms": current_time,
                "end_ms": current_time + duration
            })
            current_time += duration
        
        return result


def words_to_phoneme_timestamps(
    words: List[str],
    word_timings: List[Dict[str, float]],
) -> List[Dict[str, any]]:
    """Convert word-level timing to phoneme-level timing.
    
    Distributes word duration evenly across its phonemes.
    For better accuracy, use forced alignment (e.g., Montreal Forced Aligner).
    
    Args:
        words: List of words
        word_timings: List of dicts with 'start_ms' and 'end_ms' per word
    
    Returns:
        List of dicts with 'phoneme', 'start_ms', 'end_ms'
    """
    if len(words) != len(word_timings):
        logger.warning(
            "Words and timings length mismatch",
            words_len=len(words),
            timings_len=len(word_timings)
        )
        return []
    
    phoneme_timings = []
    
    for word, timing in zip(words, word_timings):
        word_lower = re.sub(r'[^\w]', '', word.lower())
        
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

