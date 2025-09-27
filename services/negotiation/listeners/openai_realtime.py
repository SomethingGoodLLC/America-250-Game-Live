"""OpenAI realtime listener adapter for bidirectional audio streaming (production)."""

import asyncio
import base64
import json
import os
from typing import AsyncIterator, Dict, Any, Optional

import numpy as np
from scipy.signal import resample_poly
import websockets
import structlog
from .base import Listener


class OpenAIRealtimeListener(Listener):
    """OpenAI Realtime WebSocket integration.

    - Resamples incoming PCM to 24kHz 16-bit mono
    - Streams audio via input_audio_buffer.{append,commit}
    - Periodically sends response.create to elicit transcripts
    - Emits partial/final subtitle events
    """

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.logger = structlog.get_logger(__name__)
        self.api_key: Optional[str] = config.get("api_key") or os.getenv("OPENAI_API_KEY")
        self.model: str = config.get("model", "gpt-4o-realtime-preview-2024-10-01")
        self.websocket: Optional[websockets.WebSocketClientProtocol] = None
        self.event_queue: asyncio.Queue = asyncio.Queue()
        self.running: bool = False
        self._send_lock = asyncio.Lock()
        self._buffer = bytearray()
        self._last_commit = 0.0
        self._last_requested = 0.0
        self._commit_interval = 1.0
        self._request_interval = 2.0
        self._last_final_text = ""

    async def start(self) -> None:
        if not self.api_key:
            self.logger.warning("OPENAI_API_KEY not set. Using mock mode.")
            self.running = True
            asyncio.create_task(self._mock_stream())
            return

        try:
            uri = f"wss://api.openai.com/v1/realtime?model={self.model}"
            headers = {
                "Authorization": f"Bearer {self.api_key}",
                "OpenAI-Beta": "realtime=v1",
            }
            self.websocket = await websockets.connect(uri, extra_headers=headers, max_size=None)
            await self._send_json({
                "type": "session.update",
                "session": {
                    "modalities": ["text", "audio"],
                    "instructions": "You are a diplomatic negotiation assistant.",
                    "voice": "alloy",
                },
            })
            self.running = True
            asyncio.create_task(self._recv_loop())
            self.logger.info("OpenAI Realtime connected", model=self.model)
        except Exception as e:
            self.logger.error("Failed to connect to OpenAI Realtime", error=str(e))
            self.running = True
            asyncio.create_task(self._mock_stream())

    async def stop(self) -> None:
        """Stop the connection."""
        self.running = False
        if self.websocket:
            await self.websocket.close()
        if self.event_queue:
            await self.event_queue.put({"type": "stop"})

    async def feed_pcm(self, pcm_bytes: bytes, ts_ms: int) -> None:
        if not self.running or not self.websocket:
            return
        try:
            # Interpret incoming as 16-bit mono int16 @ 48kHz (adjust if needed)
            src = np.frombuffer(pcm_bytes, dtype=np.int16)
            if src.size == 0:
                return
            # Resample 48kHz -> 24kHz
            res = resample_poly(src, up=1, down=2).astype(np.int16)
            self._buffer.extend(res.tobytes())

            loop_time = asyncio.get_event_loop().time()
            # Send ~100ms chunks
            chunk_bytes = int(0.1 * 24000) * 2
            while len(self._buffer) >= chunk_bytes:
                chunk = bytes(self._buffer[:chunk_bytes])
                del self._buffer[:chunk_bytes]
                await self._send_json({
                    "type": "input_audio_buffer.append",
                    "audio": base64.b64encode(chunk).decode("utf-8"),
                })

            if loop_time - self._last_commit >= self._commit_interval:
                await self._send_json({"type": "input_audio_buffer.commit"})
                self._last_commit = loop_time

            if loop_time - self._last_requested >= self._request_interval:
                await self._send_json({
                    "type": "response.create",
                    "response": {"instructions": "Transcribe and summarize intents succinctly."},
                })
                self._last_requested = loop_time
        except Exception as e:
            self.logger.error("Error feeding PCM to OpenAI Realtime", error=str(e))

    async def final_text(self) -> str:
        """Get final recognized text."""
        return self._last_final_text

    async def stream_events(self) -> AsyncIterator[Dict[str, Any]]:
        """Stream events from OpenAI."""
        while self.running:
            try:
                event = await asyncio.wait_for(self.event_queue.get(), timeout=0.1)
                if event.get("type") == "stop":
                    break
                yield event
            except asyncio.TimeoutError:
                continue
            except Exception as e:
                self.logger.error("Error in stream_events", error=str(e))
                break

    async def _mock_stream(self) -> None:
        """Mock streaming for testing."""
        while self.running:
            try:
                await asyncio.sleep(2.0)
                await self.event_queue.put({"type": "subtitle", "text": "Analyzing audio...", "final": False, "confidence": 0.9})
                await asyncio.sleep(1.0)
                await self.event_queue.put({"type": "subtitle", "text": "Proposal detected: trade agreement.", "final": True, "confidence": 0.95})
                self._last_final_text = "Proposal detected: trade agreement."
            except Exception as e:
                self.logger.error("Error in mock stream", error=str(e))

    async def _recv_loop(self) -> None:
        if not self.websocket:
            return
        try:
            async for message in self.websocket:
                try:
                    data = json.loads(message)
                except Exception:
                    continue
                typ = data.get("type", "")
                # Handle text deltas (streaming text from model)
                if typ in ("response.audio_transcript.delta", "conversation.item.output_audio_transcript.delta"):
                    delta = data.get("delta") or data.get("transcript", "")
                    if isinstance(delta, str) and delta:
                        await self.event_queue.put({"type": "subtitle", "text": delta, "final": False, "confidence": 0.9})
                # Handle completed responses  
                elif typ in ("response.audio_transcript.done", "conversation.item.output_audio_transcript.done"):
                    text = data.get("transcript", "")
                    if isinstance(text, str) and text:
                        self._last_final_text = text
                        await self.event_queue.put({"type": "subtitle", "text": text, "final": True, "confidence": 1.0})
                # Handle text-only responses (fallback)
                elif typ in ("response.text.delta", "response.output_text.delta"):
                    delta = data.get("delta") or data.get("text", "")
                    if isinstance(delta, str) and delta:
                        await self.event_queue.put({"type": "subtitle", "text": delta, "final": False, "confidence": 0.9})
                elif typ in ("response.text.done", "response.done"):
                    text = data.get("text") or data.get("output", "")
                    if isinstance(text, str) and text:
                        self._last_final_text = text
                        await self.event_queue.put({"type": "subtitle", "text": text, "final": True, "confidence": 1.0})
                elif typ == "error":
                    self.logger.error("OpenAI Realtime error", details=data)
                elif typ in ("session.created", "session.updated"):
                    self.logger.info("OpenAI session event", event_type=typ)
                else:
                    self.logger.debug("Unhandled OpenAI event", event_type=typ, data=data)
        except Exception as e:
            self.logger.error("OpenAI Realtime receive loop failed", error=str(e))

    async def _send_json(self, payload: Dict[str, Any]) -> None:
        if not self.websocket:
            return
        async with self._send_lock:
            await self.websocket.send(json.dumps(payload))




# Re-export for convenience
__all__ = ["OpenAIRealtimeListener"]
