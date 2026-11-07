"""ARPAbet to Oculus OVR Lip Sync viseme mapping.

This module provides mappings from ARPAbet phonemes (used by most TTS systems)
to Oculus OVR viseme IDs for lip-sync animation. Also includes SAPI/Azure viseme
mappings for providers that use Microsoft's viseme system.

References:
    - ARPAbet: https://en.wikipedia.org/wiki/ARPABET
    - OVR Lip Sync: https://developer.oculus.com/documentation/native/audio-ovrlipsync/
    - Azure Visemes: https://learn.microsoft.com/en-us/azure/cognitive-services/speech-service/how-to-speech-synthesis-viseme
"""

from enum import IntEnum


class OVRViseme(IntEnum):
    """Oculus OVR Lip Sync viseme IDs."""
    SIL = 0      # Silence
    PP = 1       # p, b, m
    FF = 2       # f, v
    TH = 3       # th (voiced and unvoiced)
    DD = 4       # t, d
    KK = 5       # k, g
    CH = 6       # ch, j, sh, zh
    SS = 7       # s, z
    NN = 8       # n, l
    RR = 9       # r
    AA = 10      # a (as in "father")
    E = 11       # e (as in "bed")
    I = 12       # noqa: E741  # i (as in "sit")
    O = 13       # noqa: E741  # o (as in "go")
    U = 14       # u (as in "put")


class AzureViseme(IntEnum):
    """Azure/SAPI viseme IDs (0-21)."""
    SIL = 0      # Silence
    AE_AX_AH = 1 # æ ʌ ə
    AA = 2       # ɑ
    AO = 3       # ɔ
    EY_EH_UH = 4 # e ɛ ʊ
    ER = 5       # ɚ
    Y_IY_IH_IX = 6 # j i ɪ ɨ
    W_UW = 7     # w u
    OW = 8       # o
    AW = 9       # aʊ
    OY = 10      # ɔɪ
    AY = 11      # aɪ
    H = 12       # h
    R = 13       # ɹ
    L = 14       # l
    S_Z = 15     # s z
    SH_CH_JH_ZH = 16 # ʃ tʃ dʒ ʒ
    TH_DH = 17   # θ ð
    F_V = 18     # f v
    D_T_N = 19   # d t n
    K_G_NG = 20  # k ɡ ŋ
    P_B_M = 21   # p b m


# ARPAbet to OVR Viseme mapping
# ARPAbet uses two-letter codes (e.g., "AA", "AE", "B", "CH")
ARPABET_TO_OVR: dict[str, int] = {
    # Silence
    "SIL": OVRViseme.SIL,
    "SP": OVRViseme.SIL,

    # Consonants - Stops
    "P": OVRViseme.PP,
    "B": OVRViseme.PP,
    "T": OVRViseme.DD,
    "D": OVRViseme.DD,
    "K": OVRViseme.KK,
    "G": OVRViseme.KK,

    # Consonants - Fricatives
    "F": OVRViseme.FF,
    "V": OVRViseme.FF,
    "TH": OVRViseme.TH,
    "DH": OVRViseme.TH,
    "S": OVRViseme.SS,
    "Z": OVRViseme.SS,
    "SH": OVRViseme.CH,
    "ZH": OVRViseme.CH,
    "HH": OVRViseme.SIL,  # h is typically silent visually

    # Consonants - Affricates
    "CH": OVRViseme.CH,
    "JH": OVRViseme.CH,

    # Consonants - Nasals
    "M": OVRViseme.PP,
    "N": OVRViseme.NN,
    "NG": OVRViseme.NN,

    # Consonants - Liquids
    "L": OVRViseme.NN,
    "R": OVRViseme.RR,

    # Consonants - Glides
    "W": OVRViseme.U,
    "Y": OVRViseme.I,

    # Vowels - Monophthongs
    "AA": OVRViseme.AA,  # odd     ɑ
    "AE": OVRViseme.AA,  # at      æ
    "AH": OVRViseme.AA,  # hut     ʌ
    "AO": OVRViseme.O,   # ought   ɔ
    "AW": OVRViseme.AA,  # cow     aʊ
    "AY": OVRViseme.AA,  # hide    aɪ
    "EH": OVRViseme.E,   # Ed      ɛ
    "ER": OVRViseme.RR,  # hurt    ɚ
    "EY": OVRViseme.E,   # ate     eɪ
    "IH": OVRViseme.I,   # it      ɪ
    "IY": OVRViseme.I,   # eat     i
    "OW": OVRViseme.O,   # oat     oʊ
    "OY": OVRViseme.O,   # toy     ɔɪ
    "UH": OVRViseme.U,   # hood    ʊ
    "UW": OVRViseme.U,   # two     u

    # Additional variants (stress markers)
    "AA0": OVRViseme.AA, "AA1": OVRViseme.AA, "AA2": OVRViseme.AA,
    "AE0": OVRViseme.AA, "AE1": OVRViseme.AA, "AE2": OVRViseme.AA,
    "AH0": OVRViseme.AA, "AH1": OVRViseme.AA, "AH2": OVRViseme.AA,
    "AO0": OVRViseme.O,  "AO1": OVRViseme.O,  "AO2": OVRViseme.O,
    "AW0": OVRViseme.AA, "AW1": OVRViseme.AA, "AW2": OVRViseme.AA,
    "AY0": OVRViseme.AA, "AY1": OVRViseme.AA, "AY2": OVRViseme.AA,
    "EH0": OVRViseme.E,  "EH1": OVRViseme.E,  "EH2": OVRViseme.E,
    "ER0": OVRViseme.RR, "ER1": OVRViseme.RR, "ER2": OVRViseme.RR,
    "EY0": OVRViseme.E,  "EY1": OVRViseme.E,  "EY2": OVRViseme.E,
    "IH0": OVRViseme.I,  "IH1": OVRViseme.I,  "IH2": OVRViseme.I,
    "IY0": OVRViseme.I,  "IY1": OVRViseme.I,  "IY2": OVRViseme.I,
    "OW0": OVRViseme.O,  "OW1": OVRViseme.O,  "OW2": OVRViseme.O,
    "OY0": OVRViseme.O,  "OY1": OVRViseme.O,  "OY2": OVRViseme.O,
    "UH0": OVRViseme.U,  "UH1": OVRViseme.U,  "UH2": OVRViseme.U,
    "UW0": OVRViseme.U,  "UW1": OVRViseme.U,  "UW2": OVRViseme.U,
}


# ARPAbet to Azure Viseme mapping (for reference/conversion)
ARPABET_TO_AZURE: dict[str, int] = {
    # Silence
    "SIL": AzureViseme.SIL,
    "SP": AzureViseme.SIL,

    # Consonants
    "P": AzureViseme.P_B_M,
    "B": AzureViseme.P_B_M,
    "M": AzureViseme.P_B_M,
    "T": AzureViseme.D_T_N,
    "D": AzureViseme.D_T_N,
    "N": AzureViseme.D_T_N,
    "K": AzureViseme.K_G_NG,
    "G": AzureViseme.K_G_NG,
    "NG": AzureViseme.K_G_NG,
    "F": AzureViseme.F_V,
    "V": AzureViseme.F_V,
    "TH": AzureViseme.TH_DH,
    "DH": AzureViseme.TH_DH,
    "S": AzureViseme.S_Z,
    "Z": AzureViseme.S_Z,
    "SH": AzureViseme.SH_CH_JH_ZH,
    "ZH": AzureViseme.SH_CH_JH_ZH,
    "CH": AzureViseme.SH_CH_JH_ZH,
    "JH": AzureViseme.SH_CH_JH_ZH,
    "HH": AzureViseme.H,
    "L": AzureViseme.L,
    "R": AzureViseme.R,
    "W": AzureViseme.W_UW,
    "Y": AzureViseme.Y_IY_IH_IX,

    # Vowels
    "AA": AzureViseme.AA,
    "AE": AzureViseme.AE_AX_AH,
    "AH": AzureViseme.AE_AX_AH,
    "AO": AzureViseme.AO,
    "EH": AzureViseme.EY_EH_UH,
    "ER": AzureViseme.ER,
    "EY": AzureViseme.EY_EH_UH,
    "IH": AzureViseme.Y_IY_IH_IX,
    "IY": AzureViseme.Y_IY_IH_IX,
    "OW": AzureViseme.OW,
    "UH": AzureViseme.EY_EH_UH,
    "UW": AzureViseme.W_UW,
    "AW": AzureViseme.AW,
    "AY": AzureViseme.AY,
    "OY": AzureViseme.OY,
}


def arpabet_to_ovr(phoneme: str) -> int:
    """Convert ARPAbet phoneme to OVR viseme ID.

    Args:
        phoneme: ARPAbet phoneme (e.g., "AA", "B", "SH")
                 Stress markers (0, 1, 2) are automatically stripped.

    Returns:
        OVR viseme ID (0-14)

    Examples:
        >>> arpabet_to_ovr("AA")
        10
        >>> arpabet_to_ovr("P")
        1
        >>> arpabet_to_ovr("AA1")  # Stress marker stripped
        10
    """
    # Strip stress markers (0, 1, 2) from vowels
    phoneme_base = phoneme.rstrip("012")
    return ARPABET_TO_OVR.get(phoneme_base, OVRViseme.SIL)


def arpabet_to_azure_viseme(phoneme: str) -> int:
    """Convert ARPAbet phoneme to Azure viseme ID.

    Args:
        phoneme: ARPAbet phoneme (e.g., "AA", "B", "SH")

    Returns:
        Azure viseme ID (0-21)
    """
    phoneme_base = phoneme.rstrip("012")
    return ARPABET_TO_AZURE.get(phoneme_base, AzureViseme.SIL)


def azure_to_ovr(azure_viseme_id: int) -> int:
    """Convert Azure viseme ID to OVR viseme ID.

    This is a lossy mapping as Azure has more viseme granularity.

    Args:
        azure_viseme_id: Azure viseme ID (0-21)

    Returns:
        OVR viseme ID (0-14)
    """
    # Mapping Azure's 22 visemes to OVR's 15 visemes
    mapping = {
        0: OVRViseme.SIL,     # Silence
        1: OVRViseme.AA,      # AE_AX_AH
        2: OVRViseme.AA,      # AA
        3: OVRViseme.O,       # AO
        4: OVRViseme.E,       # EY_EH_UH
        5: OVRViseme.RR,      # ER
        6: OVRViseme.I,       # Y_IY_IH_IX
        7: OVRViseme.U,       # W_UW
        8: OVRViseme.O,       # OW
        9: OVRViseme.AA,      # AW
        10: OVRViseme.O,      # OY
        11: OVRViseme.AA,     # AY
        12: OVRViseme.SIL,    # H
        13: OVRViseme.RR,     # R
        14: OVRViseme.NN,     # L
        15: OVRViseme.SS,     # S_Z
        16: OVRViseme.CH,     # SH_CH_JH_ZH
        17: OVRViseme.TH,     # TH_DH
        18: OVRViseme.FF,     # F_V
        19: OVRViseme.DD,     # D_T_N
        20: OVRViseme.KK,     # K_G_NG
        21: OVRViseme.PP,     # P_B_M
    }
    return mapping.get(azure_viseme_id, OVRViseme.SIL)


# Precomputed phoneme groups for quick lookups
VOWELS: list[str] = ["AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH", "IY", "OW", "OY", "UH", "UW"]
CONSONANTS: list[str] = ["B", "CH", "D", "DH", "F", "G", "HH", "JH", "K", "L", "M", "N", "NG", "P", "R", "S", "SH", "T", "TH", "V", "W", "Y", "Z", "ZH"]


def is_vowel(phoneme: str) -> bool:
    """Check if a phoneme is a vowel."""
    return phoneme.rstrip("012") in VOWELS


def is_consonant(phoneme: str) -> bool:
    """Check if a phoneme is a consonant."""
    return phoneme.rstrip("012") in CONSONANTS


def get_viseme_duration_hint(phoneme: str) -> tuple[float, float]:
    """Get typical duration range for a phoneme in milliseconds.

    This provides rough estimates for phoneme durations to help with
    lip-sync timing when precise timing isn't available.

    Args:
        phoneme: ARPAbet phoneme

    Returns:
        Tuple of (min_ms, max_ms) typical duration range
    """
    phoneme_base = phoneme.rstrip("012")

    # Stops have short durations
    if phoneme_base in ["P", "B", "T", "D", "K", "G"]:
        return (50, 100)

    # Fricatives are longer
    if phoneme_base in ["F", "V", "TH", "DH", "S", "Z", "SH", "ZH", "HH"]:
        return (80, 150)

    # Nasals and liquids
    if phoneme_base in ["M", "N", "NG", "L", "R"]:
        return (60, 120)

    # Vowels vary by length
    # Short vowels
    if phoneme_base in ["IH", "EH", "AE", "UH"]:
        return (80, 140)

    # Long vowels and diphthongs
    if phoneme_base in ["IY", "EY", "AA", "AO", "OW", "UW", "AW", "AY", "OY", "ER"]:
        return (100, 200)

    # Default
    return (80, 120)

