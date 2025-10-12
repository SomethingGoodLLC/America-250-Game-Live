"""FastAPI application for diplomatic negotiation service."""

import asyncio
import uuid
import base64
from contextlib import asynccontextmanager
from typing import Dict, Any, Optional

import structlog
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect, Request, Response
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel, ValidationError

from schemas.models import (
    ErrorModel,
    WorldContextModel,
    NegotiationReportModel,
    IntentModel
)
from app.schemas import (
    StreamEvent,
    SpeakRequest,
    StreamRequest,
    TextChunk,
    PhonemeTimestamp,
    VisemeEvent,
    BlendshapeFrame
)
from core.session_manager import SessionManager
from core.webrtc_manager import WebRTCManager
from core.logging_config import setup_logging
from core.yaml_utils import yaml_helper
from core.settings import settings
from core.yaml_middleware import YAMLMiddleware, YAMLResponse


# Configure logging
setup_logging()
logger = structlog.get_logger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan context manager."""
    # Startup
    logger.info("Starting negotiation service")
    yield
    # Shutdown
    logger.info("Shutting down negotiation service")


app = FastAPI(
    title="Samson Negotiation Service",
    description="FastAPI service for diplomatic negotiations with WebRTC A/V streaming",
    version="0.1.0",
    lifespan=lifespan
)

# Add YAML middleware
app.add_middleware(YAMLMiddleware)


class CreateSessionRequest(BaseModel):
    """Request model for creating a negotiation session."""
    initiator_info: Dict[str, Any]
    counterpart_faction_id: str
    scenario_tags: list[str]


class SessionResponse(BaseModel):
    """Response model for session operations."""
    session_id: str
    status: str


class WebRTCSDPOffer(BaseModel):
    """WebRTC SDP offer."""
    sdp: str
    type: str = "offer"


class WebRTCSDPAnswer(BaseModel):
    """WebRTC SDP answer."""
    sdp: str
    type: str = "answer"


class ControlMessage(BaseModel):
    """WebSocket control message."""
    type: str
    data: Dict[str, Any]


# Global managers
session_manager = SessionManager()
webrtc_manager = WebRTCManager()


@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "healthy"}


@app.get("/healthz")
async def healthz():
    """Kubernetes-style health check endpoint."""
    return {"status": "ok", "service": "negotiation"}


@app.post("/speak")
async def speak(request: SpeakRequest):
    """Diagnostic endpoint: synthesize full utterance via selected provider.
    
    Returns a WAV file with the complete synthesized audio.
    This is a synchronous endpoint for testing and development.
    
    For production streaming, use the /ws/stream WebSocket endpoint.
    """
    try:
        logger.info(
            "Speak request received",
            provider=request.provider,
            voice_id=request.voice_id,
            text_length=len(request.text)
        )
        
        # TODO: Implement actual TTS provider integration
        # For now, return a mock response
        
        # Mock: Generate silent PCM16LE audio (1 second at 24kHz)
        sample_rate = request.sample_rate
        duration_sec = 1.0
        num_samples = int(sample_rate * duration_sec)
        
        # Silent audio: array of zeros as int16
        import numpy as np
        audio_data = np.zeros(num_samples, dtype=np.int16)
        
        # Convert to WAV format
        import io
        import wave
        
        wav_buffer = io.BytesIO()
        with wave.open(wav_buffer, 'wb') as wav_file:
            wav_file.setnchannels(1)  # Mono
            wav_file.setsampwidth(2)  # 16-bit
            wav_file.setframerate(sample_rate)
            wav_file.writeframes(audio_data.tobytes())
        
        wav_buffer.seek(0)
        
        logger.info("Speak request completed", provider=request.provider)
        
        return StreamingResponse(
            wav_buffer,
            media_type="audio/wav",
            headers={
                "Content-Disposition": f"attachment; filename=speech_{request.provider}.wav"
            }
        )
        
    except Exception as e:
        logger.error("Speak request failed", error=str(e), provider=request.provider)
        raise HTTPException(status_code=500, detail=f"TTS synthesis failed: {str(e)}")


@app.websocket("/ws/stream")
async def stream_websocket(websocket: WebSocket):
    """WebSocket endpoint for real-time TTS streaming.
    
    Protocol:
    1. Client connects and sends StreamRequest with provider, voice_id, mode
    2. Client sends TextChunk messages with text to synthesize
    3. Server streams back StreamEvent messages (audio, phonemes, visemes, etc.)
    4. Client sends TextChunk with is_final=True to signal end
    5. Server sends StreamEvent with kind="done" and closes
    
    The normalized StreamEvent format allows clients to handle any provider
    (Azure, ElevenLabs, Cartesia) with the same code.
    """
    await websocket.accept()
    
    provider: Optional[str] = None
    voice_id: Optional[str] = None
    mode: Optional[str] = None
    
    try:
        # Receive initial configuration
        config_data = await websocket.receive_json()
        
        try:
            config = StreamRequest(**config_data)
            provider = config.provider
            voice_id = config.voice_id
            mode = config.mode
            
            logger.info(
                "Stream WebSocket connected",
                provider=provider,
                voice_id=voice_id,
                mode=mode
            )
            
            # Send acknowledgment
            await websocket.send_json({
                "type": "config_ack",
                "message": f"Streaming configured with {provider}"
            })
            
        except ValidationError as e:
            error_event = StreamEvent(kind="error", error=f"Invalid configuration: {str(e)}")
            await websocket.send_json(error_event.model_dump())
            await websocket.close(code=4000)
            return
        
        # Main streaming loop
        while True:
            # Receive text chunks from client
            chunk_data = await websocket.receive_json()
            
            try:
                chunk = TextChunk(**chunk_data)
                
                logger.debug(
                    "Received text chunk",
                    text_length=len(chunk.text),
                    is_final=chunk.is_final,
                    provider=provider
                )
                
                # TODO: Implement actual TTS provider streaming
                # For now, send mock events
                
                # Mock: Send audio event
                mock_audio = base64.b64encode(b'\x00' * 1000).decode('utf-8')
                audio_event = StreamEvent(
                    kind="audio",
                    pcm16_b64=mock_audio,
                    sample_rate=24000
                )
                await websocket.send_json(audio_event.model_dump())
                
                # Mock: Send viseme event
                viseme_event = StreamEvent(
                    kind="viseme",
                    viseme=VisemeEvent(viseme_id=8, audio_offset_ms=125.5)
                )
                await websocket.send_json(viseme_event.model_dump())
                
                # If this is the final chunk, send done event
                if chunk.is_final:
                    done_event = StreamEvent(kind="done")
                    await websocket.send_json(done_event.model_dump())
                    
                    logger.info(
                        "Stream completed",
                        provider=provider,
                        voice_id=voice_id
                    )
                    break
                    
            except ValidationError as e:
                error_event = StreamEvent(kind="error", error=f"Invalid text chunk: {str(e)}")
                await websocket.send_json(error_event.model_dump())
                continue
        
    except WebSocketDisconnect:
        logger.info("Stream WebSocket disconnected", provider=provider)
    except Exception as e:
        logger.error("Stream WebSocket error", error=str(e), provider=provider)
        try:
            error_event = StreamEvent(kind="error", error=str(e))
            await websocket.send_json(error_event.model_dump())
        except:
            pass
        await websocket.close(code=4000)


@app.post("/v1/session", response_model=SessionResponse)
async def create_session(request: CreateSessionRequest):
    """Create a new negotiation session."""
    try:
        session_id = str(uuid.uuid4())

        # Create world context
        world_context = WorldContextModel(
            scenario_tags=request.scenario_tags,
            initiator_faction=request.initiator_info,
            counterpart_faction={"id": request.counterpart_faction_id, "name": "Unknown"}
        )

        # Create session
        await session_manager.create_session(session_id, world_context)

        logger.info("Created negotiation session", session_id=session_id)

        return SessionResponse(
            session_id=session_id,
            status="created"
        )

    except Exception as e:
        logger.error("Failed to create session", error=str(e))
        raise HTTPException(status_code=500, detail="Failed to create session")


@app.post("/v1/session/{session_id}/end")
async def end_session(session_id: str):
    """End a negotiation session."""
    try:
        # Get the final report
        report = await session_manager.end_session(session_id)

        if not report:
            raise HTTPException(status_code=404, detail="Session not found")

        logger.info("Ended negotiation session", session_id=session_id)

        return {"status": "ended", "report": report.model_dump()}

    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to end session", session_id=session_id, error=str(e))
        raise HTTPException(status_code=500, detail="Failed to end session")


@app.websocket("/v1/session/{session_id}/control")
async def control_websocket(websocket: WebSocket, session_id: str):
    """WebSocket for control messages."""
    await websocket.accept()

    try:
        # Verify session exists
        if not await session_manager.session_exists(session_id):
            await websocket.close(code=4004, reason="Session not found")
            return

        # Handle control messages
        async for message in websocket.iter_json():
            control_msg = ControlMessage(**message)

            if control_msg.type == "mic_state":
                # Handle microphone state changes
                await session_manager.update_mic_state(session_id, control_msg.data.get("enabled", False))
                await websocket.send_json({"type": "ack", "message": "Mic state updated"})

            elif control_msg.type == "push_to_talk":
                # Handle push-to-talk events
                await session_manager.handle_push_to_talk(session_id, control_msg.data)
                await websocket.send_json({"type": "ack", "message": "PTT handled"})

            elif control_msg.type == "text_message":
                # Handle text messages
                await session_manager.handle_text_message(session_id, control_msg.data)
                await websocket.send_json({"type": "ack", "message": "Text message handled"})

    except WebSocketDisconnect:
        logger.info("Control WebSocket disconnected", session_id=session_id)
    except Exception as e:
        logger.error("Control WebSocket error", session_id=session_id, error=str(e))
        await websocket.close(code=4000, reason="Internal error")


@app.post("/v1/session/{session_id}/webrtc/offer")
async def handle_webrtc_offer(session_id: str, offer: WebRTCSDPOffer):
    """Handle WebRTC SDP offer."""
    try:
        # Verify session exists
        if not await session_manager.session_exists(session_id):
            raise HTTPException(status_code=404, detail="Session not found")

        # Create peer connection and handle offer
        answer = await webrtc_manager.handle_offer(session_id, offer.sdp)

        logger.info("Handled WebRTC offer", session_id=session_id)

        return WebRTCSDPAnswer(sdp=answer)

    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to handle WebRTC offer", session_id=session_id, error=str(e))
        raise HTTPException(status_code=500, detail="Failed to handle WebRTC offer")


@app.post("/v1/session/{session_id}/proposed-intents")
async def inject_proposed_intents(session_id: str, intents: list[IntentModel]):
    """Inject proposed intents (for development/testing)."""
    try:
        # Verify session exists
        if not await session_manager.session_exists(session_id):
            raise HTTPException(status_code=404, detail="Session not found")

        # Inject intents
        await session_manager.inject_intents(session_id, intents)

        logger.info("Injected proposed intents", session_id=session_id, intent_count=len(intents))

        return {"status": "injected", "count": len(intents)}

    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to inject intents", session_id=session_id, error=str(e))
        raise HTTPException(status_code=500, detail="Failed to inject intents")


@app.get("/v1/session/{session_id}/report", response_model=NegotiationReportModel)
async def get_session_report(session_id: str):
    """Get the negotiation report for a session."""
    try:
        report = await session_manager.get_session_report(session_id)

        if not report:
            raise HTTPException(status_code=404, detail="Session not found")

        return report

    except HTTPException:
        raise
    except Exception as e:
        logger.error("Failed to get session report", session_id=session_id, error=str(e))
        raise HTTPException(status_code=500, detail="Failed to get session report")


@app.exception_handler(Exception)
async def global_exception_handler(request, exc):
    """Global exception handler."""
    logger.error("Unhandled exception", error=str(exc), path=request.url.path)

    error_response = ErrorModel(
        code="INTERNAL_ERROR",
        message="An internal error occurred",
        details={"path": str(request.url.path)}
    )

    return JSONResponse(
        status_code=500,
        content={"error": error_response.model_dump()}
    )
