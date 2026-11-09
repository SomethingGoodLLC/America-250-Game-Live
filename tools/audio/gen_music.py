"""Generate campaign music and the battle-begins stinger with ElevenLabs.

Run with the negotiation venv:
  services/negotiation/.venv/bin/python tools/audio/gen_music.py [--force]

Writes 44.1 kHz 16-bit WAVs:
  SourceAudio/music/campaign_theme.wav  (ElevenLabs Music, loop-crossfaded;
    falls back to a Sound Effects loop when Music needs a paid plan)
  SourceAudio/sfx/battle_begins.wav     (ElevenLabs Sound Effects)

Existing files are skipped (no credits spent) unless --force. The API key
is read from services/negotiation/.env and never printed. Import into
Unreal with Scripts/import_audio.py.
Requires ffmpeg on PATH to decode the MP3 the APIs return.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "unreal" / "PlayAmerica250" / "SourceAudio"
ENV = ROOT / "services" / "negotiation" / ".env"
SR = 44100

CAMPAIGN_PROMPT = (
    "Instrumental background music for a grand-strategy campaign map set in 1775 "
    "during the American Revolutionary War, no vocals, no lyrics, no singing. A calm, "
    "contemplative, seamlessly loopable loop: solo fife and wooden flute carrying a "
    "gentle period melody over soft strings, harpsichord, acoustic guitar and a very "
    "quiet rope-tension snare drum far in the distance. Colonial American folk "
    "character, thoughtful and tense but hopeful, like studying a war map by "
    "candlelight in a headquarters tent. Slow tempo, restrained dynamics, period "
    "instruments only, no modern drums, no synths, no piano. The ending resolves back "
    "to the opening chord so it can loop forever under gameplay."
)
CAMPAIGN_MS = 60_000
# Sound Effects prompts are capped at 450 characters.
CAMPAIGN_SFX_PROMPT = (
    "Loopable instrumental music, no vocals: calm 1775 American Revolutionary "
    "War campaign theme. Solo fife and wooden flute over soft strings, "
    "harpsichord and gentle acoustic guitar, very quiet distant snare drum. "
    "Thoughtful, tense, hopeful colonial folk mood, slow and restrained, "
    "period instruments only, seamless loop."
)

BATTLE_PROMPT = (
    "The start of an American Revolutionary War battle in 1775: a rope-tension snare "
    "drum roll that rapidly crescendos, joined by a piercing fife playing a call to "
    "arms, soldiers shouting in the distance, then a single deep cannon boom and a "
    "first ragged musket volley cracking across a field, ending with ringing battle "
    "noise fading out. Dramatic, urgent, cinematic, no music melody, no modern "
    "sounds."
)
BATTLE_SECONDS = 7.0


def read_key() -> str:
    for line in ENV.read_text().splitlines():
        if line.startswith("ELEVENLABS_API_KEY="):
            return line.split("=", 1)[1].strip().strip('"').strip("'")
    sys.exit("ELEVENLABS_API_KEY not found in services/negotiation/.env")


def decode_mp3(mp3: Path, channels: int) -> np.ndarray:
    """Decode an MP3 to float samples in [-1, 1], shape (frames, channels)."""
    raw = subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp3),
            "-f",
            "f32le",
            "-ac",
            str(channels),
            "-ar",
            str(SR),
            "-",
        ],
        check=True,
        capture_output=True,
    ).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, channels).copy()


def write_wav(path: Path, x: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * 10 ** (-1.0 / 20.0)
    data = (np.clip(x, -1.0, 1.0) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())


def make_loopable(x: np.ndarray, crossfade_s: float = 3.0) -> np.ndarray:
    """Blend the tail into the head so the file loops with no click or jump."""
    n = int(crossfade_s * SR)
    if len(x) <= 2 * n:
        return x
    ramp = np.linspace(0.0, 1.0, n)[:, None]
    head = x[:n] * ramp + x[-n:] * (1.0 - ramp)
    return np.concatenate([head, x[n:-n]])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--force", action="store_true", help="regenerate even if the WAV exists"
    )
    args = ap.parse_args()

    jobs = [
        (SRC / "sfx" / "battle_begins.wav", "sfx"),
        (SRC / "music" / "campaign_theme.wav", "music"),
    ]
    todo = [(p, kind) for p, kind in jobs if args.force or not p.exists()]
    for p, _ in jobs:
        if (p, _) not in todo:
            print(f"  skip existing {p.relative_to(ROOT)} (use --force to regenerate)")
    if not todo:
        return

    from elevenlabs.client import ElevenLabs

    client = ElevenLabs(api_key=read_key())
    tmp = ROOT / "tools" / "audio" / ".tmp_gen"
    tmp.mkdir(exist_ok=True)
    try:
        for path, kind in todo:
            mp3 = tmp / (path.stem + ".mp3")
            if kind == "music":
                try:
                    audio = client.music.compose(
                        prompt=CAMPAIGN_PROMPT,
                        music_length_ms=CAMPAIGN_MS,
                        model_id="music_v1",
                        output_format="mp3_44100_128",
                    )
                    # Materialize now so a plan error falls back before writing.
                    audio = list(audio)
                except Exception as e:
                    if "paid_plan_required" not in str(e) and "402" not in str(e):
                        raise
                    print("  Music API needs a paid plan; using Sound Effects loop")
                    audio = client.text_to_sound_effects.convert(
                        text=CAMPAIGN_SFX_PROMPT,
                        duration_seconds=30.0,
                        prompt_influence=0.7,
                        loop=True,
                        model_id="eleven_text_to_sound_v2",
                        output_format="mp3_44100_128",
                    )
            else:
                audio = client.text_to_sound_effects.convert(
                    text=BATTLE_PROMPT,
                    duration_seconds=BATTLE_SECONDS,
                    prompt_influence=0.7,
                    model_id="eleven_text_to_sound_v2",
                    output_format="mp3_44100_128",
                )
            with mp3.open("wb") as f:
                for chunk in audio:
                    f.write(chunk)
            channels = 2 if kind == "music" else 1
            x = decode_mp3(mp3, channels)
            if kind == "music":
                x = make_loopable(x)
            else:  # keep the stinger tight: 30 ms head/tail fades, no silence padding
                f = int(0.03 * SR)
                x[:f] *= np.linspace(0, 1, f)[:, None]
                x[-f:] *= np.linspace(1, 0, f)[:, None]
            write_wav(path, x)
            print(f"  {path.relative_to(ROOT)}  ({len(x) / SR:.1f} s, {channels} ch)")
    finally:
        for f in tmp.glob("*"):
            f.unlink()
        tmp.rmdir()


if __name__ == "__main__":
    main()
