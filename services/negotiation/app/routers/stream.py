"""WebSocket router for TTS streaming with backpressure management.

This module implements the streaming WebSocket endpoint that:
- Accepts configuration with provider, voice, and mode
- Handles text input and command messages
- Streams audio, phonemes, visemes, and blendshapes
- Implements backpressure by dropping old timing events (never audio)
- Properly cleans up on disconnect
"""

import asyncio
from collections import deque
from typing import Literal

import structlog
from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, ValidationError

from app.schemas import StreamEvent
from core.settings import settings
from tts.streaming import AzureTTSStreamer, CartesiaTTSStreamer, ElevenLabsTTSStreamer
from tts.streaming.base import TTSStreamer

logger = structlog.get_logger(__name__)
router = APIRouter()


# Configuration models
class StreamInitConfig(BaseModel):
    """Initial configuration for streaming session."""
    provider: Literal["cartesia", "azure", "elevenlabs"]
    voice_id: str
    mode: Literal["phonemes", "viseme", "blendshapes"] = "phonemes"
    sample_rate: int = 24000


class TextMessage(BaseModel):
    """Text message to synthesize."""
    text: str


class CommandMessage(BaseModel):
    """Command message."""
    command: Literal["stop"]


# Backpressure configuration
MAX_QUEUE_SIZE = 50  # Maximum pending events before dropping
DROP_EVENT_KINDS = {"phonemes", "viseme", "blendshapes"}  # Event types that can be dropped


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


class BackpressureQueue:
    """Queue with backpressure management for streaming events.

    Implements selective event dropping when queue is full:
    - Audio events are NEVER dropped (critical for sync)
    - Timing events (phonemes, visemes, blendshapes) can be dropped
    - When queue is full, oldest droppable events are removed
    """

    def __init__(self, max_size: int = MAX_QUEUE_SIZE):
        """Initialize backpressure queue.

        Args:
            max_size: Maximum queue size before backpressure kicks in
        """
        self._queue: deque[StreamEvent] = deque(maxlen=max_size * 2)  # Allow temporary overflow
        self._max_size = max_size
        self._dropped_count = 0

    async def put(self, event: StreamEvent) -> None:
        """Add event to queue with backpressure management.

        Args:
            event: StreamEvent to queue
        """
        self._queue.append(event)

        # Check if we need to apply backpressure
        if len(self._queue) > self._max_size:
            self._apply_backpressure()

    async def get(self) -> StreamEvent:
        """Get next event from queue.

        Returns:
            Next StreamEvent

        Raises:
            IndexError: If queue is empty
        """
        if not self._queue:
            raise IndexError("Queue is empty")
        return self._queue.popleft()

    def empty(self) -> bool:
        """Check if queue is empty."""
        return len(self._queue) == 0

    def _apply_backpressure(self) -> None:
        """Apply backpressure by dropping old timing events.

        Strategy:
        1. Scan queue from oldest to newest
        2. Remove droppable events (phonemes, visemes, blendshapes)
        3. Never remove audio, done, or error events
        4. Stop when queue is under max_size
        """
        # Find indices of droppable events (from oldest first)
        droppable_indices = [
            i for i, event in enumerate(self._queue)
            if event.kind in DROP_EVENT_KINDS
        ]

        if not droppable_indices:
            # No droppable events, queue is full of critical events
            logger.warning(
                "Backpressure queue full but no droppable events",
                queue_size=len(self._queue),
                max_size=self._max_size
            )
            return

        # Calculate how many to drop
        overflow = len(self._queue) - self._max_size
        to_drop = min(len(droppable_indices), overflow)

        # Drop oldest droppable events
        drop_indices = set(droppable_indices[:to_drop])

        # Rebuild queue without dropped events
        new_queue = deque(
            event for i, event in enumerate(self._queue)
            if i not in drop_indices
        )

        self._queue = new_queue
        self._dropped_count += to_drop

        logger.debug(
            "Applied backpressure",
            dropped=to_drop,
            queue_size=len(self._queue),
            total_dropped=self._dropped_count
        )


@router.websocket("/ws/stream")
async def stream_websocket(websocket: WebSocket):
    """WebSocket endpoint for real-time TTS streaming with backpressure.

    Protocol:
    1. Client connects and sends init message:
       {
         "provider": "cartesia"|"azure"|"elevenlabs",
         "voice_id": "voice-id-string",
         "mode": "phonemes"|"viseme"|"blendshapes",
         "sample_rate": 24000
       }

    2. Server sends acknowledgment:
       {"type": "config_ack", "message": "Streaming configured with <provider>"}

    3. Client sends text messages:
       {"text": "Text to synthesize"}

    4. Client can send stop command:
       {"command": "stop"}

    5. Server streams StreamEvent objects:
       - kind="audio": PCM16LE audio chunk (never dropped)
       - kind="phonemes": Phoneme timestamps (droppable)
       - kind="viseme": Viseme event (droppable)
       - kind="blendshapes": Blendshape frames (droppable)
       - kind="done": Synthesis complete
       - kind="error": Error occurred

    Backpressure:
    - If client is slow and queue grows > MAX_QUEUE_SIZE, oldest timing events
      are dropped to prevent memory issues
    - Audio events are NEVER dropped to maintain A/V sync
    - Dropped event count is logged for monitoring
    """
    await websocket.accept()

    streamer: TTSStreamer | None = None
    provider: str | None = None
    voice_id: str | None = None
    mode: str | None = None
    event_task: asyncio.Task | None = None
    send_task: asyncio.Task | None = None
    event_queue: BackpressureQueue | None = None
    shutdown_event = asyncio.Event()

    try:
        # Step 1: Receive initial configuration
        logger.info("WebSocket connection accepted, waiting for config")
        config_data = await websocket.receive_json()

        try:
            config = StreamInitConfig(**config_data)
            provider = config.provider
            voice_id = config.voice_id
            mode = config.mode

            logger.info(
                "Stream WebSocket configured",
                provider=provider,
                voice_id=voice_id,
                mode=mode,
                sample_rate=config.sample_rate
            )

            # Create and start TTS streamer
            streamer = _create_tts_streamer(provider)
            await streamer.start(
                voice_id=voice_id,
                sample_rate=config.sample_rate
            )

            # Initialize backpressure queue
            event_queue = BackpressureQueue(max_size=MAX_QUEUE_SIZE)

            # Send acknowledgment
            await websocket.send_json({
                "type": "config_ack",
                "message": f"Streaming configured with {provider}",
                "mode": mode
            })

        except ValidationError as e:
            error_msg = f"Invalid configuration: {str(e)}"
            logger.error("Config validation failed", error=error_msg)
            error_event = StreamEvent(kind="error", error=error_msg)
            await websocket.send_json(error_event.model_dump())
            await websocket.close(code=4000, reason="Invalid configuration")
            return

        except ValueError as e:
            error_msg = f"Configuration error: {str(e)}"
            logger.error("Config error", error=error_msg)
            error_event = StreamEvent(kind="error", error=error_msg)
            await websocket.send_json(error_event.model_dump())
            await websocket.close(code=4000, reason=str(e))
            return

        # Step 2: Start event forwarding tasks
        async def consume_streamer_events():
            """Consume events from TTS streamer and queue them."""
            try:
                logger.debug("Starting event consumer")
                async for event in streamer.aiter_events():
                    if shutdown_event.is_set():
                        break

                    await event_queue.put(event)

                    # Stop on terminal events
                    if event.kind in ("done", "error"):
                        logger.info("Terminal event received", kind=event.kind)
                        shutdown_event.set()
                        break

            except asyncio.CancelledError:
                logger.debug("Event consumer cancelled")
                raise
            except Exception as e:
                logger.error("Error consuming events", error=str(e))
                # Queue error event
                error_event = StreamEvent(kind="error", error=str(e))
                await event_queue.put(error_event)
                shutdown_event.set()

        async def send_events_to_client():
            """Send queued events to WebSocket client."""
            try:
                logger.debug("Starting event sender")
                while not shutdown_event.is_set() or not event_queue.empty():
                    # Wait for events with timeout to check shutdown
                    try:
                        if event_queue.empty():
                            await asyncio.wait_for(
                                asyncio.shield(shutdown_event.wait()),
                                timeout=0.1
                            )
                            continue
                    except TimeoutError:
                        if not event_queue.empty():
                            pass  # Have events to send
                        else:
                            continue

                    # Send all queued events
                    while not event_queue.empty():
                        event = await event_queue.get()

                        try:
                            await websocket.send_json(event.model_dump())

                            # Log audio events for debugging
                            if event.kind == "audio":
                                logger.debug("Sent audio chunk", sample_rate=event.sample_rate)

                        except Exception as e:
                            logger.error("Error sending event", error=str(e), kind=event.kind)
                            # If we can't send, client probably disconnected
                            shutdown_event.set()
                            break

                logger.debug("Event sender completed")

            except asyncio.CancelledError:
                logger.debug("Event sender cancelled")
                raise
            except Exception as e:
                logger.error("Error in event sender", error=str(e))

        # Start background tasks
        event_task = asyncio.create_task(consume_streamer_events())
        send_task = asyncio.create_task(send_events_to_client())

        # Step 3: Main message receiving loop
        logger.debug("Entering message receive loop")
        while not shutdown_event.is_set():
            try:
                # Receive message from client with timeout
                message_data = await asyncio.wait_for(
                    websocket.receive_json(),
                    timeout=0.1
                )

            except TimeoutError:
                # Check if we should continue
                continue
            except WebSocketDisconnect:
                logger.info("Client disconnected")
                break

            # Parse message type
            try:
                # Check for text message
                if "text" in message_data:
                    text_msg = TextMessage(**message_data)

                    logger.debug(
                        "Received text message",
                        text_length=len(text_msg.text),
                        provider=provider
                    )

                    # Send text to TTS streamer
                    await streamer.send_text(text_msg.text, final=False)

                # Check for command message
                elif "command" in message_data:
                    cmd_msg = CommandMessage(**message_data)

                    if cmd_msg.command == "stop":
                        logger.info("Received stop command", provider=provider)
                        shutdown_event.set()
                        break

                else:
                    logger.warning("Unknown message format", message_data=message_data)

            except ValidationError as e:
                logger.warning("Invalid message format", error=str(e))
                # Don't close connection on invalid message, just log
                continue

            except Exception as e:
                logger.error("Error processing message", error=str(e))
                error_event = StreamEvent(kind="error", error=str(e))
                await websocket.send_json(error_event.model_dump())
                break

        # Wait for send task to complete (flush remaining events)
        logger.info("Waiting for event sender to complete")
        if send_task and not send_task.done():
            await asyncio.wait_for(send_task, timeout=5.0)

    except WebSocketDisconnect:
        logger.info("Stream WebSocket disconnected", provider=provider)

    except TimeoutError:
        logger.warning("Timeout waiting for tasks to complete")

    except Exception as e:
        logger.error("Stream WebSocket error", error=str(e), provider=provider)
        try:
            error_event = StreamEvent(kind="error", error=str(e))
            await websocket.send_json(error_event.model_dump())
        except Exception:
            pass

    finally:
        # Step 4: Cleanup
        logger.info("Cleaning up WebSocket connection", provider=provider)

        shutdown_event.set()

        # Cancel background tasks
        if event_task and not event_task.done():
            event_task.cancel()
            try:
                await asyncio.wait_for(event_task, timeout=1.0)
            except (TimeoutError, asyncio.CancelledError):
                pass

        if send_task and not send_task.done():
            send_task.cancel()
            try:
                await asyncio.wait_for(send_task, timeout=1.0)
            except (TimeoutError, asyncio.CancelledError):
                pass

        # Stop TTS streamer
        if streamer:
            try:
                await streamer.stop()
                logger.debug("TTS streamer stopped")
            except Exception as e:
                logger.warning("Error stopping streamer", error=str(e))

        # Close WebSocket
        try:
            await websocket.close()
            logger.debug("WebSocket closed")
        except Exception:
            pass

        # Log backpressure stats
        if event_queue and event_queue._dropped_count > 0:
            logger.warning(
                "Session completed with dropped events",
                dropped_count=event_queue._dropped_count,
                provider=provider
            )

        logger.info(
            "WebSocket session ended",
            provider=provider,
            voice_id=voice_id,
            mode=mode
        )

