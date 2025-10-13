"""NVIDIA Audio2Face integration for facial animation.

This module provides async gRPC client to NVIDIA Audio2Face Microservice
and re-exposes blendshape coefficients via FastAPI WebSocket.
"""

from .arkit52 import (
    ARKIT52_NAMES,
    ARKIT52,  # Alias
    ARKIT52_INDEX,  # Alias
    arkit52_name_to_index,
    validate_arkit52_weights,
    create_empty_arkit52,
    clamp01,
)
from .a2f_client import (
    Audio2FaceClient,
    Audio2FaceStubClient,
    Audio2FaceConfig,
    BlendshapeFrame,
)
from .ws_stream import router as a2f_router

__all__ = [
    # ARKit-52 utilities
    "ARKIT52_NAMES",
    "ARKIT52",
    "ARKIT52_INDEX",
    "arkit52_name_to_index",
    "validate_arkit52_weights",
    "create_empty_arkit52",
    "clamp01",
    # A2F client
    "Audio2FaceClient",
    "Audio2FaceStubClient",
    "Audio2FaceConfig",
    "BlendshapeFrame",
    # Router
    "a2f_router",
]

