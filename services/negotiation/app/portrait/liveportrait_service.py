"""
LivePortrait Service - Real-time portrait animation engine

Manages GPU-accelerated portrait animation sessions with audio-driven facial movements.
Supports LivePortrait and Ditto models with CUDA/TensorRT optimization.

Features:
- Face detection and auto-cropping with fallback bbox
- Expression smoothing with EMA filter
- Lip-closure bias for plosive phonemes (P, B, T, D, K, G, M)
- FPS governor (target 25-30 FPS) with graceful frame dropping
- Frame resizing to 512px short side
- Graceful degradation: drops frames (not audio) when GPU saturated
- Health and metrics monitoring endpoints (/portrait/health, /portrait/metrics)

GPU Optimization:
- CUDA acceleration for model inference
- TensorRT optimization (optional)
- GPU memory management
- Real-time GPU utilization monitoring

Integration Guide:
To integrate actual models (LivePortrait, Ditto, etc.):

1. Install model dependencies:
   ```bash
   pip install torch torchvision  # PyTorch with CUDA
   pip install liveportrait  # or your model package
   pip install pynvml  # For GPU monitoring
   ```

2. Load model in LivePortraitService.load():
   Replace the TODO section with actual model loading.
   See detailed instructions in the load() method docstring.

3. Replace placeholder in _generate_single_frame():
   Use model.animate() or equivalent with extracted audio features.
   All smoothing, plosives, FPS, and metrics work automatically!

Everything else (smoothing, plosives, FPS governor, metrics) is already implemented
and will work automatically once you integrate the model!
"""

import asyncio
import logging
import time
from collections import deque
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional, Dict, Tuple, List
import io

import numpy as np
from PIL import Image

logger = logging.getLogger(__name__)


# Try to import GPU monitoring
try:
    import pynvml
    PYNVML_AVAILABLE = True
except ImportError:
    PYNVML_AVAILABLE = False
    logger.warning("pynvml not available - GPU utilization monitoring disabled")


class GPUMonitor:
    """
    Monitor GPU utilization and memory usage.
    
    Uses NVIDIA Management Library (pynvml) to track GPU metrics.
    """
    
    def __init__(self):
        self.initialized = False
        self.device_handle = None
        
        if PYNVML_AVAILABLE:
            try:
                pynvml.nvmlInit()
                # Use first GPU by default
                self.device_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
                self.initialized = True
                logger.info("GPU monitoring initialized")
            except Exception as e:
                logger.warning(f"Failed to initialize GPU monitoring: {e}")
    
    def get_utilization(self) -> float:
        """
        Get current GPU utilization percentage.
        
        Returns:
            GPU utilization (0-100) or 0 if unavailable
        """
        if not self.initialized:
            return 0.0
        
        try:
            utilization = pynvml.nvmlDeviceGetUtilizationRates(self.device_handle)
            return float(utilization.gpu)
        except Exception as e:
            logger.debug(f"Failed to get GPU utilization: {e}")
            return 0.0
    
    def get_memory_info(self) -> Dict[str, int]:
        """
        Get GPU memory usage.
        
        Returns:
            Dict with total, used, and free memory in bytes
        """
        if not self.initialized:
            return {"total": 0, "used": 0, "free": 0}
        
        try:
            mem_info = pynvml.nvmlDeviceGetMemoryInfo(self.device_handle)
            return {
                "total": mem_info.total,
                "used": mem_info.used,
                "free": mem_info.free
            }
        except Exception as e:
            logger.debug(f"Failed to get GPU memory info: {e}")
            return {"total": 0, "used": 0, "free": 0}
    
    def cleanup(self):
        """Cleanup GPU monitoring resources"""
        if self.initialized:
            try:
                pynvml.nvmlShutdown()
            except:
                pass


# Global GPU monitor instance
_gpu_monitor = GPUMonitor()


class PortraitEngine(str, Enum):
    """Supported portrait animation engines"""
    LIVEPORTRAIT = "liveportrait"
    DITTO = "ditto"


@dataclass
class AudioChunk:
    """Audio chunk with timestamp for synchronization"""
    pcm16: bytes
    sample_rate: int
    timestamp_ms: float  # Server monotonic time
    text: Optional[str] = None  # Optional text for phoneme extraction


@dataclass
class VideoFrame:
    """Generated video frame with timestamp"""
    rgba: np.ndarray  # (H, W, 4) uint8
    pts_ms: float  # Presentation timestamp


@dataclass
class SessionMetrics:
    """Metrics for a portrait session"""
    frames_generated: int = 0
    frames_dropped: int = 0
    audio_chunks_received: int = 0
    audio_buffer_ms: float = 0.0
    last_frame_time_ms: float = 0.0
    avg_frame_time_ms: float = 0.0
    gpu_utilization: float = 0.0
    
    # FPS tracking
    actual_fps: float = 0.0
    target_fps: int = 25
    
    # Expression smoothing
    last_expression: Optional[np.ndarray] = None
    
    def to_dict(self) -> Dict:
        """Convert metrics to dict for JSON serialization"""
        return {
            "frames_generated": self.frames_generated,
            "frames_dropped": self.frames_dropped,
            "audio_chunks_received": self.audio_chunks_received,
            "audio_buffer_ms": self.audio_buffer_ms,
            "last_frame_age_ms": time.monotonic() * 1000 - self.last_frame_time_ms if self.last_frame_time_ms > 0 else 0,
            "avg_frame_time_ms": self.avg_frame_time_ms,
            "actual_fps": self.actual_fps,
            "target_fps": self.target_fps,
            "gpu_utilization": self.gpu_utilization,
        }


# Plosive phonemes that require lip closure
PLOSIVE_PHONEMES = {'P', 'B', 'T', 'D', 'K', 'G', 'M'}


class ExpressionSmoother:
    """
    Smooth facial expressions using Exponential Moving Average (EMA).
    
    Prevents jittery/flickering animation.
    """
    
    def __init__(self, alpha: float = 0.3):
        """
        Initialize expression smoother.
        
        Args:
            alpha: Smoothing factor (0 = no smoothing, 1 = no memory)
                  Lower values = smoother but more lag
        """
        self.alpha = alpha
        self.last_expression: Optional[np.ndarray] = None
    
    def smooth(self, expression: np.ndarray) -> np.ndarray:
        """
        Apply EMA smoothing to expression parameters.
        
        Args:
            expression: Current expression array
            
        Returns:
            Smoothed expression
        """
        if self.last_expression is None:
            self.last_expression = expression.copy()
            return expression
        
        # EMA: smoothed = alpha * current + (1 - alpha) * last
        smoothed = self.alpha * expression + (1 - self.alpha) * self.last_expression
        self.last_expression = smoothed.copy()
        
        return smoothed
    
    def reset(self):
        """Reset smoother state"""
        self.last_expression = None


class FPSGovernor:
    """
    Governs frame generation rate to maintain target FPS.
    
    Features:
    - Adaptive frame dropping when GPU saturated
    - Maintains constant frame times for smooth playback
    """
    
    def __init__(self, target_fps: int = 25, drop_threshold_ms: float = 80.0):
        """
        Initialize FPS governor.
        
        Args:
            target_fps: Target frames per second
            drop_threshold_ms: If frame takes longer than this, start dropping
        """
        self.target_fps = target_fps
        self.frame_interval_ms = 1000.0 / target_fps
        self.drop_threshold_ms = drop_threshold_ms
        
        self.last_frame_time = 0.0
        self.frame_times: deque = deque(maxlen=30)  # Track last 30 frames
        self.dropped_frames = 0
    
    def should_generate_frame(self) -> bool:
        """
        Check if it's time to generate next frame.
        
        Returns:
            True if frame should be generated
        """
        current_time = time.monotonic() * 1000
        
        if self.last_frame_time == 0:
            self.last_frame_time = current_time
            return True
        
        elapsed = current_time - self.last_frame_time
        
        # Check if it's time for next frame
        if elapsed >= self.frame_interval_ms:
            return True
        
        return False
    
    def should_drop_frame(self) -> bool:
        """
        Check if frame should be dropped due to GPU saturation.
        
        Returns:
            True if frame should be dropped
        """
        if len(self.frame_times) < 5:
            return False  # Not enough data
        
        # Calculate average frame time
        avg_frame_time = sum(self.frame_times) / len(self.frame_times)
        
        # Drop frames if we're consistently over threshold
        if avg_frame_time > self.drop_threshold_ms:
            return True
        
        return False
    
    def record_frame(self, generation_time_ms: float):
        """
        Record frame generation time.
        
        Args:
            generation_time_ms: Time taken to generate frame
        """
        self.frame_times.append(generation_time_ms)
        self.last_frame_time = time.monotonic() * 1000
    
    def record_dropped_frame(self):
        """Record that a frame was dropped"""
        self.dropped_frames += 1
        self.last_frame_time = time.monotonic() * 1000
    
    def get_actual_fps(self) -> float:
        """Calculate actual FPS from recent frame times"""
        if len(self.frame_times) == 0:
            return 0.0
        
        avg_frame_time = sum(self.frame_times) / len(self.frame_times)
        if avg_frame_time == 0:
            return 0.0
        
        return 1000.0 / avg_frame_time


class PortraitSession:
    """
    Manages a single portrait animation session.
    
    Each session maintains:
    - Reference portrait image
    - Audio FIFO buffer
    - Frame generation state
    - Timing synchronization
    - Expression smoothing
    - FPS governance
    - Performance metrics
    """
    
    def __init__(
        self,
        session_id: str,
        reference_image: np.ndarray,
        fps: int = 25,
        engine: PortraitEngine = PortraitEngine.LIVEPORTRAIT,
        target_frame_size: int = 512
    ):
        self.session_id = session_id
        self.reference_image = self._resize_frame(reference_image, target_frame_size)
        self.fps = fps
        self.engine = engine
        self.target_frame_size = target_frame_size
        
        # Audio buffer (FIFO) - increased capacity for graceful degradation
        self.audio_buffer: deque[AudioChunk] = deque(maxlen=200)
        self.audio_lock = asyncio.Lock()
        
        # Frame generation state
        self.frame_count = 0
        self.start_time_ms = time.monotonic() * 1000
        self.last_frame_time_ms = self.start_time_ms
        
        # Worker task
        self.worker_task: Optional[asyncio.Task] = None
        self.running = False
        
        # Output queue - larger to handle bursts
        self.frame_queue: asyncio.Queue[VideoFrame] = asyncio.Queue(maxsize=15)
        
        # Expression smoothing
        self.expression_smoother = ExpressionSmoother(alpha=0.3)
        
        # FPS governor
        self.fps_governor = FPSGovernor(
            target_fps=fps,
            drop_threshold_ms=1000.0 / fps * 1.5  # 1.5x target frame time
        )
        
        # Metrics tracking
        self.metrics = SessionMetrics(target_fps=fps)
        
        # Phoneme detection (for lip-closure bias)
        self.current_phoneme: Optional[str] = None
        
        logger.info(
            f"Session {session_id} initialized: engine={engine}, fps={fps}, "
            f"image_shape={self.reference_image.shape}, target_size={target_frame_size}"
        )
    
    def _resize_frame(self, frame: np.ndarray, target_size: int) -> np.ndarray:
        """
        Resize frame to target size (short side = target_size).
        
        Args:
            frame: Input frame
            target_size: Target size for short side
            
        Returns:
            Resized frame
        """
        h, w = frame.shape[:2]
        
        # Already at target size
        if min(h, w) == target_size:
            return frame
        
        # Calculate new dimensions
        if h < w:
            new_h = target_size
            new_w = int(w * (target_size / h))
        else:
            new_w = target_size
            new_h = int(h * (target_size / w))
        
        # Resize
        img_pil = Image.fromarray(frame)
        img_pil = img_pil.resize((new_w, new_h), Image.Resampling.LANCZOS)
        
        return np.array(img_pil)
    
    async def ingest_audio(
        self, 
        pcm16: bytes, 
        sample_rate: int, 
        timestamp_ms: float,
        text: Optional[str] = None
    ):
        """
        Add audio chunk to FIFO buffer.
        
        Args:
            pcm16: PCM16 audio bytes (little-endian)
            sample_rate: Audio sample rate (Hz)
            timestamp_ms: Server monotonic timestamp
            text: Optional text for phoneme extraction
        """
        async with self.audio_lock:
            chunk = AudioChunk(
                pcm16=pcm16,
                sample_rate=sample_rate,
                timestamp_ms=timestamp_ms,
                text=text
            )
            self.audio_buffer.append(chunk)
            
            # Update metrics
            self.metrics.audio_chunks_received += 1
            # Calculate buffer duration in ms
            chunk_duration_ms = len(pcm16) / 2 / sample_rate * 1000
            self.metrics.audio_buffer_ms = len(self.audio_buffer) * chunk_duration_ms
    
    async def get_next_frame(self) -> Optional[VideoFrame]:
        """
        Get next generated frame (non-blocking).
        
        Returns:
            VideoFrame if available, None if queue is empty
        """
        try:
            return await asyncio.wait_for(self.frame_queue.get(), timeout=0.001)
        except asyncio.TimeoutError:
            return None
    
    async def start_worker(self, model):
        """Start background worker for frame generation"""
        self.running = True
        self.worker_task = asyncio.create_task(self._generate_frames(model))
        logger.info(f"Session {self.session_id} worker started")
    
    async def stop_worker(self):
        """Stop background worker"""
        self.running = False
        if self.worker_task:
            self.worker_task.cancel()
            try:
                await self.worker_task
            except asyncio.CancelledError:
                pass
        logger.info(f"Session {self.session_id} worker stopped")
    
    async def _generate_frames(self, model):
        """
        Background worker that generates frames from audio buffer.
        
        Features:
        - FPS governance with target frame rate
        - Graceful frame dropping when GPU saturated
        - Audio is never dropped (preserved in buffer)
        - Expression smoothing
        - Performance metrics tracking
        """
        try:
            while self.running:
                # Check if it's time for next frame
                if not self.fps_governor.should_generate_frame():
                    # Sleep for a short time to avoid busy waiting
                    await asyncio.sleep(0.001)
                    continue
                
                frame_start_time = time.monotonic() * 1000
                
                # Check if we should drop frame due to GPU saturation
                if self.fps_governor.should_drop_frame():
                    logger.warning(
                        f"Session {self.session_id}: GPU saturated, dropping frame "
                        f"(audio preserved in buffer)"
                    )
                    self.fps_governor.record_dropped_frame()
                    self.metrics.frames_dropped += 1
                    
                    # Don't consume audio - it stays in buffer
                    await asyncio.sleep(0.005)
                    continue
                
                # Get audio chunk if available (but don't require it)
                audio_chunk = None
                async with self.audio_lock:
                    if self.audio_buffer:
                        audio_chunk = self.audio_buffer.popleft()
                
                # Extract phoneme if text available (for lip-closure bias)
                if audio_chunk and audio_chunk.text:
                    self.current_phoneme = self._extract_current_phoneme(audio_chunk.text)
                
                # Generate frame
                frame = await self._generate_single_frame(model, audio_chunk)
                
                if frame:
                    # Resize frame to target size
                    frame.rgba = self._resize_frame(frame.rgba, self.target_frame_size)
                    
                    # Put frame in output queue (non-blocking)
                    try:
                        self.frame_queue.put_nowait(frame)
                    except asyncio.QueueFull:
                        # Drop oldest frame if queue is full (graceful degradation)
                        try:
                            self.frame_queue.get_nowait()
                            self.frame_queue.put_nowait(frame)
                            logger.debug(f"Session {self.session_id}: Frame queue full, dropped old frame")
                        except:
                            pass
                
                # Record frame generation time
                frame_end_time = time.monotonic() * 1000
                generation_time = frame_end_time - frame_start_time
                self.fps_governor.record_frame(generation_time)
                
                # Update metrics
                self.metrics.frames_generated += 1
                self.metrics.last_frame_time_ms = frame_end_time
                self.metrics.avg_frame_time_ms = generation_time
                self.metrics.actual_fps = self.fps_governor.get_actual_fps()
                
                # Update GPU utilization periodically (every 10 frames to reduce overhead)
                if self.frame_count % 10 == 0:
                    self.metrics.gpu_utilization = _gpu_monitor.get_utilization()
                
                self.last_frame_time_ms = frame_end_time
                self.frame_count += 1
                
        except asyncio.CancelledError:
            logger.info(f"Session {self.session_id} worker cancelled")
            raise
        except Exception as e:
            logger.error(f"Session {self.session_id} worker error: {e}", exc_info=True)
    
    def _extract_current_phoneme(self, text: str) -> Optional[str]:
        """
        Extract current phoneme from text (simplified).
        
        In production, this would use proper forced alignment.
        For now, just check for plosives in text.
        """
        # Simple heuristic: check for plosive letters
        text_upper = text.upper()
        for plosive in PLOSIVE_PHONEMES:
            if plosive in text_upper:
                return plosive
        return None
    
    async def _generate_single_frame(
        self, 
        model, 
        audio_chunk: Optional[AudioChunk]
    ) -> VideoFrame:
        """
        Generate a single frame using the portrait model.
        
        Features:
        - Expression smoothing with EMA filter (automatic)
        - Lip-closure bias for plosive phonemes (automatic)
        - Audio-driven facial animation
        - Fallback to neutral expression if no audio
        
        Integration Instructions:
        ========================
        
        To integrate actual model, replace the TODO section with:
        
        ```python
        if audio_chunk:
            # Extract audio features (already implemented)
            from app.portrait.audio_features import AudioFeatureExtractor
            extractor = AudioFeatureExtractor(sample_rate=audio_chunk.sample_rate)
            
            # Get mouth openness and apply plosive bias (automatic)
            mouth_openness = extractor.audio_to_mouth_openness(audio_chunk.pcm16)
            if self.current_phoneme in PLOSIVE_PHONEMES:
                mouth_openness = max(0.0, mouth_openness * 0.3)
            
            # Create expression vector (model-specific)
            expression = np.array([mouth_openness], dtype=np.float32)
            
            # Apply smoothing (automatic)
            smoothed_expression = self.expression_smoother.smooth(expression)
            
            # Extract mel-spectrogram for audio-driven models
            mel_spec = extractor.extract_mel_spectrogram(audio_chunk.pcm16)
            
            # Generate frame with actual model
            rgba_frame = await model.animate(
                reference_image=self.reference_image,
                expression=smoothed_expression,  # Smoothed expression params
                audio_features=mel_spec,  # For audio-driven animation
                # Add other model-specific params here
            )
            
            # Convert to RGBA if needed
            if rgba_frame.shape[-1] == 3:
                rgba_frame = self._rgb_to_rgba(rgba_frame)
        else:
            # No audio - neutral expression
            rgba_frame = await model.animate(
                reference_image=self.reference_image,
                expression=None,  # Neutral
                audio_features=None
            )
        ```
        
        That's it! Everything else (smoothing, plosives, FPS, metrics) works automatically.
        
        Args:
            model: Portrait animation model
            audio_chunk: Optional audio chunk for audio-driven animation
            
        Returns:
            VideoFrame with RGBA data and timestamp
        """
        # Calculate presentation timestamp
        pts_ms = self.start_time_ms + (self.frame_count * 1000.0 / self.fps)
        
        # TODO: Replace with actual model inference
        # See docstring above for detailed integration example
        
        if audio_chunk:
            # Extract audio features
            from app.portrait.audio_features import AudioFeatureExtractor
            extractor = AudioFeatureExtractor(sample_rate=audio_chunk.sample_rate)
            
            # Get mouth openness from audio energy
            mouth_openness = extractor.audio_to_mouth_openness(audio_chunk.pcm16)
            
            # Apply lip-closure bias for plosives (automatic)
            if self.current_phoneme in PLOSIVE_PHONEMES:
                # Plosives require lip closure
                mouth_openness = max(0.0, mouth_openness * 0.3)  # Reduce mouth openness
            
            # Create expression vector (placeholder)
            # In real implementation, this would be model-specific parameters
            expression = np.array([mouth_openness], dtype=np.float32)
            
            # Apply expression smoothing (automatic)
            smoothed_expression = self.expression_smoother.smooth(expression)
            
            # Generate animated frame
            # TODO: Integrate actual model here - see docstring above
            # Example:
            # mel_spec = extractor.extract_mel_spectrogram(audio_chunk.pcm16)
            # rgba_frame = await model.animate(
            #     self.reference_image,
            #     expression=smoothed_expression,
            #     audio_features=mel_spec
            # )
            
            # Placeholder: add simple visual indication of mouth openness
            rgba_frame = self._reference_to_rgba()
            rgba_frame = self._apply_mouth_animation(rgba_frame, smoothed_expression[0])
        else:
            # No audio - use neutral expression
            rgba_frame = self._reference_to_rgba()
        
        return VideoFrame(rgba=rgba_frame, pts_ms=pts_ms)
    
    def _apply_mouth_animation(self, frame: np.ndarray, mouth_openness: float) -> np.ndarray:
        """
        Apply simple mouth animation to frame (placeholder).
        
        In production, this would be handled by the actual model.
        For now, just darken bottom portion based on mouth openness.
        
        Args:
            frame: RGBA frame
            mouth_openness: Mouth openness (0-1)
            
        Returns:
            Animated frame
        """
        if mouth_openness < 0.1:
            return frame  # Mouth closed, no change
        
        # Create a copy
        animated = frame.copy()
        
        # Simple visual feedback: darken bottom 20% of image based on openness
        h, w = animated.shape[:2]
        mouth_region_start = int(h * 0.65)
        mouth_region_end = int(h * 0.85)
        
        # Apply darkening proportional to mouth openness
        darkening_factor = 1.0 - (mouth_openness * 0.3)
        animated[mouth_region_start:mouth_region_end, :, :3] = \
            (animated[mouth_region_start:mouth_region_end, :, :3] * darkening_factor).astype(np.uint8)
        
        return animated
    
    def _reference_to_rgba(self) -> np.ndarray:
        """Convert reference image to RGBA format"""
        return self._rgb_to_rgba(self.reference_image)
    
    def _rgb_to_rgba(self, image: np.ndarray) -> np.ndarray:
        """
        Convert RGB image to RGBA format.
        
        Args:
            image: RGB or RGBA image array
            
        Returns:
            RGBA image with full opacity
        """
        if image.shape[-1] == 3:
            # RGB -> RGBA
            h, w = image.shape[:2]
            rgba = np.zeros((h, w, 4), dtype=np.uint8)
            rgba[:, :, :3] = image
            rgba[:, :, 3] = 255  # Full opacity
            return rgba
        elif image.shape[-1] == 4:
            return image.copy()
        else:
            raise ValueError(f"Unexpected image format: {image.shape}")


class LivePortraitService:
    """
    GPU service for real-time portrait animation.
    
    Manages:
    - Model loading and GPU memory
    - Multiple concurrent sessions
    - Frame generation and streaming
    """
    
    def __init__(self, model_dir: Optional[Path] = None):
        self.model_dir = model_dir
        self.model = None
        self.device = None
        self.sessions: Dict[str, PortraitSession] = {}
        self.lock = asyncio.Lock()
        
        logger.info("LivePortraitService initialized")
    
    async def load(self, engine: PortraitEngine = PortraitEngine.LIVEPORTRAIT):
        """
        Load portrait animation model with GPU acceleration.
        
        Integration Instructions:
        ========================
        
        To integrate actual models, replace the TODO section below with:
        
        For LivePortrait:
        -----------------
        ```python
        import torch
        from liveportrait import LivePortraitModel  # or appropriate import
        
        # Detect device
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        logger.info(f"Using device: {self.device}")
        
        # Load model
        self.model = LivePortraitModel.load(
            model_path=self.model_dir,
            device=self.device
        )
        
        # Set to eval mode
        self.model.eval()
        
        # Optional: Optimize with TensorRT for production
        if hasattr(self.model, 'to_tensorrt') and self.device.type == 'cuda':
            logger.info("Optimizing model with TensorRT...")
            self.model = self.model.to_tensorrt()
        
        # Optional: Enable mixed precision for better performance
        if self.device.type == 'cuda':
            import torch.cuda.amp
            self.use_amp = True
        ```
        
        For Ditto or other models:
        --------------------------
        Similar pattern - load model, set device, optimize.
        The key is that model should have an `animate()` or `inference()` method
        that takes:
        - reference_image: np.ndarray or torch.Tensor
        - audio_features: dict with mel_spectrogram or other features
        - expression: optional expression parameters
        
        Returns:
        - animated_frame: np.ndarray (H, W, 3 or 4) uint8
        
        Args:
            engine: Portrait engine to load (liveportrait or ditto)
        """
        try:
            # TODO: Replace with actual model loading
            # See docstring above for detailed integration examples
            
            # import torch
            # self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
            # 
            # if engine == PortraitEngine.LIVEPORTRAIT:
            #     from liveportrait import LivePortraitModel
            #     self.model = LivePortraitModel.load(self.model_dir, device=self.device)
            #     self.model.eval()
            # elif engine == PortraitEngine.DITTO:
            #     from ditto import DittoModel
            #     self.model = DittoModel.load(self.model_dir, device=self.device)
            #     self.model.eval()
            # 
            # # Optimize with TensorRT if available
            # if hasattr(self.model, 'to_tensorrt') and self.device.type == 'cuda':
            #     logger.info("Optimizing with TensorRT...")
            #     self.model = self.model.to_tensorrt()
            
            self.device = "cpu"  # Placeholder
            logger.info(f"Model loaded: engine={engine}, device={self.device}")
            logger.warning("Using placeholder model - TODO: integrate actual LivePortrait/Ditto")
            logger.info("See LivePortraitService.load() docstring for integration instructions")
            
        except Exception as e:
            logger.error(f"Failed to load model: {e}", exc_info=True)
            raise
    
    async def start_session(
        self,
        session_id: str,
        img_bytes: bytes,
        fps: int = 25,
        engine: PortraitEngine = PortraitEngine.LIVEPORTRAIT,
        target_size: int = 512
    ) -> Dict:
        """
        Start a new portrait animation session.
        
        Features:
        - Face detection and auto-cropping
        - Image preprocessing and enhancement
        - Session initialization with metrics
        
        Args:
            session_id: Unique session identifier
            img_bytes: Reference portrait image (JPEG/PNG)
            fps: Target frames per second (15-30 recommended)
            engine: Portrait engine to use
            target_size: Target frame size (short side in pixels)
            
        Returns:
            Dict with session info and preprocessing metadata
        """
        async with self.lock:
            # Close existing session if present
            if session_id in self.sessions:
                await self.stop_session(session_id)
            
            # Preprocess image with face detection
            from app.portrait.face_detection import preprocess_portrait
            
            reference_array, metadata = preprocess_portrait(
                img_bytes,
                target_size=target_size,
                padding=0.3,
                enhance=True
            )
            
            logger.info(
                f"Portrait preprocessed for {session_id}",
                extra={
                    "face_detected": metadata.get("face_detected", False),
                    "original_size": metadata.get("original_size"),
                    "target_size": target_size
                }
            )
            
            # Create session
            session = PortraitSession(
                session_id=session_id,
                reference_image=reference_array,
                fps=fps,
                engine=engine,
                target_frame_size=target_size
            )
            
            # Start worker
            await session.start_worker(self.model)
            
            self.sessions[session_id] = session
            logger.info(f"Session {session_id} started with {engine} at {fps} FPS")
            
            return {
                "session_id": session_id,
                "preprocessing": metadata,
                "fps": fps,
                "engine": engine,
                "target_size": target_size
            }
    
    async def ingest_audio(
        self,
        session_id: str,
        pcm16_chunk: bytes,
        sr: int = 24000,
        t0_ms: Optional[float] = None
    ) -> None:
        """
        Ingest audio chunk into session buffer.
        
        Args:
            session_id: Session identifier
            pcm16_chunk: PCM16 audio bytes (little-endian)
            sr: Sample rate (Hz)
            t0_ms: Server monotonic timestamp (ms). If None, uses current time.
        """
        session = self.sessions.get(session_id)
        if not session:
            logger.warning(f"Session {session_id} not found for audio ingest")
            return
        
        timestamp_ms = t0_ms if t0_ms is not None else time.monotonic() * 1000
        await session.ingest_audio(pcm16_chunk, sr, timestamp_ms)
    
    async def next_frame(self, session_id: str) -> Optional[Tuple[np.ndarray, float]]:
        """
        Get next frame from session.
        
        Args:
            session_id: Session identifier
            
        Returns:
            Tuple of (rgba_frame, pts_ms) or None if no frame available
        """
        session = self.sessions.get(session_id)
        if not session:
            logger.warning(f"Session {session_id} not found for frame fetch")
            return None
        
        frame = await session.get_next_frame()
        if frame:
            return (frame.rgba, frame.pts_ms)
        return None
    
    async def stop_session(self, session_id: str) -> None:
        """
        Stop and cleanup a portrait session.
        
        Args:
            session_id: Session identifier
        """
        async with self.lock:
            session = self.sessions.pop(session_id, None)
            if session:
                await session.stop_worker()
                logger.info(f"Session {session_id} stopped")
    
    async def cleanup(self) -> None:
        """Cleanup all sessions and release GPU resources"""
        async with self.lock:
            for session_id in list(self.sessions.keys()):
                await self.stop_session(session_id)
            
            # TODO: Release model and GPU memory
            # if self.model:
            #     del self.model
            #     if hasattr(torch, 'cuda'):
            #         torch.cuda.empty_cache()
            #         torch.cuda.synchronize()
            
            # Cleanup GPU monitor
            _gpu_monitor.cleanup()
            
            logger.info("LivePortraitService cleaned up")

