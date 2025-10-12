"""Tests for the WebSocket streaming router with backpressure management.

This module tests the /ws/stream endpoint including:
- Configuration and initialization
- Text message handling
- Command message handling (stop)
- Backpressure queue behavior
- Event streaming
- Error handling and cleanup
"""

import asyncio
import base64
import json
from typing import AsyncIterator, Optional
from unittest.mock import AsyncMock, Mock, patch

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient
from starlette.websockets import WebSocket

from app.routers.stream import (
    BackpressureQueue,
    StreamInitConfig,
    TextMessage,
    CommandMessage,
    router as stream_router,
)
from app.schemas import StreamEvent, PhonemeTimestamp, VisemeEvent
from tts.streaming.base import TTSStreamer


# Test BackpressureQueue
class TestBackpressureQueue:
    """Test backpressure queue behavior."""
    
    @pytest.mark.asyncio
    async def test_basic_queue_operations(self):
        """Test basic put/get operations."""
        queue = BackpressureQueue(max_size=10)
        
        event1 = StreamEvent(kind="audio", pcm16_b64="test1")
        event2 = StreamEvent(kind="phonemes", phonemes=[])
        
        await queue.put(event1)
        await queue.put(event2)
        
        assert not queue.empty()
        assert await queue.get() == event1
        assert await queue.get() == event2
        assert queue.empty()
    
    @pytest.mark.asyncio
    async def test_backpressure_drops_timing_events(self):
        """Test that backpressure drops timing events but not audio."""
        queue = BackpressureQueue(max_size=5)
        
        # Fill queue with mix of audio and timing events
        for i in range(10):
            if i % 2 == 0:
                # Audio (critical, never drop)
                event = StreamEvent(kind="audio", pcm16_b64=f"audio{i}")
            else:
                # Phonemes (droppable)
                event = StreamEvent(kind="phonemes", phonemes=[])
            await queue.put(event)
        
        # Queue should have dropped some phoneme events
        assert queue._dropped_count > 0
        
        # All remaining events should be retrievable
        events = []
        while not queue.empty():
            events.append(await queue.get())
        
        # Audio events should be preserved
        audio_events = [e for e in events if e.kind == "audio"]
        assert len(audio_events) == 5  # All 5 audio events preserved
    
    @pytest.mark.asyncio
    async def test_backpressure_preserves_terminal_events(self):
        """Test that done and error events are never dropped."""
        queue = BackpressureQueue(max_size=5)
        
        # Fill queue with droppable events
        for i in range(10):
            event = StreamEvent(kind="phonemes", phonemes=[])
            await queue.put(event)
        
        # Add terminal events
        done_event = StreamEvent(kind="done")
        error_event = StreamEvent(kind="error", error="test error")
        await queue.put(done_event)
        await queue.put(error_event)
        
        # Drain queue
        events = []
        while not queue.empty():
            events.append(await queue.get())
        
        # Terminal events should be present
        kinds = [e.kind for e in events]
        assert "done" in kinds
        assert "error" in kinds
    
    @pytest.mark.asyncio
    async def test_empty_queue_raises(self):
        """Test that getting from empty queue raises IndexError."""
        queue = BackpressureQueue()
        
        with pytest.raises(IndexError):
            await queue.get()


# Mock TTS Streamer for testing
class MockTTSStreamer(TTSStreamer):
    """Mock TTS streamer for testing."""
    
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.events: list[StreamEvent] = []
        self.received_texts: list[tuple[str, bool]] = []
        self.start_called = False
        self.stop_called = False
    
    async def start(self, *, voice_id: Optional[str] = None, **kwargs) -> None:
        """Mock start."""
        self._started = True
        self.start_called = True
    
    async def send_text(self, text: str, *, final: bool = False) -> None:
        """Mock send_text."""
        self._validate_started()
        self._validate_not_stopped()
        self.received_texts.append((text, final))
    
    async def stop(self) -> None:
        """Mock stop."""
        self._stopped = True
        self.stop_called = True
    
    async def aiter_events(self) -> AsyncIterator[StreamEvent]:
        """Mock event iteration."""
        for event in self.events:
            yield event
            await asyncio.sleep(0.01)  # Simulate streaming delay


# Test configuration models
class TestConfigModels:
    """Test configuration Pydantic models."""
    
    def test_stream_init_config_valid(self):
        """Test valid StreamInitConfig."""
        config = StreamInitConfig(
            provider="cartesia",
            voice_id="test-voice",
            mode="phonemes",
            sample_rate=24000
        )
        assert config.provider == "cartesia"
        assert config.voice_id == "test-voice"
        assert config.mode == "phonemes"
        assert config.sample_rate == 24000
    
    def test_stream_init_config_defaults(self):
        """Test StreamInitConfig defaults."""
        config = StreamInitConfig(
            provider="azure",
            voice_id="test-voice"
        )
        assert config.mode == "phonemes"  # Default
        assert config.sample_rate == 24000  # Default
    
    def test_stream_init_config_invalid_provider(self):
        """Test invalid provider raises validation error."""
        with pytest.raises(ValueError):
            StreamInitConfig(
                provider="invalid",
                voice_id="test-voice"
            )
    
    def test_text_message_valid(self):
        """Test valid TextMessage."""
        msg = TextMessage(text="Hello world")
        assert msg.text == "Hello world"
    
    def test_command_message_valid(self):
        """Test valid CommandMessage."""
        msg = CommandMessage(command="stop")
        assert msg.command == "stop"
    
    def test_command_message_invalid(self):
        """Test invalid command raises validation error."""
        with pytest.raises(ValueError):
            CommandMessage(command="invalid")


# Test WebSocket endpoint
class TestStreamWebSocket:
    """Test the /ws/stream WebSocket endpoint."""
    
    @pytest.fixture
    def app(self):
        """Create test FastAPI app."""
        app = FastAPI()
        app.include_router(stream_router)
        return app
    
    @pytest.mark.asyncio
    async def test_websocket_accepts_connection(self, app):
        """Test that WebSocket accepts connection."""
        client = TestClient(app)
        
        with patch("app.routers.stream._create_tts_streamer") as mock_create:
            mock_streamer = MockTTSStreamer()
            mock_create.return_value = mock_streamer
            
            with client.websocket_connect("/ws/stream") as websocket:
                # Send config
                websocket.send_json({
                    "provider": "cartesia",
                    "voice_id": "test-voice",
                    "mode": "phonemes"
                })
                
                # Should receive ack
                response = websocket.receive_json()
                assert response["type"] == "config_ack"
                assert "cartesia" in response["message"]
    
    @pytest.mark.asyncio
    async def test_websocket_handles_text_messages(self, app):
        """Test WebSocket handles text messages."""
        client = TestClient(app)
        
        with patch("app.routers.stream._create_tts_streamer") as mock_create:
            mock_streamer = MockTTSStreamer()
            mock_streamer.events = [
                StreamEvent(kind="audio", pcm16_b64="test_audio"),
                StreamEvent(kind="done")
            ]
            mock_create.return_value = mock_streamer
            
            with client.websocket_connect("/ws/stream") as websocket:
                # Send config
                websocket.send_json({
                    "provider": "cartesia",
                    "voice_id": "test-voice"
                })
                
                # Receive ack
                websocket.receive_json()
                
                # Send text
                websocket.send_json({"text": "Hello world"})
                
                # Should receive events
                event1 = websocket.receive_json()
                assert event1["kind"] == "audio"
                
                event2 = websocket.receive_json()
                assert event2["kind"] == "done"
                
                # Check streamer received text
                assert ("Hello world", False) in mock_streamer.received_texts
    
    @pytest.mark.asyncio
    async def test_websocket_handles_stop_command(self, app):
        """Test WebSocket handles stop command."""
        client = TestClient(app)
        
        with patch("app.routers.stream._create_tts_streamer") as mock_create:
            mock_streamer = MockTTSStreamer()
            mock_create.return_value = mock_streamer
            
            with client.websocket_connect("/ws/stream") as websocket:
                # Send config
                websocket.send_json({
                    "provider": "cartesia",
                    "voice_id": "test-voice"
                })
                
                # Receive ack
                websocket.receive_json()
                
                # Send stop command
                websocket.send_json({"command": "stop"})
                
                # Connection should close
                # (TestClient will handle this automatically)
                
                # Check streamer was stopped
                await asyncio.sleep(0.1)  # Give cleanup time
                assert mock_streamer.stop_called
    
    @pytest.mark.asyncio
    async def test_websocket_invalid_config(self, app):
        """Test WebSocket rejects invalid configuration."""
        client = TestClient(app)
        
        with client.websocket_connect("/ws/stream") as websocket:
            # Send invalid config
            websocket.send_json({
                "provider": "invalid_provider",
                "voice_id": "test-voice"
            })
            
            # Should receive error
            response = websocket.receive_json()
            assert response["kind"] == "error"
            assert "configuration" in response["error"].lower()
    
    @pytest.mark.asyncio
    async def test_websocket_cleanup_on_error(self, app):
        """Test WebSocket properly cleans up on error."""
        client = TestClient(app)
        
        with patch("app.routers.stream._create_tts_streamer") as mock_create:
            mock_streamer = MockTTSStreamer()
            
            # Make send_text raise an error
            async def error_send_text(text: str, *, final: bool = False):
                raise Exception("Test error")
            
            mock_streamer.send_text = error_send_text
            mock_create.return_value = mock_streamer
            
            with client.websocket_connect("/ws/stream") as websocket:
                # Send config
                websocket.send_json({
                    "provider": "cartesia",
                    "voice_id": "test-voice"
                })
                
                # Receive ack
                websocket.receive_json()
                
                # Send text (will cause error)
                websocket.send_json({"text": "Hello"})
                
                # Should receive error event
                response = websocket.receive_json()
                assert response["kind"] == "error"
                
                # Streamer should be stopped
                await asyncio.sleep(0.1)
                assert mock_streamer.stop_called


# Integration tests
class TestStreamRouterIntegration:
    """Integration tests for the stream router."""
    
    @pytest.mark.asyncio
    async def test_backpressure_in_websocket(self, monkeypatch):
        """Test that backpressure is applied in WebSocket streaming."""
        # This is a complex integration test that would require
        # setting up a full WebSocket connection and streaming
        # many events to trigger backpressure
        # Skipping for now, but would be valuable for full coverage
        pass
    
    @pytest.mark.asyncio
    async def test_concurrent_text_and_events(self, monkeypatch):
        """Test concurrent text sending and event receiving."""
        # Another integration test that would verify the dual-task
        # architecture works correctly
        pass


# Performance tests
class TestStreamPerformance:
    """Performance tests for streaming router."""
    
    @pytest.mark.asyncio
    async def test_high_throughput_streaming(self):
        """Test streaming with high event throughput."""
        queue = BackpressureQueue(max_size=100)
        
        # Generate many events
        events = [
            StreamEvent(kind="audio" if i % 10 == 0 else "phonemes", pcm16_b64=f"data{i}")
            for i in range(1000)
        ]
        
        # Put all events
        for event in events:
            await queue.put(event)
        
        # Should have applied backpressure
        assert queue._dropped_count > 0
        
        # Drain queue
        retrieved = []
        while not queue.empty():
            retrieved.append(await queue.get())
        
        # Should have reasonable number of events
        assert len(retrieved) <= 200  # Some were dropped
        
        # All audio events should be preserved
        audio_retrieved = [e for e in retrieved if e.kind == "audio"]
        audio_original = [e for e in events if e.kind == "audio"]
        assert len(audio_retrieved) == len(audio_original)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])

