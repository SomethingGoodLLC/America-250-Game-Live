# /services/negotiation/app.py
from __future__ import annotations
import asyncio, os, uuid, time
from datetime import datetime
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
from schemas.models import SpeakerTurnModel, WorldContextModel
from listeners.base import make_listener_from_env
import structlog
from core.logging_config import setup_logging

# Setup logging
setup_logging()
logger = structlog.get_logger(__name__)

yaml = YAML()

app = FastAPI(title="Negotiation Service (YAML/WebRTC)")
app.mount("/assets", StaticFiles(directory="assets"), name="assets")

SESSIONS: Dict[str, Dict[str, Any]] = {}

DEFAULT_WORLD_CONTEXT = {
    "scenario_tags": ["default_scenario"],
    "initiator_faction": {"id": "player::default", "name": "Player Faction"},
    "counterpart_faction": {"id": "ai::default", "name": "AI Faction"},
    "current_state": {},
}


def sanitize_world_context(raw: Dict[str, Any] | None, session_id: str) -> Dict[str, Any]:
    ctx = raw.copy() if isinstance(raw, dict) else {}
    scenario_tags = ctx.get("scenario_tags")
    if not isinstance(scenario_tags, list) or not scenario_tags:
        scenario_tags = DEFAULT_WORLD_CONTEXT["scenario_tags"].copy()
    ctx["scenario_tags"] = scenario_tags

    initiator = ctx.get("initiator_faction") if isinstance(ctx.get("initiator_faction"), dict) else {}
    initiator.setdefault("name", DEFAULT_WORLD_CONTEXT["initiator_faction"]["name"])
    initiator_id = initiator.get("id") or f"player::{session_id}"
    initiator["id"] = initiator_id
    ctx["initiator_faction"] = initiator

    counterpart = ctx.get("counterpart_faction") if isinstance(ctx.get("counterpart_faction"), dict) else {}
    counterpart.setdefault("name", DEFAULT_WORLD_CONTEXT["counterpart_faction"]["name"])
    counterpart_id = counterpart.get("id") or f"ai::{session_id}"
    counterpart["id"] = counterpart_id
    ctx["counterpart_faction"] = counterpart

    current_state = ctx.get("current_state") if isinstance(ctx.get("current_state"), dict) else {}
    ctx["current_state"] = current_state

    return ctx

class SDPIn(BaseModel):
    sdp: str
    type: str = "offer"

def _dump_yaml(obj: Any) -> str:
    from io import StringIO
    buf = StringIO()
    yaml.dump(obj, buf)
    return buf.getvalue()

async def generate_ai_response(sess: dict, user_text: str, send_yaml_func):
    """Generate AI response to user input and trigger avatar generation."""
    try:
        logger.info("Generating AI response", user_text=user_text)
        if sess["model"] == "veo3":
            provider = Veo3Provider(use_veo3=True)
        else:
            # Use MockLocalProvider for both "mock_local" and "teller" models
            # The difference is in avatar generation, not AI logic
            provider = MockLocalProvider({"strict": True})
        turns = [turn if isinstance(turn, SpeakerTurnModel) else SpeakerTurnModel(**turn) for turn in sess["turns"][-5:]]
        world_context_dict = sanitize_world_context(sess.get("world_context"), sess.get("session_id", "default"))
        world_context = WorldContextModel(**world_context_dict)
        await send_yaml_func({"type": "ai_thinking", "text": "🤔 AI is thinking..."})
        async for ev in provider.stream_dialogue(
            turns=turns,
            world_context=world_context,
            system_guidelines="You are a diplomatic AI negotiator. Respond conversationally to the human's statement. Be engaging and strategic."
        ):
            logger.info("Provider event received", event_type=ev.type, payload_keys=list(ev.payload.keys()) if ev.payload else [])
            
            if ev.type == "subtitle":
                logger.info("Sending AI response", text=ev.payload.get("text", ""), final=ev.is_final)
                await send_yaml_func({"type": "ai_response", "text": ev.payload.get("text", ""), "final": ev.is_final})
                if ev.is_final and ev.payload.get("text", "").strip():
                    ai_text = ev.payload.get("text", "").strip()
                    ai_turn = {
                        "speaker": "AI",
                        "text": ai_text,
                        "speaker_id": world_context_dict["counterpart_faction"]["id"],
                        "timestamp": datetime.utcnow().isoformat(),
                    }
                    sess["turns"].append(ai_turn)
                    logger.info("Added AI turn to session", ai_text=ai_text)
                    
                    # Generate avatar based on model type
                    if sess["model"] == "veo3":
                        await generate_veo3_avatar(ai_text, send_yaml_func)
                    elif sess["model"] == "teller":
                        await generate_teller_avatar(ai_text, send_yaml_func)
                        
            elif ev.type == "intent":
                logger.info("Sending AI intent", intent_type=ev.payload.get("intent", {}).get("type", "unknown"))
                await send_yaml_func({"type": "ai_intent", "payload": ev.payload})
    except Exception as e:
        logger.error("Error generating AI response", error=str(e))
        await send_yaml_func({"type": "ai_response", "text": "I apologize, I'm having trouble processing that. Could you rephrase?", "final": True})

async def generate_veo3_avatar(ai_text: str, send_yaml_func):
    try:
        logger.info("Generating Veo3 avatar", text=ai_text)
        veo3 = Veo3Provider(use_veo3=True)
        await send_yaml_func({"type": "avatar_generating", "text": "🎬 Generating avatar response..."})
        await asyncio.sleep(2)
        await send_yaml_func({"type": "avatar_ready", "text": f"🎭 Avatar ready: {ai_text}", "video_url": "/api/avatar/latest.mp4", "final": True})
    except Exception as e:
        logger.error("Error generating Veo3 avatar", error=str(e))

async def generate_teller_avatar(ai_text: str, send_yaml_func):
    """Generate Teller Avatar animation for AI speech."""
    try:
        logger.info("Generating Teller avatar", text=ai_text)
        
        # Send avatar generation status
        await send_yaml_func({
            "type": "avatar_generating",
            "text": "🎭 Teller Avatar animating...",
            "final": False
        })
        
        # Simulate processing time (in real implementation, this would trigger TTS + animation)
        await asyncio.sleep(0.5)
        
        # Send avatar ready status
        await send_yaml_func({
            "type": "avatar_ready",
            "text": f"🎬 Teller Avatar speaking: {ai_text}",
            "animation_data": {
                "text": ai_text,
                "duration": len(ai_text) * 100,  # Estimate speaking duration
                "emotion": "diplomatic"
            },
            "final": True
        })
        
    except Exception as e:
        logger.error("Error generating Teller avatar", error=str(e))

@app.get("/", response_class=HTMLResponse)
async def root():
    # Serve the enhanced test page
    with open("web/enhanced_test_client.html", "r", encoding="utf-8") as f:
        return HTMLResponse(f.read())

@app.get("/teller-avatar-component.html", response_class=HTMLResponse)
async def teller_avatar_component():
    # Serve the Teller Avatar component
    with open("web/teller-avatar-component.html", "r", encoding="utf-8") as f:
        return HTMLResponse(f.read())

from fastapi.staticfiles import StaticFiles
# Mount the teller-avatar directory to serve videos
app.mount("/teller-avatar", StaticFiles(directory="teller-avatar"), name="teller-avatar")

@app.post("/v1/session")
async def create_session(request: Request):
    try:
        body = await request.json()
    except:
        body = {}
    session_id = str(uuid.uuid4())[:8]
    model = body.get("model", "mock_local")  # mock_local | veo3
    pc = RTCPeerConnection()
    world_context = sanitize_world_context(body.get("world_context"), session_id)
    initiator_id = world_context["initiator_faction"]["id"]
    counterpart_id = world_context["counterpart_faction"]["id"]
    # NOTE: we attach avatar track after SDP answer (in /offer)
    SESSIONS[session_id] = {
        "pc": pc,
        "model": model,
        "ws_clients": set(),
        "turns": [],
        "world_context": world_context,
        "initiator_id": initiator_id,
        "counterpart_id": counterpart_id,
        "provider_task": None,
        "provider_tasks": [],
        "blackhole": MediaBlackhole(),
        "listener": None,
        "session_id": session_id,
    }
    return {"session_id": session_id}

@app.post("/v1/session/{sid}/webrtc/offer")
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

    # Validate and set remote description
    from aiortc import RTCSessionDescription
    from fastapi import HTTPException
    
    # Basic SDP validation
    if not sdp_in.sdp or not sdp_in.sdp.strip():
        logger.error("Empty SDP offer received")
        raise HTTPException(status_code=400, detail="SDP offer cannot be empty")
        
    if not sdp_in.sdp.startswith("v="):
        logger.error("Invalid SDP format - missing version line")
        raise HTTPException(status_code=400, detail="Invalid SDP format")
    
    # Required SDP lines for a basic offer
    required_lines = ["o=", "s=", "m="]
    for req_line in required_lines:
        if req_line not in sdp_in.sdp:
            logger.error(f"Missing required SDP line: {req_line}")
            raise HTTPException(status_code=400, detail=f"Missing required SDP line: {req_line}")
    
    try:
        # Use string type directly (aiortc accepts strings)
        remote_desc = RTCSessionDescription(sdp=sdp_in.sdp, type=sdp_in.type)
        await pc.setRemoteDescription(remote_desc)
        logger.info("Remote description set successfully")
    except Exception as e:
        logger.error("Failed to set remote description", error=str(e))
        raise HTTPException(status_code=400, detail=f"Failed to set remote description: {str(e)}")

    # Skip video track for now to avoid aiortc direction bug
    # Video tracks will be added after successful WebRTC connection
    logger.info("Skipping video track attachment to avoid aiortc direction bug")

    # Create and set local description with enhanced error handling
    try:
        answer = await pc.createAnswer()
        await pc.setLocalDescription(answer)
        logger.info("Local description set successfully")
        # Return the answer SDP as JSON (browser expects JSON)
        return {"type": answer.type, "sdp": answer.sdp}
        
    except ValueError as e:
        if "None is not in list" in str(e):
            logger.error("aiortc direction bug encountered - attempting workaround", error=str(e))
            # Try to create a new RTCPeerConnection and retry
            try:
                # Create fresh peer connection
                new_pc = RTCPeerConnection()
                
                # Re-attach the same event handlers
                @new_pc.on("track")
                async def on_track_retry(track):
                    if track.kind == "audio":
                        try:
                            while True:
                                try:
                                    frame = await track.recv()
                                    if hasattr(frame, 'data'):
                                        audio_data = frame.data
                                        if hasattr(audio_data, 'tobytes'):
                                            pcm_bytes = audio_data.tobytes()
                                        else:
                                            pcm_bytes = bytes(audio_data)
                                        await listener.feed_pcm(pcm_bytes, int(time.time() * 1000))
                                except Exception as e:
                                    logger.debug("Audio frame processing error in retry", error=str(e))
                                    break
                        except Exception as e:
                            logger.error("Audio track error in retry", error=str(e))
                
                # Set remote description on new connection
                from aiortc import RTCSessionDescription
                remote_desc = RTCSessionDescription(sdp=sdp_in.sdp, type=sdp_in.type)
                await new_pc.setRemoteDescription(remote_desc)
                
                # Skip video track on retry as well
                logger.info("Skipping video track on retry to avoid direction bug")
                
                # Create answer with new connection
                answer = await new_pc.createAnswer()
                await new_pc.setLocalDescription(answer)
                
                # Replace the old peer connection
                sess["pc"] = new_pc
                
                logger.info("Successfully created answer with fresh RTCPeerConnection")
                return {"type": answer.type, "sdp": answer.sdp}
                
            except Exception as retry_error:
                logger.error("Retry with fresh RTCPeerConnection failed", error=str(retry_error))
                from fastapi import HTTPException
                raise HTTPException(status_code=500, detail=f"WebRTC connection failed: {str(retry_error)}")
        else:
            logger.error("Failed to create/set local description", error=str(e))
            from fastapi import HTTPException
            raise HTTPException(status_code=400, detail=f"ValueError in local description: {str(e)}")
    except Exception as e:
        logger.error("Failed to create/set local description", error=str(e))
        from fastapi import HTTPException
        raise HTTPException(status_code=500, detail=f"Failed to create local description: {str(e)}")

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
    async def pump_subtitles():
        if sess["listener"]:
            async for ev in sess["listener"].stream_events():
                if ev.get("type") == "subtitle":
                    await send_yaml({"type": "subtitle", "text": ev.get("text", ""), "final": ev.get("final", False), "confidence": ev.get("confidence", 0.8)})
                    if ev.get("final", False) and ev.get("text", "").strip():
                        user_text = ev.get("text", "").strip()
                        logger.info("User said (final)", text=user_text)
                        turn_entry = {
                            "speaker": "PLAYER",
                            "speaker_id": sess.get("initiator_id", f"player::{sid}"),
                            "text": user_text,
                            "timestamp": datetime.utcnow().isoformat(),
                        }
                        sess["turns"].append(turn_entry)
                        task = asyncio.create_task(generate_ai_response(sess, user_text, send_yaml))
                        sess["provider_tasks"].append(task)
    subtitle_task = asyncio.create_task(pump_subtitles())
    sess["provider_tasks"].append(subtitle_task)

    # Remove old demo provider loop; real-time responses now handled via listener trigger

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                turn_entry = {
                    "speaker": "PLAYER",
                    "speaker_id": sess.get("initiator_id", f"player::{sid}"),
                    "text": text,
                    "timestamp": datetime.utcnow().isoformat(),
                }
                sess["turns"].append(turn_entry)
                task = asyncio.create_task(generate_ai_response(sess, text, send_yaml))
                sess["provider_tasks"].append(task)
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not subtitle_task.done():
            subtitle_task.cancel()
        if sess["listener"]:
            await sess["listener"].stop()
        sess["ws_clients"].discard(ws)
        # Cancel any outstanding AI tasks
        for t in sess.get("provider_tasks", []):
            if not t.done():
                t.cancel()
