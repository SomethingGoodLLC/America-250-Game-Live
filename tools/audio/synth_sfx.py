"""Synthesize original battle sound effects for Play America 250 (no samples, no licences).

Run with the negotiation venv (numpy + scipy):
  services/negotiation/.venv/bin/python tools/audio/synth_sfx.py

Writes 44.1 kHz mono 16-bit WAVs to unreal/PlayAmerica250/SourceAudio/sfx/.
Deterministic: every file is seeded, so re-running gives identical audio.

Sounds (several variants each so volleys don't sound copy-pasted):
  musket_fire_N      flintlock: flint snap, priming-pan whoosh, main charge boom + crack, rolling tail
  musket_distant_N   the same far away: low-passed, softer onset, longer echo
  ball_whiz_N        a ball passing close: band-limited noise with a falling pitch and a sharp crest
  impact_body_N      ball / bayonet into a man: dull thud
  impact_ground_N    ball into earth / fence: short dirt burst
  ramrod_N           reload step: iron ramrod clink and scrape
  flint_click        misfire / jam: flint snap, no boom
  melee_swing        bayonet / butt-stroke swish
  drum_roll          rope-tension field drum: a crescendo roll ending on an accent ("Make ready!")
  march_patriot      fife and drum, "Yankee Doodle" (traditional, public domain; in 1775 a British
                     tune mocking the colonists, taken up by the Americans after Lexington and Concord)
  march_british      fife and drum, an original march written for the game (not a period tune)
"""

from __future__ import annotations

import wave
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

SR = 44100
OUT = Path(__file__).resolve().parents[2] / "unreal" / "PlayAmerica250" / "SourceAudio" / "sfx"


def env_exp(n: int, attack_s: float, decay_s: float) -> np.ndarray:
    t = np.arange(n) / SR
    a = np.clip(t / max(attack_s, 1e-4), 0.0, 1.0)
    return a * np.exp(-np.maximum(t - attack_s, 0.0) / max(decay_s, 1e-4))


def one_pole_lp(x: np.ndarray, cutoff_hz: float) -> np.ndarray:
    a = np.exp(-2.0 * np.pi * cutoff_hz / SR)
    return lfilter([1.0 - a], [1.0, -a], x)


def lp(x: np.ndarray, cutoff_hz: float, order: int = 2) -> np.ndarray:
    for _ in range(order):
        x = one_pole_lp(x, cutoff_hz)
    return x


def hp(x: np.ndarray, cutoff_hz: float) -> np.ndarray:
    return x - lp(x, cutoff_hz, 1)


def bandpass(x: np.ndarray, lo: float, hi: float) -> np.ndarray:
    return lp(hp(x, lo), hi, 2)


def reverb_tail(x: np.ndarray, rng: np.random.Generator, seconds: float, damp_hz: float, mix: float) -> np.ndarray:
    """Cheap outdoor echo: a few delayed, damped copies plus a noise-convolved decay."""
    n = len(x) + int(seconds * SR)
    y = np.zeros(n)
    y[: len(x)] += x
    for delay_s, gain in ((0.11, 0.35), (0.23, 0.22), (0.41, 0.14), (0.67, 0.08)):
        d = int((delay_s + rng.uniform(-0.02, 0.02)) * SR)
        echo = lp(x, damp_hz, 2) * gain
        y[d : d + len(echo)] += echo[: max(0, n - d)]
    ir_len = int(seconds * SR)
    ir = rng.standard_normal(ir_len) * np.exp(-np.arange(ir_len) / (0.25 * seconds * SR))
    ir = lp(ir, damp_hz, 1)
    wet = np.convolve(x[: int(0.15 * SR)], ir)[:n] * 0.004
    y[: len(wet)] += wet * mix
    return y


def normalize(x: np.ndarray, peak_db: float = -1.0) -> np.ndarray:
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * (10 ** (peak_db / 20.0))
    fade = int(0.01 * SR)
    if len(x) > 2 * fade:
        x[-fade:] *= np.linspace(1.0, 0.0, fade)
    return x


def write(name: str, x: np.ndarray, peak_db: float = -1.0) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    data = (normalize(x, peak_db) * 32767.0).astype("<i2")
    with wave.open(str(OUT / f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())


def flint_snap(rng: np.random.Generator) -> np.ndarray:
    n = int(0.03 * SR)
    click = rng.standard_normal(n) * env_exp(n, 0.0005, 0.004)
    click = hp(click, 2500)
    t = np.arange(n) / SR
    ring = np.sin(2 * np.pi * rng.uniform(3200, 4200) * t) * env_exp(n, 0.0002, 0.006) * 0.4
    return click + ring


def musket_fire(seed: int, distant: bool = False) -> np.ndarray:
    rng = np.random.default_rng(seed)
    gap = int(rng.uniform(0.045, 0.075) * SR)  # flint -> pan -> main charge: the flintlock "fft-BOOM"
    pan_n = int(0.06 * SR)
    pan = bandpass(rng.standard_normal(pan_n), 900, 6000) * env_exp(pan_n, 0.004, 0.02) * 0.35
    n = int(1.2 * SR)
    t = np.arange(n) / SR
    crack = hp(rng.standard_normal(n), 1800) * env_exp(n, 0.0008, 0.012) * 1.0
    blast = lp(rng.standard_normal(n), rng.uniform(1600, 2400), 2) * env_exp(n, 0.002, rng.uniform(0.09, 0.14)) * 2.2
    f0 = rng.uniform(55, 75)
    boom = np.sin(2 * np.pi * (f0 * t - 18 * t * t)) * env_exp(n, 0.003, 0.16) * 1.6
    rumble = lp(rng.standard_normal(n), 220, 2) * env_exp(n, 0.02, 0.45) * 1.4
    main = crack + blast + boom + rumble
    x = np.zeros(gap + n)
    snap = flint_snap(rng)
    x[: len(snap)] += snap * 0.5
    x[gap // 3 : gap // 3 + pan_n] += pan
    x[gap:] += main
    if distant:
        x = lp(x, 900, 2)
        x = reverb_tail(x, rng, 1.6, 700, 1.0)
        x *= np.clip(np.arange(len(x)) / (0.03 * SR), 0, 1)  # softer onset far away
    else:
        x = reverb_tail(x, rng, 1.2, 2500, 0.8)
    return x


def ball_whiz(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    dur = rng.uniform(0.35, 0.5)
    n = int(dur * SR)
    t = np.arange(n) / SR
    centre = dur * rng.uniform(0.45, 0.6)
    amp = np.exp(-((t - centre) ** 2) / (2 * (dur * 0.12) ** 2))
    noise = rng.standard_normal(n)
    # Doppler: the hiss band falls as the ball goes past.
    out = np.zeros(n)
    for lo, hi in ((1800, 3200), (1100, 2000), (700, 1300)):
        seg = bandpass(noise, lo, hi)
        w = np.clip(1.0 - np.abs((t - centre) / (dur * 0.5)) * 1.5, 0, 1) if lo == 1100 else (t < centre) * 1.0 if lo == 1800 else (t >= centre) * 1.0
        out += seg * w
    tone = np.sin(2 * np.pi * np.cumsum(np.interp(t, [0, centre, dur], [1500, 1100, 650])) / SR) * 0.25
    return (out + tone) * amp


def impact_body(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(0.35 * SR)
    t = np.arange(n) / SR
    thud = np.sin(2 * np.pi * rng.uniform(85, 120) * t) * env_exp(n, 0.002, 0.05)
    slap = lp(rng.standard_normal(n), 1800, 2) * env_exp(n, 0.001, 0.02) * 0.8
    cloth = bandpass(rng.standard_normal(n), 300, 2500) * env_exp(n, 0.004, 0.06) * 0.3
    return thud + slap + cloth


def impact_ground(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(0.45 * SR)
    burst = lp(rng.standard_normal(n), 3500, 1) * env_exp(n, 0.001, 0.03)
    grit = hp(rng.standard_normal(n), 3000) * env_exp(n, 0.01, 0.12) * 0.25 * (rng.random(n) > 0.85)
    thump = np.sin(2 * np.pi * 70 * np.arange(n) / SR) * env_exp(n, 0.002, 0.04) * 0.6
    return burst + grit + thump


def ramrod(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(0.5 * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    # Two or three clinks of iron on iron (inharmonic partials), then a short scrape.
    for k in range(rng.integers(2, 4)):
        start = int((0.02 + k * rng.uniform(0.07, 0.12)) * SR)
        m = n - start
        tt = np.arange(m) / SR
        base = rng.uniform(1900, 2600)
        clink = sum(np.sin(2 * np.pi * base * r * tt) / (i + 1) for i, r in enumerate((1.0, 2.76, 5.40, 8.93)))
        out[start:] += clink * env_exp(m, 0.0005, 0.03) * (0.8 ** k)
    scrape_n = int(0.18 * SR)
    s0 = int(0.28 * SR)
    out[s0 : s0 + scrape_n] += bandpass(rng.standard_normal(scrape_n), 2500, 7000) * env_exp(scrape_n, 0.03, 0.06) * 0.35
    return out


def melee_swing(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    n = int(0.3 * SR)
    t = np.arange(n) / SR
    amp = np.sin(np.pi * np.clip(t / 0.25, 0, 1)) ** 2
    return bandpass(rng.standard_normal(n), 400, 2400) * amp


def hit_confirm(seed: int) -> np.ndarray:
    """UI tick for 'you hit him': two short bright blips."""
    rng = np.random.default_rng(seed)
    n = int(0.12 * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for start, f in ((0.0, 2400.0), (0.045, 3100.0)):
        m = n - int(start * SR)
        tt = np.arange(m) / SR
        out[int(start * SR):] += np.sin(2 * np.pi * f * tt) * env_exp(m, 0.001, 0.018)
    return out + hp(rng.standard_normal(n), 4000) * env_exp(n, 0.0005, 0.004) * 0.3


def kill_confirm(seed: int) -> np.ndarray:
    """UI cue for 'he's down': a low thump under a falling chime."""
    rng = np.random.default_rng(seed)
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    thump = np.sin(2 * np.pi * (140 * t - 60 * t * t)) * env_exp(n, 0.002, 0.08) * 1.2
    chime = sum(np.sin(2 * np.pi * f * t) / (i + 1) for i, f in enumerate((1600.0, 2400.0, 3200.0))) * env_exp(n, 0.002, 0.16) * 0.5
    return thump + chime + lp(rng.standard_normal(n), 800, 1) * env_exp(n, 0.001, 0.03) * 0.4


def drum_stroke(rng: np.random.Generator, accent: float) -> np.ndarray:
    """One stick stroke on a rope-tension field drum: deep shell thump plus gut-snare rattle."""
    n = int(0.35 * SR)
    t = np.arange(n) / SR
    shell = np.sin(2 * np.pi * 165 * t + 2.5 * np.exp(-t * 60)) * env_exp(n, 0.001, 0.07)
    snare = bandpass(rng.standard_normal(n), 900, 6500) * env_exp(n, 0.0008, 0.11)
    return accent * (0.55 * shell + 0.8 * snare)


def drum_roll(seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    out = np.zeros(int(2.4 * SR))
    t = 0.0
    k = 0
    while t < 1.55:  # double-stroke roll, ~18 strokes/s, crescendo
        acc = (0.35 + 0.55 * t / 1.55) * (1.0 if k % 2 == 0 else 0.8) * rng.uniform(0.9, 1.05)
        s = drum_stroke(rng, acc)
        i = int(t * SR)
        out[i : i + len(s)] += s[: len(out) - i]
        t += 1.0 / 18.0 + rng.uniform(-0.004, 0.004)
        k += 1
    s = drum_stroke(rng, 1.5)  # the accent that ends the roll
    i = int(1.62 * SR)
    out[i : i + len(s)] += s[: len(out) - i]
    return reverb_tail(out, rng, 0.8, 3000, 0.4)


NOTE = {"A4": 440.0, "B4": 493.88, "C5": 523.25, "C#5": 554.37, "D5": 587.33, "E5": 659.25, "F#5": 739.99,
        "G5": 783.99, "A5": 880.0, "B5": 987.77, "C#6": 1108.73, "D6": 1174.66, "E6": 1318.51, "R": 0.0}


def fife_note(freq: float, dur: float, rng: np.random.Generator) -> np.ndarray:
    """A wooden fife: near-sine with a little 2nd/3rd harmonic, breath noise, vibrato after the attack."""
    n = int(dur * SR)
    if freq <= 0:
        return np.zeros(n)
    t = np.arange(n) / SR
    vib = 1.0 + 0.006 * np.sin(2 * np.pi * 5.5 * t) * np.clip((t - 0.12) / 0.1, 0, 1)
    phase = 2 * np.pi * np.cumsum(freq * vib) / SR
    tone = np.sin(phase) + 0.18 * np.sin(2 * phase) + 0.07 * np.sin(3 * phase)
    breath = bandpass(rng.standard_normal(n), 2000, 8000) * 0.05
    env = np.clip(t / 0.02, 0, 1) * np.clip((dur - t) / 0.03, 0, 1)
    return (tone + breath) * env


def march(seed: int, tune: list[tuple[str, float]], beat_s: float) -> np.ndarray:
    """Two fifes (the second slightly detuned, as a fife corps never quite agrees) over a march beat."""
    rng = np.random.default_rng(seed)
    total = sum(d for _, d in tune) * beat_s
    out = np.zeros(int((total + 1.0) * SR))
    t = 0.0
    for name, beats in tune:
        dur = beats * beat_s
        f = NOTE[name]
        a = fife_note(f, dur * 0.95, rng)
        b = fife_note(f * 1.004, dur * 0.95, rng)
        i = int(t * SR)
        out[i : i + len(a)] += 0.5 * a + 0.35 * b
        t += dur
    # Drum: a stroke on each beat, a flam on the downbeat of every bar (2/4).
    k = 0
    tb = 0.0
    while tb < total:
        acc = 1.0 if k % 2 == 0 else 0.6
        for off, a2 in (((-0.03, 0.5), (0.0, acc)) if k % 2 == 0 else ((0.0, acc),)):
            s = drum_stroke(rng, a2 * 0.45)
            i = int(max(0.0, tb + off) * SR)
            out[i : i + len(s)] += s[: len(out) - i]
        tb += beat_s
        k += 1
    return reverb_tail(out, rng, 1.0, 5000, 0.5)


# Yankee Doodle (traditional), in D for fife: (note, beats).
YANKEE_DOODLE = [
    ("D5", 0.5), ("D5", 0.5), ("E5", 0.5), ("F#5", 0.5), ("D5", 0.5), ("F#5", 0.5), ("E5", 1.0),
    ("D5", 0.5), ("D5", 0.5), ("E5", 0.5), ("F#5", 0.5), ("D5", 1.0), ("C#5", 1.0),
    ("D5", 0.5), ("D5", 0.5), ("E5", 0.5), ("F#5", 0.5), ("G5", 0.5), ("F#5", 0.5), ("E5", 0.5), ("D5", 0.5),
    ("C#5", 0.5), ("A4", 0.5), ("B4", 0.5), ("C#5", 0.5), ("D5", 1.0), ("D5", 1.0),
]
# An original quick-march for the regulars (written for this game, not a period tune).
REGULARS_MARCH = [
    ("A5", 0.5), ("F#5", 0.5), ("A5", 0.5), ("D6", 0.5), ("C#6", 0.5), ("B5", 0.5), ("A5", 1.0),
    ("B5", 0.5), ("G5", 0.5), ("B5", 0.5), ("E6", 0.5), ("D6", 0.5), ("C#6", 0.5), ("B5", 1.0),
    ("A5", 0.5), ("D6", 0.5), ("C#6", 0.5), ("B5", 0.5), ("A5", 0.5), ("G5", 0.5), ("F#5", 0.5), ("E5", 0.5),
    ("D5", 0.5), ("F#5", 0.5), ("E5", 0.5), ("C#5", 0.5), ("D5", 1.0), ("D5", 1.0),
]


def main() -> None:
    for i in range(1, 5):
        write(f"musket_fire_{i}", musket_fire(100 + i))
    for i in range(1, 3):
        write(f"musket_distant_{i}", musket_fire(200 + i, distant=True), peak_db=-6.0)
    for i in range(1, 4):
        write(f"ball_whiz_{i}", ball_whiz(300 + i), peak_db=-4.0)
    for i in range(1, 3):
        write(f"impact_body_{i}", impact_body(400 + i), peak_db=-3.0)
        write(f"impact_ground_{i}", impact_ground(500 + i), peak_db=-4.0)
    for i in range(1, 4):
        write(f"ramrod_{i}", ramrod(600 + i), peak_db=-8.0)
    write("flint_click", flint_snap(np.random.default_rng(700)), peak_db=-6.0)
    write("melee_swing", melee_swing(800), peak_db=-6.0)
    write("hit_confirm", hit_confirm(900), peak_db=-8.0)
    write("kill_confirm", kill_confirm(901), peak_db=-6.0)
    write("drum_roll", drum_roll(1000), peak_db=-4.0)
    write("march_patriot", march(1100, YANKEE_DOODLE, 0.32), peak_db=-6.0)
    write("march_british", march(1200, REGULARS_MARCH, 0.30), peak_db=-6.0)
    print(f"wrote {len(list(OUT.glob('*.wav')))} files to {OUT}")


if __name__ == "__main__":
    main()
