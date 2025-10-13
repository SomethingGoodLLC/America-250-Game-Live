"""ARKit-52 facial blendshape definitions.

Canonical ARKit blendshape names and ordering.
Reference: https://developer.apple.com/documentation/arkit/arfaceanchor/blendshapelocation
"""

from typing import Dict, List

# Canonical ARKit-52 blendshape names in order
# These match the morph target names expected in GLB models
ARKIT52_NAMES: List[str] = [
    "browDownLeft",
    "browDownRight",
    "browInnerUp",
    "browOuterUpLeft",
    "browOuterUpRight",
    "cheekPuff",
    "cheekSquintLeft",
    "cheekSquintRight",
    "eyeBlinkLeft",
    "eyeBlinkRight",
    "eyeLookDownLeft",
    "eyeLookDownRight",
    "eyeLookInLeft",
    "eyeLookInRight",
    "eyeLookOutLeft",
    "eyeLookOutRight",
    "eyeLookUpLeft",
    "eyeLookUpRight",
    "eyeSquintLeft",
    "eyeSquintRight",
    "eyeWideLeft",
    "eyeWideRight",
    "jawForward",
    "jawLeft",
    "jawOpen",
    "jawRight",
    "mouthClose",
    "mouthDimpleLeft",
    "mouthDimpleRight",
    "mouthFrownLeft",
    "mouthFrownRight",
    "mouthFunnel",
    "mouthLeft",
    "mouthLowerDownLeft",
    "mouthLowerDownRight",
    "mouthPressLeft",
    "mouthPressRight",
    "mouthPucker",
    "mouthRight",
    "mouthRollLower",
    "mouthRollUpper",
    "mouthShrugLower",
    "mouthShrugUpper",
    "mouthSmileLeft",
    "mouthSmileRight",
    "mouthStretchLeft",
    "mouthStretchRight",
    "mouthUpperUpLeft",
    "mouthUpperUpRight",
    "noseSneerLeft",
    "noseSneerRight",
    "tongueOut",
]

# Create name-to-index mapping for quick lookup
_arkit52_index_map: Dict[str, int] = {
    name: idx for idx, name in enumerate(ARKIT52_NAMES)
}


def arkit52_name_to_index(name: str) -> int:
    """Convert ARKit blendshape name to index.
    
    Args:
        name: ARKit blendshape name (e.g., "jawOpen")
        
    Returns:
        Index in ARKIT52_NAMES list
        
    Raises:
        KeyError: If name is not a valid ARKit blendshape
    """
    return _arkit52_index_map[name]


def validate_arkit52_weights(weights: List[float]) -> bool:
    """Validate that weights list matches ARKit-52 specification.
    
    Args:
        weights: List of blendshape weights (0.0 to 1.0)
        
    Returns:
        True if valid, False otherwise
    """
    if len(weights) != 52:
        return False
    
    # Check all weights are in valid range
    return all(0.0 <= w <= 1.0 for w in weights)


def create_empty_arkit52() -> List[float]:
    """Create an empty ARKit-52 weights array (all zeros).
    
    Returns:
        List of 52 zeros
    """
    return [0.0] * 52


def clamp01(x: float) -> float:
    """Clamp a value to the range [0.0, 1.0].
    
    Args:
        x: Value to clamp
        
    Returns:
        Value clamped to [0.0, 1.0]
    """
    return 0.0 if x < 0 else 1.0 if x > 1 else x


# Alternative names for compatibility
ARKIT52 = ARKIT52_NAMES  # Alias for backward compatibility
ARKIT52_INDEX = _arkit52_index_map  # Alias for backward compatibility

