"""Pre-render the envoy's stock lines as real lip-synced fal video (720p), voiced like live lines.

  services/negotiation/.venv/bin/python scripts/render_stock_lines.py [--force]

Writes services/negotiation/generated_video/stock/british_<name>.mp4 (H.264 + AAC, voice muxed in). The service
announces the ones that exist when the room connects ("envoy_stock"); the room plays them as video:
  greeting   when the room opens
  consider   while a reply is slow to start
  acceptance when the player accepts terms
  refusal    when the player rejects terms
Costs about $0.15 per second of video (Fabric 1.0, 720p) on a fresh render; existing files are skipped.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SVC = ROOT / "services" / "negotiation"
LINES = {
    "greeting": "General Thomas Gage, at your service. You asked for a parley, sir. I am listening.",
    "consider": "Hm. Let me consider that a moment, sir.",
    "acceptance": "Then we are agreed, sir. I shall send word to my officers this very hour.",
    "refusal": "No, sir. That I will not grant, and you would do well not to ask it again.",
}


async def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    os.chdir(SVC)
    sys.path.insert(0, str(SVC))
    from dotenv import load_dotenv

    load_dotenv(".env")
    os.environ["PA250_TTS"] = "elevenlabs"
    os.environ["FAL_RESOLUTION"] = "720p"
    from diplomacy import avatar
    from providers.video_sources.fal_talking_source import FalTalkingVideoSource

    out_dir = avatar.VIDEO_DIR / "stock"
    out_dir.mkdir(parents=True, exist_ok=True)
    src = FalTalkingVideoSource(model="veed/fabric-1.0")
    reason = src.unavailable_reason()
    if reason:
        print("fal unavailable:", reason)
        return 2
    total_cost = 0.0
    for name, text in LINES.items():
        mp4 = out_dir / f"british_{name}.mp4"
        if mp4.exists() and not a.force:
            print(f"{name}: exists, skipped")
            continue
        tts = await avatar.synthesize_line_audio(text, f"stock_british_{name}", envoy_id="british")
        if not tts:
            print(f"{name}: TTS failed")
            return 1
        wav = out_dir / f"british_{name}.wav"
        shutil.move(tts[0], wav)
        r = await src.render(str(wav), str(avatar.DEFAULT_PORTRAIT), str(mp4))
        if not r.path:
            print(f"{name}: render failed: {r.error}")
            return 1
        (out_dir / f"british_{name}.txt").write_text(text + "\n")
        total_cost += r.est_cost_usd
        print(f"{name}: {r.seconds:.2f}s video, render {r.render_s:.0f}s, est ${r.est_cost_usd:.2f} -> {mp4}")
    print(f"total est ${total_cost:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
