"""FastAPI WebSocket endpoint for Audio2Face streaming.

Provides WebSocket interface that accepts PCM audio input and streams
back JSON blendshape frames for frontend consumption.

Protocol:
1. Client connects to /a2f/stream
2. Client sends binary PCM16 mono audio chunks
3. Server responds with JSON blendshape frames:
   {
     "timestamp_ms": 123.45,
     "weights": [0.0, 0.1, ..., 0.0],  // 52 ARKit weights
     "frame_index": 42
   }
4. Server sends {"done": true} when streaming complete
"""

import asyncio
from typing import AsyncIterator, Optional
import os

import structlog
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Query
from pydantic import BaseModel

from .a2f_client import Audio2FaceClient, Audio2FaceStubClient, Audio2FaceConfig
from .arkit52 import ARKIT52_NAMES, validate_arkit52_weights

logger = structlog.get_logger(__name__)

router = APIRouter(prefix="/a2f")


class BlendshapeFrameMessage(BaseModel):
    """WebSocket message containing blendshape frame."""
    timestamp_ms: float
    weights: list[float]
    frame_index: int


class ErrorMessage(BaseModel):
    """WebSocket error message."""
    error: str
    details: Optional[str] = None


class DoneMessage(BaseModel):
    """WebSocket completion message."""
    done: bool = True


async def audio_chunk_generator(websocket: WebSocket) -> AsyncIterator[bytes]:
    """Generate audio chunks from WebSocket binary messages.
    
    Args:
        websocket: Active WebSocket connection
        
    Yields:
        PCM16 audio bytes
    """
    try:
        while True:
            # Receive binary audio data
            data = await websocket.receive_bytes()
            
            if len(data) == 0:
                # Empty chunk signals end of audio
                break
            
            yield data
            
    except WebSocketDisconnect:
        logger.info("Client disconnected during audio streaming")
    except Exception as e:
        logger.error("Error receiving audio chunks", error=str(e))
        raise


@router.websocket("/stream")
async def stream_a2f(
    websocket: WebSocket,
    use_stub: bool = Query(
        default=True,
        description="Use stub client (for dev without A2F service)"
    )
):
    """WebSocket endpoint for Audio2Face streaming.
    
    Query Parameters:
        use_stub: If True, use procedural stub instead of real A2F service.
                  Set to False when NVIDIA A2F service is available.
    
    Protocol:
        Client -> Server: Binary PCM16 mono audio chunks
        Server -> Client: JSON blendshape frames
        
    Example usage:
        ws = new WebSocket('ws://localhost:8000/a2f/stream?use_stub=true');
        ws.binaryType = 'arraybuffer';
        
        // Send audio
        ws.send(pcm16AudioBuffer);
        
        // Receive blendshapes
        ws.onmessage = (event) => {
            const frame = JSON.parse(event.data);
            if (frame.weights) {
                applyBlendshapes(frame.weights);
            }
        };
    """
    await websocket.accept()
    
    client: Optional[Audio2FaceClient] = None
    
    try:
        # Load configuration from environment
        config = Audio2FaceConfig(
            host=os.getenv("A2F_GRPC_HOST", "127.0.0.1"),
            port=int(os.getenv("A2F_GRPC_PORT", "50051")),
            model_id=os.getenv("A2F_MODEL_ID", "audio2face_3d"),
            sample_rate=int(os.getenv("AUDIO_SAMPLE_RATE", "16000"))
        )
        
        # Create client (stub or real)
        if use_stub:
            client = Audio2FaceStubClient(config)
            logger.info("Using Audio2Face stub client for development")
        else:
            client = Audio2FaceClient(config)
            logger.info("Using real Audio2Face gRPC client")
        
        # Connect to A2F service
        await client.connect()
        
        # Send initial ready message
        await websocket.send_json({"ready": True, "stub_mode": use_stub})
        
        # Create audio chunk generator
        audio_chunks = audio_chunk_generator(websocket)
        
        # Stream audio and send blendshape frames
        frame_count = 0
        async for frame in client.stream_audio(audio_chunks):
            # Validate blendshape weights
            if not validate_arkit52_weights(frame.weights):
                logger.warning(
                    "Invalid blendshape weights received",
                    frame_index=frame.frame_index,
                    weight_count=len(frame.weights)
                )
                continue
            
            # Send frame to client
            message = BlendshapeFrameMessage(
                timestamp_ms=frame.timestamp_ms,
                weights=frame.weights,
                frame_index=frame.frame_index
            )
            
            await websocket.send_json(message.model_dump())
            frame_count += 1
        
        # Send completion message
        await websocket.send_json(DoneMessage().model_dump())
        
        logger.info(
            "Audio2Face streaming session completed",
            total_frames=frame_count
        )
        
    except WebSocketDisconnect:
        logger.info("Client disconnected from Audio2Face stream")
        
    except Exception as e:
        logger.error("Error during Audio2Face streaming", error=str(e))
        
        # Try to send error message to client
        try:
            error_msg = ErrorMessage(
                error="streaming_error",
                details=str(e)
            )
            await websocket.send_json(error_msg.model_dump())
        except:
            pass
        
    finally:
        # Clean up
        if client and client.is_connected:
            await client.disconnect()
        
        try:
            await websocket.close()
        except:
            pass


@router.get("/blendshapes")
async def list_blendshapes():
    """Get list of ARKit-52 blendshape names.
    
    Useful for debugging and documentation.
    
    Returns:
        {
            "count": 52,
            "names": ["browDownLeft", "browDownRight", ...]
        }
    """
    return {
        "count": len(ARKIT52_NAMES),
        "names": ARKIT52_NAMES
    }


@router.get("/health")
async def a2f_health_check():
    """Health check for Audio2Face integration.
    
    Returns:
        Service status and configuration
    """
    config = Audio2FaceConfig(
        host=os.getenv("A2F_GRPC_HOST", "127.0.0.1"),
        port=int(os.getenv("A2F_GRPC_PORT", "50051")),
        model_id=os.getenv("A2F_MODEL_ID", "audio2face_3d")
    )
    
    return {
        "status": "ready",
        "service": "audio2face",
        "stub_available": True,
        "config": {
            "host": config.host,
            "port": config.port,
            "model_id": config.model_id
        }
    }

