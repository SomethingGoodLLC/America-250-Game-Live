#!/usr/bin/env python3
"""
Enhanced test harness for AI Avatar negotiation.
This version properly integrates with the existing provider system and schemas.
"""

from __future__ import annotations
import asyncio, os, uuid
from typing import Dict, Any, List
from datetime import datetime
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
import structlog

# Configure logging
structlog.configure(
    processors=[
        structlog.stdlib.filter_by_level,
        structlog.stdlib.add_logger_name,
        structlog.stdlib.add_log_level,
        structlog.stdlib.PositionalArgumentsFormatter(),
        structlog.processors.TimeStamper(fmt="iso"),
        structlog.processors.StackInfoRenderer(),
        structlog.processors.format_exc_info,
        structlog.processors.UnicodeDecoder(),
        structlog.processors.JSONRenderer()
    ],
    context_class=dict,
    logger_factory=structlog.stdlib.LoggerFactory(),
    wrapper_class=structlog.stdlib.BoundLogger,
    cache_logger_on_first_use=True,
)

yaml = YAML()
logger = structlog.get_logger(__name__)

app = FastAPI(title="Enhanced AI Avatar Test Harness")
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

# Enhanced video source with better avatar simulation
class EnhancedVideoSource:
    def __init__(self, avatar_style: str = "diplomatic"):
        self.frame_count = 0
        self.avatar_style = avatar_style
        self.logger = structlog.get_logger(__name__)

    async def start(self):
        self.logger.info("Starting enhanced video source", avatar_style=self.avatar_style)

    async def frames(self):
        while True:
            frame = await self._generate_avatar_frame()
            self.frame_count += 1
            yield frame
            await asyncio.sleep(1/30)  # 30 FPS

    async def _generate_avatar_frame(self):
        """Generate realistic avatar frames with expressions."""
        height, width = 480, 640
        frame = np.zeros((height, width, 3), dtype=np.uint8)

        # Background gradient
        for y in range(height):
            bg_intensity = int(40 + 20 * (y / height))
            frame[y, :] = [bg_intensity, bg_intensity + 10, bg_intensity + 20]

        # Avatar face (centered circle)
        center_x, center_y = width // 2, height // 2
        face_radius = min(width, height) // 6

        # Face
        for y in range(max(0, center_y - face_radius), min(height, center_y + face_radius)):
            for x in range(max(0, center_x - face_radius), min(width, center_x + face_radius)):
                dx, dy = x - center_x, y - center_y
                if dx*dx + dy*dy <= face_radius*face_radius:
                    # Skin tone with slight animation
                    pulse = 0.1 * np.sin(self.frame_count * 0.05)
                    frame[y, x] = [
                        int(200 + 20 * pulse),
                        int(180 + 15 * pulse), 
                        int(160 + 10 * pulse)
                    ]

        # Eyes
        eye_y = center_y - face_radius // 3
        eye_radius = face_radius // 8
        
        # Left eye
        left_eye_x = center_x - face_radius // 3
        for y in range(max(0, eye_y - eye_radius), min(height, eye_y + eye_radius)):
            for x in range(max(0, left_eye_x - eye_radius), min(width, left_eye_x + eye_radius)):
                dx, dy = x - left_eye_x, y - eye_y
                if dx*dx + dy*dy <= eye_radius*eye_radius:
                    frame[y, x] = [50, 50, 50]

        # Right eye
        right_eye_x = center_x + face_radius // 3
        for y in range(max(0, eye_y - eye_radius), min(height, eye_y + eye_radius)):
            for x in range(max(0, right_eye_x - eye_radius), min(width, right_eye_x + eye_radius)):
                dx, dy = x - right_eye_x, y - eye_y
                if dx*dx + dy*dy <= eye_radius*eye_radius:
                    frame[y, x] = [50, 50, 50]

        # Mouth (animated based on speaking)
        mouth_y = center_y + face_radius // 3
        mouth_width = face_radius // 2
        speaking_phase = np.sin(self.frame_count * 0.3)  # Speaking animation
        
        if speaking_phase > 0.3:  # "Speaking" state
            # Open mouth
            for dy in range(-3, 4):
                for x in range(center_x - mouth_width//2, center_x + mouth_width//2):
                    y = mouth_y + dy
                    if 0 <= y < height and 0 <= x < width:
                        frame[y, x] = [80, 60, 50]
        else:
            # Closed mouth
            for x in range(center_x - mouth_width//2, center_x + mouth_width//2):
                if 0 <= mouth_y < height and 0 <= x < width:
                    frame[mouth_y, x] = [120, 100, 90]

        return frame

# Enhanced FrameTrack
class EnhancedFrameTrack(MediaStreamTrack):
    kind = "video"

    def __init__(self, source):
        super().__init__()
        self.source = source
        self._ait = None
        self.logger = structlog.get_logger(__name__)

    async def recv(self) -> VideoFrame:
        if self._ait is None:
            self._ait = self.source.frames()

        try:
            frame_np = await self._ait.__anext__()
            h, w, _ = frame_np.shape
            vf = VideoFrame.from_ndarray(frame_np, format="rgb24")
            vf.pts, vf.time_base = None, None
            return vf
        except Exception as e:
            self.logger.error("Error generating video frame", error=str(e))
            # Return black frame on error
            black_frame = np.zeros((480, 640, 3), dtype=np.uint8)
            vf = VideoFrame.from_ndarray(black_frame, format="rgb24")
            vf.pts, vf.time_base = None, None
            return vf

# Mock provider that follows the actual provider interface
class EnhancedMockProvider:
    def __init__(self, config: Dict[str, Any] = None):
        self.config = config or {}
        self.logger = structlog.get_logger(__name__)

    async def stream_dialogue(
        self,
        turns: List[Dict[str, Any]],
        world_context: Dict[str, Any],
        system_guidelines: str = None
    ):
        """Stream dialogue processing with proper event types."""
        
        # Safety check first
        yield {
            "type": "safety",
            "payload": {
                "is_safe": True,
                "flags": ["deterministic_mode"],
                "severity": "info",
                "reason": "Operating in enhanced mock mode"
            },
            "is_final": True,
            "timestamp": datetime.now().isoformat()
        }

        # Analysis of the input
        if turns:
            last_turn = turns[-1]
            text = last_turn.get("text", "")
            
            yield {
                "type": "analysis",
                "tag": "input_analysis",
                "payload": {
                    "text_length": len(text),
                    "word_count": len(text.split()),
                    "detected_keywords": self._extract_keywords(text),
                    "sentiment": self._analyze_sentiment(text)
                },
                "is_final": True,
                "timestamp": datetime.now().isoformat()
            }

            # Simulate subtitle generation (streaming) - simplified for testing
            words = text.split()
            subtitle_text = ""
            for i, word in enumerate(words[:3]):  # Limit to first 3 words for faster testing
                subtitle_text += word + " "
                is_final = (i == 2 or i == len(words) - 1)
                
                yield {
                    "type": "subtitle",
                    "text": subtitle_text.strip(),
                    "final": is_final,
                    "timestamp": datetime.now().isoformat()
                }
                
                if not is_final:
                    await asyncio.sleep(0.1)  # Faster for testing
            
            # Final subtitle with complete text
            yield {
                "type": "subtitle",
                "text": text,
                "final": True,
                "timestamp": datetime.now().isoformat()
            }

            # Generate appropriate intent based on content
            intent = self._generate_intent(text, world_context)
            
            yield {
                "type": "intent",
                "payload": intent,
                "is_final": True,
                "timestamp": datetime.now().isoformat()
            }

    def _extract_keywords(self, text: str) -> List[str]:
        """Extract diplomatic keywords from text."""
        keywords = []
        diplomatic_terms = [
            "trade", "access", "withdraw", "troops", "ceasefire", "alliance",
            "negotiate", "agreement", "treaty", "proposal", "offer", "demand"
        ]
        
        text_lower = text.lower()
        for term in diplomatic_terms:
            if term in text_lower:
                keywords.append(term)
        
        return keywords

    def _analyze_sentiment(self, text: str) -> str:
        """Simple sentiment analysis."""
        positive_words = ["grant", "offer", "agree", "accept", "cooperate", "peace"]
        negative_words = ["refuse", "deny", "war", "attack", "threaten", "ultimatum"]
        
        text_lower = text.lower()
        positive_count = sum(1 for word in positive_words if word in text_lower)
        negative_count = sum(1 for word in negative_words if word in text_lower)
        
        if positive_count > negative_count:
            return "positive"
        elif negative_count > positive_count:
            return "negative"
        else:
            return "neutral"

    def _generate_intent(self, text: str, world_context: Dict[str, Any]) -> Dict[str, Any]:
        """Generate diplomatic intent based on text analysis."""
        text_lower = text.lower()
        
        # Counter-offer detection
        if "grant" in text_lower and "access" in text_lower and "withdraw" in text_lower:
            return {
                "kind": "COUNTER_OFFER",
                "demand": {
                    "military_withdrawal": "Ohio Country",
                    "troop_reduction": True
                },
                "offer": {
                    "trade_access": True,
                    "economic_benefits": "Mutual trade agreement"
                },
                "rationale": [
                    "Strategic military concession requested",
                    "Economic incentive offered in return",
                    "Balanced diplomatic exchange"
                ],
                "confidence": 0.87,
                "timestamp": datetime.now().isoformat()
            }
        
        # Ultimatum detection
        elif "ceasefire" in text_lower and ("or else" in text_lower or "deadline" in text_lower):
            return {
                "kind": "ULTIMATUM",
                "demand": {
                    "immediate_ceasefire": True,
                    "military_action_halt": True
                },
                "consequence": {
                    "war_declaration": True,
                    "alliance_termination": True
                },
                "deadline": "immediate",
                "rationale": [
                    "Urgent military situation",
                    "Clear consequences stated",
                    "Time-sensitive demand"
                ],
                "confidence": 0.92,
                "timestamp": datetime.now().isoformat()
            }
        
        # Default proposal
        else:
            return {
                "kind": "PROPOSAL",
                "offer": {
                    "diplomatic_meeting": True,
                    "trade_discussion": True,
                    "peaceful_resolution": True
                },
                "terms": {
                    "neutral_territory": True,
                    "good_faith_negotiation": True
                },
                "rationale": [
                    "Diplomatic engagement preferred",
                    "Peaceful resolution sought",
                    "Mutual benefit potential"
                ],
                "confidence": 0.75,
                "timestamp": datetime.now().isoformat()
            }

async def attach_enhanced_avatar_track(pc: RTCPeerConnection, use_veo3: bool = False, avatar_style: str = "diplomatic"):
    """Attach enhanced avatar track to peer connection."""
    source = EnhancedVideoSource(avatar_style)
    await source.start()
    track = EnhancedFrameTrack(source)
    pc.addTrack(track)
    logger.info("Attached enhanced avatar track", use_veo3=use_veo3, avatar_style=avatar_style)

@app.get("/", response_class=HTMLResponse)
async def root():
    try:
        with open("web/enhanced_test_client.html", "r", encoding="utf-8") as f:
            return HTMLResponse(f.read())
    except FileNotFoundError:
        try:
            with open("web/test_client.html", "r", encoding="utf-8") as f:
                return HTMLResponse(f.read())
        except FileNotFoundError:
            return HTMLResponse("""
            <html><body>
            <h1>Enhanced AI Avatar Test Harness</h1>
            <p>Test client HTML not found. Please ensure web/enhanced_test_client.html or web/test_client.html exists.</p>
            <p>Server is running on <a href="http://localhost:8000">http://localhost:8000</a></p>
            </body></html>
            """)

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
        "world_context": body.get("world_context", {
            "scenario": "colonial_america",
            "year": 1755,
            "initiator_faction": {"id": "player", "name": "Colonial Delegation"},
            "counterpart_faction": {"id": "ai_diplomat", "name": "British Crown Representative"}
        }),
        "provider_task": None,
        "blackhole": MediaBlackhole(),
    }
    
    logger.info("Created enhanced session", session_id=session_id, model=model)
    return _dump_yaml({"session_id": session_id, "status": "created"})

@app.post("/v1/session/{sid}/webrtc/offer", response_class=PlainTextResponse)
async def sdp_offer(sid: str, sdp_in: SDPIn):
    if sid not in SESSIONS:
        return _dump_yaml({"error": "Session not found"})
        
    sess = SESSIONS[sid]
    pc: RTCPeerConnection = sess["pc"]

    # Handle incoming audio
    @pc.on("track")
    async def on_track(track):
        if track.kind == "audio":
            logger.info("Received audio track", session_id=sid)
            sess["blackhole"].addTrack(track)

    # Attach enhanced avatar video
    avatar_style = sess["world_context"].get("avatar_style", "diplomatic")
    await attach_enhanced_avatar_track(pc, use_veo3=(sess["model"] == "veo3"), avatar_style=avatar_style)

    await pc.setRemoteDescription({"type": sdp_in.type, "sdp": sdp_in.sdp})
    answer = await pc.createAnswer()
    await pc.setLocalDescription(answer)
    
    logger.info("WebRTC offer processed", session_id=sid)
    return _dump_yaml({"type": pc.localDescription.type, "sdp": pc.localDescription.sdp})

@app.websocket("/v1/session/{sid}/control")
async def ws_control(ws: WebSocket, sid: str):
    await ws.accept()
    
    if sid not in SESSIONS:
        await ws.close(code=4004, reason="Session not found")
        return
        
    sess = SESSIONS[sid]
    sess["ws_clients"].add(ws)
    logger.info("WebSocket connected", session_id=sid)

    async def send_yaml(ev: dict):
        if ws.client_state == WebSocketState.CONNECTED:
            try:
                await ws.send_text(_dump_yaml(ev))
            except Exception as e:
                logger.error("Failed to send WebSocket message", error=str(e))

    # Enhanced provider loop
    async def provider_loop():
        provider = EnhancedMockProvider({"strict": True})
        
        # Use existing turns or provide default
        turns = sess["turns"] or [{
            "speaker": "PLAYER",
            "text": "We'll grant trade access if you withdraw troops from Ohio Country.",
            "timestamp": datetime.now().isoformat()
        }]

        try:
            async for event in provider.stream_dialogue(
                turns=turns,
                world_context=sess["world_context"],
                system_guidelines="YAML-only outputs; 1607–1799 colonial tone; safe diplomatic content."
            ):
                await send_yaml(event)
                await asyncio.sleep(0.1)  # Small delay for better UX
                
        except Exception as e:
            logger.error("Provider loop error", error=str(e), session_id=sid)
            await send_yaml({
                "type": "error",
                "payload": {"message": f"Provider error: {str(e)}"},
                "timestamp": datetime.now().isoformat()
            })

    task = asyncio.create_task(provider_loop())
    sess["provider_task"] = task

    try:
        while True:
            msg = await ws.receive_text()
            obj = yaml.load(msg) or {}
            
            if obj.get("type") == "player_utterance":
                text = obj.get("text", "")
                sess["turns"].append({
                    "speaker": "PLAYER",
                    "text": text,
                    "timestamp": datetime.now().isoformat()
                })
                logger.info("Received player utterance", session_id=sid, text=text[:50])
                
                # Restart provider loop with new turns
                if not task.done():
                    task.cancel()
                task = asyncio.create_task(provider_loop())
                sess["provider_task"] = task
                
            await send_yaml({"type": "ack", "timestamp": datetime.now().isoformat()})
            
    except WebSocketDisconnect:
        logger.info("WebSocket disconnected", session_id=sid)
    except Exception as e:
        logger.error("WebSocket error", session_id=sid, error=str(e))
    finally:
        if not task.done():
            task.cancel()
        sess["ws_clients"].discard(ws)

@app.get("/health")
async def health_check():
    return {
        "status": "healthy",
        "service": "Enhanced AI Avatar Test Harness",
        "timestamp": datetime.now().isoformat(),
        "sessions": len(SESSIONS)
    }

if __name__ == "__main__":
    import uvicorn
    print("🚀 Starting Enhanced AI Avatar Test Harness...")
    print("📖 Open http://localhost:8000 in your browser!")
    print("🎭 Features: Enhanced avatar animation, proper provider integration, structured logging")
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")
