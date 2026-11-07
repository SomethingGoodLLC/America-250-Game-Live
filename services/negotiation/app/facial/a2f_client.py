"""Async gRPC client for NVIDIA Audio2Face Microservice.

This client wraps the NVIDIA Audio2Face gRPC service and provides
an async interface for streaming audio and receiving blendshape coefficients.

Architecture:
- Audio input: PCM16 mono audio chunks
- Output: Time-aligned blendshape coefficients (ARKit-52 compatible)
- Transport: gRPC with async/await support

Reference: NVIDIA Audio2Face Microservice Documentation
https://docs.omniverse.nvidia.com/audio2face/latest/
"""

import time
from collections.abc import AsyncIterator
from dataclasses import dataclass
from typing import Any

import grpc
import numpy as np
import structlog

# Note: Actual proto imports will be generated from NVIDIA's .proto files
# Using placeholder imports until proto compilation
# from nvidia_a2f import audio2face_pb2, audio2face_pb2_grpc

logger = structlog.get_logger(__name__)


@dataclass
class BlendshapeFrame:
    """A single frame of blendshape coefficients.

    Attributes:
        timestamp_ms: Timestamp in milliseconds (relative to audio start)
        weights: List of 52 ARKit blendshape weights (0.0 to 1.0)
        frame_index: Sequential frame number
    """
    timestamp_ms: float
    weights: list[float]
    frame_index: int


@dataclass
class Audio2FaceConfig:
    """Configuration for Audio2Face client.

    Attributes:
        host: gRPC server host (e.g., "127.0.0.1")
        port: gRPC server port (e.g., 50051)
        model_id: A2F model identifier (e.g., "audio2face_3d")
        sample_rate: Audio sample rate in Hz (default: 16000)
        chunk_duration_ms: Duration of each audio chunk in ms (default: 100)
    """
    host: str = "127.0.0.1"
    port: int = 50051
    model_id: str = "audio2face_3d"
    sample_rate: int = 16000
    chunk_duration_ms: int = 100


class Audio2FaceClient:
    """Async gRPC client for NVIDIA Audio2Face Microservice.

    Usage:
        client = Audio2FaceClient(config)
        await client.connect()

        # Start streaming session
        async for frame in client.stream_audio(audio_chunks):
            # frame contains blendshape weights
            apply_to_3d_model(frame.weights)

        await client.disconnect()
    """

    def __init__(self, config: Audio2FaceConfig):
        """Initialize Audio2Face client.

        Args:
            config: Client configuration
        """
        self.config = config
        self._channel: grpc.aio.Channel | None = None
        self._stub: Any | None = None
        self._connected = False
        self._frame_count = 0

    async def connect(self) -> None:
        """Establish gRPC connection to Audio2Face service.

        Raises:
            grpc.RpcError: If connection fails
        """
        try:
            # Create async gRPC channel
            target = f"{self.config.host}:{self.config.port}"
            self._channel = grpc.aio.insecure_channel(target)

            # TODO: Replace with actual generated stub when proto is available
            # self._stub = audio2face_pb2_grpc.Audio2FaceServiceStub(self._channel)

            # Test connection
            await self._channel.channel_ready()

            self._connected = True
            logger.info(
                "Connected to Audio2Face service",
                host=self.config.host,
                port=self.config.port,
                model_id=self.config.model_id
            )

        except Exception as e:
            logger.error("Failed to connect to Audio2Face", error=str(e))
            raise

    async def disconnect(self) -> None:
        """Close gRPC connection."""
        if self._channel:
            await self._channel.close()
            self._channel = None
            self._stub = None
            self._connected = False
            logger.info("Disconnected from Audio2Face service")

    async def stream_audio(
        self,
        audio_chunks: AsyncIterator[bytes]
    ) -> AsyncIterator[BlendshapeFrame]:
        """Stream audio to A2F and receive blendshape frames.

        Args:
            audio_chunks: Async iterator of PCM16 mono audio bytes

        Yields:
            BlendshapeFrame objects with time-aligned coefficients

        Raises:
            ValueError: If not connected
            grpc.RpcError: If gRPC call fails
        """
        if not self._connected:
            raise ValueError("Client not connected. Call connect() first.")

        # Reset frame counter for new session
        self._frame_count = 0
        start_time = time.time()

        try:
            # TODO: Replace with actual gRPC streaming call
            # This is a placeholder implementation
            logger.info("Starting Audio2Face streaming session")

            # Process audio chunks
            async for audio_bytes in audio_chunks:
                # Send audio to A2F and receive blendshapes
                # In actual implementation, this would be:
                # request = audio2face_pb2.AudioRequest(
                #     audio_data=audio_bytes,
                #     sample_rate=self.config.sample_rate,
                #     model_id=self.config.model_id
                # )
                # response = await self._stub.ProcessAudio(request)

                # For now, generate placeholder blendshapes
                # This will be replaced with actual A2F response
                frame = await self._process_audio_chunk(audio_bytes, start_time)

                if frame:
                    yield frame

        except Exception as e:
            logger.error("Error during Audio2Face streaming", error=str(e))
            raise

    async def _process_audio_chunk(
        self,
        audio_bytes: bytes,
        start_time: float
    ) -> BlendshapeFrame:
        """Process a single audio chunk (placeholder implementation).

        TODO: Replace with actual NVIDIA A2F gRPC call

        Args:
            audio_bytes: PCM16 audio data
            start_time: Session start timestamp

        Returns:
            BlendshapeFrame with coefficients
        """
        # Calculate timestamp
        elapsed_ms = (time.time() - start_time) * 1000

        # TODO: Replace with actual A2F response parsing
        # For now, return empty blendshapes (neutral face)
        weights = [0.0] * 52

        # Add slight jaw movement for testing (will be removed)
        # In production, these come from NVIDIA A2F
        if self._frame_count % 10 < 5:
            weights[24] = 0.3  # jawOpen

        frame = BlendshapeFrame(
            timestamp_ms=elapsed_ms,
            weights=weights,
            frame_index=self._frame_count
        )

        self._frame_count += 1
        return frame

    async def process_audio_file(
        self,
        audio_path: str
    ) -> list[BlendshapeFrame]:
        """Process an entire audio file and return all blendshape frames.

        Convenience method for batch processing.

        Args:
            audio_path: Path to audio file (WAV, MP3, etc.)

        Returns:
            List of all blendshape frames
        """
        # TODO: Implement audio file loading and processing
        # This would load the file, convert to PCM16 chunks,
        # and collect all blendshape frames
        raise NotImplementedError("Batch file processing not yet implemented")

    @property
    def is_connected(self) -> bool:
        """Check if client is connected to A2F service."""
        return self._connected


class Audio2FaceStubClient(Audio2FaceClient):
    """Stub implementation for development/testing without NVIDIA A2F.

    Generates procedural blendshapes based on audio amplitude.
    Use this for development until NVIDIA A2F service is available.
    """

    async def connect(self) -> None:
        """Simulate connection (no actual gRPC)."""
        self._connected = True
        logger.warning(
            "Using Audio2Face STUB client (no actual A2F service)",
            host=self.config.host,
            port=self.config.port
        )

    async def disconnect(self) -> None:
        """Simulate disconnection."""
        self._connected = False
        logger.info("Audio2Face stub client disconnected")

    async def _process_audio_chunk(
        self,
        audio_bytes: bytes,
        start_time: float
    ) -> BlendshapeFrame:
        """Generate procedural blendshapes based on audio amplitude.

        This creates simple lip-sync by analyzing audio RMS and mapping
        to jaw/mouth blendshapes. Replace with actual A2F when available.
        """
        elapsed_ms = (time.time() - start_time) * 1000

        # Parse PCM16 audio and calculate RMS
        samples = np.frombuffer(audio_bytes, dtype=np.int16)
        if len(samples) > 0:
            rms = np.sqrt(np.mean(samples.astype(np.float32) ** 2))
            # Normalize to 0-1 range
            amplitude = min(rms / 5000.0, 1.0)
        else:
            amplitude = 0.0

        # Generate simple blendshapes from amplitude
        weights = [0.0] * 52

        # Map amplitude to mouth movements
        weights[24] = amplitude * 0.6  # jawOpen
        weights[31] = amplitude * 0.3  # mouthFunnel
        weights[43] = amplitude * 0.2  # mouthSmileLeft
        weights[44] = amplitude * 0.2  # mouthSmileRight

        # Add subtle variations
        if self._frame_count % 20 < 10:
            weights[37] = amplitude * 0.15  # mouthPucker

        frame = BlendshapeFrame(
            timestamp_ms=elapsed_ms,
            weights=weights,
            frame_index=self._frame_count
        )

        self._frame_count += 1
        return frame

