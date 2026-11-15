"""Render the battle stories' narration with ElevenLabs.

The key is the one configured for the negotiation service. Run with its venv:
  services/negotiation/.venv/bin/python tools/audio/gen_story_narration.py
  (options: --force, --only key,key)

Reads unreal/PA250Sim/data/battle_stories.json and writes one 24 kHz mono 16-bit
WAV for each story and terrain fallback to
unreal/PlayAmerica250/SourceAudio/story/<narration>.wav: the title, the date and
the sentences, read as one piece. Existing files are skipped (no credits spent)
unless --force. The API key is read from services/negotiation/.env and never
printed. Without a key nothing is rendered and the game shows the text alone.

Then import (only /Game/PA250/Audio/Story is touched):
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript
    -script=Scripts/import_story_audio.py -unattended -nullrhi
"""

from __future__ import annotations

import argparse
import json
import sys
import wave
from pathlib import Path

import httpx
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
STORIES = ROOT / "unreal" / "PA250Sim" / "data" / "battle_stories.json"
OUT = ROOT / "unreal" / "PlayAmerica250" / "SourceAudio" / "story"
ENV = ROOT / "services" / "negotiation" / ".env"
SR = 24000
MODEL = "eleven_multilingual_v2"
# The premade voice of the Continental officer's shouted lines, read evenly.
VOICE = "pNInz6obpgDQGcFmaJgB"
SETTINGS = {
    "stability": 0.6,
    "similarity_boost": 0.8,
    "style": 0.15,
    "use_speaker_boost": True,
}


def read_key() -> str | None:
    if not ENV.exists():
        return None
    for line in ENV.read_text().splitlines():
        if line.startswith("ELEVENLABS_API_KEY="):
            return line.split("=", 1)[1].strip().strip('"').strip("'") or None
    return None


def tidy(pcm: np.ndarray) -> np.ndarray:
    """Trim leading and trailing silence, normalise to -3 dBFS, 20 ms fades."""
    x = pcm.astype(np.float64) / 32768.0
    loud = np.flatnonzero(np.abs(x) > 0.01)
    if len(loud):
        x = x[max(0, loud[0] - int(0.05 * SR)) : loud[-1] + int(0.25 * SR)]
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * 10 ** (-3.0 / 20.0)
    f = int(0.02 * SR)
    if len(x) > 2 * f:
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
    return (x * 32767).astype("<i2")


def spoken(story: dict[str, object]) -> str:
    sentences = story["text"]
    assert isinstance(sentences, list)
    return f"{story['title']}. {story['date']}. " + " ".join(str(s) for s in sentences)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--force", action="store_true", help="render even if the WAV exists"
    )
    ap.add_argument("--only", default="", help="comma-separated story keys")
    args = ap.parse_args()
    data = json.loads(STORIES.read_text())
    stories = data["stories"] + data["fallbacks"]
    only = {k.strip() for k in args.only.split(",") if k.strip()}
    key = read_key()
    if not key:
        print(
            "no ELEVENLABS_API_KEY configured: nothing rendered; "
            "the stories show their text alone"
        )
        return
    made = skipped = chars = 0
    with httpx.Client(timeout=120) as client:
        for story in stories:
            if only and story["key"] not in only:
                continue
            path = OUT / f"{story['narration']}.wav"
            if path.exists() and not args.force:
                skipped += 1
                continue
            text = spoken(story)
            r = client.post(
                f"https://api.elevenlabs.io/v1/text-to-speech/{VOICE}",
                params={"output_format": f"pcm_{SR}"},
                headers={"xi-api-key": key, "Content-Type": "application/json"},
                json={"text": text, "model_id": MODEL, "voice_settings": SETTINGS},
            )
            if r.status_code != 200:
                sys.exit(f"{story['key']}: HTTP {r.status_code} {r.text[:200]}")
            pcm = np.frombuffer(r.content, dtype="<i2")
            path.parent.mkdir(parents=True, exist_ok=True)
            with wave.open(str(path), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(SR)
                w.writeframes(tidy(pcm).tobytes())
            made += 1
            chars += len(text)
            print(f"  {path.name}  ({len(pcm) / SR:.1f} s, {len(text)} characters)")
    print(f"rendered {made}, skipped {skipped} existing, ~{chars} characters billed")


if __name__ == "__main__":
    main()
