"""Tests for TTS (Text-to-Speech) providers and functionality."""

import pytest
import asyncio
from unittest.mock import Mock, patch, AsyncMock
import numpy as np
from fractions import Fraction

from tts.elevenlabs import ElevenLabsProvider, ElevenLabsAudioTrack
from tts.xtts import XTTSProvider, TTSGeneratedAudioTrack


class TestXTTSProvider:
    """Test the XTTS TTS provider."""

    def test_xtts_provider_initialization(self):
        """Test XTTS provider initialization."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        assert provider.device == "cpu"
        assert provider.model_path == "test_path"
        assert provider.sample_rate == 16000
        assert provider.channels == 1

    def test_xtts_synthesize_speech(self):
        """Test XTTS speech synthesis."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        test_text = "Hello, this is a test."

        async def run_test():
            audio_chunks = []
            async for chunk in provider.synthesize_speech(test_text):
                audio_chunks.append(chunk)

            # Should yield exactly one chunk
            assert len(audio_chunks) == 1
            audio_data = audio_chunks[0]

            # Should be bytes
            assert isinstance(audio_data, bytes)
            # Should be non-empty
            assert len(audio_data) > 0

            # Should be 16-bit PCM audio
            samples = np.frombuffer(audio_data, dtype=np.int16)
            assert len(samples) > 0

        asyncio.run(run_test())

    def test_xtts_get_audio_track(self):
        """Test XTTS audio track creation."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Test audio track"
            audio_track = await provider.get_audio_track(test_text)

            assert audio_track is not None
            assert isinstance(audio_track, TTSGeneratedAudioTrack)
            assert audio_track.text == test_text
            assert audio_track.sample_rate == 16000

        asyncio.run(run_test())

    def test_xtts_provider_close(self):
        """Test XTTS provider cleanup."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            # Should not raise any exceptions
            await provider.close()

        asyncio.run(run_test())


class TestElevenLabsProvider:
    """Test the ElevenLabs TTS provider."""

    def test_elevenlabs_provider_initialization(self):
        """Test ElevenLabs provider initialization."""
        config = {
            "api_key": "test_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        assert provider.api_key == "test_key"
        assert provider.voice_id == "test_voice"
        assert provider.model == "test_model"
        assert provider.api_url == "https://api.elevenlabs.io/v1/text-to-speech"

    @patch('aiohttp.ClientSession')
    def test_elevenlabs_synthesize_speech_success(self, mock_session):
        """Test successful ElevenLabs speech synthesis."""
        # Mock successful API response
        mock_response = AsyncMock()
        mock_response.status = 200
        mock_response.read.return_value = b"mock_audio_data"

        mock_session_instance = AsyncMock()
        mock_session_instance.post.return_value = mock_response
        mock_session.return_value = mock_session_instance

        config = {
            "api_key": "test_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        async def run_test():
            test_text = "Hello from ElevenLabs"
            audio_chunks = []
            async for chunk in provider.synthesize_speech(test_text):
                audio_chunks.append(chunk)

            assert len(audio_chunks) == 1
            audio_data = audio_chunks[0]
            assert audio_data == b"mock_audio_data"

        asyncio.run(run_test())

    @patch('aiohttp.ClientSession')
    def test_elevenlabs_synthesize_speech_api_error(self, mock_session):
        """Test ElevenLabs API error handling."""
        # Mock API error response
        mock_response = AsyncMock()
        mock_response.status = 401
        mock_response.text.return_value = "Unauthorized"

        mock_session_instance = AsyncMock()
        mock_session_instance.post.return_value = mock_response
        mock_session.return_value = mock_session_instance

        config = {
            "api_key": "invalid_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        async def run_test():
            test_text = "Test with invalid key"
            audio_chunks = []
            async for chunk in provider.synthesize_speech(test_text):
                audio_chunks.append(chunk)

            # Should still yield audio data (fallback)
            assert len(audio_chunks) == 1
            audio_data = audio_chunks[0]
            assert isinstance(audio_data, bytes)
            assert len(audio_data) > 0

        asyncio.run(run_test())

    def test_elevenlabs_get_audio_track(self):
        """Test ElevenLabs audio track creation."""
        config = {
            "api_key": "test_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        async def run_test():
            test_text = "Test ElevenLabs audio track"
            audio_track = await provider.get_audio_track(test_text)

            assert audio_track is not None
            assert isinstance(audio_track, ElevenLabsAudioTrack)
            assert audio_track.text == test_text
            assert audio_track.sample_rate == 16000

        asyncio.run(run_test())


class TestTTSGeneratedAudioTrack:
    """Test the TTS-generated audio track."""

    def test_audio_track_initialization(self):
        """Test audio track initialization."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Test track"
            audio_track = await provider.get_audio_track(test_text)

            assert audio_track.text == test_text
            assert audio_track.sample_rate == 16000
            assert audio_track.frame_size == 1024
            assert audio_track.frame_duration == 1.0 / 30.0

        asyncio.run(run_test())

    def test_audio_track_frame_generation(self):
        """Test audio frame generation."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Test frame generation"
            audio_track = await provider.get_audio_track(test_text)

            # Generate a few frames
            frames = []
            for _ in range(3):
                frame = await audio_track.recv()
                if frame is not None:
                    frames.append(frame)

            # Should generate at least one frame
            assert len(frames) > 0

            # Check frame properties
            frame = frames[0]
            assert hasattr(frame, 'sample_rate')
            assert frame.sample_rate == 16000
            assert hasattr(frame, 'time_base')
            assert isinstance(frame.time_base, Fraction)

        asyncio.run(run_test())

    def test_audio_track_end_of_stream(self):
        """Test audio track end-of-stream behavior."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Short test"
            audio_track = await provider.get_audio_track(test_text)

            # Generate frames until end of stream
            frames = []
            for _ in range(10):  # Generate more frames than audio data
                frame = await audio_track.recv()
                frames.append(frame)
                if frame is not None:
                    # Check if frame is valid (not None)
                    samples = np.frombuffer(frame.to_ndarray().tobytes(), dtype=np.int16)
                    assert len(samples) > 0

            # Should generate at least some frames
            assert len(frames) > 0

        asyncio.run(run_test())


class TestElevenLabsAudioTrack:
    """Test the ElevenLabs audio track."""

    @patch('aiohttp.ClientSession')
    def test_elevenlabs_audio_track_success(self, mock_session):
        """Test ElevenLabs audio track with successful API response."""
        # Mock successful API response
        mock_response = AsyncMock()
        mock_response.status = 200
        mock_response.read.return_value = b"mock_mp3_audio_data"

        mock_session_instance = AsyncMock()
        mock_session_instance.post.return_value.__aenter__.return_value = mock_response
        mock_session.return_value = mock_session_instance

        config = {
            "api_key": "test_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        async def run_test():
            test_text = "ElevenLabs test track"
            audio_track = await provider.get_audio_track(test_text)

            assert audio_track is not None
            assert isinstance(audio_track, ElevenLabsAudioTrack)

            # Test frame generation
            frame = await audio_track.recv()
            assert frame is not None

        asyncio.run(run_test())

    def test_elevenlabs_audio_track_api_failure(self):
        """Test ElevenLabs audio track with API failure."""
        config = {
            "api_key": "invalid_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        provider = ElevenLabsProvider(config)

        async def run_test():
            test_text = "Test with API failure"
            audio_track = await provider.get_audio_track(test_text)

            # Should still work with fallback
            frame = await audio_track.recv()
            assert frame is not None

        asyncio.run(run_test())


class TestTTSIntegration:
    """Test TTS integration with the main application."""

    def test_health_endpoint_exists(self, client):
        """Test that health endpoint exists."""
        response = client.get("/health")
        assert response.status_code == 200

    def test_tts_provider_initialization(self):
        """Test TTS provider initialization without client."""
        # Test XTTS provider
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)
        assert provider.device == "cpu"

        # Test ElevenLabs provider
        eleven_config = {
            "api_key": "test_key",
            "voice_id": "test_voice",
            "model": "test_model"
        }
        eleven_provider = ElevenLabsProvider(eleven_config)
        assert eleven_provider.api_key == "test_key"

    def test_basic_audio_generation(self):
        """Test basic audio generation functionality."""
        config = {"device": "cpu", "model_path": "test_path"}
        provider = XTTSProvider(config)

        async def run_test():
            test_text = "Basic audio test"

            # Generate audio
            audio_chunks = []
            async for chunk in provider.synthesize_speech(test_text):
                audio_chunks.append(chunk)

            assert len(audio_chunks) == 1
            audio_data = audio_chunks[0]

            # Verify audio data
            assert isinstance(audio_data, bytes)
            assert len(audio_data) > 0

            # Verify it's 16-bit PCM
            import struct
            # Check first few samples
            for i in range(min(10, len(audio_data) // 2)):
                sample_bytes = audio_data[i*2:(i+1)*2]
                sample = struct.unpack('<h', sample_bytes)[0]
                assert -32768 <= sample <= 32767

        asyncio.run(run_test())
