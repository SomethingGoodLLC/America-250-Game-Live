"""Render one envoy line through the negotiation service chain and play it with afplay.

  services/negotiation/.venv/bin/python scripts/speak_test.py                     # in-process main:app
  services/negotiation/.venv/bin/python scripts/speak_test.py --url http://127.0.0.1:8000   # a running service
  options: --text "..." (player line)  --llm mock|claude (in-process only)  --no-play

Path exercised: POST /v1/session (sim DiplomacyContext from protocol/test_vectors) -> control socket
player_utterance -> provider reply -> detached speech job -> sadtalker_video_ready{audio_file}.
Exit 0 only when ElevenLabs (or `say` with --allow-say) produced the WAV and afplay played it to the end.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import subprocess
import sys
import time
import wave
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SVC = ROOT / "services" / "negotiation"
CTX = ROOT / "protocol" / "test_vectors" / "clauses" / "_context.json"
WAIT_TYPES = ("sadtalker_video_ready", "avatar_unavailable", "representative_unavailable")


def _session_body() -> dict[str, Any]:
    return {"diplomacy_context": json.loads(CTX.read_text()),
            "envoy": {"name": "General Thomas Gage", "title": "Governor of Massachusetts Bay"}}


def via_testclient(text: str, timeout: float) -> list[dict[str, Any]]:
    os.chdir(SVC)
    sys.path.insert(0, str(SVC))
    from fastapi.testclient import TestClient

    import main  # noqa: PLC0415  (loads services/negotiation/.env)

    msgs: list[dict[str, Any]] = []
    with TestClient(main.app) as client:
        sid = client.post("/v1/session", json=_session_body()).json()["session_id"]
        with client.websocket_connect(f"/v1/session/{sid}/control?format=json") as ws:
            ws.send_text(json.dumps({"type": "player_utterance", "text": text}))
            deadline = time.time() + timeout
            while time.time() < deadline:
                m = json.loads(ws.receive_text())
                msgs.append(m)
                if m.get("type") in WAIT_TYPES:
                    break
    return msgs


async def via_url(url: str, text: str, timeout: float) -> list[dict[str, Any]]:
    import httpx
    import websockets

    async with httpx.AsyncClient(timeout=15) as c:
        r = await c.post(f"{url}/v1/session", json=_session_body())
        r.raise_for_status()
        sid = r.json()["session_id"]
    msgs: list[dict[str, Any]] = []
    ws_url = url.replace("http", "ws", 1) + f"/v1/session/{sid}/control?format=json"
    async with websockets.connect(ws_url) as ws:
        await ws.send(json.dumps({"type": "player_utterance", "text": text}))
        async with asyncio.timeout(timeout):
            while True:
                m = json.loads(await ws.recv())
                msgs.append(m)
                if m.get("type") in WAIT_TYPES:
                    break
    return msgs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", help="running service base URL; default runs main:app in-process")
    ap.add_argument("--text", default="Good day, General. I propose a ceasefire.")
    ap.add_argument("--llm", choices=["mock", "claude"], default="mock")
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--allow-say", action="store_true", help="accept the macOS say backup as a pass")
    ap.add_argument("--no-play", action="store_true")
    a = ap.parse_args()
    os.environ.setdefault("PA250_LLM", a.llm)
    os.environ.setdefault("PA250_AVATAR", "none")  # voice only: Stage 1

    t0 = time.monotonic()
    msgs = asyncio.run(via_url(a.url.rstrip("/"), a.text, a.timeout)) if a.url else via_testclient(a.text, a.timeout)
    spoken = next((m.get("text") for m in msgs if m.get("type") == "ai_response" and m.get("final")), None)
    ready = msgs[-1] if msgs else {}
    print(f"envoy said: {spoken!r}")
    print(f"event: {ready.get('type')}  tts_backend={ready.get('tts_backend')}  backend={ready.get('backend')}  "
          f"render_wall={time.monotonic() - t0:.2f}s")
    if ready.get("type") != "sadtalker_video_ready":
        print(f"FAIL: no audio ({ready})")
        return 1
    wav = Path(ready["audio_file"])
    with wave.open(str(wav), "rb") as w:
        rate, ch, width, secs = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes() / w.getframerate()
    print(f"wav: {wav}  {rate} Hz  {ch} ch  {8 * width}-bit  {secs:.2f} s")
    ok_backend = ready.get("tts_backend") == "elevenlabs" or (a.allow_say and ready.get("tts_backend") == "say")
    if not ok_backend:
        print("FAIL: ElevenLabs did not produce this line (see the service log for 'ElevenLabs TTS failed')")
    if ready.get("tts_backend") == "elevenlabs" and (rate, ch, width) != (24000, 1, 2):
        print("FAIL: ElevenLabs WAV is not 24 kHz mono 16-bit")
        ok_backend = False
    if a.no_play:
        return 0 if ok_backend else 1
    print(f"afplay started {wav.name} {secs:.2f}s")
    p0 = time.monotonic()
    rc = subprocess.run(["afplay", str(wav)]).returncode
    played = time.monotonic() - p0
    print(f"afplay finished rc={rc} wall={played:.2f}s")
    return 0 if ok_backend and rc == 0 and played >= secs * 0.9 else 1


if __name__ == "__main__":
    sys.exit(main())
