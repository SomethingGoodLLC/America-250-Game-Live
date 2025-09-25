"""WebRTC track publishing utilities for negotiation providers."""

import asyncio
from typing import Optional, Dict, Any, List
from dataclasses import dataclass
import structlog

from aiortc import RTCPeerConnection, MediaStreamTrack, RTCDataChannel
from av import VideoFrame
from aiortc.mediastreams import VideoStreamTrack, AudioStreamTrack

from core.webrtc_manager import WebRTCManager
from providers.video_sources.base import AvatarVideoTrack
from providers.types import VideoSourceConfig


@dataclass
class TrackPublication:
    """Information about a published track."""
    track: MediaStreamTrack
    kind: str  # "video" or "audio"
    source: str  # "avatar", "tts", "provider"
    metadata: Optional[Dict[str, Any]] = None
    is_active: bool = True


class TrackPublisher:
    """Helper class for publishing media tracks to WebRTC peer connections.

    This class manages the publication of video and audio tracks from various
    sources (avatar generation, TTS, etc.) to WebRTC peer connections.
    """

    def __init__(self, webrtc_manager: WebRTCManager):
        self.webrtc_manager = webrtc_manager
        self.logger = structlog.get_logger(__name__)
        self._published_tracks: Dict[str, TrackPublication] = {}
        self._session_tracks: Dict[str, List[str]] = {}  # session_id -> track_ids

    async def publish_avatar_video(
        self,
        session_id: str,
        video_source_config: VideoSourceConfig,
        avatar_style: str = "diplomatic"
    ) -> str:
        """Publish avatar video track for a session.

        Args:
            session_id: Session identifier
            video_source_config: Configuration for the video source
            avatar_style: Style of avatar to generate

        Returns:
            Track ID for the published video
        """
        try:
            # Import here to avoid circular imports
            from providers.video_sources.base import BaseVideoSource
            from providers.video_sources.placeholder_loop import PlaceholderLoopVideoSource

            # Create video source based on configuration
            if video_source_config.source_type == "placeholder":
                video_source = PlaceholderLoopVideoSource(video_source_config)
            elif video_source_config.source_type == "veo3":
                # TODO: Import Veo3 source when implemented
                video_source = PlaceholderLoopVideoSource(video_source_config)
            else:
                video_source = PlaceholderLoopVideoSource(video_source_config)

            # Start the video source
            await video_source.start()

            # Create WebRTC-compatible track
            avatar_track = AvatarVideoTrack(video_source)

            # Add track to peer connection
            await self.webrtc_manager.send_video_track(session_id, avatar_track)

            # Track publication info
            track_id = f"avatar_video_{session_id}_{len(self._published_tracks)}"
            publication = TrackPublication(
                track=avatar_track,
                kind="video",
                source="avatar",
                metadata={
                    "avatar_style": avatar_style,
                    "resolution": video_source_config.resolution,
                    "framerate": video_source_config.framerate
                }
            )

            self._published_tracks[track_id] = publication

            # Associate track with session
            if session_id not in self._session_tracks:
                self._session_tracks[session_id] = []
            self._session_tracks[session_id].append(track_id)

            self.logger.info(
                "Published avatar video track",
                session_id=session_id,
                track_id=track_id,
                avatar_style=avatar_style
            )

            return track_id

        except Exception as e:
            self.logger.error(
                "Failed to publish avatar video",
                session_id=session_id,
                error=str(e)
            )
            raise

    async def publish_tts_audio(
        self,
        session_id: str,
        tts_provider,
        voice_id: str = "default"
    ) -> str:
        """Publish TTS audio track for a session.

        Args:
            session_id: Session identifier
            tts_provider: TTS provider instance
            voice_id: Voice identifier

        Returns:
            Track ID for the published audio
        """
        try:
            # Import here to avoid circular imports
            from tts.base import BaseTTSProvider

            if not isinstance(tts_provider, BaseTTSProvider):
                raise ValueError("Invalid TTS provider")

            # Create audio track from TTS provider
            audio_track = await self._create_tts_audio_track(tts_provider, voice_id)

            # Add track to peer connection
            await self.webrtc_manager.send_audio_track(session_id, audio_track)

            # Track publication info
            track_id = f"tts_audio_{session_id}_{len(self._published_tracks)}"
            publication = TrackPublication(
                track=audio_track,
                kind="audio",
                source="tts",
                metadata={
                    "voice_id": voice_id,
                    "provider": tts_provider.__class__.__name__
                }
            )

            self._published_tracks[track_id] = publication

            # Associate track with session
            if session_id not in self._session_tracks:
                self._session_tracks[session_id] = []
            self._session_tracks[session_id].append(track_id)

            self.logger.info(
                "Published TTS audio track",
                session_id=session_id,
                track_id=track_id,
                voice_id=voice_id
            )

            return track_id

        except Exception as e:
            self.logger.error(
                "Failed to publish TTS audio",
                session_id=session_id,
                error=str(e)
            )
            raise

    async def publish_provider_audio(
        self,
        session_id: str,
        audio_data: bytes,
        sample_rate: int = 16000
    ) -> str:
        """Publish audio data from a provider.

        Args:
            session_id: Session identifier
            audio_data: Raw audio bytes
            sample_rate: Audio sample rate

        Returns:
            Track ID for the published audio
        """
        try:
            # Create audio track from raw data
            audio_track = await self._create_audio_track_from_data(audio_data, sample_rate)

            # Add track to peer connection
            await self.webrtc_manager.send_audio_track(session_id, audio_track)

            # Track publication info
            track_id = f"provider_audio_{session_id}_{len(self._published_tracks)}"
            publication = TrackPublication(
                track=audio_track,
                kind="audio",
                source="provider",
                metadata={
                    "sample_rate": sample_rate,
                    "data_length": len(audio_data)
                }
            )

            self._published_tracks[track_id] = publication

            # Associate track with session
            if session_id not in self._session_tracks:
                self._session_tracks[session_id] = []
            self._session_tracks[session_id].append(track_id)

            self.logger.info(
                "Published provider audio track",
                session_id=session_id,
                track_id=track_id,
                sample_rate=sample_rate
            )

            return track_id

        except Exception as e:
            self.logger.error(
                "Failed to publish provider audio",
                session_id=session_id,
                error=str(e)
            )
            raise

    async def unpublish_track(self, track_id: str) -> bool:
        """Unpublish a track.

        Args:
            track_id: ID of the track to unpublish

        Returns:
            True if track was unpublished, False otherwise
        """
        try:
            if track_id not in self._published_tracks:
                return False

            publication = self._published_tracks[track_id]

            # Stop the track
            if hasattr(publication.track, 'stop'):
                await publication.track.stop()

            # Remove from published tracks
            del self._published_tracks[track_id]

            # Remove from session associations
            for session_tracks in self._session_tracks.values():
                if track_id in session_tracks:
                    session_tracks.remove(track_id)

            self.logger.info("Unpublished track", track_id=track_id)
            return True

        except Exception as e:
            self.logger.error("Failed to unpublish track", track_id=track_id, error=str(e))
            return False

    async def unpublish_session_tracks(self, session_id: str) -> int:
        """Unpublish all tracks for a session.

        Args:
            session_id: Session identifier

        Returns:
            Number of tracks unpublished
        """
        if session_id not in self._session_tracks:
            return 0

        track_ids = self._session_tracks[session_id][:]
        unpublished_count = 0

        for track_id in track_ids:
            if await self.unpublish_track(track_id):
                unpublished_count += 1

        # Remove session entry
        del self._session_tracks[session_id]

        self.logger.info(
            "Unpublished session tracks",
            session_id=session_id,
            count=unpublished_count
        )

        return unpublished_count

    async def get_session_tracks(self, session_id: str) -> List[TrackPublication]:
        """Get all tracks published for a session.

        Args:
            session_id: Session identifier

        Returns:
            List of track publications
        """
        if session_id not in self._session_tracks:
            return []

        track_ids = self._session_tracks[session_id]
        return [
            self._published_tracks[track_id]
            for track_id in track_ids
            if track_id in self._published_tracks
        ]

    async def update_track_metadata(
        self,
        track_id: str,
        metadata: Dict[str, Any]
    ) -> bool:
        """Update metadata for a published track.

        Args:
            track_id: ID of the track
            metadata: New metadata

        Returns:
            True if metadata was updated, False otherwise
        """
        if track_id not in self._published_tracks:
            return False

        publication = self._published_tracks[track_id]
        publication.metadata.update(metadata)

        self.logger.info("Updated track metadata", track_id=track_id, metadata=metadata)
        return True

    async def _create_tts_audio_track(self, tts_provider, voice_id: str) -> AudioStreamTrack:
        """Create an audio track from TTS provider."""
        # This is a simplified implementation
        # In a real implementation, this would create a streaming audio track
        # that generates audio from the TTS provider on demand

        class TTSAudioTrack(AudioStreamTrack):
            def __init__(self, tts_provider, voice_id):
                super().__init__()
                self.tts_provider = tts_provider
                self.voice_id = voice_id
                self.sample_rate = 16000

            async def recv(self):
                # TODO: Implement actual TTS audio generation
                # For now, return silence
                import av
                import numpy as np

                # Generate silence frame
                samples = np.zeros(1024, dtype=np.int16)
                audio_frame = av.AudioFrame.from_ndarray(
                    samples.reshape(1, -1),
                    format="s16",
                    layout="mono"
                )
                audio_frame.sample_rate = self.sample_rate
                audio_frame.time_base = av.Fraction(1, self.sample_rate)

                await asyncio.sleep(1024 / self.sample_rate)
                return audio_frame

        return TTSAudioTrack(tts_provider, voice_id)

    async def _create_audio_track_from_data(
        self,
        audio_data: bytes,
        sample_rate: int
    ) -> AudioStreamTrack:
        """Create an audio track from raw audio data."""
        # This is a simplified implementation
        # In a real implementation, this would create a streaming audio track
        # that plays the provided audio data

        class DataAudioTrack(AudioStreamTrack):
            def __init__(self, audio_data, sample_rate):
                super().__init__()
                self.audio_data = audio_data
                self.sample_rate = sample_rate
                self.position = 0

            async def recv(self):
                # TODO: Implement actual audio data streaming
                # For now, return silence
                import av
                import numpy as np

                # Generate silence frame
                samples = np.zeros(1024, dtype=np.int16)
                audio_frame = av.AudioFrame.from_ndarray(
                    samples.reshape(1, -1),
                    format="s16",
                    layout="mono"
                )
                audio_frame.sample_rate = self.sample_rate
                audio_frame.time_base = av.Fraction(1, self.sample_rate)

                await asyncio.sleep(1024 / self.sample_rate)
                return audio_frame

        return DataAudioTrack(audio_data, sample_rate)


class FrameTrack(VideoStreamTrack):
    """WebRTC video track that pulls frames from a VideoSource.

    This class implements the WebRTC VideoStreamTrack interface by
    pulling frames from a VideoSource.frames() async generator and
    converting them to av.VideoFrame objects.
    """

    def __init__(self, video_source):
        """Initialize FrameTrack with a video source.

        Args:
            video_source: Video source that provides frames() async generator
        """
        super().__init__()
        self.video_source = video_source
        self.logger = structlog.get_logger(__name__)
        self._frame_generator = None
        self._is_stopped = False

    async def recv(self):
        """Receive the next video frame.

        Returns:
            av.VideoFrame: Next video frame for WebRTC streaming

        Raises:
            Exception: If frame generation fails or track is stopped
        """
        if self._is_stopped:
            raise Exception("FrameTrack has been stopped")
            
        try:
            # Initialize frame generator if needed
            if self._frame_generator is None:
                self._frame_generator = self.video_source.frames()

            # Get next frame from video source
            try:
                frame_array = await self._frame_generator.__anext__()
            except StopAsyncIteration:
                # Restart generator if exhausted
                self.logger.debug("Video frame generator exhausted, restarting")
                self._frame_generator = self.video_source.frames()
                frame_array = await self._frame_generator.__anext__()

            # Convert numpy array to av.VideoFrame
            import av
            import numpy as np

            # Validate frame array
            if frame_array is None:
                raise ValueError("Received None frame from video source")
                
            if not isinstance(frame_array, np.ndarray):
                raise ValueError(f"Expected numpy array, got {type(frame_array)}")

            # Ensure frame_array is in correct format (HxWxC)
            if len(frame_array.shape) == 3 and frame_array.shape[2] == 3:
                # RGB format
                av_frame = av.VideoFrame.from_ndarray(frame_array, format="rgb24")
            elif len(frame_array.shape) == 3 and frame_array.shape[2] == 1:
                # Grayscale - convert to RGB
                rgb_frame = np.repeat(frame_array, 3, axis=2)
                av_frame = av.VideoFrame.from_ndarray(rgb_frame, format="rgb24")
            elif len(frame_array.shape) == 3 and frame_array.shape[2] == 4:
                # RGBA - drop alpha channel
                rgb_frame = frame_array[:, :, :3]
                av_frame = av.VideoFrame.from_ndarray(rgb_frame, format="rgb24")
            else:
                # Unsupported format - create blank frame
                self.logger.warning(
                    "Unsupported frame format", 
                    shape=frame_array.shape,
                    dtype=str(frame_array.dtype)
                )
                raise ValueError(f"Unsupported frame format: {frame_array.shape}")

            # Set timing information
            av_frame.pts = self.video_source.get_frame_count()
            av_frame.time_base = av.Fraction(1, self.video_source.config.framerate)

            return av_frame

        except Exception as e:
            self.logger.error("Error in FrameTrack.recv", error=str(e))

            # Return a blank frame on error to prevent stream interruption
            import av
            import numpy as np
            
            try:
                # Create a simple colored frame to indicate error
                width, height = self.video_source.config.resolution
                error_array = np.full((height, width, 3), [128, 0, 0], dtype=np.uint8)  # Dark red
                error_frame = av.VideoFrame.from_ndarray(error_array, format="rgb24")
                error_frame.pts = self.video_source.get_frame_count()
                error_frame.time_base = av.Fraction(1, self.video_source.config.framerate)
                return error_frame
            except Exception as fallback_error:
                self.logger.error("Failed to create error frame", error=str(fallback_error))
                # Last resort - create minimal frame
                error_frame = av.VideoFrame(width=320, height=240, format="rgb24")
                error_frame.pts = 0
                error_frame.time_base = av.Fraction(1, 30)
                return error_frame

    async def stop(self):
        """Stop the track gracefully."""
        self._is_stopped = True
        self.logger.info("FrameTrack stopped")


async def attach_avatar_track(
    peer: RTCPeerConnection,
    source: 'VideoSource',
    session_id: str = None
) -> FrameTrack:
    """Attach an avatar video track to a WebRTC peer connection.

    Args:
        peer: WebRTC peer connection to attach track to
        source: Video source providing frames
        session_id: Optional session identifier for logging

    Returns:
        FrameTrack: The attached video track

    Raises:
        Exception: If track attachment fails
    """
    logger = structlog.get_logger(__name__)

    try:
        # Create FrameTrack from video source
        frame_track = FrameTrack(source)

        # Add track to peer connection
        sender = peer.addTrack(frame_track)

        # Configure sender (optional)
        if hasattr(sender, 'setParameters'):
            # Set encoding parameters for optimal quality
            from aiortc import RTCRtpSender
            if isinstance(sender, RTCRtpSender):
                # This is where we could configure bitrate, resolution, etc.
                pass

        logger.info(
            "Attached avatar track to peer connection",
            session_id=session_id,
            track_id=id(frame_track),
            source_type=source.__class__.__name__
        )

        return frame_track

    except Exception as e:
        logger.error(
            "Failed to attach avatar track",
            session_id=session_id,
            error=str(e)
        )
        raise


# Simplified FrameTrack for test harness
class FrameTrack(MediaStreamTrack):
    kind = "video"
    def __init__(self, source):
        super().__init__()
        self.source = source
        self._ait = None

    async def recv(self) -> VideoFrame:
        if self._ait is None:
            self._ait = self.source.frames().__aiter__()
        frame_np = await self._ait.__anext__()  # HxWxC uint8
        h, w, _ = frame_np.shape
        vf = VideoFrame.from_ndarray(frame_np, format="rgb24")
        vf.pts, vf.time_base = None, None
        return vf


async def attach_avatar_track_simple(pc: RTCPeerConnection, use_veo3: bool = False):
    """Simplified avatar track attachment for test harness."""
    # Import video sources
    from providers.video_sources.placeholder_loop import PlaceholderLoopVideoSource
    from providers.video_sources.veo3_stream import Veo3StreamVideoSource
    from providers.types import VideoSourceConfig

    # Create video source config
    config = VideoSourceConfig(
        source_type="veo3" if use_veo3 else "placeholder",
        resolution=(320, 240),
        framerate=30,
        avatar_style="diplomatic"
    )

    # Create and start video source
    if use_veo3:
        source = Veo3StreamVideoSource(config)
    else:
        source = PlaceholderLoopVideoSource(config)

    await source.start()
    track = FrameTrack(source)
    pc.addTrack(track)
    # NOTE: stopping handled by session teardown (omitted here for brevity)
