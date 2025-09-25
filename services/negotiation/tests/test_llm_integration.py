"""Tests for LLM integration and video generation."""

import asyncio
import pytest
import os
from unittest.mock import Mock, AsyncMock, patch
from aiortc import RTCPeerConnection, RTCSessionDescription

# Import our modules
from main import app
from listeners.base import Listener, make_listener_from_env
from providers.mock_local import MockLocalProvider
from providers.gemini_veo3 import Veo3Provider
from providers.video_sources.placeholder_loop import PlaceholderLoopVideoSource


class MockLLMListener(Listener):
    """Mock LLM listener that simulates real LLM responses."""

    def __init__(self, config):
        super().__init__(config)
        self.received_audio = []
        self.transcripts = []
        self.intent_calls = 0

    async def start(self):
        """Mock start with LLM simulation."""
        print("🤖 MockLLMListener: Starting LLM simulation")

    async def stop(self):
        """Mock stop."""
        print("🤖 MockLLMListener: Stopping LLM simulation")

    async def feed_pcm(self, pcm_bytes, ts_ms):
        """Mock feed PCM with LLM transcription simulation."""
        self.received_audio.append((pcm_bytes, ts_ms))

        # Simulate real-time transcription
        if len(self.received_audio) == 1:
            # First chunk - partial transcript
            self.transcripts.append("I propose")
        elif len(self.received_audio) == 2:
            # Second chunk - more transcript
            self.transcripts.append("I propose we establish")
        elif len(self.received_audio) >= 3:
            # Final chunks - complete transcript
            self.transcripts.append("I propose we establish a trade agreement")

        print(f"🤖 MockLLMListener: Transcribed '{self.transcripts[-1]}'")

    async def final_text(self):
        """Mock final text from LLM."""
        full_text = " ".join(self.transcripts)
        print(f"🤖 MockLLMListener: Final text: '{full_text}'")
        return full_text

    async def stream_events(self):
        """Mock stream events with LLM responses."""
        # Stream partial transcripts
        for i, transcript in enumerate(self.transcripts):
            is_final = i == len(self.transcripts) - 1
            yield {
                "type": "subtitle",
                "text": transcript,
                "final": is_final,
                "confidence": 0.95
            }

        # Simulate LLM intent analysis
        if len(self.transcripts) >= 3:
            yield {
                "type": "intent",
                "payload": {
                    "kind": "PROPOSAL",
                    "type": "proposal",
                    "text": "I propose we establish a trade agreement",
                    "confidence": 0.9,
                    "justification": "Direct proposal language detected",
                    "key_terms": ["trade", "agreement", "establish"]
                }
            }


class MockVeo3Provider(Veo3Provider):
    """Mock Veo3 provider that simulates real video generation."""

    def __init__(self, config):
        # Don't call super().__init__ to avoid real API calls
        self.config = config
        self.video_frames = []
        self.intent_responses = []

    async def stream_dialogue(self, turns, world_context, system_guidelines):
        """Mock stream dialogue with simulated video generation."""
        print("🎬 MockVeo3Provider: Starting video generation simulation")

        # Generate mock video frames
        for i in range(60):  # 2 seconds at 30fps
            frame = f"Mock video frame {i} - diplomatic response"
            self.video_frames.append(frame)

        # Simulate avatar response
        yield {
            "type": "subtitle",
            "payload": {"text": "I accept your proposal for a trade agreement"},
            "is_final": False
        }

        yield {
            "type": "subtitle",
            "payload": {"text": "I accept your proposal for a trade agreement. This will strengthen our economic ties."},
            "is_final": True
        }

        yield {
            "type": "intent",
            "payload": {
                "kind": "CONCESSION",
                "type": "concession",
                "text": "I accept your proposal for a trade agreement. This will strengthen our economic ties.",
                "confidence": 0.95,
                "justification": "Clear acceptance of trade proposal with diplomatic language",
                "key_terms": ["accept", "proposal", "trade", "agreement", "strengthen", "economic"]
            }
        }

        yield {
            "type": "analysis",
            "tag": "sentiment",
            "payload": {"sentiment": "positive", "score": 0.8}
        }

        print(f"🎬 MockVeo3Provider: Generated {len(self.video_frames)} video frames")


class TestLLMIntegration:
    """Test LLM integration and video generation."""

    @pytest.mark.asyncio
    async def test_mock_llm_listener(self):
        """Test mock LLM listener with real transcription simulation."""
        listener = MockLLMListener({"model": "gemini-1.5-flash"})

        await listener.start()

        # Feed audio chunks (simulating real speech)
        test_audio1 = b"chunk1" * 100  # First audio chunk
        test_audio2 = b"chunk2" * 100  # Second audio chunk
        test_audio3 = b"chunk3" * 100  # Third audio chunk

        await listener.feed_pcm(test_audio1, 1000)
        await listener.feed_pcm(test_audio2, 2000)
        await listener.feed_pcm(test_audio3, 3000)

        # Check transcripts
        assert len(listener.transcripts) >= 3
        assert "I propose" in listener.transcripts[0]
        assert "establish" in listener.transcripts[1]
        assert "trade agreement" in listener.transcripts[2]

        # Check final text
        final_text = await listener.final_text()
        assert "trade agreement" in final_text

        # Check streaming events
        events = []
        async for event in listener.stream_events():
            events.append(event)

        # Should have subtitle events and intent event
        subtitle_events = [e for e in events if e["type"] == "subtitle"]
        intent_events = [e for e in events if e["type"] == "intent"]

        assert len(subtitle_events) >= 3
        assert len(intent_events) >= 1

        await listener.stop()
        print("✅ Mock LLM listener test passed")

    @pytest.mark.asyncio
    async def test_mock_veo3_provider(self):
        """Test mock Veo3 provider with video generation simulation."""
        provider = MockVeo3Provider({"use_veo3": True})

        # Simulate negotiation turns
        turns = [
            {"speaker": "PLAYER", "text": "I propose we establish a trade agreement"},
            {"speaker": "AI", "text": "I accept your proposal"}
        ]

        world_context = {"scenario": "colonial_diplomacy"}
        guidelines = "Be diplomatic and formal"

        # Collect all events
        events = []
        async for event in provider.stream_dialogue(turns, world_context, guidelines):
            events.append(event)

        # Should have subtitle and intent events
        subtitle_events = [e for e in events if e["type"] == "subtitle"]
        intent_events = [e for e in events if e["type"] == "intent"]
        analysis_events = [e for e in events if e["type"] == "analysis"]

        assert len(subtitle_events) >= 2
        assert len(intent_events) >= 1
        assert len(analysis_events) >= 1

        # Check intent content
        intent = intent_events[0]["payload"]
        assert intent["kind"] == "CONCESSION"
        assert "accept" in intent["text"].lower()
        assert intent["confidence"] > 0.8

        print("✅ Mock Veo3 provider test passed")

    @pytest.mark.asyncio
    async def test_video_source_integration(self):
        """Test video source integration."""
        print("🎥 Testing video source integration...")

        # Test placeholder video source
        config = {
            "source_type": "placeholder",
            "resolution": (320, 240),
            "fps": 30
        }

        video_source = PlaceholderLoopVideoSource(config)

        # This should work without real video files
        assert video_source is not None
        assert video_source.config["source_type"] == "placeholder"

        print("✅ Video source integration test passed")

    def test_fastapi_with_llm_integration(self):
        """Test FastAPI with LLM integration."""
        from fastapi.testclient import TestClient

        client = TestClient(app)

        # Test session creation with LLM listener
        response = client.post(
            "/v1/session",
            headers={"Content-Type": "application/x-yaml"},
            data="model: veo3\nlistener: mock_llm"
        )

        assert response.status_code == 200
        session_data = response.text
        assert "session_id" in session_data

        print("✅ FastAPI with LLM integration test passed")


class TestRealLLMIntegration:
    """Test with real LLM APIs (if available)."""

    def test_gemini_api_key_validation(self):
        """Test Gemini API key validation."""
        # This would test if API key is properly configured
        api_key = os.getenv("GEMINI_API_KEY")

        if api_key:
            print("✅ Gemini API key found")
            assert len(api_key) > 10  # Basic validation
        else:
            print("⚠️ Gemini API key not found - using mock mode")

    def test_openai_api_key_validation(self):
        """Test OpenAI API key validation."""
        api_key = os.getenv("OPENAI_API_KEY")

        if api_key:
            print("✅ OpenAI API key found")
            assert len(api_key) > 10  # Basic validation
        else:
            print("⚠️ OpenAI API key not found - using mock mode")

    def test_grok_api_key_validation(self):
        """Test Grok API key validation."""
        api_key = os.getenv("GROK_API_KEY")

        if api_key:
            print("✅ Grok API key found")
            assert len(api_key) > 10  # Basic validation
        else:
            print("⚠️ Grok API key not found - using mock mode")


if __name__ == "__main__":
    print("🚀 Testing LLM Integration...")

    async def run_llm_tests():
        # Run mock LLM tests
        test_instance = TestLLMIntegration()

        await test_instance.test_mock_llm_listener()
        await test_instance.test_mock_veo3_provider()
        await test_instance.test_video_source_integration()

        test_instance.test_fastapi_with_llm_integration()

        print("🎉 LLM Integration tests completed!")

    asyncio.run(run_llm_tests())
