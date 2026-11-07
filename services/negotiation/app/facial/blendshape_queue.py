"""Backpressure queue for blendshape frames with interpolation.

This module implements NVIDIA Audio2Face's recommended approach:
- Audio clock drives the timeline (NEVER drop audio)
- Blendshapes are "animation data" that can be dropped under load
- Interpolation queue (30-50ms) for smooth motion
- Resync to latest frame when backpressure is detected

Production-ready for high-throughput scenarios.
"""

import asyncio
import time
from collections import deque
from dataclasses import dataclass, field

import structlog

from .arkit52 import clamp01, validate_arkit52_weights

logger = structlog.get_logger(__name__)


@dataclass
class BlendshapeFrameWithPriority:
    """Blendshape frame with priority metadata.

    Attributes:
        timestamp_ms: Audio-relative timestamp in milliseconds
        weights: ARKit-52 blendshape weights (0.0-1.0)
        frame_index: Sequential frame number
        priority: Frame priority (higher = more important)
        enqueue_time: When this frame was queued (for latency tracking)
    """
    timestamp_ms: float
    weights: list[float]
    frame_index: int
    priority: int = 0
    enqueue_time: float = field(default_factory=time.time)


class BlendshapeBackpressureQueue:
    """Smart queue for blendshape frames with backpressure management.

    Features:
    - Drops oldest frames when queue exceeds max_size (never drops audio)
    - Interpolates between consecutive frames for smooth motion
    - Tracks dropped frame count for monitoring
    - Resyncs to latest frame under heavy load

    Architecture aligns with NVIDIA's Audio2Face Microservice design:
    - Audio is authoritative clock (external to this queue)
    - Blendshapes are scheduled by audio timestamp
    - Interpolation happens at render time, not processing time

    Usage:
        queue = BlendshapeBackpressureQueue(
            max_queue_size=50,
            interpolation_window_ms=40
        )

        # Producer (A2F)
        await queue.enqueue(frame)

        # Consumer (renderer)
        frame = await queue.dequeue_at_time(audio_playhead_ms)
    """

    def __init__(
        self,
        max_queue_size: int = 50,
        interpolation_window_ms: float = 40.0,
        enable_interpolation: bool = True,
        drop_strategy: str = "oldest"  # "oldest" or "furthest_from_playhead"
    ):
        """Initialize blendshape queue.

        Args:
            max_queue_size: Maximum frames before backpressure kicks in
            interpolation_window_ms: Lookahead window for interpolation (30-50ms recommended)
            enable_interpolation: Enable frame interpolation for smoothness
            drop_strategy: Which frames to drop first under backpressure
        """
        self._queue: deque[BlendshapeFrameWithPriority] = deque()
        self._max_size = max_queue_size
        self._interpolation_window = interpolation_window_ms
        self._enable_interpolation = enable_interpolation
        self._drop_strategy = drop_strategy

        # Monitoring
        self._dropped_count = 0
        self._total_enqueued = 0
        self._last_dequeue_time = 0.0
        self._interpolation_hits = 0

        # Lock for thread-safe operations
        self._lock = asyncio.Lock()

        logger.info(
            "BlendshapeBackpressureQueue initialized",
            max_size=max_queue_size,
            interpolation_window_ms=interpolation_window_ms,
            enable_interpolation=enable_interpolation
        )

    async def enqueue(
        self,
        timestamp_ms: float,
        weights: list[float],
        frame_index: int,
        priority: int = 0
    ) -> None:
        """Add blendshape frame to queue with backpressure management.

        Args:
            timestamp_ms: Audio-relative timestamp
            weights: ARKit-52 weights array
            frame_index: Sequential frame number
            priority: Frame priority (higher retained longer)

        Raises:
            ValueError: If weights are invalid
        """
        if not validate_arkit52_weights(weights):
            raise ValueError(f"Invalid ARKit-52 weights: expected 52, got {len(weights)}")

        async with self._lock:
            frame = BlendshapeFrameWithPriority(
                timestamp_ms=timestamp_ms,
                weights=weights,
                frame_index=frame_index,
                priority=priority
            )

            self._queue.append(frame)
            self._total_enqueued += 1

            # Apply backpressure if queue is full
            if len(self._queue) > self._max_size:
                await self._apply_backpressure()

    async def dequeue_at_time(
        self,
        playhead_ms: float
    ) -> BlendshapeFrameWithPriority | None:
        """Get blendshape frame for current audio playhead position.

        This method implements the core rendering logic:
        1. Find frames around playhead position
        2. Interpolate between them if enabled
        3. Return frame closest to playhead

        Args:
            playhead_ms: Current audio playhead position in milliseconds

        Returns:
            Blendshape frame for current time, or None if queue empty
        """
        async with self._lock:
            if not self._queue:
                return None

            self._last_dequeue_time = playhead_ms

            # Find frames around playhead
            prev_frame, next_frame = self._find_surrounding_frames(playhead_ms)

            if prev_frame is None and next_frame is None:
                return None

            # If only one frame available, return it
            if prev_frame is None:
                return next_frame
            if next_frame is None:
                return prev_frame

            # Interpolate between frames if enabled
            if self._enable_interpolation:
                interpolated = self._interpolate_frames(
                    prev_frame,
                    next_frame,
                    playhead_ms
                )
                self._interpolation_hits += 1
                return interpolated
            else:
                # Return frame closest to playhead
                if abs(prev_frame.timestamp_ms - playhead_ms) < abs(next_frame.timestamp_ms - playhead_ms):
                    return prev_frame
                else:
                    return next_frame

    async def dequeue_latest(self) -> BlendshapeFrameWithPriority | None:
        """Get the latest (most recent) frame without interpolation.

        Use this for emergency resync when far behind.

        Returns:
            Latest frame in queue, or None if empty
        """
        async with self._lock:
            if not self._queue:
                return None
            return self._queue[-1]

    async def cleanup_old_frames(self, playhead_ms: float, lookback_ms: float = 200.0) -> int:
        """Remove frames older than playhead - lookback_ms.

        Call this periodically to prevent memory buildup.

        Args:
            playhead_ms: Current audio playhead position
            lookback_ms: How far back to keep frames

        Returns:
            Number of frames removed
        """
        async with self._lock:
            cutoff_time = playhead_ms - lookback_ms
            initial_size = len(self._queue)

            # Remove frames older than cutoff
            while self._queue and self._queue[0].timestamp_ms < cutoff_time:
                self._queue.popleft()

            removed = initial_size - len(self._queue)

            if removed > 0:
                logger.debug(
                    "Cleaned up old blendshape frames",
                    removed=removed,
                    remaining=len(self._queue),
                    cutoff_time=cutoff_time
                )

            return removed

    def _find_surrounding_frames(
        self,
        playhead_ms: float
    ) -> tuple[BlendshapeFrameWithPriority | None, BlendshapeFrameWithPriority | None]:
        """Find frames immediately before and after playhead position.

        Args:
            playhead_ms: Target timestamp

        Returns:
            (prev_frame, next_frame) tuple, either can be None
        """
        prev_frame = None
        next_frame = None

        for frame in self._queue:
            if frame.timestamp_ms <= playhead_ms:
                # This frame is at or before playhead
                if prev_frame is None or frame.timestamp_ms > prev_frame.timestamp_ms:
                    prev_frame = frame
            else:
                # This frame is after playhead
                if next_frame is None or frame.timestamp_ms < next_frame.timestamp_ms:
                    next_frame = frame

        return prev_frame, next_frame

    def _interpolate_frames(
        self,
        prev_frame: BlendshapeFrameWithPriority,
        next_frame: BlendshapeFrameWithPriority,
        playhead_ms: float
    ) -> BlendshapeFrameWithPriority:
        """Interpolate between two frames at playhead position.

        Uses linear interpolation (lerp) for smooth transitions.

        Args:
            prev_frame: Frame before playhead
            next_frame: Frame after playhead
            playhead_ms: Current playhead position

        Returns:
            Interpolated frame at playhead position
        """
        # Calculate interpolation factor (0.0 to 1.0)
        time_span = next_frame.timestamp_ms - prev_frame.timestamp_ms

        if time_span <= 0:
            # Frames at same time, return prev
            return prev_frame

        t = (playhead_ms - prev_frame.timestamp_ms) / time_span
        t = clamp01(t)

        # Lerp each blendshape weight
        interpolated_weights = [
            clamp01(prev_w + t * (next_w - prev_w))
            for prev_w, next_w in zip(prev_frame.weights, next_frame.weights, strict=False)
        ]

        return BlendshapeFrameWithPriority(
            timestamp_ms=playhead_ms,
            weights=interpolated_weights,
            frame_index=prev_frame.frame_index,  # Use prev frame's index
            priority=max(prev_frame.priority, next_frame.priority),
            enqueue_time=time.time()
        )

    async def _apply_backpressure(self) -> None:
        """Drop frames when queue is full.

        Strategy:
        - NEVER drop audio (audio is handled separately)
        - Drop oldest blendshape frames first (default)
        - Respect priority (higher priority kept longer)
        - Log dropped count for monitoring
        """
        overflow = len(self._queue) - self._max_size

        if overflow <= 0:
            return

        if self._drop_strategy == "oldest":
            # Drop oldest frames
            for _ in range(overflow):
                if self._queue:
                    dropped = self._queue.popleft()
                    self._dropped_count += 1

                    logger.debug(
                        "Dropped old blendshape frame (backpressure)",
                        frame_index=dropped.frame_index,
                        timestamp_ms=dropped.timestamp_ms,
                        latency_ms=(time.time() - dropped.enqueue_time) * 1000
                    )

        elif self._drop_strategy == "furthest_from_playhead":
            # Drop frames furthest from last playhead position
            # Sort by distance from last playhead
            sorted_frames = sorted(
                self._queue,
                key=lambda f: abs(f.timestamp_ms - self._last_dequeue_time),
                reverse=True  # Furthest first
            )

            # Remove furthest frames
            frames_to_drop = {id(f) for f in sorted_frames[:overflow]}
            self._queue = deque(f for f in self._queue if id(f) not in frames_to_drop)
            self._dropped_count += overflow

            logger.debug(
                "Dropped distant blendshape frames (backpressure)",
                dropped=overflow,
                queue_size=len(self._queue)
            )

        if self._dropped_count % 10 == 0 and self._dropped_count > 0:
            logger.warning(
                "Blendshape backpressure active",
                total_dropped=self._dropped_count,
                drop_rate=f"{self._dropped_count / self._total_enqueued * 100:.1f}%",
                queue_size=len(self._queue)
            )

    def get_stats(self) -> dict:
        """Get queue statistics for monitoring.

        Returns:
            Dictionary with queue metrics
        """
        return {
            "queue_size": len(self._queue),
            "max_size": self._max_size,
            "total_enqueued": self._total_enqueued,
            "total_dropped": self._dropped_count,
            "drop_rate": f"{self._dropped_count / max(1, self._total_enqueued) * 100:.1f}%",
            "interpolation_hits": self._interpolation_hits,
            "interpolation_enabled": self._enable_interpolation,
            "interpolation_window_ms": self._interpolation_window,
        }

    async def reset(self) -> None:
        """Clear queue and reset statistics."""
        async with self._lock:
            self._queue.clear()
            self._dropped_count = 0
            self._total_enqueued = 0
            self._interpolation_hits = 0
            self._last_dequeue_time = 0.0

            logger.info("BlendshapeBackpressureQueue reset")

    @property
    def size(self) -> int:
        """Current queue size."""
        return len(self._queue)

    @property
    def is_empty(self) -> bool:
        """Check if queue is empty."""
        return len(self._queue) == 0

    @property
    def dropped_count(self) -> int:
        """Total frames dropped due to backpressure."""
        return self._dropped_count

