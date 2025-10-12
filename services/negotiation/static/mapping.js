/**
 * ARPAbet to OVR Viseme mapping
 * 
 * This module provides mappings from ARPAbet phonemes (used by most TTS systems)
 * to Oculus OVR viseme IDs for lip-sync animation.
 * 
 * References:
 * - ARPAbet: https://en.wikipedia.org/wiki/ARPABET
 * - OVR Lip Sync: https://developer.oculus.com/documentation/native/audio-ovrlipsync/
 */

// OVR Viseme IDs (0-14)
export const OVRViseme = {
    SIL: 0,      // Silence
    PP: 1,       // p, b, m
    FF: 2,       // f, v
    TH: 3,       // th (voiced and unvoiced)
    DD: 4,       // t, d
    KK: 5,       // k, g
    CH: 6,       // ch, j, sh, zh
    SS: 7,       // s, z
    NN: 8,       // n, l
    RR: 9,       // r
    AA: 10,      // a (as in "father")
    E: 11,       // e (as in "bed")
    I: 12,       // i (as in "sit")
    O: 13,       // o (as in "go")
    U: 14        // u (as in "put")
};

// Azure/SAPI Viseme IDs (0-21)
export const AzureViseme = {
    SIL: 0,          // Silence
    AE_AX_AH: 1,     // æ ʌ ə
    AA: 2,           // ɑ
    AO: 3,           // ɔ
    EY_EH_UH: 4,     // e ɛ ʊ
    ER: 5,           // ɚ
    Y_IY_IH_IX: 6,   // j i ɪ ɨ
    W_UW: 7,         // w u
    OW: 8,           // o
    AW: 9,           // aʊ
    OY: 10,          // ɔɪ
    AY: 11,          // aɪ
    H: 12,           // h
    R: 13,           // ɹ
    L: 14,           // l
    S_Z: 15,         // s z
    SH_CH_JH_ZH: 16, // ʃ tʃ dʒ ʒ
    TH_DH: 17,       // θ ð
    F_V: 18,         // f v
    D_T_N: 19,       // d t n
    K_G_NG: 20,      // k ɡ ŋ
    P_B_M: 21        // p b m
};

// ARPAbet to OVR Viseme mapping
export const ARPABET_TO_OVR = {
    // Silence
    'SIL': OVRViseme.SIL,
    'SP': OVRViseme.SIL,
    
    // Consonants - Stops
    'P': OVRViseme.PP,
    'B': OVRViseme.PP,
    'T': OVRViseme.DD,
    'D': OVRViseme.DD,
    'K': OVRViseme.KK,
    'G': OVRViseme.KK,
    
    // Consonants - Fricatives
    'F': OVRViseme.FF,
    'V': OVRViseme.FF,
    'TH': OVRViseme.TH,
    'DH': OVRViseme.TH,
    'S': OVRViseme.SS,
    'Z': OVRViseme.SS,
    'SH': OVRViseme.CH,
    'ZH': OVRViseme.CH,
    'HH': OVRViseme.SIL,
    
    // Consonants - Affricates
    'CH': OVRViseme.CH,
    'JH': OVRViseme.CH,
    
    // Consonants - Nasals
    'M': OVRViseme.PP,
    'N': OVRViseme.NN,
    'NG': OVRViseme.NN,
    
    // Consonants - Liquids
    'L': OVRViseme.NN,
    'R': OVRViseme.RR,
    
    // Consonants - Glides
    'W': OVRViseme.U,
    'Y': OVRViseme.I,
    
    // Vowels - Monophthongs
    'AA': OVRViseme.AA,
    'AE': OVRViseme.AA,
    'AH': OVRViseme.AA,
    'AO': OVRViseme.O,
    'AW': OVRViseme.AA,
    'AY': OVRViseme.AA,
    'EH': OVRViseme.E,
    'ER': OVRViseme.RR,
    'EY': OVRViseme.E,
    'IH': OVRViseme.I,
    'IY': OVRViseme.I,
    'OW': OVRViseme.O,
    'OY': OVRViseme.O,
    'UH': OVRViseme.U,
    'UW': OVRViseme.U,
    
    // Variants with stress markers (0, 1, 2)
    'AA0': OVRViseme.AA, 'AA1': OVRViseme.AA, 'AA2': OVRViseme.AA,
    'AE0': OVRViseme.AA, 'AE1': OVRViseme.AA, 'AE2': OVRViseme.AA,
    'AH0': OVRViseme.AA, 'AH1': OVRViseme.AA, 'AH2': OVRViseme.AA,
    'AO0': OVRViseme.O,  'AO1': OVRViseme.O,  'AO2': OVRViseme.O,
    'AW0': OVRViseme.AA, 'AW1': OVRViseme.AA, 'AW2': OVRViseme.AA,
    'AY0': OVRViseme.AA, 'AY1': OVRViseme.AA, 'AY2': OVRViseme.AA,
    'EH0': OVRViseme.E,  'EH1': OVRViseme.E,  'EH2': OVRViseme.E,
    'ER0': OVRViseme.RR, 'ER1': OVRViseme.RR, 'ER2': OVRViseme.RR,
    'EY0': OVRViseme.E,  'EY1': OVRViseme.E,  'EY2': OVRViseme.E,
    'IH0': OVRViseme.I,  'IH1': OVRViseme.I,  'IH2': OVRViseme.I,
    'IY0': OVRViseme.I,  'IY1': OVRViseme.I,  'IY2': OVRViseme.I,
    'OW0': OVRViseme.O,  'OW1': OVRViseme.O,  'OW2': OVRViseme.O,
    'OY0': OVRViseme.O,  'OY1': OVRViseme.O,  'OY2': OVRViseme.O,
    'UH0': OVRViseme.U,  'UH1': OVRViseme.U,  'UH2': OVRViseme.U,
    'UW0': OVRViseme.U,  'UW1': OVRViseme.U,  'UW2': OVRViseme.U
};

// Azure to OVR Viseme mapping
export const AZURE_TO_OVR = {
    0: OVRViseme.SIL,     // Silence
    1: OVRViseme.AA,      // AE_AX_AH
    2: OVRViseme.AA,      // AA
    3: OVRViseme.O,       // AO
    4: OVRViseme.E,       // EY_EH_UH
    5: OVRViseme.RR,      // ER
    6: OVRViseme.I,       // Y_IY_IH_IX
    7: OVRViseme.U,       // W_UW
    8: OVRViseme.O,       // OW
    9: OVRViseme.AA,      // AW
    10: OVRViseme.O,      // OY
    11: OVRViseme.AA,     // AY
    12: OVRViseme.SIL,    // H
    13: OVRViseme.RR,     // R
    14: OVRViseme.NN,     // L
    15: OVRViseme.SS,     // S_Z
    16: OVRViseme.CH,     // SH_CH_JH_ZH
    17: OVRViseme.TH,     // TH_DH
    18: OVRViseme.FF,     // F_V
    19: OVRViseme.DD,     // D_T_N
    20: OVRViseme.KK,     // K_G_NG
    21: OVRViseme.PP      // P_B_M
};

/**
 * Convert ARPAbet phoneme to OVR viseme ID
 * @param {string} phoneme - ARPAbet phoneme (e.g., "AA", "B", "SH")
 * @returns {number} OVR viseme ID (0-14)
 */
export function arpabetToOVR(phoneme) {
    // Strip stress markers (0, 1, 2) from vowels
    const phonemeBase = phoneme.replace(/[012]$/, '').toUpperCase();
    return ARPABET_TO_OVR[phonemeBase] ?? OVRViseme.SIL;
}

/**
 * Convert Azure viseme ID to OVR viseme ID
 * @param {number} azureVisemeId - Azure viseme ID (0-21)
 * @returns {number} OVR viseme ID (0-14)
 */
export function azureToOVR(azureVisemeId) {
    return AZURE_TO_OVR[azureVisemeId] ?? OVRViseme.SIL;
}

/**
 * OVR Viseme to blendshape weights mapping
 * Maps OVR visemes to normalized morph target weights
 */
export const OVR_VISEME_BLENDSHAPES = {
    [OVRViseme.SIL]: { jawOpen: 0.0, mouthClose: 1.0 },
    [OVRViseme.PP]: { jawOpen: 0.0, mouthClose: 0.9, mouthPressLeft: 0.6, mouthPressRight: 0.6 },
    [OVRViseme.FF]: { jawOpen: 0.1, mouthUpperUpLeft: 0.4, mouthUpperUpRight: 0.4, mouthRollLower: 0.3 },
    [OVRViseme.TH]: { jawOpen: 0.2, tongueOut: 0.5, mouthUpperUpLeft: 0.2, mouthUpperUpRight: 0.2 },
    [OVRViseme.DD]: { jawOpen: 0.15, mouthClose: 0.6 },
    [OVRViseme.KK]: { jawOpen: 0.3, jawForward: 0.2 },
    [OVRViseme.CH]: { jawOpen: 0.25, mouthPucker: 0.5, mouthFunnel: 0.3 },
    [OVRViseme.SS]: { jawOpen: 0.1, mouthSmileLeft: 0.4, mouthSmileRight: 0.4 },
    [OVRViseme.NN]: { jawOpen: 0.2, mouthClose: 0.5 },
    [OVRViseme.RR]: { jawOpen: 0.3, mouthPucker: 0.3, mouthFunnel: 0.2 },
    [OVRViseme.AA]: { jawOpen: 0.8, mouthOpen: 0.7 },
    [OVRViseme.E]: { jawOpen: 0.4, mouthSmileLeft: 0.5, mouthSmileRight: 0.5, mouthStretchLeft: 0.3, mouthStretchRight: 0.3 },
    [OVRViseme.I]: { jawOpen: 0.3, mouthSmileLeft: 0.6, mouthSmileRight: 0.6 },
    [OVRViseme.O]: { jawOpen: 0.5, mouthFunnel: 0.6, mouthPucker: 0.4 },
    [OVRViseme.U]: { jawOpen: 0.2, mouthPucker: 0.7, mouthFunnel: 0.5 }
};

/**
 * Get default blendshape weights for a viseme
 * @param {number} visemeId - OVR viseme ID
 * @returns {Object} Blendshape weights dictionary
 */
export function getVisemeBlendshapes(visemeId) {
    return OVR_VISEME_BLENDSHAPES[visemeId] ?? OVR_VISEME_BLENDSHAPES[OVRViseme.SIL];
}

