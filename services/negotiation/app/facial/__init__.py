"""NVIDIA Audio2Face integration for facial animation.

This module provides async gRPC client to NVIDIA Audio2Face Microservice
and re-exposes blendshape coefficients via FastAPI WebSocket.
"""

from .arkit52 import ARKIT52_NAMES, arkit52_name_to_index
from .a2f_client import Audio2FaceClient
from .ws_stream import router as a2f_router

__all__ = [
    "ARKIT52_NAMES",
    "arkit52_name_to_index",
    "Audio2FaceClient",
    "a2f_router",
]

