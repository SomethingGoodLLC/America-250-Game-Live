"""
Portrait animation module for real-time talking head generation.

Supports:
- LivePortrait: Fast single-image talking head
- Ditto: Diffusion-based portrait animation (optimized for realtime)
"""

from app.portrait.liveportrait_service import LivePortraitService

__all__ = ["LivePortraitService"]

