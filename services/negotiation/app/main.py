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
from tts.streaming import CartesiaTTSStreamer, AzureTTSStreamer, ElevenLabsTTSStreamer
from tts.streaming.base import TTSStreamer
from app.routers import stream_router
from app.providers import cartesia_router, elevenlabs_router, azure_router


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

# Include routers
app.include_router(stream_router, tags=["streaming"])
app.include_router(cartesia_router, tags=["providers", "cartesia"])
app.include_router(elevenlabs_router, tags=["providers", "elevenlabs"])
app.include_router(azure_router, tags=["providers", "azure"])


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


def _create_tts_streamer(provider: str) -> TTSStreamer:
    """Factory function to create TTS streamer based on provider.
    
    Args:
        provider: Provider name ("cartesia", "azure", "elevenlabs")
    
    Returns:
        Configured TTSStreamer instance
    
    Raises:
        ValueError: If provider is invalid or not configured
    """
    if provider == "cartesia":
        if not settings.cartesia_api_key:
            raise ValueError("CARTESIA_API_KEY not configured")
        return CartesiaTTSStreamer(
            api_key=settings.cartesia_api_key,
            model_id=settings.cartesia_model
        )
    
    elif provider == "azure":
        if not settings.azure_speech_key or not settings.azure_speech_region:
            raise ValueError("AZURE_SPEECH_KEY and AZURE_SPEECH_REGION required")
        return AzureTTSStreamer(
            subscription_key=settings.azure_speech_key,
            region=settings.azure_speech_region,
            endpoint=settings.azure_speech_endpoint
        )
    
    elif provider == "elevenlabs":
        if not settings.elevenlabs_api_key:
            raise ValueError("ELEVENLABS_API_KEY not configured")
        return ElevenLabsTTSStreamer(
            api_key=settings.elevenlabs_api_key,
            model_id=settings.elevenlabs_model
        )
    
    else:
        raise ValueError(f"Unknown TTS provider: {provider}")


@app.post("/speak")
async def speak(request: SpeakRequest):
    """Diagnostic endpoint: synthesize full utterance via selected provider.
    
    Returns a WAV file with the complete synthesized audio.
    This is a synchronous endpoint for testing and development.
    
    For production streaming, use the /ws/stream WebSocket endpoint.
    """
    streamer = None
    
    try:
        logger.info(
            "Speak request received",
            provider=request.provider,
            voice_id=request.voice_id,
            text_length=len(request.text)
        )
        
        # Create TTS streamer
        streamer = _create_tts_streamer(request.provider)
        
        # Start streaming
        await streamer.start(
            voice_id=request.voice_id,
            sample_rate=request.sample_rate
        )
        
        # Collect all audio chunks
        audio_chunks = []
        
        # Send text and consume events
        send_task = asyncio.create_task(
            streamer.send_text(request.text, final=True)
        )
        
        async for event in streamer.aiter_events():
            if event.kind == "audio" and event.pcm16_b64:
                # Decode base64 audio
                audio_bytes = base64.b64decode(event.pcm16_b64)
                audio_chunks.append(audio_bytes)
            
            elif event.kind == "error":
                raise Exception(event.error)
            
            elif event.kind == "done":
                break
        
        # Wait for send to complete
        await send_task
        
        # Combine audio chunks
        if not audio_chunks:
            raise Exception("No audio received from provider")
        
        combined_audio = b''.join(audio_chunks)
        
        # Convert to WAV format
        import io
        import wave
        
        wav_buffer = io.BytesIO()
        with wave.open(wav_buffer, 'wb') as wav_file:
            wav_file.setnchannels(1)  # Mono
            wav_file.setsampwidth(2)  # 16-bit
            wav_file.setframerate(request.sample_rate)
            wav_file.writeframes(combined_audio)
        
        wav_buffer.seek(0)
        
        logger.info(
            "Speak request completed",
            provider=request.provider,
            audio_size=len(combined_audio)
        )
        
        return StreamingResponse(
            wav_buffer,
            media_type="audio/wav",
            headers={
                "Content-Disposition": f"attachment; filename=speech_{request.provider}.wav"
            }
        )
        
    except ValueError as e:
        logger.error("Configuration error", error=str(e), provider=request.provider)
        raise HTTPException(status_code=400, detail=str(e))
    
    except Exception as e:
        logger.error("Speak request failed", error=str(e), provider=request.provider)
        raise HTTPException(status_code=500, detail=f"TTS synthesis failed: {str(e)}")
    
    finally:
        # Clean up streamer
        if streamer:
            try:
                await streamer.stop()
            except:
                pass


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
