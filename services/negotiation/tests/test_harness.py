# Minimal test harness for negotiation service
from __future__ import annotations
import asyncio, os, uuid
from typing import Dict, Any
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import HTMLResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketState
from ruamel.yaml import YAML
from pydantic import BaseModel
from aiortc import RTCPeerConnection, MediaStreamTrack, RTCSessionDescription
from aiortc.contrib.media import MediaBlackhole
from av import VideoFrame
import numpy as np
import fractions

yaml = YAML()

app = FastAPI(title="Negotiation Test Harness")

SESSIONS: Dict[str, Dict[str, Any]] = {}

class SDPIn(BaseModel):
    sdp: str
    type: str = "offer"

def _dump_yaml(obj: Any) -> str:
    from io import StringIO
    buf = StringIO()
    yaml.dump(obj, buf)
    return buf.getvalue()

# Simple video track for testing
class TestVideoTrack(MediaStreamTrack):
    kind = "video"
    
    def __init__(self):
        super().__init__()
        self.counter = 0
        self._start_time = None
    
    async def recv(self) -> VideoFrame:
        import time
        
        # Generate a simple test pattern
        self.counter += 1
        
        if self._start_time is None:
            self._start_time = time.time()
        
        # Create a 320x240 RGB frame with a simple pattern
        width, height = 320, 240
        frame_data = np.zeros((height, width, 3), dtype=np.uint8)
        
        # Create a simple animated pattern
        offset = (self.counter // 10) % 255
        frame_data[:, :, 0] = offset  # Red channel
        frame_data[:, :, 1] = (offset + 85) % 255  # Green channel  
        frame_data[:, :, 2] = (offset + 170) % 255  # Blue channel
        
        # Add some text-like pattern
        for i in range(0, height, 20):
            frame_data[i:i+5, :, :] = 255
        
        vf = VideoFrame.from_ndarray(frame_data, format="rgb24")
        vf.pts = self.counter * 1000  # Use milliseconds
        vf.time_base = fractions.Fraction(1, 30000)  # 30 FPS in proper time base
        
        # Add a small delay to simulate real video timing
        await asyncio.sleep(1/30)
        
        return vf

# Simple mock provider for testing
class SimpleMockProvider:
    def __init__(self):
        self.counter = 0
    
    async def stream_dialogue(self, turns, world_context, system_guidelines):
        """Generate simple mock responses."""
        await asyncio.sleep(0.5)  # Simulate processing
        
        # Extract last player message
        player_text = ""
        for turn in reversed(turns):
            if turn.get("speaker") == "PLAYER":
                player_text = turn.get("text", "")
                break
        
        # Generate subtitle
        yield {
            "type": "subtitle",
            "payload": {"text": f"Processing: {player_text[:50]}..."},
            "is_final": False
        }
        
        await asyncio.sleep(1.0)
        
        yield {
            "type": "subtitle", 
            "payload": {"text": f"Understood: {player_text}"},
            "is_final": True
        }
        
        await asyncio.sleep(0.5)
        
        # Generate intent based on keywords
        if "trade" in player_text.lower() and "withdraw" in player_text.lower():
            intent = {
                "kind": "COUNTER_OFFER",
                "confidence": 0.85,
                "summary": "Trade access for troop withdrawal",
                "details": {
                    "offer": "Trade access to colonial ports",
                    "demand": "Withdrawal of troops from Ohio Country"
                }
            }
        elif "ceasefire" in player_text.lower() and "or else" in player_text.lower():
            intent = {
                "kind": "ULTIMATUM", 
                "confidence": 0.92,
                "summary": "Ceasefire demand with war threat",
                "details": {
                    "demand": "Immediate ceasefire",
                    "consequence": "Declaration of war"
                }
            }
        else:
            intent = {
                "kind": "PROPOSAL",
                "confidence": 0.75,
                "summary": "General diplomatic proposal",
                "details": {
                    "topic": "Diplomatic relations",
                    "stance": "Cooperative"
                }
            }
        
        yield {
            "type": "intent",
            "payload": intent
        }
        
        # Safety check
        yield {
            "type": "safety",
            "payload": {
                "is_safe": True,
                "reason": "Content passed all safety checks",
                "flags": []
            }
        }

@app.get("/", response_class=HTMLResponse)
async def root():
    # Serve the test page
    with open("web/test_client.html", "r", encoding="utf-8") as f:
        return HTMLResponse(f.read())

@app.post("/v1/session", response_class=PlainTextResponse)
async def create_session(request: Request):
    body = yaml.load(await request.body() or b"") or {}
    session_id = str(uuid.uuid4())[:8]
    model = body.get("model", "mock_local")
    pc = RTCPeerConnection()
    
    SESSIONS[session_id] = {
        "pc": pc,
        "model": model,
        "ws_clients": set(),
        "turns": [],
        "world_context": body.get("world_context", {}),
        "provider_task": None,
        "blackhole": MediaBlackhole(),
    }
    return _dump_yaml({"session_id": session_id})

@app.post("/v1/session/{sid}/webrtc/offer", response_class=PlainTextResponse)
async def sdp_offer(sid: str, sdp_in: SDPIn):
    sess = SESSIONS[sid]
    pc: RTCPeerConnection = sess["pc"]

    # Handle incoming audio
    @pc.on("track")
    async def on_track(track):
        if track.kind == "audio":
            sess["blackhole"].addTrack(track)

    # Add test video track before setting remote description
    video_track = TestVideoTrack()
    pc.addTrack(video_track)

    try:
        await pc.setRemoteDescription(RTCSessionDescription(sdp=sdp_in.sdp, type=sdp_in.type))
        answer = await pc.createAnswer()
        await pc.setLocalDescription(answer)
        return _dump_yaml({"type": pc.localDescription.type, "sdp": pc.localDescription.sdp})
    except Exception as e:
        # If WebRTC fails, return a simple error response
        return _dump_yaml({"error": f"WebRTC setup failed: {str(e)}", "type": "error"})

@app.websocket("/v1/session/{sid}/control")
async def ws_control(ws: WebSocket, sid: str):
    await ws.accept()
    sess = SESSIONS[sid]
    sess["ws_clients"].add(ws)

    async def send_yaml(ev: dict):
        if ws.client_state == WebSocketState.CONNECTED:
            await ws.send_text(_dump_yaml(ev))

    # Start provider loop
    async def provider_loop():
        provider = SimpleMockProvider()
        turns = sess["turns"] or [{"speaker":"PLAYER","text":"We'll grant trade access if you withdraw troops from Ohio Country."}]
        
        async for ev in provider.stream_dialogue(
            turns=turns,
            world_context=sess["world_context"],
            system_guidelines="Test harness mode"
        ):
            if ev["type"] == "subtitle":
                await send_yaml({"type":"subtitle","text": ev["payload"].get("text",""), "final": ev.get("is_final", False)})
            elif ev["type"] == "intent":
                await send_yaml({"type":"intent","payload": ev["payload"]})
            elif ev["type"] == "safety":
                await send_yaml({"type":"safety","payload": ev["payload"]})

    task = asyncio.create_task(provider_loop())
    sess["provider_task"] = task

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                sess["turns"].append({"speaker":"PLAYER","text":text})
                # Restart provider with new turn
                if not task.done():
                    task.cancel()
                task = asyncio.create_task(provider_loop())
                sess["provider_task"] = task
            # Echo ack
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not task.done():
            task.cancel()
        sess["ws_clients"].discard(ws)