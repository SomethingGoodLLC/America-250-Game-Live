"""Placeholder video source that generates a simple looping animation."""

import asyncio
import math
from typing import AsyncGenerator, Optional
try:
    import numpy as np
except ImportError:
    np = None
import structlog

from .base import BaseVideoSource, VideoFrame
from ..types import VideoSourceConfig


class PlaceholderLoopVideoSource(BaseVideoSource):
    """Video source that generates a simple animated placeholder avatar.

    This creates a diplomatic-style avatar with subtle animations:
    - Gentle pulsing background
    - Smooth color transitions
    - Subtle geometric patterns
    """

    def __init__(self, config: VideoSourceConfig):
        super().__init__(config)
        self.phase = 0.0  # Animation phase
        self.animation_speed = 0.02  # Animation speed multiplier

        # Avatar styling parameters
        self.background_colors = [
            (50, 100, 150),    # Deep blue
            (60, 120, 170),    # Medium blue
            (70, 140, 190),    # Light blue
        ]

        self.accent_colors = [
            (200, 200, 220),   # Light gray
            (180, 180, 200),   # Slightly warmer gray
        ]

    async def start(self) -> None:
        """Start the placeholder video source."""
        self.logger.info("Starting placeholder video source", style=self.config.avatar_style)
        self._is_running = True
        self.phase = 0.0

    async def stop(self) -> None:
        """Stop the placeholder video source."""
        self.logger.info("Stopping placeholder video source")
        self._is_running = False

    async def get_frame(self) -> Optional[VideoFrame]:
        """Get the next frame in the animation sequence."""
        if not self._is_running:
            return None

        # Generate frame data
        width, height = self.config.resolution
        frame_data = await self._generate_frame_data(width, height)

        # Create frame object
        frame = VideoFrame(
            data=frame_data.tobytes(),
            timestamp=asyncio.get_event_loop().time(),
            width=width,
            height=height,
            format="rgb24"
        )

        self._frame_count += 1
        self.phase += self.animation_speed

        return frame

    async def stream_frames(self) -> AsyncGenerator[VideoFrame, None]:
        """Stream frames continuously."""
        while self._is_running:
            frame = await self.get_frame()
            if frame:
                yield frame

            # Control frame rate
            await asyncio.sleep(1.0 / self.config.framerate)

    async def _generate_frame_data(self, width: int, height: int):
        """Generate the actual frame pixel data."""
        if np is None:
            # Fallback to basic array if numpy not available
            return [[[(50, 100, 150) for _ in range(width)] for _ in range(height)]]
        
        # Create base frame
        frame = np.zeros((height, width, 3), dtype=np.uint8)

        # Add background gradient
        await self._draw_background_gradient(frame)

        # Add animated elements based on avatar style
        if self.config.avatar_style == "diplomatic":
            await self._draw_diplomatic_elements(frame)
        elif self.config.avatar_style == "formal":
            await self._draw_formal_elements(frame)
        else:
            await self._draw_simple_elements(frame)

        return frame

    async def _draw_background_gradient(self, frame: np.ndarray) -> None:
        """Draw animated background gradient."""
        height, width, _ = frame.shape

        # Create pulsing background based on animation phase
        pulse = (math.sin(self.phase * 2) + 1) / 2  # 0 to 1
        color_index = int(pulse * (len(self.background_colors) - 1))
        base_color = self.background_colors[color_index]

        # Create vertical gradient
        for y in range(height):
            # Gradient from dark to light
            gradient_factor = y / height
            color = (
                int(base_color[0] * (0.7 + 0.3 * gradient_factor)),
                int(base_color[1] * (0.7 + 0.3 * gradient_factor)),
                int(base_color[2] * (0.7 + 0.3 * gradient_factor))
            )
            frame[y, :] = color

    async def _draw_diplomatic_elements(self, frame: np.ndarray) -> None:
        """Draw diplomatic-style avatar elements."""
        height, width, _ = frame.shape
        center_x, center_y = width // 2, height // 2

        # Draw pulsing circle (representing diplomatic presence)
        radius = min(width, height) // 6
        animated_radius = int(radius * (0.8 + 0.2 * math.sin(self.phase * 3)))

        # Draw outer circle
        for y in range(max(0, center_y - animated_radius),
                      min(height, center_y + animated_radius + 1)):
            for x in range(max(0, center_x - animated_radius),
                          min(width, center_x + animated_radius + 1)):
                dx, dy = x - center_x, y - center_y
                distance = math.sqrt(dx*dx + dy*dy)

                if distance <= animated_radius:
                    # Fade out towards edge
                    alpha = 1.0 - (distance / animated_radius)
                    color = self.accent_colors[0] if distance <= animated_radius * 0.7 else (100, 100, 120)
                    frame[y, x] = (
                        int(frame[y, x][0] * (1 - alpha) + color[0] * alpha),
                        int(frame[y, x][1] * (1 - alpha) + color[1] * alpha),
                        int(frame[y, x][2] * (1 - alpha) + color[2] * alpha)
                    )

    async def _draw_formal_elements(self, frame: np.ndarray) -> None:
        """Draw formal business-style avatar elements."""
        height, width, _ = frame.shape

        # Draw horizontal lines (representing formal structure)
        line_spacing = height // 8
        for i in range(1, 7):
            y = i * line_spacing
            # Animated line opacity
            opacity = 0.3 + 0.2 * math.sin(self.phase + i * 0.5)

            if 0 <= y < height:
                for x in range(width):
                    if 0 <= x < width:
                        frame[y, x] = (
                            int(frame[y, x][0] * (1 - opacity) + 200 * opacity),
                            int(frame[y, x][1] * (1 - opacity) + 200 * opacity),
                            int(frame[y, x][2] * (1 - opacity) + 220 * opacity)
                        )

    async def _draw_simple_elements(self, frame: np.ndarray) -> None:
        """Draw simple geometric elements."""
        height, width, _ = frame.shape
        center_x, center_y = width // 2, height // 2

        # Draw simple rotating square
        size = min(width, height) // 8
        angle = self.phase * 2

        # Calculate square corners
        corners = []
        for i in range(4):
            corner_angle = angle + i * math.pi / 2
            x = center_x + int(size * math.cos(corner_angle))
            y = center_y + int(size * math.sin(corner_angle))
            corners.append((x, y))

        # Draw square outline
        for i in range(4):
            x1, y1 = corners[i]
            x2, y2 = corners[(i + 1) % 4]

            # Simple line drawing
            await self._draw_line(frame, x1, y1, x2, y2, (220, 220, 220))

    async def _draw_line(self, frame: np.ndarray, x1: int, y1: int, x2: int, y2: int, color: tuple) -> None:
        """Draw a simple line on the frame."""
        # Bresenham's line algorithm (simplified)
        dx = abs(x2 - x1)
        dy = abs(y2 - y1)
        sx = 1 if x1 < x2 else -1
        sy = 1 if y1 < y2 else -1
        err = dx - dy

        x, y = x1, y1
        while True:
            if 0 <= x < frame.shape[1] and 0 <= y < frame.shape[0]:
                frame[y, x] = color

            if x == x2 and y == y2:
                break

            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x += sx
            if e2 < dx:
                err += dx
                y += sy
