"""Tests for video source implementations."""

import asyncio
import os
import tempfile
from pathlib import Path
from unittest.mock import patch, MagicMock, AsyncMock
import pytest

try:
    import numpy as np
except ImportError:
    np = None

from providers.video_sources.base import BaseVideoSource, VideoFrame, AvatarVideoTrack
from providers.video_sources.placeholder_loop import PlaceholderLoopVideoSource
from providers.video_sources.veo3_stream import Veo3StreamVideoSource
from providers.video_sources import create_video_source
from providers.types import VideoSourceConfig


class TestVideoFrame:
    """Test the VideoFrame dataclass."""

    def test_video_frame_creation(self):
        """Test creating a VideoFrame instance."""
        frame = VideoFrame(
            data=b"test_data",
            timestamp=123.45,
            width=640,
            height=480,
            format="rgb24"
        )

        assert frame.data == b"test_data"
        assert frame.timestamp == 123.45
        assert frame.width == 640
        assert frame.height == 480
        assert frame.format == "rgb24"

    def test_video_frame_defaults(self):
        """Test VideoFrame with default format."""
        frame = VideoFrame(
            data=b"test_data",
            timestamp=123.45,
            width=640,
            height=480
        )

        assert frame.format == "rgb24"  # Default format


class TestPlaceholderLoopVideoSource:
    """Test the PlaceholderLoopVideoSource implementation."""

    @pytest.fixture
    def config(self):
        """Create a test configuration."""
        return VideoSourceConfig(
            source_type="placeholder",
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

    @pytest.fixture
    def video_source(self, config):
        """Create a PlaceholderLoopVideoSource instance."""
        return PlaceholderLoopVideoSource(config)

    def test_initialization(self, config):
        """Test video source initialization."""
        source = PlaceholderLoopVideoSource(config)

        assert source.config == config
        assert not source.is_running()
        assert source.get_frame_count() == 0
        assert source.video_path.endswith("loop.mp4")
        assert source.assets_dir.exists()

    @pytest.mark.asyncio
    async def test_start_stop(self, video_source):
        """Test starting and stopping the video source."""
        assert not video_source.is_running()

        await video_source.start()
        assert video_source.is_running()

        await video_source.stop()
        assert not video_source.is_running()

    @pytest.mark.asyncio
    async def test_synthetic_frame_generation(self, video_source):
        """Test synthetic frame generation when video file is not available."""
        # Mock av as None to force synthetic fallback
        with patch('providers.video_sources.placeholder_loop.av', None):
            await video_source.start()

            frame = await video_source.get_frame()
            assert frame is not None
            assert isinstance(frame, VideoFrame)
            assert frame.width == 320
            assert frame.height == 240
            assert frame.format == "rgb24"

            # Test frames() method
            frame_array = None
            async for frame_array in video_source.frames():
                break

            if np is not None:
                assert frame_array is not None
                assert isinstance(frame_array, np.ndarray)
                assert frame_array.shape == (240, 320, 3)
            else:
                assert frame_array is None

    @pytest.mark.asyncio
    async def test_video_file_fallback(self, video_source):
        """Test fallback to synthetic when video file doesn't exist."""
        # Create a non-existent video path
        video_source.video_path = "/nonexistent/path/video.mp4"

        await video_source.start()

        # Should fall back to synthetic generation
        frame = await video_source.get_frame()
        assert frame is not None
        assert isinstance(frame, VideoFrame)
        assert frame.width == 320
        assert frame.height == 240

    @pytest.mark.asyncio
    async def test_stream_frames(self, video_source):
        """Test streaming frames."""
        await video_source.start()

        # Collect a few frames
        frames = []
        async for frame in video_source.stream_frames():
            frames.append(frame)
            if len(frames) >= 3:
                break

        assert len(frames) == 3
        for frame in frames:
            assert isinstance(frame, VideoFrame)
            assert frame.width == 320
            assert frame.height == 240

    def test_wait_for_frame_timeout(self, video_source):
        """Test waiting for frame with timeout."""
        async def test_timeout():
            frame = await video_source.wait_for_frame(timeout_seconds=0.1)
            assert frame is None

        # Should timeout since source is not running
        asyncio.run(test_timeout())


class TestVeo3StreamVideoSource:
    """Test the Veo3StreamVideoSource implementation."""

    @pytest.fixture
    def config(self):
        """Create a test configuration."""
        return VideoSourceConfig(
            source_type="veo3",
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

    @pytest.fixture
    def video_source(self, config):
        """Create a Veo3StreamVideoSource instance."""
        return Veo3StreamVideoSource(config)

    def test_initialization(self, config):
        """Test video source initialization."""
        source = Veo3StreamVideoSource(config)

        assert source.config == config
        assert not source.is_running()
        assert source.get_frame_count() == 0
        assert source.use_veo3 is False  # Default from env
        assert source.api_key is None
        assert source.model_name == "gemini-veo3"

    def test_initialization_with_env_vars(self, config):
        """Test initialization with environment variables set."""
        env_vars = {
            "USE_VEO3": "1",
            "GEMINI_API_KEY": "test_key",
            "GOOGLE_CLOUD_PROJECT": "test_project",
            "VEO3_PROMPT_STYLE": "formal",
            "VEO3_AVATAR_STYLE": "business",
            "VEO3_LATENCY_TARGET_MS": "200"
        }

        with patch.dict(os.environ, env_vars):
            source = Veo3StreamVideoSource(config)

            assert source.use_veo3 is True
            assert source.api_key == "test_key"
            assert source.project_id == "test_project"
            assert source.prompt_style == "formal"
            assert source.avatar_style == "business"
            assert source.latency_target_ms == 200

    @pytest.mark.asyncio
    async def test_start_mock_mode(self, video_source):
        """Test starting in mock mode."""
        await video_source.start()

        assert video_source.is_running()
        assert video_source._session_active
        assert len(video_source._mock_frame_buffer) > 0

    @pytest.mark.asyncio
    async def test_start_veo3_mode(self, video_source):
        """Test starting in Veo3 mode."""
        with patch.dict(os.environ, {"USE_VEO3": "1"}):
            # Reinitialize to pick up env var
            video_source = Veo3StreamVideoSource(video_source.config)

            with pytest.raises(NotImplementedError, match="Wire Veo3 SDK here"):
                await video_source.start()

    @pytest.mark.asyncio
    async def test_mock_frame_generation(self, video_source):
        """Test mock frame generation."""
        await video_source.start()

        frame = await video_source.get_frame()
        assert frame is not None
        assert isinstance(frame, VideoFrame)
        assert frame.width == 320
        assert frame.height == 240

        # Test frames() method
        frame_arrays = []
        async for frame_array in video_source.frames():
            frame_arrays.append(frame_array)
            if len(frame_arrays) >= 2:
                break

        assert len(frame_arrays) == 2
        for frame_array in frame_arrays:
            if np is not None:
                assert isinstance(frame_array, np.ndarray)
                assert frame_array.shape == (240, 320, 3)

    @pytest.mark.asyncio
    async def test_update_dialogue_context(self, video_source):
        """Test updating dialogue context."""
        await video_source.start()

        context = {
            "speaker": "A",
            "intent": "proposal",
            "emotion": "confident"
        }

        # Should not raise an error
        await video_source.update_dialogue_context(context)
        assert video_source._last_dialogue_context == str(context)


class TestVideoSourceFactory:
    """Test the video source factory function."""

    @pytest.fixture
    def config(self):
        """Create a test configuration."""
        return VideoSourceConfig(
            source_type="placeholder",
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

    def test_create_placeholder_source(self, config):
        """Test creating a placeholder video source."""
        source = create_video_source(config)

        assert isinstance(source, PlaceholderLoopVideoSource)
        assert source.config == config

    def test_create_veo3_source(self):
        """Test creating a Veo3 video source."""
        config = VideoSourceConfig(
            source_type="veo3",
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

        source = create_video_source(config)

        assert isinstance(source, Veo3StreamVideoSource)
        assert source.config == config

    def test_create_veo3_source_with_env_override(self):
        """Test Veo3 source creation with environment override."""
        config = VideoSourceConfig(
            source_type="placeholder",  # Config says placeholder
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

        with patch.dict(os.environ, {"DEFAULT_VIDEO_SOURCE": "veo3"}):
            source = create_video_source(config)

            assert isinstance(source, Veo3StreamVideoSource)

    def test_veo3_fallback_to_placeholder(self):
        """Test Veo3 source falls back to placeholder when USE_VEO3=0."""
        config = VideoSourceConfig(
            source_type="veo3",
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

        with patch.dict(os.environ, {"USE_VEO3": "0"}):
            source = create_video_source(config)

            assert isinstance(source, PlaceholderLoopVideoSource)

    def test_unknown_source_type_fallback(self):
        """Test unknown source type falls back to placeholder."""
        config = VideoSourceConfig(
            source_type="unknown_type",
            avatar_style="diplomatic",
            resolution=(320, 240),
            framerate=30,
            quality="medium"
        )

        source = create_video_source(config)

        assert isinstance(source, PlaceholderLoopVideoSource)


class TestAvatarVideoTrack:
    """Test the AvatarVideoTrack implementation."""

    @pytest.fixture
    def mock_video_source(self):
        """Create a mock video source."""
        source = MagicMock(spec=BaseVideoSource)
        source.config.resolution = (320, 240)
        source.config.framerate = 30
        source.get_frame_count.return_value = 0
        source.get_frame = AsyncMock()
        source.frames = AsyncMock()
        return source

    def test_initialization(self, mock_video_source):
        """Test AvatarVideoTrack initialization."""
        track = AvatarVideoTrack(mock_video_source)

        assert track.video_source == mock_video_source

    @pytest.mark.asyncio
    async def test_recv_with_video_frame(self, mock_video_source):
        """Test receiving frames with video frame data."""
        # Mock frame data
        mock_frame = VideoFrame(
            data=b"test_frame_data",
            timestamp=123.45,
            width=320,
            height=240,
            format="rgb24"
        )

        mock_video_source.get_frame.return_value = mock_frame

        track = AvatarVideoTrack(mock_video_source)
        av_frame = await track.recv()

        assert av_frame is not None
        mock_video_source.get_frame.assert_called_once()

    @pytest.mark.asyncio
    async def test_recv_no_frame(self, mock_video_source):
        """Test receiving when no frame is available."""
        mock_video_source.get_frame.return_value = None

        track = AvatarVideoTrack(mock_video_source)
        av_frame = await track.recv()

        assert av_frame is not None
        assert av_frame.width == 320
        assert av_frame.height == 240

    @pytest.mark.asyncio
    async def test_recv_error_handling(self, mock_video_source):
        """Test error handling in recv method."""
        mock_video_source.get_frame.side_effect = Exception("Test error")

        track = AvatarVideoTrack(mock_video_source)
        av_frame = await track.recv()

        assert av_frame is not None
        assert av_frame.width == 1
        assert av_frame.height == 1
