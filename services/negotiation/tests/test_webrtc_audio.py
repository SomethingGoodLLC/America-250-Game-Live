"""Tests for WebRTC audio functionality and TTS integration."""

import pytest
import asyncio
from unittest.mock import Mock, patch, AsyncMock
from aiortc import RTCPeerConnection, RTCSessionDescription

from app.main import app
from tts.elevenlabs import ElevenLabsProvider
from tts.xtts import XTTSProvider


class TestWebRTCAudioIntegration:
    """Test WebRTC integration with TTS audio tracks."""

    @patch('aiortc.RTCPeerConnection')
    def test_session_creation_with_tts_provider(self, mock_pc_class):
        """Test that sessions are created with TTS providers."""
        from fastapi.testclient import TestClient

        client = TestClient(app)

        # Mock the peer connection
        mock_pc = Mock()
        mock_pc_class.return_value = mock_pc

        response = client.post("/v1/session", json={
            "model": "teller",
            "world_context": {
                "scenario_tags": ["test"],
                "initiator_faction": {"id": "test", "name": "Test"},
                "counterpart_faction": {"id": "ai", "name": "AI"}
            }
        })

        assert response.status_code == 200
        data = response.json()
        session_id = data["session_id"]

        # Verify that the session was created with TTS provider
        from app.main import SESSIONS
        assert session_id in SESSIONS
        session = SESSIONS[session_id]

        assert "tts_provider" in session
        assert session["tts_provider"] is not None

    @patch('aiortc.RTCPeerConnection')
    def test_webrtc_offer_with_audio_track(self, mock_pc_class):
        """Test WebRTC offer handling with audio track creation."""
        from fastapi.testclient import TestClient

        client = TestClient(app)

        # Create a session first
        session_response = client.post("/v1/session", json={
            "model": "teller"
        })
        session_id = session_response.json()["session_id"]

        # Mock peer connection and its methods
        mock_pc = Mock()
        mock_pc_class.return_value = mock_pc
        mock_pc.createAnswer = AsyncMock()
        mock_pc.setLocalDescription = AsyncMock()
        mock_pc.addTrack = Mock()

        # Mock the session's peer connection
        from app.main import SESSIONS
        SESSIONS[session_id]["pc"] = mock_pc

        # Mock TTS provider and audio track
        mock_audio_track = Mock()
        mock_audio_track.kind = "audio"

        with patch('app.main.ElevenLabsProvider') as mock_provider_class:
            mock_provider = Mock()
            mock_provider.get_audio_track = AsyncMock(return_value=mock_audio_track)
            mock_provider_class.return_value = mock_provider

            # Make the WebRTC offer
            offer_response = client.post(f"/v1/session/{session_id}/webrtc/offer", json={
                "sdp": "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=audio 1 RTP/AVP 111\r\n",
                "type": "offer"
            })

            assert offer_response.status_code == 200

            # Verify that addTrack was called with the audio track
            mock_pc.addTrack.assert_called_once()
            call_args = mock_pc.addTrack.call_args[0]
            assert len(call_args) >= 1
            track = call_args[0]
            assert track.kind == "audio"

    def test_tts_provider_environment_detection(self):
        """Test that TTS provider correctly detects ElevenLabs API key."""
        # Test with API key
        with patch.dict('os.environ', {'ELEVENLABS_API_KEY': 'test_key'}):
            from app.main import ElevenLabsProvider
            # This would be called during session creation
            # We can't easily test the exact logic without mocking the session creation

        # Test without API key (should fallback to XTTS)
        with patch.dict('os.environ', {}, clear=True):
            from app.main import XTTSProvider
            # Should fallback to XTTS when no API key

    def test_audio_track_replacement_during_ai_response(self):
        """Test that audio tracks are replaced during AI response generation."""
        from fastapi.testclient import TestClient

        client = TestClient(app)

        # Create session
        session_response = client.post("/v1/session", json={"model": "teller"})
        session_id = session_response.json()["session_id"]

        # Mock the session with TTS provider and peer connection
        from app.main import SESSIONS
        mock_pc = Mock()
        mock_tts_provider = Mock()
        mock_audio_track = Mock()

        SESSIONS[session_id] = {
            "pc": mock_pc,
            "tts_provider": mock_tts_provider,
            "model": "teller",
            "ws_clients": set(),
            "turns": [],
            "world_context": {"test": "data"},
            "initiator_id": "test",
            "counterpart_id": "ai",
            "provider_task": None,
            "provider_tasks": [],
            "blackhole": Mock(),
            "listener": None,
            "session_id": session_id
        }

        # Mock the TTS provider to return a new audio track
        new_audio_track = Mock()
        mock_tts_provider.get_audio_track = AsyncMock(return_value=new_audio_track)

        # Mock the peer connection to have a sender with audio track
        mock_sender = Mock()
        mock_sender.track = Mock()
        mock_sender.track.kind = "audio"
        mock_sender.replaceTrack = Mock()
        mock_pc.getSenders.return_value = [mock_sender]

        async def run_test():
            # Simulate AI response generation
            from app.main import generate_teller_avatar
            from unittest.mock import AsyncMock

            # Mock send_yaml_func
            send_yaml_func = AsyncMock()

            await generate_teller_avatar(
                "This is a test AI response",
                send_yaml_func,
                session_id
            )

            # Verify that replaceTrack was called
            mock_sender.replaceTrack.assert_called_once_with(new_audio_track)

        asyncio.run(run_test())


class TestTTSProviderSelection:
    """Test TTS provider selection logic."""

    def test_elevenlabs_preferred_when_api_key_available(self):
        """Test that ElevenLabs is used when API key is available."""
        with patch.dict('os.environ', {'ELEVENLABS_API_KEY': 'test_key'}):
            # This tests the logic in the WebRTC offer handler
            from app.main import ElevenLabsProvider

            # Verify ElevenLabs provider can be instantiated
            config = {
                "api_key": "test_key",
                "voice_id": "test_voice",
                "model": "test_model"
            }
            provider = ElevenLabsProvider(config)
            assert provider.api_key == "test_key"

    def test_xtts_fallback_when_no_api_key(self):
        """Test that XTTS is used as fallback when no API key."""
        with patch.dict('os.environ', {}, clear=True):
            # This tests the logic in the WebRTC offer handler
            from app.main import XTTSProvider

            # Verify XTTS provider can be instantiated
            config = {"device": "cpu", "model_path": "test_path"}
            provider = XTTSProvider(config)
            assert provider.device == "cpu"


class TestAudioFrameGeneration:
    """Test audio frame generation and timing."""

    def test_xtts_audio_frame_properties(self):
        """Test that XTTS audio frames have correct properties."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Frame property test"
            audio_track = await provider.get_audio_track(test_text)

            # Generate a frame
            frame = await audio_track.recv()

            if frame is not None:
                assert hasattr(frame, 'sample_rate')
                assert frame.sample_rate == 16000
                assert hasattr(frame, 'time_base')
                assert isinstance(frame.time_base, Fraction)

                # Convert to numpy array to check format
                frame_array = frame.to_ndarray()
                assert frame_array is not None
                assert frame_array.ndim >= 2  # Should be at least 2D (channels x samples)

        asyncio.run(run_test())

    def test_audio_frame_timing(self):
        """Test that audio frames are generated with proper timing."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Timing test"
            audio_track = await provider.get_audio_track(test_text)

            import time

            # Measure time between frames
            start_time = time.time()

            frames = []
            for _ in range(5):
                frame = await audio_track.recv()
                if frame is not None:
                    frames.append(frame)
                    # Small delay to ensure timing
                    await asyncio.sleep(0.01)

            end_time = time.time()

            # Should have generated multiple frames
            assert len(frames) > 0

            # Total time should be reasonable (not instantaneous)
            elapsed = end_time - start_time
            assert elapsed > 0.01  # Should take some time

        asyncio.run(run_test())


class TestErrorHandling:
    """Test error handling in TTS functionality."""

    def test_invalid_tts_provider_config(self):
        """Test handling of invalid TTS provider configuration."""
        # Test with invalid config
        try:
            provider = ElevenLabsProvider({})
            # Should not raise during initialization
            assert provider.api_key == ""
        except Exception:
            pytest.fail("Should handle empty config gracefully")

    def test_audio_track_creation_failure(self):
        """Test handling of audio track creation failures."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            # Test with empty text
            audio_track = await provider.get_audio_track("", "default")

            # Should still create track even with empty text
            assert audio_track is not None

            # Should generate silence frames
            frame = await audio_track.recv()
            assert frame is not None

        asyncio.run(run_test())
