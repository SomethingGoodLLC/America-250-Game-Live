"""ElevenLabs WebSocket proxy for browser-side TTS streaming with alignment.

This module provides a WebSocket endpoint that proxies connections from the browser
to ElevenLabs' streaming TTS API. The proxy keeps the API key server-side and implements
rate limiting and origin checking for security.

ElevenLabs provides alignment/timing data that can be converted to phonemes for lip-sync.

Endpoint: /ws/elevenlabs
Query Parameters:
    - voice_id: Voice ID (required)
    - model: Model ID (default: eleven_turbo_v2)
    - output_format: Audio format (default: pcm_24000)
    - latency: Latency optimization (default: normal, options: normal, optimized)

Example Usage:
    const ws = new WebSocket('ws://localhost:8000/ws/elevenlabs?voice_id=abc123');
    
    ws.onopen = () => {
        // Send text to synthesize
        ws.send(JSON.stringify({
            text: 'Hello world',
            flush: true
        }));
    };
    
    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.audio) {
            // Audio chunk (base64)
            playAudio(data.audio);
        }
        if (data.alignment || data.normalizedAlignment) {
            // Use for lip-sync timing
            updateLipSync(data.alignment);
        }
    };
"""

import asyncio
from typing import Optional

import structlog
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Query, HTTPException
from fastapi.responses import JSONResponse

from core.settings import settings
from ._bridge import bridge_client_server, ConnectionRateLimiter, check_origin

logger = structlog.get_logger(__name__)
router = APIRouter()

# Rate limiter: max 3 concurrent connections per IP
rate_limiter = ConnectionRateLimiter(max_connections_per_ip=3)

# Allowed origins for CORS
ALLOWED_ORIGINS = ["http://localhost:*", "http://127.0.0.1:*", "https://*.your-domain.com"]


@router.websocket("/ws/elevenlabs")
async def elevenlabs_websocket_proxy(
    websocket: WebSocket,
    voice_id: str = Query(..., description="ElevenLabs voice ID (required)"),
    model: str = Query("eleven_turbo_v2", description="Model ID"),
    output_format: str = Query("pcm_24000", description="Audio format"),
    latency: str = Query("normal", description="Latency optimization: normal or optimized"),
):
    """WebSocket proxy endpoint for ElevenLabs streaming TTS with alignment.
    
    This endpoint creates a bridge between the browser client and ElevenLabs' API,
    keeping the API key secure on the server side.
    
    Protocol Flow:
    1. Client connects with query parameters (voice_id, model, etc.)
    2. Server establishes connection to ElevenLabs with init message
    3. Server bridges all subsequent messages between client and ElevenLabs
    4. Client sends text messages, receives audio + alignment data
    
    ElevenLabs Features:
    - High-quality voice cloning and synthesis
    - Alignment data for precise lip-sync
    - Character count tracking for billing
    - Support for emotional expression and SSML
    
    Security:
    - API key is never exposed to the client
    - Origin checking prevents unauthorized domains
    - Rate limiting prevents abuse (max 3 connections per IP)
    
    Args:
        websocket: FastAPI WebSocket connection
        voice_id: ElevenLabs voice ID (required)
        model: Model ID (default: eleven_turbo_v2, options: eleven_turbo_v2, eleven_monolingual_v1, etc.)
        output_format: Audio format (default: pcm_24000, options: pcm_16000, pcm_22050, pcm_24000, pcm_44100)
        latency: Latency optimization (default: normal, options: normal, optimized)
    """
    client_ip = websocket.client.host if websocket.client else "unknown"
    origin = websocket.headers.get("origin")
    
    logger.info(
        "ElevenLabs WebSocket connection attempt",
        client_ip=client_ip,
        origin=origin,
        voice_id=voice_id,
        model=model
    )
    
    # Check origin
    if not check_origin(origin, ALLOWED_ORIGINS):
        logger.warning("Rejected connection from disallowed origin", origin=origin)
        await websocket.close(code=4003, reason="Origin not allowed")
        return
    
    # Check API key configuration
    if not settings.elevenlabs_api_key:
        logger.error("ELEVENLABS_API_KEY not configured")
        await websocket.close(code=4000, reason="Service not configured")
        return
    
    # Rate limiting
    if not await rate_limiter.acquire(client_ip):
        logger.warning("Rate limit exceeded for client", client_ip=client_ip)
        await websocket.close(code=4029, reason="Too many connections")
        return
    
    try:
        # Accept WebSocket connection
        await websocket.accept()
        logger.info("ElevenLabs WebSocket accepted", client_ip=client_ip)
        
        # Build ElevenLabs WebSocket URL
        # ElevenLabs uses path parameter for voice_id
        elevenlabs_url = f"wss://api.elevenlabs.io/v1/text-to-speech/{voice_id}/stream-input?model_id={model}"
        
        # API key goes in headers
        provider_headers = {
            "xi-api-key": settings.elevenlabs_api_key
        }
        
        # Prepare initialization message
        # ElevenLabs expects this format on connect
        init_message = {
            "text": " ",  # Initial space to start the stream
            "voice_settings": {
                "stability": 0.5,
                "similarity_boost": 0.75,
                "style": 0.0,
                "use_speaker_boost": True
            },
            "generation_config": {
                "chunk_length_schedule": [120, 160, 250, 290]  # Optimize for low latency
            },
            "xi_api_key": settings.elevenlabs_api_key,
            "model_id": model,
            "output_format": output_format,
            "optimize_streaming_latency": 4 if latency == "optimized" else 2,
            "enable_ssml_parsing": False,
            # Request alignment data for lip-sync
            "apply_text_normalization": "auto"
        }
        
        logger.info(
            "Initializing ElevenLabs session",
            model=model,
            voice_id=voice_id,
            output_format=output_format,
            latency=latency
        )
        
        # Bridge messages between client and ElevenLabs
        await bridge_client_server(
            client_ws=websocket,
            provider_url=elevenlabs_url,
            provider_headers=provider_headers,
            init_messages=[init_message],
            transform_client_msg=None,  # Pass through client messages
            transform_server_msg=None,  # Pass through ElevenLabs responses
            on_error=lambda e: logger.error("Bridge error", error=str(e))
        )
        
        logger.info("ElevenLabs WebSocket session completed", client_ip=client_ip)
    
    except WebSocketDisconnect:
        logger.info("ElevenLabs WebSocket disconnected by client", client_ip=client_ip)
    
    except Exception as e:
        logger.error("ElevenLabs WebSocket error", error=str(e), client_ip=client_ip)
        try:
            await websocket.close(code=4000, reason="Internal error")
        except:
            pass
    
    finally:
        # Release rate limiter slot
        await rate_limiter.release(client_ip)
        logger.debug("Rate limiter slot released", client_ip=client_ip)


@router.get("/elevenlabs/voices")
async def list_elevenlabs_voices():
    """List available ElevenLabs voices.
    
    This is a helper endpoint for clients to discover available voices
    without exposing the API key.
    
    Returns:
        JSON list of available voices with metadata
    """
    if not settings.elevenlabs_api_key:
        raise HTTPException(status_code=503, detail="ElevenLabs not configured")
    
    try:
        import httpx
        
        async with httpx.AsyncClient() as client:
            response = await client.get(
                "https://api.elevenlabs.io/v1/voices",
                headers={"xi-api-key": settings.elevenlabs_api_key},
                timeout=10.0
            )
            response.raise_for_status()
            
            data = response.json()
            voices = data.get("voices", [])
            
            logger.info("Fetched ElevenLabs voices", count=len(voices))
            return JSONResponse(content={"voices": voices})
    
    except Exception as e:
        logger.error("Failed to fetch ElevenLabs voices", error=str(e))
        raise HTTPException(status_code=500, detail=f"Failed to fetch voices: {str(e)}")


@router.get("/elevenlabs/models")
async def list_elevenlabs_models():
    """List available ElevenLabs models.
    
    Returns:
        JSON list of available models
    """
    if not settings.elevenlabs_api_key:
        raise HTTPException(status_code=503, detail="ElevenLabs not configured")
    
    try:
        import httpx
        
        async with httpx.AsyncClient() as client:
            response = await client.get(
                "https://api.elevenlabs.io/v1/models",
                headers={"xi-api-key": settings.elevenlabs_api_key},
                timeout=10.0
            )
            response.raise_for_status()
            
            models = response.json()
            
            logger.info("Fetched ElevenLabs models", count=len(models))
            return JSONResponse(content=models)
    
    except Exception as e:
        logger.error("Failed to fetch ElevenLabs models", error=str(e))
        raise HTTPException(status_code=500, detail=f"Failed to fetch models: {str(e)}")


@router.get("/elevenlabs/user")
async def get_elevenlabs_user_info():
    """Get ElevenLabs user subscription info.
    
    Returns user quota, character count, and subscription details.
    Useful for displaying usage information to clients.
    
    Returns:
        JSON with user subscription data
    """
    if not settings.elevenlabs_api_key:
        raise HTTPException(status_code=503, detail="ElevenLabs not configured")
    
    try:
        import httpx
        
        async with httpx.AsyncClient() as client:
            response = await client.get(
                "https://api.elevenlabs.io/v1/user",
                headers={"xi-api-key": settings.elevenlabs_api_key},
                timeout=10.0
            )
            response.raise_for_status()
            
            user_data = response.json()
            
            # Extract relevant info for client
            info = {
                "character_count": user_data.get("subscription", {}).get("character_count", 0),
                "character_limit": user_data.get("subscription", {}).get("character_limit", 0),
                "tier": user_data.get("subscription", {}).get("tier", "free"),
                "can_use_instant_voice_cloning": user_data.get("subscription", {}).get("can_use_instant_voice_cloning", False),
                "available_models": user_data.get("subscription", {}).get("available_models", [])
            }
            
            logger.info("Fetched ElevenLabs user info", tier=info["tier"])
            return JSONResponse(content=info)
    
    except Exception as e:
        logger.error("Failed to fetch ElevenLabs user info", error=str(e))
        raise HTTPException(status_code=500, detail=f"Failed to fetch user info: {str(e)}")

