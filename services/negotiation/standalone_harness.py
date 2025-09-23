#!/usr/bin/env python3
"""
Standalone test harness for AI Avatar negotiation.
This is a simplified version that focuses on the core WebRTC + WebSocket functionality
without depending on the complex existing infrastructure.
"""

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
from aiortc.mediastreams import MediaStreamTrack
from av import VideoFrame
import numpy as np

yaml = YAML()

app = FastAPI(title="AI Avatar Test Harness")
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

# Simplified video source for testing
class TestVideoSource:
    def __init__(self):
        self.frame_count = 0

    async def start(self):
        pass

    async def frames(self):
        while True:
            # Generate a simple animated test pattern
            frame = await self._generate_test_frame()
            self.frame_count += 1
            yield frame
            await asyncio.sleep(1/30)  # 30 FPS

    async def _generate_test_frame(self):
        # Create a simple RGB test pattern
        height, width = 240, 320
        frame = np.zeros((height, width, 3), dtype=np.uint8)

        # Add some animation
        phase = self.frame_count * 0.1
        for y in range(height):
            for x in range(width):
                # Simple pulsing pattern
                r = int(128 + 127 * np.sin(phase + x * 0.01))
                g = int(128 + 127 * np.sin(phase + y * 0.01))
                b = int(128 + 127 * np.cos(phase + (x + y) * 0.01))
                frame[y, x] = [r, g, b]

        return frame

# Simplified FrameTrack
class FrameTrack(MediaStreamTrack):
    kind = "video"

    def __init__(self, source):
        super().__init__()
        self.source = source
        self._ait = None

    async def recv(self) -> VideoFrame:
        if self._ait is None:
            self._ait = self.source.frames()

        frame_np = await self._ait.__anext__()
        h, w, _ = frame_np.shape
        vf = VideoFrame.from_ndarray(frame_np, format="rgb24")
        vf.pts, vf.time_base = None, None
        return vf

async def attach_avatar_track(pc: RTCPeerConnection, use_veo3: bool = False):
    """Attach a test video track to peer connection."""
    source = TestVideoSource()
    await source.start()
    track = FrameTrack(source)
    pc.addTrack(track)

@app.get("/", response_class=HTMLResponse)
async def root():
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

    # Attach test video
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

    # Simple mock provider loop
    async def provider_loop():
        turns = sess["turns"] or [{"speaker":"PLAYER","text":"We'll grant trade access if you withdraw troops from Ohio Country."}]

        # Simulate subtitle events
        for i, turn in enumerate(turns):
            await send_yaml({"type":"subtitle","text": turn["text"], "final": True})
            await asyncio.sleep(1)

        # Simulate intent detection
        await send_yaml({
            "type": "intent",
            "payload": {
                "kind": "COUNTER_OFFER",
                "demand": {"troops_withdrawal": "Ohio Country"},
                "offer": {"trade_access": True},
                "rationale": ["Strategic necessity", "Economic benefits"],
                "confidence": 0.85
            }
        })

    task = asyncio.create_task(provider_loop())
    sess["provider_task"] = task

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            if obj.get("type") == "player_utterance":
                text = obj.get("text","")
                sess["turns"].append({"speaker":"PLAYER","text":text})
            await send_yaml({"type":"ack"})
    except WebSocketDisconnect:
        pass
    finally:
        if not task.done():
            task.cancel()
        sess["ws_clients"].discard(ws)

if __name__ == "__main__":
    import uvicorn
    print("🚀 Starting AI Avatar Test Harness...")
    print("📖 Open http://localhost:8000 in your browser!")
    uvicorn.run(app, host="0.0.0.0", port=8000)
