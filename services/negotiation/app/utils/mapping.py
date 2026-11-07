"""Phoneme to viseme mapping for lip-sync animation.

This module provides utilities to convert ARPAbet phonemes to OVR viseme keys
for driving facial animation systems (Oculus Lip Sync, Unity, Unreal, etc.).

Viseme IDs:
    - PP (1): b, p, m - Lips together
    - FF (2): f, v - Lower lip to upper teeth
    - TH (3): th - Tongue between teeth
    - DD (4): t, d, n, l - Tongue to alveolar ridge
    - KK (5): k, g, ng - Back of tongue to soft palate
    - CH (6): ch, j, sh, zh - Tongue near hard palate
    - SS (7): s, z - Tongue near alveolar ridge (narrow gap)
    - RR (9): r - Retroflex tongue
    - AA (10): aa, ah, ao - Open jaw
    - E (11): eh, ey - Slightly open
    - I (12): ih, iy - Narrow mouth
    - O (13): ow, oy - Rounded lips
    - U (14): uh, uw - Very rounded lips
    - SIL (0): Silence/neutral

Reference: https://developer.oculus.com/documentation/native/audio-ovrlipsync/
"""

import structlog

logger = structlog.get_logger(__name__)


# ARPAbet to OVR Lip Sync viseme mapping
ARPABET_TO_OVR: dict[str, str] = {
    # Silence
    "SIL": "SIL",
    "SP": "SIL",

    # Bilabial stops (lips together)
    "P": "PP",
    "B": "PP",
    "M": "PP",

    # Labiodental fricatives (lower lip to teeth)
    "F": "FF",
    "V": "FF",

    # Dental fricatives (tongue between teeth)
    "TH": "TH",
    "DH": "TH",

    # Alveolar stops (tongue to ridge)
    "T": "DD",
    "D": "DD",
    "N": "DD",
    "L": "DD",

    # Velar stops (back of tongue)
    "K": "KK",
    "G": "KK",
    "NG": "KK",

    # Palatal/postalveolar (tongue near palate)
    "CH": "CH",
    "JH": "CH",
    "SH": "CH",
    "ZH": "CH",

    # Alveolar fricatives (narrow gap)
    "S": "SS",
    "Z": "SS",

    # Retroflex approximant
    "R": "RR",
    "ER": "RR",

    # Open vowels
    "AA": "AA",  # father
    "AE": "AA",  # at
    "AH": "AA",  # hut
    "AW": "AA",  # cow (starts open)
    "AY": "AA",  # hide (starts open)

    # Mid vowels
    "EH": "E",   # bed
    "EY": "E",   # ate
    "AO": "O",   # ought

    # Close vowels
    "IH": "I",   # it
    "IY": "I",   # eat
    "OW": "O",   # oat
    "OY": "O",   # toy
    "UH": "U",   # hood
    "UW": "U",   # two

    # Glides/approximants
    "W": "U",    # Similar to U
    "Y": "I",    # Similar to I
    "HH": "SIL", # h is visually silent
}


# Add stress-marked variants (AA0, AA1, AA2, etc.)
_base_map = dict(ARPABET_TO_OVR)
for phoneme, viseme in _base_map.items():
    if phoneme not in ("SIL", "SP"):
        for stress in ("0", "1", "2"):
            ARPABET_TO_OVR[f"{phoneme}{stress}"] = viseme


def arpabet_to_ovr_viseme(phoneme: str) -> str:
    """Convert single ARPAbet phoneme to OVR viseme key.

    Args:
        phoneme: ARPAbet phoneme (e.g., "AA", "B", "SH", "AA1")

    Returns:
        OVR viseme key (e.g., "AA", "PP", "CH", "SIL")

    Examples:
        >>> arpabet_to_ovr_viseme("AA")
        'AA'
        >>> arpabet_to_ovr_viseme("P")
        'PP'
        >>> arpabet_to_ovr_viseme("AA1")
        'AA'
        >>> arpabet_to_ovr_viseme("UNKNOWN")
        'SIL'
    """
    # Strip stress markers if not in map
    if phoneme not in ARPABET_TO_OVR:
        phoneme_base = phoneme.rstrip("012")
        return ARPABET_TO_OVR.get(phoneme_base, "SIL")

    return ARPABET_TO_OVR.get(phoneme, "SIL")


class VisemeEvent:
    """A viseme event with timing information."""

    def __init__(self, viseme: str, start_ms: float, end_ms: float):
        """Initialize viseme event.

        Args:
            viseme: OVR viseme key (e.g., "PP", "AA")
            start_ms: Start time in milliseconds
            end_ms: End time in milliseconds
        """
        self.viseme = viseme
        self.start_ms = start_ms
        self.end_ms = end_ms

    @property
    def duration_ms(self) -> float:
        """Get duration in milliseconds."""
        return self.end_ms - self.start_ms

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {
            "viseme": self.viseme,
            "start_ms": round(self.start_ms, 2),
            "end_ms": round(self.end_ms, 2),
            "duration_ms": round(self.duration_ms, 2)
        }

    def __repr__(self) -> str:
        return f"VisemeEvent({self.viseme}, {self.start_ms:.1f}-{self.end_ms:.1f}ms)"


def phonemes_to_visemes(
    phoneme_timestamps: list[dict[str, any]],
    collapse_similar: bool = True,
    min_duration_ms: float = 30.0
) -> list[dict[str, any]]:
    """Convert phoneme timestamps to viseme events.

    This function:
    1. Converts each phoneme to its corresponding viseme
    2. Optionally collapses consecutive similar visemes
    3. Filters out very short visemes (below min_duration_ms)

    Args:
        phoneme_timestamps: List of dicts with keys:
            - phoneme: ARPAbet phoneme
            - start_ms: Start time in milliseconds
            - end_ms: End time in milliseconds
        collapse_similar: If True, merge consecutive identical visemes
        min_duration_ms: Minimum duration for a viseme (shorter ones are merged)

    Returns:
        List of viseme event dicts with keys:
            - viseme: OVR viseme key
            - start_ms: Start time
            - end_ms: End time
            - duration_ms: Duration

    Example:
        >>> phonemes = [
        ...     {"phoneme": "HH", "start_ms": 0, "end_ms": 50},
        ...     {"phoneme": "AH0", "start_ms": 50, "end_ms": 150},
        ...     {"phoneme": "L", "start_ms": 150, "end_ms": 200},
        ...     {"phoneme": "OW1", "start_ms": 200, "end_ms": 350}
        ... ]
        >>> visemes = phonemes_to_visemes(phonemes)
        >>> print(visemes)
        [
            {"viseme": "SIL", "start_ms": 0, "end_ms": 50, "duration_ms": 50},
            {"viseme": "AA", "start_ms": 50, "end_ms": 150, "duration_ms": 100},
            {"viseme": "DD", "start_ms": 150, "end_ms": 200, "duration_ms": 50},
            {"viseme": "O", "start_ms": 200, "end_ms": 350, "duration_ms": 150}
        ]
    """
    if not phoneme_timestamps:
        return []

    # Convert phonemes to visemes
    viseme_events = []
    for p in phoneme_timestamps:
        phoneme = p.get("phoneme", "SIL")
        start_ms = p.get("start_ms", 0)
        end_ms = p.get("end_ms", 0)

        viseme = arpabet_to_ovr_viseme(phoneme)
        viseme_events.append(VisemeEvent(viseme, start_ms, end_ms))

    # Collapse consecutive similar visemes if requested
    if collapse_similar:
        viseme_events = collapse_visemes(viseme_events, min_duration_ms)

    # Convert to dict format
    return [v.to_dict() for v in viseme_events]


def collapse_visemes(
    viseme_events: list[VisemeEvent],
    min_duration_ms: float = 30.0
) -> list[VisemeEvent]:
    """Collapse consecutive identical visemes into single events.

    This reduces the number of viseme changes and creates smoother animation.
    Very short visemes (< min_duration_ms) are also merged with neighbors.

    Args:
        viseme_events: List of VisemeEvent objects
        min_duration_ms: Minimum duration for a viseme

    Returns:
        List of collapsed VisemeEvent objects
    """
    if not viseme_events:
        return []

    collapsed = []
    current = viseme_events[0]

    for next_event in viseme_events[1:]:
        # If same viseme, extend the current event
        if next_event.viseme == current.viseme:
            current.end_ms = next_event.end_ms
        else:
            # Different viseme
            # Check if current is too short
            if current.duration_ms < min_duration_ms and collapsed:
                # Merge with previous
                collapsed[-1].end_ms = current.end_ms
            elif current.duration_ms < min_duration_ms:
                # First event is too short, extend it
                current.end_ms = next_event.start_ms
            else:
                # Keep current
                collapsed.append(current)

            current = next_event

    # Add last event
    if current.duration_ms >= min_duration_ms or not collapsed:
        collapsed.append(current)
    elif collapsed:
        collapsed[-1].end_ms = current.end_ms

    logger.debug(
        "Collapsed visemes",
        original_count=len(viseme_events),
        collapsed_count=len(collapsed)
    )

    return collapsed


def get_viseme_name(viseme_key: str) -> str:
    """Get human-readable name for a viseme.

    Args:
        viseme_key: OVR viseme key (e.g., "PP", "AA")

    Returns:
        Human-readable name
    """
    names = {
        "SIL": "Silence",
        "PP": "Lips together (p, b, m)",
        "FF": "Lip to teeth (f, v)",
        "TH": "Tongue between teeth (th)",
        "DD": "Tongue to ridge (t, d, n, l)",
        "KK": "Back of tongue (k, g, ng)",
        "CH": "Tongue near palate (ch, j, sh)",
        "SS": "Narrow gap (s, z)",
        "RR": "Retroflex (r)",
        "AA": "Open jaw (aa, ah)",
        "E": "Slightly open (eh, ey)",
        "I": "Narrow mouth (ih, iy)",
        "O": "Rounded lips (ow, oy)",
        "U": "Very rounded (uh, uw)",
    }
    return names.get(viseme_key, "Unknown")


# Precomputed phoneme durations for estimation (in milliseconds)
# Based on typical English speech at normal speed
PHONEME_DURATIONS: dict[str, tuple[float, float]] = {
    # Stops (short)
    "P": (50, 100), "B": (50, 100), "T": (50, 100),
    "D": (50, 100), "K": (50, 100), "G": (50, 100),

    # Fricatives (medium)
    "F": (80, 150), "V": (80, 150), "TH": (80, 150), "DH": (80, 150),
    "S": (100, 180), "Z": (100, 180), "SH": (100, 180), "ZH": (100, 180),
    "HH": (60, 100),

    # Affricates
    "CH": (100, 150), "JH": (100, 150),

    # Nasals and liquids
    "M": (60, 120), "N": (60, 120), "NG": (60, 120),
    "L": (60, 120), "R": (70, 130),

    # Short vowels
    "IH": (80, 140), "EH": (80, 140), "AE": (80, 140), "UH": (80, 140),

    # Long vowels and diphthongs
    "IY": (100, 200), "EY": (100, 200), "AA": (100, 200), "AO": (100, 200),
    "OW": (100, 200), "UW": (100, 200), "AW": (120, 220), "AY": (120, 220),
    "OY": (120, 220), "ER": (100, 180), "AH": (80, 140),

    # Glides
    "W": (60, 100), "Y": (60, 100),
}


def estimate_phoneme_duration(phoneme: str) -> float:
    """Estimate typical duration for a phoneme in milliseconds.

    Args:
        phoneme: ARPAbet phoneme

    Returns:
        Estimated duration in milliseconds (average of min/max)
    """
    phoneme_base = phoneme.rstrip("012")
    min_dur, max_dur = PHONEME_DURATIONS.get(phoneme_base, (80, 120))
    return (min_dur + max_dur) / 2

