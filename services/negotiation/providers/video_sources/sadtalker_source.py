"""SadTalker video source for portrait animation with lip-sync.

This video source uses SadTalker to generate realistic talking portraits
from static images, optimized for Apple Silicon Macs using MPS.
"""

import asyncio
import os
import sys
import subprocess
import tempfile
from pathlib import Path
from typing import AsyncGenerator, Optional, AsyncIterator
import structlog

try:
    import av
    import numpy as np
except ImportError:
    av = None
    np = None

from .base import BaseVideoSource, VideoFrame
from ..types import VideoSourceConfig


class SadTalkerVideoSource(BaseVideoSource):
    """Video source that uses SadTalker for portrait animation.
    
    Integrates with ElevenLabs TTS to generate lip-synced avatar videos
    from static portrait images. Optimized for Apple Silicon Macs.
    
    Environment Variables:
        SADTALKER_DIR: Path to SadTalker installation (default: ~/Projects/SadTalker)
        PORTRAIT_PATH: Path to portrait image to animate
        SADTALKER_DEVICE: Device to use (default: mps for Mac, cpu for others)
        SADTALKER_ENHANCE: Whether to use GFPGAN enhancement (default: true)
    """
    
    def __init__(self, config: VideoSourceConfig):
        super().__init__(config)
        
        # SadTalker configuration
        self.sadtalker_dir = Path(os.getenv("SADTALKER_DIR", "~/Projects/SadTalker")).expanduser()
        self.portrait_path = Path(os.getenv("PORTRAIT_PATH", "assets/avatars/portrait.jpg"))
        self.device = os.getenv("SADTALKER_DEVICE", "mps")  # mps for Apple Silicon
        self.enhance = os.getenv("SADTALKER_ENHANCE", "true").lower() == "true"
        self.still_mode = os.getenv("SADTALKER_STILL_MODE", "false").lower() == "true"
        
        # Output directory for generated videos
        self.output_dir = Path("./diplomatic_videos")
        self.output_dir.mkdir(exist_ok=True)
        
        # Current video state
        self._container = None
        self._video_stream = None
        self._current_video_path = None
        self._current_frame_index = 0
        
        # Validate SadTalker installation
        if not self.sadtalker_dir.exists():
            self.logger.warning(
                "SadTalker not found",
                path=str(self.sadtalker_dir),
                message="Will fall back to synthetic frames. Install SadTalker: git clone https://github.com/OpenTalker/SadTalker.git"
            )
            self._sadtalker_available = False
        else:
            self._sadtalker_available = True
            self.logger.info(
                "SadTalker found",
                path=str(self.sadtalker_dir),
                device=self.device
            )
    
    async def start(self) -> None:
        """Start the SadTalker video source."""
        self.logger.info(
            "Starting SadTalker video source",
            portrait=str(self.portrait_path),
            device=self.device,
            enhance=self.enhance
        )
        self._is_running = True
    
    async def stop(self) -> None:
        """Stop the SadTalker video source."""
        self.logger.info("Stopping SadTalker video source")
        self._is_running = False
        
        # Clean up video resources
        if self._container:
            self._container.close()
            self._container = None
            self._video_stream = None
    
    async def generate_video_from_audio(
        self,
        audio_path: Path,
        output_name: Optional[str] = None
    ) -> Path:
        """Generate a talking portrait video from audio file.
        
        Args:
            audio_path: Path to audio file (WAV or MP3)
            output_name: Optional custom name for output video
            
        Returns:
            Path to generated video file
        """
        if not self._sadtalker_available:
            raise RuntimeError("SadTalker not available")
        
        # Build SadTalker command
        result_dir = self.output_dir / "sadtalker_results"
        result_dir.mkdir(exist_ok=True)
        
        cmd = [
            sys.executable,
            str(self.sadtalker_dir / "inference.py"),
            "--driven_audio", str(audio_path),
            "--source_image", str(self.portrait_path),
            "--result_dir", str(result_dir),
            "--device", self.device,
            "--batch_size", "1",  # Conservative for stability
        ]
        
        if self.still_mode:
            cmd.append("--still")
        
        if self.enhance:
            cmd.extend(["--enhancer", "gfpgan"])
        
        self.logger.info("Running SadTalker", command=" ".join(cmd))
        
        # Run SadTalker asynchronously
        process = await asyncio.create_subprocess_exec(
            *cmd,
            cwd=self.sadtalker_dir,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        
        stdout, stderr = await process.communicate()
        
        if process.returncode != 0:
            self.logger.error(
                "SadTalker failed",
                returncode=process.returncode,
                stderr=stderr.decode()
            )
            raise RuntimeError(f"SadTalker failed: {stderr.decode()}")
        
        # Find generated video
        videos = sorted(result_dir.glob("**/*.mp4"), key=lambda p: p.stat().st_mtime)
        
        if not videos:
            raise RuntimeError("No video generated by SadTalker")
        
        video_path = videos[-1]  # Most recent
        
        # Optionally rename
        if output_name:
            final_path = self.output_dir / f"{output_name}.mp4"
            video_path.rename(final_path)
            video_path = final_path
        
        self.logger.info("Video generated successfully", path=str(video_path))
        self._current_video_path = video_path
        
        return video_path
    
    async def load_video(self, video_path: Path) -> None:
        """Load a video file for streaming.
        
        Args:
            video_path: Path to video file to load
        """
        if av is None:
            self.logger.error("PyAV not available, cannot decode video")
            return
        
        try:
            # Close existing container if any
            if self._container:
                self._container.close()
            
            # Open new video
            self._container = av.open(str(video_path))
            self._video_stream = self._container.streams.video[0]
            self._current_frame_index = 0
            
            self.logger.info(
                "Loaded video",
                path=str(video_path),
                duration=self._container.duration / av.time_base if self._container.duration else 0,
                fps=self._video_stream.average_rate
            )
        except Exception as e:
            self.logger.error("Failed to load video", error=str(e), path=str(video_path))
            raise
    
    async def get_frame(self) -> Optional[VideoFrame]:
        """Get the next frame from the loaded video."""
        if not self._is_running:
            return None
        
        if not self._container or not self._video_stream:
            # No video loaded, return synthetic frame
            return await self._get_synthetic_frame()
        
        try:
            # Decode next frame
            frame = next(self._container.decode(video=0))
            
            # Convert to RGB format if needed
            if frame.format.name != 'rgb24':
                frame = frame.reformat(format='rgb24')
            
            # Create VideoFrame object
            video_frame = VideoFrame(
                data=frame.to_ndarray(format='rgb24').tobytes(),
                timestamp=frame.time,
                width=frame.width,
                height=frame.height,
                format="rgb24"
            )
            
            self._frame_count += 1
            self._current_frame_index += 1
            return video_frame
            
        except (StopIteration, av.error.EOFError):
            # End of video, loop back to start
            self._container.seek(0)
            self._current_frame_index = 0
            return await self.get_frame()
        except Exception as e:
            self.logger.error("Error decoding frame", error=str(e))
            return None
    
    async def frames(self) -> AsyncIterator[np.ndarray]:
        """Stream video frames as numpy arrays."""
        while self._is_running:
            frame = await self.get_frame()
            if frame and np is not None:
                # Convert bytes to numpy array
                frame_array = np.frombuffer(frame.data, dtype=np.uint8).reshape(
                    (frame.height, frame.width, 3)
                )
                yield frame_array
            
            # Control frame rate
            await asyncio.sleep(1.0 / self.config.framerate)
    
    async def stream_frames(self) -> AsyncGenerator[VideoFrame, None]:
        """Stream frames continuously."""
        while self._is_running:
            frame = await self.get_frame()
            if frame:
                yield frame
            
            # Control frame rate
            await asyncio.sleep(1.0 / self.config.framerate)
    
    async def _get_synthetic_frame(self) -> Optional[VideoFrame]:
        """Generate a synthetic frame when no video is loaded."""
        width, height = self.config.resolution
        
        if np is None:
            # Fallback to basic array
            frame_data = bytes([30, 40, 50] * (width * height))
        else:
            # Create simple placeholder
            frame_array = np.zeros((height, width, 3), dtype=np.uint8)
            frame_array[:, :] = [30, 40, 50]  # Dark blue-gray
            frame_data = frame_array.tobytes()
        
        frame = VideoFrame(
            data=frame_data,
            timestamp=asyncio.get_event_loop().time(),
            width=width,
            height=height,
            format="rgb24"
        )
        
        self._frame_count += 1
        return frame


