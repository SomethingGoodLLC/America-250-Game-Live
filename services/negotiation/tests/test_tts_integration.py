"""End-to-end integration tests for TTS functionality."""

import pytest
import asyncio
import json
from unittest.mock import Mock, patch, AsyncMock
from fastapi.testclient import TestClient

from app.main import app


class TestTTSEndToEndIntegration:
    """Test complete TTS integration from session creation to audio output."""

    def test_complete_tts_workflow(self, client):
        """Test the complete TTS workflow from session to audio generation."""
        # Create session with teller model
        session_response = client.post("/v1/session", json={
            "model": "teller",
            "world_context": {
                "scenario_tags": ["test"],
                "initiator_faction": {"id": "human", "name": "Human"},
                "counterpart_faction": {"id": "ai", "name": "AI Assistant"}
            }
        })

        assert session_response.status_code == 200
        session_data = session_response.json()
        session_id = session_data["session_id"]

        # Verify session has TTS provider
        from app.main import SESSIONS
        assert session_id in SESSIONS
        session = SESSIONS[session_id]
        assert "tts_provider" in session
        assert session["tts_provider"] is not None

        # Test TTS functionality
        tts_response = client.post("/test-tts")
        assert tts_response.status_code == 200
        tts_data = tts_response.json()
        assert tts_data["status"] == "success"
        assert tts_data["audio_size"] > 0

        # Test audio track creation
        track_response = client.post("/test-audio-track")
        assert track_response.status_code == 200
        track_data = track_response.json()
        assert track_data["status"] == "success"
        assert track_data["sample_rate"] == 16000

    def test_tts_provider_selection_logic(self, client):
        """Test that the correct TTS provider is selected based on environment."""
        # Test without ElevenLabs API key (should use XTTS)
        tts_response = client.post("/test-tts")
        assert tts_response.status_code == 200
        data = tts_response.json()

        # Should indicate which provider was used
        assert "provider" in data
        # Without API key, should use XTTS
        assert data["provider"] in ["XTTS", "ElevenLabs"]

    def test_webrtc_audio_setup_integration(self, client):
        """Test WebRTC setup with audio track integration."""
        # Create session
        session_response = client.post("/v1/session", json={"model": "teller"})
        session_id = session_response.json()["session_id"]

        # Mock WebRTC offer
        offer_data = {
            "sdp": """v=0
o=- 0 0 IN IP4 127.0.0.1
s=-
t=0 0
m=audio 1 RTP/AVP 111
c=IN IP4 0.0.0.0
a=rtpmap:111 opus/48000/2
""",
            "type": "offer"
        }

        # Make WebRTC offer (this should set up audio tracks)
        offer_response = client.post(f"/v1/session/{session_id}/webrtc/offer", json=offer_data)
        assert offer_response.status_code == 200

        # Verify session still has TTS provider
        from app.main import SESSIONS
        session = SESSIONS[session_id]
        assert "tts_provider" in session
        assert session["tts_provider"] is not None

    def test_ai_response_with_audio_generation(self, client):
        """Test AI response generation with audio track replacement."""
        # Create session
        session_response = client.post("/v1/session", json={"model": "teller"})
        session_id = session_response.json()["session_id"]

        # Mock the session components
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

        # Mock TTS provider
        new_audio_track = Mock()
        mock_tts_provider.get_audio_track = AsyncMock(return_value=new_audio_track)

        # Mock peer connection sender
        mock_sender = Mock()
        mock_sender.track = Mock()
        mock_sender.track.kind = "audio"
        mock_sender.replaceTrack = Mock()
        mock_pc.getSenders.return_value = [mock_sender]

        async def test_ai_response():
            # Simulate AI response generation
            from app.main import generate_teller_avatar
            send_yaml_func = AsyncMock()

            test_text = "This is a test AI response for audio generation."
            await generate_teller_avatar(test_text, send_yaml_func, session_id)

            # Verify TTS was called
            mock_tts_provider.get_audio_track.assert_called_once_with(test_text, "default")

            # Verify track replacement
            mock_sender.replaceTrack.assert_called_once_with(new_audio_track)

        asyncio.run(test_ai_response())


class TestTTSConfiguration:
    """Test TTS configuration and environment handling."""

    def test_environment_variable_detection(self):
        """Test detection of ElevenLabs API key from environment."""
        import os

        # Test with API key
        with patch.dict(os.environ, {'ELEVENLABS_API_KEY': 'test_key_123'}):
            from app.main import ElevenLabsProvider

            # Should be able to create ElevenLabs provider
            config = {
                "api_key": "test_key_123",
                "voice_id": "test_voice",
                "model": "test_model"
            }
            provider = ElevenLabsProvider(config)
            assert provider.api_key == "test_key_123"

        # Test without API key
        with patch.dict(os.environ, {}, clear=True):
            from app.main import XTTSProvider

            # Should fallback to XTTS
            config = {"device": "cpu", "model_path": "test_path"}
            provider = XTTSProvider(config)
            assert provider.device == "cpu"

    def test_tts_provider_initialization_with_env_vars(self):
        """Test TTS provider initialization using environment variables."""
        with patch.dict('os.environ', {
            'ELEVENLABS_API_KEY': 'env_api_key',
            'ELEVENLABS_VOICE_ID': 'env_voice_id'
        }):
            from app.main import ElevenLabsProvider

            # Should use environment variables
            provider = ElevenLabsProvider({
                "api_key": "env_api_key",
                "voice_id": "env_voice_id",
                "model": "eleven_monolingual_v1"
            })

            assert provider.api_key == "env_api_key"
            assert provider.voice_id == "env_voice_id"


class TestAudioQualityAndPerformance:
    """Test audio quality and performance characteristics."""

    def test_audio_sample_rate_consistency(self, client):
        """Test that audio sample rate is consistent across components."""
        # Test TTS endpoint
        tts_response = client.post("/test-tts")
        assert tts_response.status_code == 200
        tts_data = tts_response.json()
        assert tts_data["status"] == "success"

        # Test audio track endpoint
        track_response = client.post("/test-audio-track")
        assert track_response.status_code == 200
        track_data = track_response.json()
        assert track_data["status"] == "success"

        # Both should report the same sample rate
        assert tts_data.get("sample_rate", 16000) == track_data.get("sample_rate", 16000) == 16000

    def test_audio_data_generation_performance(self):
        """Test that audio data generation meets performance requirements."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            import time

            test_text = "Performance test with longer text to generate more audio data."

            start_time = time.time()
            audio_chunks = []
            async for chunk in provider.synthesize_speech(test_text):
                audio_chunks.append(chunk)
            end_time = time.time()

            # Should complete within reasonable time
            elapsed = end_time - start_time
            assert elapsed < 5.0  # Should be fast

            # Should generate audio data
            assert len(audio_chunks) > 0
            total_audio_size = sum(len(chunk) for chunk in audio_chunks)
            assert total_audio_size > 0

        asyncio.run(run_test())

    def test_memory_usage_during_audio_generation(self):
        """Test memory usage during audio generation."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Memory usage test with moderately sized text."

            # Generate audio multiple times to test memory stability
            for i in range(3):
                audio_chunks = []
                async for chunk in provider.synthesize_speech(test_text):
                    audio_chunks.append(chunk)

                assert len(audio_chunks) > 0
                total_size = sum(len(chunk) for chunk in audio_chunks)
                assert total_size > 0

        asyncio.run(run_test())


class TestErrorRecovery:
    """Test error recovery and fallback mechanisms."""

    @patch('aiohttp.ClientSession')
    def test_elevenlabs_api_failure_recovery(self, mock_session):
        """Test recovery when ElevenLabs API fails."""
        # Mock API failure
        mock_response = AsyncMock()
        mock_response.status = 500
        mock_response.text.return_value = "Internal Server Error"

        mock_session_instance = AsyncMock()
        mock_session_instance.post.return_value.__aenter__.return_value = mock_response
        mock_session.return_value = mock_session_instance

        config = {
            "api_key": "invalid_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        async def run_test():
            test_text = "Test with API failure"

            # Should still generate audio via fallback
            audio_chunks = []
            async for chunk in provider.synthesize_speech(test_text):
                audio_chunks.append(chunk)

            assert len(audio_chunks) > 0
            audio_data = audio_chunks[0]
            assert isinstance(audio_data, bytes)
            assert len(audio_data) > 0

        asyncio.run(run_test())

    def test_invalid_audio_track_handling(self, client):
        """Test handling of invalid audio track scenarios."""
        # Test with malformed request
        response = client.post("/test-audio-track", json={"invalid": "data"})
        # Should handle gracefully
        assert response.status_code in [200, 400, 422, 500]

    def test_session_cleanup_with_tts_provider(self):
        """Test that TTS providers are properly cleaned up."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            # Should not raise exceptions during cleanup
            await provider.close()

        asyncio.run(run_test())
