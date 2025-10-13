"""
WebRTC endpoints and tracks for real-time portrait streaming.

Provides:
- POST /portrait/webrtc/offer: WebRTC SDP negotiation
- POST /portrait/session: Session initialization with reference image
- WS /portrait/audio/{session_id}: Audio streaming for portrait animation
"""

import asyncio
import base64
import logging
import time
from typing import Dict, Optional, List
import io

from fastapi import APIRouter, WebSocket, WebSocketDisconnect, UploadFile, File, Form, HTTPException
from pydantic import BaseModel
from aiortc import RTCPeerConnection, RTCSessionDescription, VideoStreamTrack
from aiortc.contrib.media import MediaBlackhole
from av import VideoFrame as AVVideoFrame
import numpy as np

from app.portrait.liveportrait_service import LivePortraitService, PortraitEngine

logger = logging.getLogger(__name__)

# Global service instance
portrait_service: Optional[LivePortraitService] = None

# Active WebRTC connections
active_connections: Dict[str, RTCPeerConnection] = {}

router = APIRouter(prefix="/portrait", tags=["portrait"])


class SDPOffer(BaseModel):
    """WebRTC SDP offer from client"""
    session_id: str
    sdp: str
    type: str  # Should be "offer"


class SDPAnswer(BaseModel):
    """WebRTC SDP answer to client"""
    sdp: str
    type: str  # Will be "answer"


class SessionResponse(BaseModel):
    """Session creation response"""
    session_id: str
    message: str


class HealthResponse(BaseModel):
    """Health check response"""
    status: str
    active_sessions: int
    service_loaded: bool


class MetricsResponse(BaseModel):
    """Per-session metrics response"""
    session_id: str
    metrics: Dict


class PortraitVideoTrack(VideoStreamTrack):
    """
    Video track that streams generated portrait frames.
    
    Pulls frames from LivePortraitService at target FPS and converts
    to WebRTC video frames.
    """
    
    kind = "video"
    
    def __init__(self, session_id: str, service: LivePortraitService, fps: int = 25):
        super().__init__()
        self.session_id = session_id
        self.service = service
        self.fps = fps
        self.frame_count = 0
        self.start_time = time.time()
        
        logger.info(f"PortraitVideoTrack created for session {session_id}")
    
    async def recv(self):
        """
        Receive next video frame.
        
        Called by WebRTC stack to get frames. Must maintain timing
        to achieve target FPS.
        """
        # Wait for next frame time
        pts = self.frame_count / self.fps
        elapsed = time.time() - self.start_time
        wait_time = pts - elapsed
        if wait_time > 0:
            await asyncio.sleep(wait_time)
        
        # Get frame from service
        result = await self.service.next_frame(self.session_id)
        
        if result:
            rgba_array, pts_ms = result
            # Convert numpy RGBA to av.VideoFrame
            frame = self._numpy_to_av_frame(rgba_array)
            frame.pts = self.frame_count
            frame.time_base = f"1/{self.fps}"
            self.frame_count += 1
            return frame
        else:
            # No frame available - return black frame to keep stream alive
            frame = self._create_black_frame()
            frame.pts = self.frame_count
            frame.time_base = f"1/{self.fps}"
            self.frame_count += 1
            return frame
    
    def _numpy_to_av_frame(self, rgba: np.ndarray) -> AVVideoFrame:
        """Convert numpy RGBA array to av.VideoFrame"""
        # Ensure correct shape and dtype
        if rgba.dtype != np.uint8:
            rgba = rgba.astype(np.uint8)
        
        # Create VideoFrame from numpy array
        frame = AVVideoFrame.from_ndarray(rgba, format="rgba")
        return frame
    
    def _create_black_frame(self, width: int = 512, height: int = 512) -> AVVideoFrame:
        """Create a black placeholder frame"""
        rgba = np.zeros((height, width, 4), dtype=np.uint8)
        rgba[:, :, 3] = 255  # Full opacity
        return AVVideoFrame.from_ndarray(rgba, format="rgba")


async def get_portrait_service() -> LivePortraitService:
    """Get or initialize the portrait service"""
    global portrait_service
    
    if portrait_service is None:
        portrait_service = LivePortraitService()
        await portrait_service.load(PortraitEngine.LIVEPORTRAIT)
    
    return portrait_service


@router.post("/session", response_model=SessionResponse)
async def create_session(
    session_id: str = Form(...),
    engine: str = Form("liveportrait"),
    fps: int = Form(25),
    image: UploadFile = File(...)
):
    """
    Initialize or reset a portrait session with reference image.
    
    Args:
        session_id: Unique session identifier
        engine: Portrait engine (liveportrait or ditto)
        fps: Target frames per second
        image: Reference portrait image (JPEG/PNG)
        
    Returns:
        SessionResponse with session_id and status message
    """
    try:
        # Validate engine
        try:
            engine_enum = PortraitEngine(engine)
        except ValueError:
            raise HTTPException(
                status_code=400,
                detail=f"Invalid engine: {engine}. Must be 'liveportrait' or 'ditto'"
            )
        
        # Read image bytes
        image_bytes = await image.read()
        
        if not image_bytes:
            raise HTTPException(status_code=400, detail="Empty image file")
        
        # Get service and create session
        service = await get_portrait_service()
        await service.start_session(
            session_id=session_id,
            img_bytes=image_bytes,
            fps=fps,
            engine=engine_enum
        )
        
        logger.info(f"Portrait session created: {session_id}, engine={engine}, fps={fps}")
        
        return SessionResponse(
            session_id=session_id,
            message=f"Session initialized with {engine} engine at {fps} FPS"
        )
        
    except Exception as e:
        logger.error(f"Failed to create session: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/webrtc/offer", response_model=SDPAnswer)
async def handle_webrtc_offer(offer: SDPOffer):
    """
    Handle WebRTC SDP offer and return answer.
    
    Creates RTCPeerConnection with video track streaming portrait frames.
    
    Args:
        offer: SDP offer from client
        
    Returns:
        SDPAnswer with SDP answer
    """
    try:
        session_id = offer.session_id
        
        # Get service
        service = await get_portrait_service()
        
        # Check if session exists
        if session_id not in service.sessions:
            raise HTTPException(
                status_code=404,
                detail=f"Session {session_id} not found. Create session first."
            )
        
        # Create RTCPeerConnection
        pc = RTCPeerConnection()
        active_connections[session_id] = pc
        
        # Add video track
        session = service.sessions[session_id]
        video_track = PortraitVideoTrack(
            session_id=session_id,
            service=service,
            fps=session.fps
        )
        pc.addTrack(video_track)
        
        # Handle connection state changes
        @pc.on("connectionstatechange")
        async def on_connectionstatechange():
            logger.info(f"WebRTC connection state: {pc.connectionState}")
            
            if pc.connectionState in ["failed", "closed"]:
                await cleanup_connection(session_id)
        
        # Set remote description (offer)
        await pc.setRemoteDescription(
            RTCSessionDescription(sdp=offer.sdp, type=offer.type)
        )
        
        # Create answer
        answer = await pc.createAnswer()
        await pc.setLocalDescription(answer)
        
        logger.info(f"WebRTC answer created for session {session_id}")
        
        return SDPAnswer(
            sdp=pc.localDescription.sdp,
            type=pc.localDescription.type
        )
        
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"WebRTC offer handling failed: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))


@router.websocket("/audio/{session_id}")
async def websocket_audio_stream(websocket: WebSocket, session_id: str):
    """
    WebSocket endpoint for streaming audio chunks to portrait engine.
    
    Accepts PCM16 audio chunks (base64 encoded) and feeds them to the
    portrait session for audio-driven animation.
    
    Message format:
    {
        "pcm16_b64": "<base64 encoded PCM16 data>",
        "sample_rate": 24000,
        "timestamp_ms": 1234567890.123  # Optional
    }
    
    Args:
        websocket: WebSocket connection
        session_id: Portrait session identifier
    """
    await websocket.accept()
    logger.info(f"Audio WebSocket connected for session {session_id}")
    
    try:
        # Get service
        service = await get_portrait_service()
        
        # Check if session exists
        if session_id not in service.sessions:
            await websocket.send_json({
                "error": f"Session {session_id} not found"
            })
            await websocket.close()
            return
        
        # Send acknowledgment
        await websocket.send_json({
            "type": "connected",
            "session_id": session_id,
            "message": "Audio stream ready"
        })
        
        # Stream loop
        while True:
            # Receive audio chunk
            data = await websocket.receive_json()
            
            # Extract PCM16 data
            pcm16_b64 = data.get("pcm16_b64")
            sample_rate = data.get("sample_rate", 24000)
            timestamp_ms = data.get("timestamp_ms")
            
            if not pcm16_b64:
                logger.warning("Received empty audio chunk")
                continue
            
            # Decode base64
            pcm16_bytes = base64.b64decode(pcm16_b64)
            
            # Ingest into service
            await service.ingest_audio(
                session_id=session_id,
                pcm16_chunk=pcm16_bytes,
                sr=sample_rate,
                t0_ms=timestamp_ms
            )
            
    except WebSocketDisconnect:
        logger.info(f"Audio WebSocket disconnected for session {session_id}")
    except Exception as e:
        logger.error(f"Audio WebSocket error: {e}", exc_info=True)
        try:
            await websocket.send_json({"error": str(e)})
        except:
            pass
    finally:
        try:
            await websocket.close()
        except:
            pass


async def cleanup_connection(session_id: str):
    """Cleanup WebRTC connection resources"""
    pc = active_connections.pop(session_id, None)
    if pc:
        await pc.close()
        logger.info(f"WebRTC connection closed for session {session_id}")


@router.get("/health", response_model=HealthResponse)
async def health_check():
    """
    Health check endpoint for portrait service.
    
    Returns:
        Service health status and active session count
    """
    service = await get_portrait_service()
    
    return HealthResponse(
        status="healthy" if service else "degraded",
        active_sessions=len(service.sessions) if service else 0,
        service_loaded=service is not None
    )


@router.get("/metrics", response_model=List[MetricsResponse])
async def get_all_metrics():
    """
    Get metrics for all active portrait sessions.
    
    Returns:
        List of metrics for each session
    """
    service = await get_portrait_service()
    
    if not service:
        raise HTTPException(status_code=503, detail="Portrait service not initialized")
    
    metrics_list = []
    for session_id, session in service.sessions.items():
        metrics_list.append(MetricsResponse(
            session_id=session_id,
            metrics=session.metrics.to_dict()
        ))
    
    return metrics_list


@router.get("/metrics/{session_id}", response_model=MetricsResponse)
async def get_session_metrics(session_id: str):
    """
    Get metrics for a specific portrait session.
    
    Args:
        session_id: Session identifier
        
    Returns:
        Session metrics including:
        - frames_generated: Total frames generated
        - frames_dropped: Frames dropped due to GPU saturation
        - audio_chunks_received: Total audio chunks received
        - audio_buffer_ms: Current audio buffer duration
        - last_frame_age_ms: Time since last frame generated
        - avg_frame_time_ms: Average frame generation time
        - actual_fps: Actual frames per second
        - target_fps: Target FPS
    """
    service = await get_portrait_service()
    
    if not service:
        raise HTTPException(status_code=503, detail="Portrait service not initialized")
    
    if session_id not in service.sessions:
        raise HTTPException(status_code=404, detail=f"Session {session_id} not found")
    
    session = service.sessions[session_id]
    
    return MetricsResponse(
        session_id=session_id,
        metrics=session.metrics.to_dict()
    )


@router.on_event("shutdown")
async def shutdown_event():
    """Cleanup on service shutdown"""
    logger.info("Shutting down portrait service...")
    
    # Close all WebRTC connections
    for session_id in list(active_connections.keys()):
        await cleanup_connection(session_id)
    
    # Cleanup portrait service
    if portrait_service:
        await portrait_service.cleanup()
    
    logger.info("Portrait service shutdown complete")

