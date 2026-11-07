"""NVIDIA Audio2Face integration for facial animation.

This module provides async gRPC client to NVIDIA Audio2Face Microservice
and re-exposes blendshape coefficients via FastAPI WebSocket.

Production Features:
- Backpressure management (drop old blendshapes, never audio)
- Frame interpolation (30-50ms queue for smooth motion)
- Emotion/style controls (smile, frown, surprise biases)
- Latency monitoring (TTFB, A/V sync tracking)
- ARKit-52 GLB validation utilities
"""

from .a2f_client import (
    Audio2FaceClient,
    Audio2FaceConfig,
    Audio2FaceStubClient,
    BlendshapeFrame,
)
from .arkit52 import (
    ARKIT52,  # Alias
    ARKIT52_INDEX,  # Alias
    ARKIT52_NAMES,
    arkit52_name_to_index,
    clamp01,
    create_empty_arkit52,
    validate_arkit52_weights,
)
from .blendshape_queue import (
    BlendshapeBackpressureQueue,
    BlendshapeFrameWithPriority,
)
from .emotion_controls import (
    DIPLOMATIC_PRESETS,
    EmotionBias,
    EmotionController,
    apply_emotion_bias,
    blend_biases,
    get_preset,
)
from .testing_utils import (
    GLBValidationResult,
    LatencyMetrics,
    check_av_sync,
    create_test_blendshapes,
    log_performance_summary,
    run_production_checklist,
    validate_glb_arkit52,
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
    # Backpressure & Interpolation
    "BlendshapeBackpressureQueue",
    "BlendshapeFrameWithPriority",
    # Emotion Controls
    "EmotionBias",
    "EmotionController",
    "apply_emotion_bias",
    "get_preset",
    "blend_biases",
    "DIPLOMATIC_PRESETS",
    # Testing Utilities
    "LatencyMetrics",
    "GLBValidationResult",
    "validate_glb_arkit52",
    "check_av_sync",
    "create_test_blendshapes",
    "log_performance_summary",
    "run_production_checklist",
    # Router
    "a2f_router",
]

