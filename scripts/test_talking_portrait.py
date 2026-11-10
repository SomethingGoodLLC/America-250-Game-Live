"""Render one real envoy line through fal_talking and check the MP4.

  services/negotiation/.venv/bin/python scripts/test_talking_portrait.py            # run twice: 2nd is a cache hit
  options: --text "..."  --model veed/fabric-1.0|fal-ai/bytedance/omnihuman  --play (open in QuickTime)

1. ElevenLabs WAV for the line (British envoy voice). Kept per text in generated_audio/talking_portrait_test_*.wav
   so a second run feeds fal the same bytes (ElevenLabs output differs on every call).
2. fal_talking render -> generated_video/talking_portrait_test.mp4. Prints model, render time, billed seconds, cost.
3. ffprobe: exactly one video stream (h264) and one audio stream (aac).
Prints "fal calls this run: N" (counted at fal_client submit). A cache hit must show 0.
Spends real money on a cache miss: about $0.08 per second of video (Fabric 480p).
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SVC = ROOT / "services" / "negotiation"


async def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--text", default="You ask a great deal, sir. The Crown will consider a truce of three days, no more.")
    ap.add_argument("--model", default=None, help="default: FAL_TALKING_MODEL or veed/fabric-1.0")
    ap.add_argument("--envoy", default="british")
    ap.add_argument("--play", action="store_true")
    a = ap.parse_args()

    os.chdir(SVC)
    sys.path.insert(0, str(SVC))
    from dotenv import load_dotenv

    load_dotenv(SVC / ".env")
    import fal_client

    from diplomacy import avatar
    from providers.video_sources.fal_talking_source import MODELS, FalTalkingVideoSource, probe_streams

    calls = {"n": 0}
    orig_submit = fal_client.AsyncClient.submit

    async def counting_submit(self, *args, **kw):  # type: ignore[no-untyped-def]
        calls["n"] += 1
        return await orig_submit(self, *args, **kw)

    fal_client.AsyncClient.submit = counting_submit  # type: ignore[method-assign]

    src = FalTalkingVideoSource(model=a.model)
    reason = src.unavailable_reason()
    if reason:
        print(f"FAIL: fal_talking unavailable: {reason}")
        return 2

    digest = hashlib.sha256(f"{a.envoy}|{a.text}".encode()).hexdigest()[:16]
    wav = avatar.AUDIO_DIR / f"talking_portrait_test_{digest}.wav"
    if not wav.exists():
        os.environ["PA250_TTS"] = "elevenlabs"
        out = await avatar.synthesize_line_audio(a.text, f"tp_{digest}", envoy_id=a.envoy)
        if not out:
            print("FAIL: ElevenLabs produced no audio (see 'ElevenLabs TTS failed' above)")
            return 1
        shutil.move(out[0], wav)
        print(f"tts: new ElevenLabs WAV {wav.name} ({avatar.wav_seconds(wav):.2f}s)")
    else:
        print(f"tts: reusing {wav.name} ({avatar.wav_seconds(wav):.2f}s)")

    out_mp4 = avatar.VIDEO_DIR / "talking_portrait_test.mp4"
    out_mp4.parent.mkdir(parents=True, exist_ok=True)
    t0 = time.monotonic()
    budget = float(os.getenv("PA250_AVATAR_TIMEOUT", "90"))
    try:
        r = await asyncio.wait_for(src.render(str(wav), str(avatar.DEFAULT_PORTRAIT), str(out_mp4)), budget)
    except TimeoutError:
        print(f"FAIL: render exceeded PA250_AVATAR_TIMEOUT={budget:.0f}s (the game would go audio-only)")
        return 1
    wall = time.monotonic() - t0
    if not r.path:
        print(f"FAIL: {r.error}")
        return 1
    rate = MODELS[src.model_id].rate(src.resolution)
    print(f"model: {r.model} {src.resolution or ''}  cache_hit={r.cache_hit}  parts={r.parts}")
    print(f"render time: {wall:.1f}s  video: {r.seconds:.2f}s  est cost: ${r.est_cost_usd:.2f} (${rate}/s)")
    print(f"fal calls this run: {calls['n']}")
    probe = await probe_streams(r.path)
    print(f"ffprobe: video={probe['video']} audio={probe['audio']} duration={probe['duration']:.2f}s  -> {r.path}")
    ok = probe["video"] == ["h264"] and probe["audio"] == ["aac"]
    ok = ok and (calls["n"] == 0) == r.cache_hit
    if a.play:
        subprocess.run(["open", "-a", "QuickTime Player", r.path])
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
