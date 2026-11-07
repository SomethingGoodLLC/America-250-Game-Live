"""Emotion and style controls for A2F blendshapes.

NVIDIA's A2F model supports emotion conditioning and style controls.
This module provides utilities to bias blendshapes for emotions like:
- Smile bias (diplomatic warmth)
- Frown bias (stern/serious)
- Eyebrow raise (surprise/attention)
- Cheek raise (friendly expression)

These are additive controls that layer on top of A2F's speech-driven animation.
"""

from dataclasses import dataclass

import structlog

from .arkit52 import ARKIT52_INDEX, clamp01, validate_arkit52_weights

logger = structlog.get_logger(__name__)


@dataclass
class EmotionBias:
    """Emotion bias parameters for blendshape modification.

    Each parameter is a float from 0.0 (no effect) to 1.0 (maximum effect).
    These are multiplicative or additive biases applied to base A2F output.

    Attributes:
        smile: Increase smile intensity (0.0-1.0)
        frown: Increase frown intensity (0.0-1.0)
        surprise: Raise eyebrows for surprised expression (0.0-1.0)
        seriousness: Reduce smiling, tighten mouth (0.0-1.0)
        warmth: Soften expression, slight smile and cheek raise (0.0-1.0)
        intensity: Overall expression intensity multiplier (0.5-2.0)
    """
    smile: float = 0.0
    frown: float = 0.0
    surprise: float = 0.0
    seriousness: float = 0.0
    warmth: float = 0.0
    intensity: float = 1.0

    def __post_init__(self):
        """Validate ranges."""
        self.smile = clamp01(self.smile)
        self.frown = clamp01(self.frown)
        self.surprise = clamp01(self.surprise)
        self.seriousness = clamp01(self.seriousness)
        self.warmth = clamp01(self.warmth)
        self.intensity = max(0.5, min(2.0, self.intensity))


# Blendshape indices for quick access
_SMILE_LEFT = ARKIT52_INDEX["mouthSmileLeft"]
_SMILE_RIGHT = ARKIT52_INDEX["mouthSmileRight"]
_FROWN_LEFT = ARKIT52_INDEX["mouthFrownLeft"]
_FROWN_RIGHT = ARKIT52_INDEX["mouthFrownRight"]
_CHEEK_PUFF = ARKIT52_INDEX["cheekPuff"]
_CHEEK_SQUINT_LEFT = ARKIT52_INDEX["cheekSquintLeft"]
_CHEEK_SQUINT_RIGHT = ARKIT52_INDEX["cheekSquintRight"]
_BROW_INNER_UP = ARKIT52_INDEX["browInnerUp"]
_BROW_OUTER_UP_LEFT = ARKIT52_INDEX["browOuterUpLeft"]
_BROW_OUTER_UP_RIGHT = ARKIT52_INDEX["browOuterUpRight"]
_BROW_DOWN_LEFT = ARKIT52_INDEX["browDownLeft"]
_BROW_DOWN_RIGHT = ARKIT52_INDEX["browDownRight"]
_JAW_OPEN = ARKIT52_INDEX["jawOpen"]
_MOUTH_PUCKER = ARKIT52_INDEX["mouthPucker"]
_MOUTH_FUNNEL = ARKIT52_INDEX["mouthFunnel"]


def apply_emotion_bias(
    base_weights: list[float],
    bias: EmotionBias
) -> list[float]:
    """Apply emotion bias to base blendshape weights.

    This is the main entry point for emotion control. It takes A2F's
    base output and layers emotion biases on top.

    Args:
        base_weights: Original ARKit-52 weights from A2F
        bias: Emotion bias parameters

    Returns:
        Modified weights with emotion applied

    Raises:
        ValueError: If base_weights is invalid
    """
    if not validate_arkit52_weights(base_weights):
        raise ValueError(f"Invalid base weights: expected 52, got {len(base_weights)}")

    # Clone weights to avoid mutating input
    weights = base_weights.copy()

    # Apply global intensity multiplier
    if bias.intensity != 1.0:
        weights = [clamp01(w * bias.intensity) for w in weights]

    # Apply smile bias
    if bias.smile > 0:
        weights[_SMILE_LEFT] = clamp01(weights[_SMILE_LEFT] + bias.smile * 0.4)
        weights[_SMILE_RIGHT] = clamp01(weights[_SMILE_RIGHT] + bias.smile * 0.4)
        weights[_CHEEK_SQUINT_LEFT] = clamp01(weights[_CHEEK_SQUINT_LEFT] + bias.smile * 0.2)
        weights[_CHEEK_SQUINT_RIGHT] = clamp01(weights[_CHEEK_SQUINT_RIGHT] + bias.smile * 0.2)

    # Apply frown bias
    if bias.frown > 0:
        weights[_FROWN_LEFT] = clamp01(weights[_FROWN_LEFT] + bias.frown * 0.3)
        weights[_FROWN_RIGHT] = clamp01(weights[_FROWN_RIGHT] + bias.frown * 0.3)
        weights[_BROW_DOWN_LEFT] = clamp01(weights[_BROW_DOWN_LEFT] + bias.frown * 0.2)
        weights[_BROW_DOWN_RIGHT] = clamp01(weights[_BROW_DOWN_RIGHT] + bias.frown * 0.2)
        # Reduce smile
        weights[_SMILE_LEFT] = clamp01(weights[_SMILE_LEFT] * (1.0 - bias.frown * 0.5))
        weights[_SMILE_RIGHT] = clamp01(weights[_SMILE_RIGHT] * (1.0 - bias.frown * 0.5))

    # Apply surprise bias (raise eyebrows)
    if bias.surprise > 0:
        weights[_BROW_INNER_UP] = clamp01(weights[_BROW_INNER_UP] + bias.surprise * 0.5)
        weights[_BROW_OUTER_UP_LEFT] = clamp01(weights[_BROW_OUTER_UP_LEFT] + bias.surprise * 0.4)
        weights[_BROW_OUTER_UP_RIGHT] = clamp01(weights[_BROW_OUTER_UP_RIGHT] + bias.surprise * 0.4)
        # Slightly open mouth
        weights[_JAW_OPEN] = clamp01(weights[_JAW_OPEN] + bias.surprise * 0.1)

    # Apply seriousness (tighten expression)
    if bias.seriousness > 0:
        # Reduce smile
        weights[_SMILE_LEFT] = clamp01(weights[_SMILE_LEFT] * (1.0 - bias.seriousness * 0.7))
        weights[_SMILE_RIGHT] = clamp01(weights[_SMILE_RIGHT] * (1.0 - bias.seriousness * 0.7))
        # Slight mouth tension
        weights[_MOUTH_PUCKER] = clamp01(weights[_MOUTH_PUCKER] + bias.seriousness * 0.1)
        # Lower brows slightly
        weights[_BROW_DOWN_LEFT] = clamp01(weights[_BROW_DOWN_LEFT] + bias.seriousness * 0.15)
        weights[_BROW_DOWN_RIGHT] = clamp01(weights[_BROW_DOWN_RIGHT] + bias.seriousness * 0.15)

    # Apply warmth (diplomatic friendliness)
    if bias.warmth > 0:
        weights[_SMILE_LEFT] = clamp01(weights[_SMILE_LEFT] + bias.warmth * 0.25)
        weights[_SMILE_RIGHT] = clamp01(weights[_SMILE_RIGHT] + bias.warmth * 0.25)
        weights[_CHEEK_SQUINT_LEFT] = clamp01(weights[_CHEEK_SQUINT_LEFT] + bias.warmth * 0.15)
        weights[_CHEEK_SQUINT_RIGHT] = clamp01(weights[_CHEEK_SQUINT_RIGHT] + bias.warmth * 0.15)
        weights[_BROW_OUTER_UP_LEFT] = clamp01(weights[_BROW_OUTER_UP_LEFT] + bias.warmth * 0.1)
        weights[_BROW_OUTER_UP_RIGHT] = clamp01(weights[_BROW_OUTER_UP_RIGHT] + bias.warmth * 0.1)

    return weights


# Preset emotion biases for common diplomatic scenarios
DIPLOMATIC_PRESETS: dict[str, EmotionBias] = {
    "neutral": EmotionBias(
        smile=0.0,
        frown=0.0,
        surprise=0.0,
        seriousness=0.0,
        warmth=0.0,
        intensity=1.0
    ),
    "friendly": EmotionBias(
        smile=0.3,
        warmth=0.4,
        intensity=1.1
    ),
    "diplomatic": EmotionBias(
        smile=0.15,
        warmth=0.3,
        seriousness=0.1,
        intensity=1.0
    ),
    "stern": EmotionBias(
        seriousness=0.6,
        frown=0.2,
        intensity=1.2
    ),
    "concerned": EmotionBias(
        surprise=0.3,
        seriousness=0.3,
        frown=0.1,
        intensity=1.1
    ),
    "negotiating": EmotionBias(
        seriousness=0.4,
        warmth=0.2,
        intensity=1.0
    ),
}


def get_preset(name: str) -> EmotionBias | None:
    """Get a preset emotion bias by name.

    Args:
        name: Preset name (e.g., "friendly", "stern")

    Returns:
        EmotionBias instance, or None if not found
    """
    return DIPLOMATIC_PRESETS.get(name)


def blend_biases(
    bias_a: EmotionBias,
    bias_b: EmotionBias,
    t: float
) -> EmotionBias:
    """Blend between two emotion biases.

    Useful for smooth transitions between emotional states.

    Args:
        bias_a: Starting emotion bias
        bias_b: Target emotion bias
        t: Blend factor (0.0 = all A, 1.0 = all B)

    Returns:
        Blended emotion bias
    """
    t = clamp01(t)

    return EmotionBias(
        smile=bias_a.smile + t * (bias_b.smile - bias_a.smile),
        frown=bias_a.frown + t * (bias_b.frown - bias_a.frown),
        surprise=bias_a.surprise + t * (bias_b.surprise - bias_a.surprise),
        seriousness=bias_a.seriousness + t * (bias_b.seriousness - bias_a.seriousness),
        warmth=bias_a.warmth + t * (bias_b.warmth - bias_a.warmth),
        intensity=bias_a.intensity + t * (bias_b.intensity - bias_a.intensity)
    )


class EmotionController:
    """Stateful controller for managing emotion transitions.

    This class maintains current emotion state and handles smooth
    transitions between different emotional expressions.

    Usage:
        controller = EmotionController()
        controller.set_emotion("friendly", transition_duration=1.0)

        # In render loop:
        current_bias = controller.update(delta_time)
        weights = apply_emotion_bias(base_weights, current_bias)
    """

    def __init__(self, initial_emotion: str = "neutral"):
        """Initialize emotion controller.

        Args:
            initial_emotion: Starting emotion preset name
        """
        self.current_bias = get_preset(initial_emotion) or EmotionBias()
        self.target_bias = self.current_bias
        self.transition_progress = 1.0  # 0.0 to 1.0
        self.transition_duration = 0.0
        self.transition_elapsed = 0.0

        logger.info(
            "EmotionController initialized",
            initial_emotion=initial_emotion
        )

    def set_emotion(
        self,
        emotion: str,
        transition_duration: float = 0.5
    ) -> None:
        """Set target emotion with smooth transition.

        Args:
            emotion: Target emotion preset name
            transition_duration: Transition time in seconds (0 = instant)
        """
        target = get_preset(emotion)
        if target is None:
            logger.warning(f"Unknown emotion preset: {emotion}")
            return

        self.target_bias = target
        self.transition_duration = max(0.0, transition_duration)
        self.transition_elapsed = 0.0
        self.transition_progress = 0.0 if transition_duration > 0 else 1.0

        logger.info(
            "Emotion transition started",
            target_emotion=emotion,
            duration=transition_duration
        )

    def set_custom_bias(
        self,
        bias: EmotionBias,
        transition_duration: float = 0.5
    ) -> None:
        """Set custom emotion bias with transition.

        Args:
            bias: Custom emotion bias
            transition_duration: Transition time in seconds
        """
        self.target_bias = bias
        self.transition_duration = max(0.0, transition_duration)
        self.transition_elapsed = 0.0
        self.transition_progress = 0.0 if transition_duration > 0 else 1.0

    def update(self, delta_time: float) -> EmotionBias:
        """Update emotion state and return current bias.

        Call this every frame in your render loop.

        Args:
            delta_time: Time since last update in seconds

        Returns:
            Current emotion bias (interpolated if transitioning)
        """
        if self.transition_progress >= 1.0:
            # Transition complete
            return self.current_bias

        # Update transition progress
        self.transition_elapsed += delta_time

        if self.transition_duration > 0:
            self.transition_progress = self.transition_elapsed / self.transition_duration
            self.transition_progress = clamp01(self.transition_progress)
        else:
            self.transition_progress = 1.0

        # Blend between current and target
        if self.transition_progress >= 1.0:
            # Transition complete
            self.current_bias = self.target_bias
            logger.debug("Emotion transition completed")
        else:
            # Still transitioning, blend
            self.current_bias = blend_biases(
                self.current_bias,
                self.target_bias,
                self.transition_progress
            )

        return self.current_bias

    def get_current_bias(self) -> EmotionBias:
        """Get current emotion bias without updating.

        Returns:
            Current emotion bias
        """
        return self.current_bias

    def is_transitioning(self) -> bool:
        """Check if currently transitioning between emotions.

        Returns:
            True if transition in progress
        """
        return self.transition_progress < 1.0

