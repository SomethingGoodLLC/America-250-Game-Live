"""Cartesia WebSocket proxy for browser-side TTS streaming.

This module provides a WebSocket endpoint that proxies connections from the browser
to Cartesia's streaming TTS API. The proxy keeps the API key server-side and implements
rate limiting and origin checking for security.

Endpoint: /ws/cartesia
Query Parameters:
    - voice_id: Optional voice ID (default uses provider default)
    - model: Model ID (default: sonic-english)
    - sample_rate: Audio sample rate (default: 24000)
    - output_format: Audio format (default: pcm_s16le)

Example Usage:
    const ws = new WebSocket('ws://localhost:8000/ws/cartesia?voice_id=abc123&model=sonic-english');

    ws.onopen = () => {
        // Send text to synthesize
        ws.send(JSON.stringify({
            type: 'text',
            text: 'Hello world'
        }));
    };

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.type === 'chunk') {
            // Audio chunk with optional phoneme timestamps
            playAudio(data.data); // base64 PCM audio
        }
    };
"""

import asyncio

import structlog
from fastapi import APIRouter, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse

from core.settings import settings

from ._bridge import ConnectionRateLimiter, bridge_client_server, check_origin

logger = structlog.get_logger(__name__)
router = APIRouter()

# Rate limiter: max 3 concurrent connections per IP
rate_limiter = ConnectionRateLimiter(max_connections_per_ip=3)

# Allowed origins for CORS (configure via env if needed)
ALLOWED_ORIGINS = ["http://localhost:*", "http://127.0.0.1:*", "https://*.your-domain.com"]


@router.websocket("/ws/cartesia")
async def cartesia_websocket_proxy(
    websocket: WebSocket,
    voice_id: str | None = Query(None, description="Voice ID for synthesis"),
    model: str = Query("sonic-english", description="Cartesia model ID"),
    sample_rate: int = Query(24000, description="Audio sample rate in Hz"),
    output_format: str = Query("pcm_s16le", description="Audio output format"),
):
    """WebSocket proxy endpoint for Cartesia streaming TTS.

    This endpoint creates a bridge between the browser client and Cartesia's API,
    keeping the API key secure on the server side.

    Protocol Flow:
    1. Client connects with query parameters (voice_id, model, etc.)
    2. Server establishes connection to Cartesia and sends session init
    3. Server bridges all subsequent messages between client and Cartesia
    4. Client sends text messages, receives audio + phoneme timestamps

    Security:
    - API key is never exposed to the client
    - Origin checking prevents unauthorized domains from using the proxy
    - Rate limiting prevents abuse (max 3 connections per IP)
    - Connection timeout after prolonged inactivity

    Args:
        websocket: FastAPI WebSocket connection
        voice_id: Optional Cartesia voice ID
        model: Cartesia model ID (default: sonic-english)
        sample_rate: Audio sample rate (default: 24000)
        output_format: Audio format (default: pcm_s16le, options: pcm_s16le, pcm_f32le, mulaw)
    """
    client_ip = websocket.client.host if websocket.client else "unknown"
    origin = websocket.headers.get("origin")

    logger.info(
        "Cartesia WebSocket connection attempt",
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
    if not settings.cartesia_api_key:
        logger.error("CARTESIA_API_KEY not configured")
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
        logger.info("Cartesia WebSocket accepted", client_ip=client_ip)

        # Build Cartesia WebSocket URL
        # Cartesia uses query param for API key in WebSocket URL
        cartesia_url = (
            f"wss://api.cartesia.ai/tts/websocket"
            f"?api_key={settings.cartesia_api_key}"
            f"&cartesia_version=2024-06-10"
        )

        # Prepare session initialization message
        # Send immediately after connecting to Cartesia
        init_message = {
            "context_id": f"session_{client_ip}_{asyncio.get_event_loop().time()}",
            "model_id": model,
            "transcript": "",
            "output_format": {
                "container": "raw",
                "encoding": output_format,
                "sample_rate": sample_rate,
            },
            "language": "en",
            "add_timestamps": True,
            "phoneme_timestamps": True,
        }

        # Add voice if specified
        if voice_id:
            init_message["voice"] = {"mode": "id", "id": voice_id}

        logger.info(
            "Initializing Cartesia session",
            model=model,
            voice_id=voice_id,
            sample_rate=sample_rate
        )

        # Bridge messages between client and Cartesia
        # The bridge will handle all message forwarding bidirectionally
        await bridge_client_server(
            client_ws=websocket,
            provider_url=cartesia_url,
            provider_headers=None,  # API key is in URL for Cartesia
            init_messages=[init_message],  # Send session init immediately
            transform_client_msg=None,  # Pass through client messages
            transform_server_msg=None,  # Pass through Cartesia responses
            on_error=lambda e: logger.error("Bridge error", error=str(e))
        )

        logger.info("Cartesia WebSocket session completed", client_ip=client_ip)

    except WebSocketDisconnect:
        logger.info("Cartesia WebSocket disconnected by client", client_ip=client_ip)

    except Exception as e:
        logger.error("Cartesia WebSocket error", error=str(e), client_ip=client_ip)
        try:
            await websocket.close(code=4000, reason="Internal error")
        except Exception:
            pass

    finally:
        # Release rate limiter slot
        await rate_limiter.release(client_ip)
        logger.debug("Rate limiter slot released", client_ip=client_ip)


@router.get("/cartesia/voices")
async def list_cartesia_voices():
    """List available Cartesia voices.

    This is a helper endpoint for clients to discover available voices
    without exposing the API key.

    Returns:
        JSON list of available voices with metadata
    """
    if not settings.cartesia_api_key:
        raise HTTPException(status_code=503, detail="Cartesia not configured")

    try:
        import httpx

        async with httpx.AsyncClient() as client:
            response = await client.get(
                "https://api.cartesia.ai/voices",
                headers={"X-API-Key": settings.cartesia_api_key},
                timeout=10.0
            )
            response.raise_for_status()

            voices = response.json()

            logger.info("Fetched Cartesia voices", count=len(voices))
            return JSONResponse(content=voices)

    except Exception as e:
        logger.error("Failed to fetch Cartesia voices", error=str(e))
        raise HTTPException(status_code=500, detail=f"Failed to fetch voices: {str(e)}") from e


@router.get("/cartesia/models")
async def list_cartesia_models():
    """List available Cartesia models.

    Returns:
        JSON list of available models
    """
    # Cartesia models are relatively static, so we can return a hardcoded list
    # This avoids an extra API call
    models = [
        {
            "id": "sonic-english",
            "name": "Sonic (English)",
            "description": "Ultra-fast, low-latency English TTS optimized for real-time applications",
            "languages": ["en"],
            "sample_rate": 24000
        },
        {
            "id": "sonic-multilingual",
            "name": "Sonic (Multilingual)",
            "description": "Ultra-fast multilingual TTS supporting 14+ languages",
            "languages": ["en", "es", "fr", "de", "it", "pt", "pl", "tr", "ru", "nl", "cs", "ar", "zh", "ja", "ko"],
            "sample_rate": 24000
        }
    ]

    return JSONResponse(content={"models": models})

