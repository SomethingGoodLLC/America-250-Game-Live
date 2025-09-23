# /services/negotiation/app.py
from __future__ import annotations
import asyncio, os, uuid
from typing import Dict, Any
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import HTMLResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketState
from ruamel.yaml import YAML
from pydantic import BaseModel
from aiortc import RTCPeerConnection
from aiortc.contrib.media import MediaBlackhole
from webrtc.publish import attach_avatar_track_simple as attach_avatar_track
from providers.mock_local import MockLocalProvider
from providers.gemini_veo3 import GeminiVeo3Provider  # stub uses placeholder video unless USE_VEO3=1
from schemas.validators import validate_or_raise

yaml = YAML()

app = FastAPI(title="Negotiation Service (YAML/WebRTC)")
app.mount("/assets", StaticFiles(directory="assets"), name="assets")

SESSIONS: Dict[str, Dict[str, Any]] = {}

class SDPIn(BaseModel):
    sdp: str
    type: str = "offer"

def _dump_yaml(obj: Any) -> str:
    from io import StringIO
    buf = StringIO()
    yaml.dump(obj, buf)
    return buf.getvalue()

@app.get("/", response_class=HTMLResponse)
async def root():
    # Serve the test page (see section 2)
    with open("web/test_client.html", "r", encoding="utf-8") as f:
        return HTMLResponse(f.read())

@app.post("/v1/session", response_class=PlainTextResponse)
async def create_session(request: Request):
    body = yaml.load(await request.body() or b"") or {}
    session_id = str(uuid.uuid4())[:8]
    model = body.get("model", "mock_local")  # mock_local | veo3
    pc = RTCPeerConnection()
    # NOTE: we attach avatar track after SDP answer (in /offer)
    SESSIONS[session_id] = {
        "pc": pc,
        "model": model,
        "ws_clients": set(),
        "turns": [],  # speaker_turn.v1 items
        "world_context": body.get("world_context", {}),
        "provider_task": None,
        "blackhole": MediaBlackhole(),
    }
    return _dump_yaml({"session_id": session_id})

@app.post("/v1/session/{sid}/webrtc/offer", response_class=PlainTextResponse)
async def sdp_offer(sid: str, sdp_in: SDPIn):
    sess = SESSIONS[sid]
    pc: RTCPeerConnection = sess["pc"]

    # Remote audio → (optional) local record or pipe to listener adapter
    @pc.on("track")
    async def on_track(track):
        if track.kind == "audio":
            # In production: feed PCM chunks → listener adapter (Gemini/Grok/OpenAI)
            sess["blackhole"].addTrack(track)
        # (No incoming video from the browser needed)

    # Attach avatar video (placeholder or Veo3) to the peer
    await attach_avatar_track(pc, use_veo3=(sess["model"] == "veo3"))

    await pc.setRemoteDescription({"type": sdp_in.type, "sdp": sdp_in.sdp})
    answer = await pc.createAnswer()
    await pc.setLocalDescription(answer)
    return _dump_yaml({"type": pc.localDescription.type, "sdp": pc.localDescription.sdp})

@app.websocket("/v1/session/{sid}/control")
async def ws_control(ws: WebSocket, sid: str):
    await ws.accept()
    sess = SESSIONS[sid]
    sess["ws_clients"].add(ws)

    async def send_yaml(ev: dict):
        if ws.client_state == WebSocketState.CONNECTED:
            await ws.send_text(_dump_yaml(ev))

    # Kick off a demo provider loop (subtitles + one intent)
    async def provider_loop():
        provider = (
            GeminiVeo3Provider({"use_veo3": True}) if sess["model"] == "veo3"
            else MockLocalProvider({"strict": True})
        )
        # For demo, turns include a single PLAYER utterance if we got one from client
        turns = sess["turns"] or [{"speaker":"PLAYER","text":"We'll grant trade access if you withdraw troops from Ohio Country."}]
        async for ev in provider.stream_dialogue(
            turns=turns,
            world_context=sess["world_context"],
            system_guidelines="YAML-only outputs; 1607–1799 tone; safe content."
        ):
            if ev.type == "subtitle":
                await send_yaml({"type":"subtitle","text": ev.payload.get("text",""), "final": ev.is_final})
            elif ev.type == "intent":
                intent = ev.payload
                # Validate and then broadcast
                validate_or_raise(intent, "counter_offer.v1" if intent.get("kind")=="COUNTER_OFFER" else "proposal.v1")
                await send_yaml({"type":"intent","payload": intent})
            elif ev.type == "safety":
                await send_yaml({"type":"safety","payload": ev.payload})
            elif ev.type == "analysis":
                await send_yaml({"type":"analysis","tag": ev.tag, "payload": ev.payload})

    task = asyncio.create_task(provider_loop())
    sess["provider_task"] = task

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                sess["turns"].append({"speaker":"PLAYER","text":text})
            # Echo ack
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not task.done():
            task.cancel()
        sess["ws_clients"].discard(ws)
