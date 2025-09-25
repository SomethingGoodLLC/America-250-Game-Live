# /services/negotiation/app.py
from __future__ import annotations
import asyncio, os, uuid, time
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
from providers.gemini_veo3 import Veo3Provider  # stub uses placeholder video unless USE_VEO3=1
from schemas.validators import validator
from listeners.base import make_listener_from_env

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
    # Serve the enhanced test page
    with open("web/enhanced_test_client.html", "r", encoding="utf-8") as f:
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
        "listener": None,
    }
    return _dump_yaml({"session_id": session_id})

@app.post("/v1/session/{sid}/webrtc/offer", response_class=PlainTextResponse)
async def sdp_offer(sid: str, sdp_in: SDPIn):
    sess = SESSIONS[sid]
    pc: RTCPeerConnection = sess["pc"]

    # Create listener adapter based on environment
    listener = make_listener_from_env()
    sess["listener"] = listener
    await listener.start()

    # Remote audio → listener adapter for real-time processing
    @pc.on("track")
    async def on_track(track):
        if track.kind == "audio":
            try:
                while True:
                    try:
                        frame = await track.recv()
                        # Convert frame to PCM16 bytes
                        if hasattr(frame, 'data'):
                            # Process frame data
                            audio_data = frame.data
                            # Convert to 16-bit PCM if needed
                            if hasattr(audio_data, 'tobytes'):
                                pcm_bytes = audio_data.tobytes()
                            else:
                                # Assume it's already bytes
                                pcm_bytes = bytes(audio_data)

                            # Feed to listener
                            await listener.feed_pcm(pcm_bytes, int(time.time() * 1000))
                    except Exception as e:
                        logger.debug("Audio frame processing error", error=str(e))
                        break
            except Exception as e:
                logger.error("Audio track error", error=str(e))

    # Set remote description first
    from aiortc import RTCSessionDescription
    try:
        # Use string type directly (aiortc accepts strings)
        remote_desc = RTCSessionDescription(sdp=sdp_in.sdp, type=sdp_in.type)
        await pc.setRemoteDescription(remote_desc)
        logger.info("Remote description set successfully")
    except Exception as e:
        logger.error("Failed to set remote description", error=str(e))
        raise

    # Attach avatar video after setting remote description
    try:
        await attach_avatar_track(pc, use_veo3=(sess["model"] == "veo3"))
        logger.info("Avatar track attached successfully")
    except Exception as e:
        logger.warning("Failed to attach avatar track", error=str(e))
        # Continue without video track - audio-only session

    # Create and set local description
    try:
        answer = await pc.createAnswer()
        await pc.setLocalDescription(answer)
        logger.info("Local description set successfully")
    except Exception as e:
        logger.error("Failed to create/set local description", error=str(e))
        raise

    # Return the answer SDP as JSON (browser expects JSON)
    return {"type": answer.type, "sdp": answer.sdp}

@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "ok", "service": "negotiation", "version": "1.0.0"}

@app.websocket("/v1/session/{sid}/control")
async def ws_control(ws: WebSocket, sid: str):
    await ws.accept()
    sess = SESSIONS[sid]
    sess["ws_clients"].add(ws)

    async def send_yaml(ev: dict):
        if ws.client_state == WebSocketState.CONNECTED:
            await ws.send_text(_dump_yaml(ev))

    # Pump subtitles from listener
    async def pump_subtitles():
        if sess["listener"]:
            async for ev in sess["listener"].stream_events():
                if ev.get("type") == "subtitle":
                    await send_yaml({
                        "type": "subtitle",
                        "text": ev.get("text", ""),
                        "final": ev.get("final", False),
                        "confidence": ev.get("confidence", 0.8)
                    })

    # Kick off subtitle pump
    subtitle_task = asyncio.create_task(pump_subtitles())

    # Kick off a demo provider loop (subtitles + one intent)
    async def provider_loop():
        provider = (
            Veo3Provider({"use_veo3": True}) if sess["model"] == "veo3"
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
                validator.validate_or_raise(intent, "counter_offer.v1" if intent.get("kind")=="COUNTER_OFFER" else "proposal.v1")
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
                # If we have a listener, get final text and route to provider
                if sess["listener"]:
                    final_text = await sess["listener"].final_text()
                    if final_text and final_text.strip():
                        # Use final text from listener for intent analysis
                        sess["turns"].append({"speaker":"PLAYER","text":final_text})
            # Echo ack
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not task.done():
            task.cancel()
        if not subtitle_task.done():
            subtitle_task.cancel()
        if sess["listener"]:
            await sess["listener"].stop()
        sess["ws_clients"].discard(ws)
