"""Character timings -> mouth-shape curves for the envoy's painted portrait (display only, never sim state).

ElevenLabs `/with-timestamps` gives a start/end time per character. Each character maps to one of six
mouth shapes, expressed as four warp controls the portrait material understands:

  jaw    JawOpen      0 shut .. 1 wide open (ah)
  wide   LipsWide     spread lips (ee)
  round  LipsRound    pursed, forward lips (oo)
  press  LipsPressed  lips closed hard (m, b, p) or lower lip on teeth (f, v)

Coarticulation: a sound is never a single frame. Each character is a Gaussian bump around its midpoint
(wider for longer sounds), and every 30 Hz sample is the weighted blend of the bumps around it, so a
vowel already shapes the consonant before it. Lip closures use the maximum instead of the blend: an
"m" must actually close the lips even between two open vowels. The game smooths the curves once more
(critically damped) before they reach the material.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from typing import Any

FPS = 30

# The six shapes as (jaw, wide, round, press).
REST = (0.0, 0.0, 0.0, 0.0)
CLOSED = (0.0, 0.0, 0.0, 1.0)  # m b p
FV = (0.12, 0.25, 0.0, 0.55)  # f v: lower lip to the upper teeth
WIDE = (0.32, 0.8, 0.0, 0.0)  # ee: i, y, e
OPEN = (0.85, 0.2, 0.0, 0.0)  # ah: a
ROUND = (0.28, 0.0, 0.95, 0.0)  # oo: u, w, and o (with more jaw)

SHAPE_OF: dict[str, tuple[float, float, float, float]] = {
    **dict.fromkeys("mbp", CLOSED),
    **dict.fromkeys("fv", FV),
    **dict.fromkeys("iy", WIDE),
    "e": (0.45, 0.6, 0.0, 0.0),
    "a": OPEN,
    "o": (0.55, 0.0, 0.7, 0.0),
    **dict.fromkeys("uw", ROUND),
    # other consonants: a little open, slightly spread (tongue/teeth sounds) or neutral
    **dict.fromkeys("stdnlczxj", (0.22, 0.35, 0.0, 0.0)),
    "r": (0.22, 0.0, 0.4, 0.0),
    **dict.fromkeys("hkgq", (0.35, 0.1, 0.0, 0.0)),
    **dict.fromkeys("0123456789", (0.3, 0.2, 0.0, 0.0)),
}
PAUSE_CHARS = set(".,;:!?—–-\n")


def shape_for(ch: str) -> tuple[tuple[float, float, float, float], float]:
    """(shape, weight). Spaces pull gently toward rest; punctuation pulls hard (a real pause)."""
    c = ch.lower()
    if c in SHAPE_OF:
        return SHAPE_OF[c], 1.0
    if c in PAUSE_CHARS:
        return REST, 1.5
    return REST, 0.35  # space, quotes, anything else


def curves(chars: Sequence[str], starts: Sequence[float], ends: Sequence[float], duration: float) -> dict[str, Any]:
    """30 Hz jaw/wide/round/press curves (0..1, 2 decimals) for a line of `duration` seconds."""
    n = max(1, int(math.ceil(duration * FPS)) + 1)
    jaw_c, wide_c, round_c, press_c = [0.0] * n, [0.0] * n, [0.0] * n, [0.0] * n
    out: dict[str, Any] = {"fps": FPS, "jaw": jaw_c, "wide": wide_c, "round": round_c, "press": press_c}
    if not chars:
        return out
    bumps = []
    for ch, t0, t1 in zip(chars, starts, ends, strict=False):
        shape, w = shape_for(ch)
        mid = 0.5 * (t0 + t1)
        sigma = max(0.03, 0.6 * (t1 - t0))
        bumps.append((mid, sigma, shape, w))
    first, last = bumps[0][0] - bumps[0][1], bumps[-1][0] + bumps[-1][1]
    for k in range(n):
        t = k / FPS
        if t < first - 0.08 or t > last + 0.08:
            continue  # before the first and after the last sound: rest
        acc = [0.0, 0.0, 0.0]
        wsum = 0.0
        press = 0.0
        for mid, sigma, shape, w in bumps:
            d = (t - mid) / sigma
            if d * d > 9.0:
                continue
            g = math.exp(-0.5 * d * d)
            ww = w * g
            acc[0] += ww * shape[0]
            acc[1] += ww * shape[1]
            acc[2] += ww * shape[2]
            wsum += ww
            press = max(press, g * shape[3])
        if wsum > 1e-6:
            jaw, wide, rnd = (a / wsum for a in acc)
            # a closure wins over the vowels around it
            jaw *= 1.0 - 0.9 * press
            jaw_c[k], wide_c[k], round_c[k] = round(jaw, 2), round(wide * (1 - press), 2), round(rnd, 2)
        press_c[k] = round(press, 2)
    return out


def curves_from_alignment(alignment: dict[str, Any] | None, duration: float) -> dict[str, Any] | None:
    """ElevenLabs `alignment` (characters / character_start_times_seconds / character_end_times_seconds)."""
    if not alignment:
        return None
    try:
        return curves(alignment["characters"], alignment["character_start_times_seconds"],
                      alignment["character_end_times_seconds"], duration)
    except (KeyError, TypeError):
        return None


def approximate_alignment(text: str, duration: float) -> dict[str, Any]:
    """No timestamps (macOS `say` backup): spread the characters evenly over the clip, pauses a bit longer."""
    weights = [2.5 if c in PAUSE_CHARS else (0.6 if c == " " else 1.0) for c in text]
    total = sum(weights) or 1.0
    lead = min(0.15, duration * 0.05)
    span = max(0.0, duration - 2 * lead)
    starts, ends, t = [], [], lead
    for w in weights:
        d = span * w / total
        starts.append(t)
        ends.append(t + d)
        t += d
    return {"characters": list(text), "character_start_times_seconds": starts, "character_end_times_seconds": ends}
