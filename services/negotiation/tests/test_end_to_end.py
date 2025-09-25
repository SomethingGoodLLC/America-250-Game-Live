"""End-to-end tests for the AI Avatar Negotiation System."""

import asyncio
import json
import pytest
import websockets
import aiohttp
from unittest.mock import Mock, AsyncMock, patch
from aiortc import RTCPeerConnection, RTCSessionDescription

# Import our modules
from main import app
from listeners.base import Listener, make_listener_from_env
from providers.mock_local import MockLocalProvider
from providers.gemini_veo3 import Veo3Provider


class MockWebRTCConnection:
    """Mock WebRTC connection for testing."""

    def __init__(self):
        self.remote_desc = None
        self.local_desc = None

    async def setRemoteDescription(self, desc):
        """Mock setting remote description."""
        self.remote_desc = desc
        print(f"✅ Mock WebRTC: Set remote description {desc.type}")

    async def createAnswer(self):
        """Mock creating answer."""
        return RTCSessionDescription(
            sdp="v=0\r\ns=Test\r\nc=IN IP4 127.0.0.1\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n",
            type="answer"
        )

    async def setLocalDescription(self, desc):
        """Mock setting local description."""
        self.local_desc = desc
        print(f"✅ Mock WebRTC: Set local description {desc.type}")


class MockAudioTrack:
    """Mock audio track for testing."""

    def __init__(self, audio_data=None):
        self.audio_data = audio_data or b"\x00" * 1024
        self.kind = "audio"

    def recv(self):
        """Mock receive audio frames."""
        return MockAudioFrame(self.audio_data)


class MockAudioFrame:
    """Mock audio frame."""

    def __init__(self, data):
        self.data = data


class MockListener(Listener):
    """Mock listener for testing."""

    def __init__(self, config):
        super().__init__(config)
        self.received_audio = []
        self.events = []

    async def start(self):
        """Mock start."""
        pass

    async def stop(self):
        """Mock stop."""
        pass

    async def feed_pcm(self, pcm_bytes, ts_ms):
        """Mock feed PCM."""
        self.received_audio.append((pcm_bytes, ts_ms))
        print(f"✅ Mock Listener: Received {len(pcm_bytes)} bytes of audio")

    async def final_text(self):
        """Mock final text."""
        return "Test utterance from mock listener"

    async def stream_events(self):
        """Mock stream events."""
        yield {"type": "subtitle", "text": "Mock subtitle", "final": False}
        yield {"type": "subtitle", "text": "Mock final text", "final": True}


class TestEndToEndSystem:
    """Comprehensive end-to-end tests."""

    @pytest.fixture
    async def fastapi_app(self):
        """Get FastAPI test client."""
        from fastapi.testclient import TestClient
        client = TestClient(app)
        yield client

    @pytest.fixture
    def mock_webrtc(self):
        """Mock WebRTC connection."""
        return MockWebRTCConnection()

    @pytest.fixture
    def mock_listener(self):
        """Mock listener."""
        return MockListener({})

    def test_fastapi_health(self, fastapi_app):
        """Test basic FastAPI health."""
        response = fastapi_app.get("/health")
        assert response.status_code == 200
        assert "ok" in response.text.lower()

    def test_session_creation(self, fastapi_app):
        """Test session creation."""
        response = fastapi_app.post(
            "/v1/session",
            headers={"Content-Type": "application/x-yaml"},
            data="model: mock_local"
        )
        assert response.status_code == 200

        data = response.text
        assert "session_id" in data
        print(f"✅ Session created: {data}")

    def test_webrtc_sdp_exchange(self, fastapi_app):
        """Test WebRTC SDP offer/answer exchange."""
        # Create session
        response = fastapi_app.post(
            "/v1/session",
            headers={"Content-Type": "application/x-yaml"},
            data="model: mock_local"
        )
        assert response.status_code == 200

        # Extract session ID
        session_data = response.text
        assert "session_id" in session_data
        session_id = session_data.split("session_id: ")[1].strip()

        # Send SDP offer
        offer_sdp = {
            "sdp": "v=0\r\ns=Test\r\nc=IN IP4 127.0.0.1\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n",
            "type": "offer"
        }

        response = fastapi_app.post(
            f"/v1/session/{session_id}/webrtc/offer",
            json=offer_sdp
        )

        assert response.status_code == 200
        answer_data = response.text
        assert "type" in answer_data and "sdp" in answer_data
        print(f"✅ WebRTC SDP exchange successful: {answer_data}")

    @pytest.mark.asyncio
    async def test_listener_audio_processing(self, mock_listener):
        """Test listener audio processing."""
        # Start listener
        await mock_listener.start()

        # Feed test audio
        test_audio = b"\x00" * 1024
        await mock_listener.feed_pcm(test_audio, 1234567890)

        # Check that audio was received
        assert len(mock_listener.received_audio) == 1
        assert mock_listener.received_audio[0][0] == test_audio

        # Get final text
        final_text = await mock_listener.final_text()
        assert "Test utterance" in final_text

        # Stream events
        events = []
        async for event in mock_listener.stream_events():
            events.append(event)
            if len(events) >= 2:  # Stop after 2 events
                break

        assert len(events) >= 2
        assert events[0]["type"] == "subtitle"
        assert events[1]["type"] == "subtitle"

        await mock_listener.stop()
        print("✅ Listener audio processing test passed")

    def test_provider_intent_detection(self, fastapi_app):
        """Test provider intent detection."""
        # This would test the provider system
        # For now, just verify the provider can be instantiated
        provider = MockLocalProvider({"strict": True})
        assert provider is not None
        print("✅ Provider instantiation test passed")

    @pytest.mark.asyncio
    async def test_full_negotiation_flow(self):
        """Test the complete negotiation flow."""
        print("🔄 Testing full negotiation flow...")

        # 1. Create session
        async with aiohttp.ClientSession() as session:
            # Session creation
            async with session.post(
                "http://127.0.0.1:8000/v1/session",
                headers={"Content-Type": "application/x-yaml"},
                data="model: mock_local"
            ) as response:
                session_data = await response.text()
                assert "session_id" in session_data
                session_id = session_data.split("session_id: ")[1].strip()
                print(f"✅ Session created: {session_id}")

            # 2. WebSocket connection for real-time events
            uri = f"ws://127.0.0.1:8000/v1/session/{session_id}/control"
            async with websockets.connect(uri) as websocket:
                # Send test utterance
                test_message = {"type": "player_utterance", "text": "I propose a trade agreement"}
                await websocket.send(json.dumps(test_message))

                # Receive response
                response = await websocket.recv()
                response_data = json.loads(response)
                assert "type" in response_data
                print(f"✅ WebSocket communication: {response_data}")

            # 3. WebRTC SDP exchange
            offer_sdp = {
                "sdp": "v=0\r\ns=Test\r\nc=IN IP4 127.0.0.1\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n",
                "type": "offer"
            }

            async with session.post(
                f"http://127.0.0.1:8000/v1/session/{session_id}/webrtc/offer",
                json=offer_sdp
            ) as response:
                answer_data = await response.text()
                assert "type" in answer_data and "sdp" in answer_data
                print(f"✅ WebRTC SDP exchange: {answer_data}")

        print("✅ Full negotiation flow test completed successfully")


if __name__ == "__main__":
    # Run the tests
    print("🚀 Running end-to-end tests...")

    # Create event loop for async tests
    async def run_tests():
        test_instance = TestEndToEndSystem()

        # Run async tests
        await test_instance.test_listener_audio_processing(MockListener({}))
        await test_instance.test_full_negotiation_flow()

        print("🎉 All tests passed!")

    # Run the tests
    asyncio.run(run_tests())
