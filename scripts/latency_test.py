"""Envoy response time: player line received -> first audio sent to the room, split into Claude and TTS.

  services/negotiation/.venv/bin/python scripts/latency_test.py            # streaming (default)
  PA250_STREAM=0 services/negotiation/.venv/bin/python scripts/latency_test.py   # baseline: whole reply, then TTS
  options: --model claude-haiku-5-5 (envoy model for this run)

Runs main:app in-process with Claude and ElevenLabs (real calls, a few hundred characters). The room adds its
own start time on top (logged in Unreal as "EnvoyLatency").
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SVC = ROOT / "services" / "negotiation"
LINES = [
    "General Gage, I would speak with you about the prisoners taken at Lexington.",
    "I offer you three hundred guineas and the governorship of Massachusetts if you open Boston's gates.",
    "Will you grant my militia safe passage through Charlestown for ten days?",
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=None)
    a = ap.parse_args()
    os.chdir(SVC)
    sys.path.insert(0, str(SVC))
    os.environ.pop("PA250_LLM", None)
    os.environ.setdefault("PA250_TTS", "auto")
    os.environ.setdefault("PA250_AVATAR", "none")
    os.environ["PA250_STT_PREWARM"] = "0"
    if a.model:
        os.environ["PA250_LLM_MODEL"] = a.model
    from fastapi.testclient import TestClient

    import main

    ctx = json.loads((ROOT / "protocol" / "test_vectors" / "clauses" / "_context.json").read_text())
    rows = []
    with TestClient(main.app) as c:
        sid = c.post("/v1/session", json={"diplomacy_context": ctx, "envoy": {"name": "General Thomas Gage",
                     "title": "Military Governor of Massachusetts"}}).json()["session_id"]
        with c.websocket_connect(f"/v1/session/{sid}/control?format=json") as ws:
            for line in LINES:
                t0 = time.time()
                ws.send_text(json.dumps({"type": "player_utterance", "text": line}))
                first, proposal, chunks, spoken = None, None, 0, ""
                while True:
                    m = json.loads(ws.receive_text())
                    if m.get("type") == "sadtalker_video_ready":
                        chunks += 1
                        if first is None:
                            first = m
                            first["_t"] = time.time() - t0
                    elif m.get("type") == "diplomacy_proposal":
                        proposal, spoken = time.time() - t0, m.get("spoken_line", "")
                    elif m.get("type") in ("envoy_speech_end", "representative_unavailable"):
                        break
                tm = (first or {}).get("timing", {})
                rows.append({"first_audio_s": round(first["_t"], 2) if first else None,
                             "claude_first_sentence_s": tm.get("sentence_ready_s"), "tts_first_s": tm.get("tts_s"),
                             "full_reply_s": round(proposal, 2) if proposal else None, "chunks": chunks,
                             "first_audio_seconds": (first or {}).get("audio_seconds"), "spoken": spoken})
    mode = "streaming" if os.getenv("PA250_STREAM", "1") != "0" else "baseline (no streaming)"
    print(f"== {mode}, model {os.getenv('PA250_LLM_MODEL') or 'default (claude-sonnet-5-5)'}")
    for r in rows:
        print(json.dumps(r))
    firsts = [r["first_audio_s"] for r in rows if r["first_audio_s"]]
    if firsts:
        print(f"first audio sent: median {statistics.median(firsts):.2f}s, max {max(firsts):.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
