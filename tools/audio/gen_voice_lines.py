"""Generate the soldiers' shouted lines with ElevenLabs (the key configured for the negotiation service).

Run with the negotiation venv:
  services/negotiation/.venv/bin/python tools/audio/gen_voice_lines.py [--force]

Writes 24 kHz mono 16-bit WAVs to unreal/PlayAmerica250/SourceAudio/voice/<side>/<key>[_b].wav.
Existing files are skipped (no credits spent) unless --force. The API key is read from
services/negotiation/.env and never printed.

Voices (ElevenLabs premade, chosen by accent):
  british      officer George (British), soldiers' cries also Daniel (British)
  continental  officer Adam (American), soldiers' cries also Harry (American, "fierce warrior")
"""

from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

import httpx
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "unreal" / "PlayAmerica250" / "SourceAudio" / "voice"
ENV = ROOT / "services" / "negotiation" / ".env"
SR = 24000
MODEL = "eleven_multilingual_v2"

VOICES = {
    "british": {"officer": "JBFqnCBsd6RMkjVDRZzb", "soldier": "onwK4e9ZLuTAKqWW03F9"},
    "continental": {"officer": "pNInz6obpgDQGcFmaJgB", "soldier": "SOYHLrjzK2X1ezoPC6cr"},
}

# key -> (text, who). Keys match PA250BattleAudio voice keys in C++.
LINES: dict[str, tuple[str, str]] = {
    "make_ready": ("Make ready!", "officer"),
    "present": ("Present!", "officer"),
    "fire": ("Fire!", "officer"),
    "forward": ("Forward! Push them off the ground!", "officer"),
    "enemy_advancing": ("Their line is advancing!", "soldier"),
    "steady": ("Steady, lads! Hold the line!", "officer"),
    "fall_back": ("Fall back! In order!", "officer"),
    "enemy_falling_back": ("They're falling back!", "soldier"),
    "breaking": ("The line is breaking! Run!", "soldier"),
    "enemy_running": ("They're running! They're running!", "soldier"),
    "rally": ("Rally! Form on me!", "officer"),
    "reformed": ("Line re-formed!", "officer"),
    "charge": ("Huzzah!", "soldier"),
    "charge_b": ("At them, boys! Charge!", "soldier"),
}

# Shouting delivery: low stability for urgency, high style.
SETTINGS = {"stability": 0.28, "similarity_boost": 0.8, "style": 0.65, "use_speaker_boost": True}


def read_key() -> str:
    for line in ENV.read_text().splitlines():
        if line.startswith("ELEVENLABS_API_KEY="):
            return line.split("=", 1)[1].strip().strip('"').strip("'")
    sys.exit("ELEVENLABS_API_KEY not found in services/negotiation/.env")


def tidy(pcm: np.ndarray) -> np.ndarray:
    """Trim leading/trailing silence, normalise to -1 dBFS, 10 ms fades."""
    x = pcm.astype(np.float64) / 32768.0
    loud = np.flatnonzero(np.abs(x) > 0.02)
    if len(loud):
        x = x[max(0, loud[0] - int(0.02 * SR)) : loud[-1] + int(0.08 * SR)]
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * 10 ** (-1.0 / 20.0)
    f = int(0.01 * SR)
    if len(x) > 2 * f:
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
    return (x * 32767).astype("<i2")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="regenerate even if the WAV exists")
    args = ap.parse_args()
    key = read_key()
    made = skipped = 0
    chars = 0
    with httpx.Client(timeout=60) as client:
        for side, voices in VOICES.items():
            for line_key, (text, who) in LINES.items():
                path = OUT / side / f"{line_key}.wav"
                if path.exists() and not args.force:
                    skipped += 1
                    continue
                r = client.post(
                    f"https://api.elevenlabs.io/v1/text-to-speech/{voices[who]}",
                    params={"output_format": f"pcm_{SR}"},
                    headers={"xi-api-key": key, "Content-Type": "application/json"},
                    json={"text": text, "model_id": MODEL, "voice_settings": SETTINGS},
                )
                if r.status_code != 200:
                    sys.exit(f"{side}/{line_key}: HTTP {r.status_code} {r.text[:200]}")
                pcm = np.frombuffer(r.content, dtype="<i2")
                path.parent.mkdir(parents=True, exist_ok=True)
                with wave.open(str(path), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(SR)
                    w.writeframes(tidy(pcm).tobytes())
                made += 1
                chars += len(text)
                print(f"  {side}/{line_key}.wav  ({len(pcm) / SR:.2f} s)  \"{text}\"")
    print(f"generated {made}, skipped {skipped} existing, ~{chars} characters billed")


if __name__ == "__main__":
    main()
