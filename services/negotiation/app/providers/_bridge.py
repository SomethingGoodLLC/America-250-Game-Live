"""WebSocket bridging utility for proxying between client and TTS provider.

This module provides a reusable bridge function that:
- Accepts WebSocket connections from browser clients
- Establishes WebSocket connections to TTS providers
- Forwards messages bidirectionally with optional transformation
- Handles errors and cleanup gracefully
"""

import asyncio
import json
from typing import Optional, Callable, Any, Dict

import structlog
from fastapi import WebSocket
import websockets
from websockets.client import WebSocketClientProtocol

logger = structlog.get_logger(__name__)


async def bridge_client_server(
    client_ws: WebSocket,
    provider_url: str,
    *,
    provider_headers: Optional[Dict[str, str]] = None,
    init_messages: Optional[list[dict]] = None,
    transform_client_msg: Optional[Callable[[dict], dict]] = None,
    transform_server_msg: Optional[Callable[[dict], dict]] = None,
    on_error: Optional[Callable[[Exception], None]] = None,
) -> None:
    """Bridge messages between client WebSocket and provider WebSocket.
    
    This function creates a bidirectional message bridge:
    - Client -> Server: Forwards client messages to provider
    - Server -> Client: Forwards provider messages to client
    
    Args:
        client_ws: FastAPI WebSocket connection from browser client
        provider_url: WebSocket URL of the TTS provider (e.g., wss://api.cartesia.ai/tts)
        provider_headers: Optional headers for provider connection (e.g., API key)
        init_messages: Optional list of messages to send to provider immediately on connect
        transform_client_msg: Optional function to transform messages from client before forwarding
        transform_server_msg: Optional function to transform messages from provider before forwarding
        on_error: Optional error handler callback
    
    Example:
        ```python
        await bridge_client_server(
            client_ws=websocket,
            provider_url="wss://api.cartesia.ai/tts",
            provider_headers={"X-API-Key": api_key},
            init_messages=[{
                "type": "session",
                "sample_rate": 24000,
                "format": "pcm_s16le"
            }]
        )
        ```
    
    Raises:
        Various WebSocket exceptions on connection/communication errors
    """
    server_ws: Optional[WebSocketClientProtocol] = None
    client_task: Optional[asyncio.Task] = None
    server_task: Optional[asyncio.Task] = None
    
    try:
        # Connect to provider WebSocket
        logger.info("Connecting to provider", provider_url=provider_url)
        server_ws = await websockets.connect(
            provider_url,
            extra_headers=provider_headers or {},
            ping_interval=20,
            ping_timeout=10,
        )
        logger.info("Connected to provider", provider_url=provider_url)
        
        # Send initial messages to provider if provided
        if init_messages:
            for msg in init_messages:
                logger.debug("Sending init message to provider", message=msg)
                await server_ws.send(json.dumps(msg))
        
        # Task to forward client messages to server
        async def forward_client_to_server():
            """Forward messages from client to provider."""
            try:
                while True:
                    # Receive from client
                    try:
                        data = await client_ws.receive_text()
                        message = json.loads(data)
                    except json.JSONDecodeError as e:
                        logger.warning("Invalid JSON from client", error=str(e))
                        continue
                    
                    # Transform if needed
                    if transform_client_msg:
                        message = transform_client_msg(message)
                        if message is None:
                            continue  # Skip this message
                    
                    # Forward to provider
                    logger.debug("Client -> Provider", message=message)
                    await server_ws.send(json.dumps(message))
                    
            except asyncio.CancelledError:
                logger.debug("Client->Server forwarding cancelled")
                raise
            except Exception as e:
                logger.error("Error forwarding client to server", error=str(e))
                if on_error:
                    on_error(e)
                raise
        
        # Task to forward server messages to client
        async def forward_server_to_client():
            """Forward messages from provider to client."""
            try:
                async for message in server_ws:
                    # Parse message
                    try:
                        if isinstance(message, bytes):
                            # Binary message (e.g., audio data)
                            logger.debug("Provider -> Client (binary)", size=len(message))
                            await client_ws.send_bytes(message)
                        else:
                            # Text message (JSON)
                            data = json.loads(message)
                            
                            # Transform if needed
                            if transform_server_msg:
                                data = transform_server_msg(data)
                                if data is None:
                                    continue  # Skip this message
                            
                            logger.debug("Provider -> Client", message=data)
                            await client_ws.send_json(data)
                            
                    except json.JSONDecodeError as e:
                        logger.warning("Invalid JSON from provider", error=str(e))
                        continue
                        
            except asyncio.CancelledError:
                logger.debug("Server->Client forwarding cancelled")
                raise
            except Exception as e:
                logger.error("Error forwarding server to client", error=str(e))
                if on_error:
                    on_error(e)
                raise
        
        # Run both forwarding tasks concurrently
        client_task = asyncio.create_task(forward_client_to_server())
        server_task = asyncio.create_task(forward_server_to_client())
        
        # Wait for either task to complete (which usually means an error or disconnect)
        done, pending = await asyncio.wait(
            [client_task, server_task],
            return_when=asyncio.FIRST_COMPLETED
        )
        
        # Cancel pending tasks
        for task in pending:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        
        # Check if any task raised an exception
        for task in done:
            if task.exception():
                raise task.exception()
        
    except websockets.exceptions.WebSocketException as e:
        logger.error("WebSocket error in bridge", error=str(e))
        if on_error:
            on_error(e)
        raise
    
    except Exception as e:
        logger.error("Unexpected error in bridge", error=str(e))
        if on_error:
            on_error(e)
        raise
    
    finally:
        # Cleanup
        logger.info("Cleaning up bridge connection")
        
        # Cancel tasks if still running
        if client_task and not client_task.done():
            client_task.cancel()
            try:
                await asyncio.wait_for(client_task, timeout=1.0)
            except (asyncio.CancelledError, asyncio.TimeoutError):
                pass
        
        if server_task and not server_task.done():
            server_task.cancel()
            try:
                await asyncio.wait_for(server_task, timeout=1.0)
            except (asyncio.CancelledError, asyncio.TimeoutError):
                pass
        
        # Close provider WebSocket
        if server_ws and not server_ws.closed:
            await server_ws.close()
            logger.debug("Provider WebSocket closed")


class ConnectionRateLimiter:
    """Simple in-memory rate limiter for WebSocket connections by client IP.
    
    Tracks concurrent connections per IP and enforces a maximum limit.
    """
    
    def __init__(self, max_connections_per_ip: int = 3):
        """Initialize rate limiter.
        
        Args:
            max_connections_per_ip: Maximum concurrent connections allowed per IP
        """
        self._connections: Dict[str, int] = {}
        self._max_per_ip = max_connections_per_ip
        self._lock = asyncio.Lock()
    
    async def acquire(self, client_ip: str) -> bool:
        """Try to acquire a connection slot for the given IP.
        
        Args:
            client_ip: Client IP address
        
        Returns:
            True if connection is allowed, False if rate limit exceeded
        """
        async with self._lock:
            current = self._connections.get(client_ip, 0)
            if current >= self._max_per_ip:
                logger.warning(
                    "Rate limit exceeded",
                    client_ip=client_ip,
                    current_connections=current,
                    max_connections=self._max_per_ip
                )
                return False
            
            self._connections[client_ip] = current + 1
            logger.debug(
                "Connection acquired",
                client_ip=client_ip,
                connections=self._connections[client_ip]
            )
            return True
    
    async def release(self, client_ip: str) -> None:
        """Release a connection slot for the given IP.
        
        Args:
            client_ip: Client IP address
        """
        async with self._lock:
            current = self._connections.get(client_ip, 0)
            if current > 0:
                self._connections[client_ip] = current - 1
                if self._connections[client_ip] == 0:
                    del self._connections[client_ip]
            
            logger.debug(
                "Connection released",
                client_ip=client_ip,
                connections=self._connections.get(client_ip, 0)
            )


def check_origin(origin: Optional[str], allowed_origins: Optional[list[str]] = None) -> bool:
    """Check if the WebSocket origin is allowed.
    
    Args:
        origin: Origin header from WebSocket request
        allowed_origins: List of allowed origins. If None, allows all origins.
    
    Returns:
        True if origin is allowed, False otherwise
    """
    if allowed_origins is None:
        return True  # Allow all origins if not configured
    
    if origin is None:
        return False  # Reject if no origin header
    
    # Exact match or wildcard match
    for allowed in allowed_origins:
        if allowed == "*":
            return True
        if origin == allowed:
            return True
        # Support wildcard subdomains like "*.example.com"
        if allowed.startswith("*."):
            domain = allowed[2:]
            if origin.endswith(domain):
                return True
    
    return False

