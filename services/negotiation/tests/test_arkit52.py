"""Tests for ARKit-52 blendshape utilities."""

import pytest

from app.facial import (
    ARKIT52,
    ARKIT52_INDEX,
    ARKIT52_NAMES,
    arkit52_name_to_index,
    clamp01,
    create_empty_arkit52,
    validate_arkit52_weights,
)


class TestARKit52Constants:
    """Test ARKit-52 constant definitions."""

    def test_arkit52_length(self):
        """Test that ARKIT52 has exactly 52 blendshapes."""
        assert len(ARKIT52) == 52
        assert len(ARKIT52_NAMES) == 52

    def test_arkit52_alias(self):
        """Test that ARKIT52 is an alias for ARKIT52_NAMES."""
        assert ARKIT52 is ARKIT52_NAMES

    def test_arkit52_index_mapping(self):
        """Test that ARKIT52_INDEX maps all names correctly."""
        assert len(ARKIT52_INDEX) == 52

        # Test specific mappings
        assert ARKIT52_INDEX["browDownLeft"] == 0
        assert ARKIT52_INDEX["jawOpen"] == 24
        assert ARKIT52_INDEX["tongueOut"] == 51

    def test_all_names_unique(self):
        """Test that all blendshape names are unique."""
        assert len(ARKIT52) == len(set(ARKIT52))

    def test_expected_blendshapes_present(self):
        """Test that expected ARKit blendshapes are present."""
        expected = [
            "browDownLeft", "browDownRight", "browInnerUp",
            "eyeBlinkLeft", "eyeBlinkRight",
            "jawOpen", "jawForward", "jawLeft", "jawRight",
            "mouthClose", "mouthFunnel", "mouthPucker",
            "mouthSmileLeft", "mouthSmileRight",
            "tongueOut",
        ]

        for name in expected:
            assert name in ARKIT52, f"Expected blendshape '{name}' not found"


class TestArkit52NameToIndex:
    """Test arkit52_name_to_index function."""

    def test_valid_names(self):
        """Test conversion of valid blendshape names."""
        assert arkit52_name_to_index("browDownLeft") == 0
        assert arkit52_name_to_index("jawOpen") == 24
        assert arkit52_name_to_index("tongueOut") == 51

    def test_invalid_name_raises_keyerror(self):
        """Test that invalid names raise KeyError."""
        with pytest.raises(KeyError):
            arkit52_name_to_index("invalidBlendshape")

    def test_case_sensitive(self):
        """Test that name matching is case-sensitive."""
        with pytest.raises(KeyError):
            arkit52_name_to_index("jawopen")  # lowercase

        with pytest.raises(KeyError):
            arkit52_name_to_index("JAWOPEN")  # uppercase


class TestValidateArkit52Weights:
    """Test validate_arkit52_weights function."""

    def test_valid_weights(self):
        """Test validation of valid weight arrays."""
        # All zeros
        assert validate_arkit52_weights([0.0] * 52) is True

        # All ones
        assert validate_arkit52_weights([1.0] * 52) is True

        # Mixed values
        weights = [0.5] * 52
        assert validate_arkit52_weights(weights) is True

        # Random valid values - create exactly 52 valid weights
        weights = [i * 0.02 for i in range(52)]  # 0.0, 0.02, 0.04, ..., 1.02 (clamped)
        weights = [min(w, 1.0) for w in weights]  # Clamp to 1.0
        assert validate_arkit52_weights(weights) is True

    def test_invalid_length(self):
        """Test that arrays with wrong length are invalid."""
        assert validate_arkit52_weights([0.0] * 51) is False
        assert validate_arkit52_weights([0.0] * 53) is False
        assert validate_arkit52_weights([]) is False

    def test_out_of_range_values(self):
        """Test that out-of-range values are invalid."""
        # Value too large
        weights = [0.0] * 52
        weights[0] = 1.1
        assert validate_arkit52_weights(weights) is False

        # Value too small
        weights = [0.0] * 52
        weights[0] = -0.1
        assert validate_arkit52_weights(weights) is False

        # Multiple out-of-range
        weights = [2.0] * 52
        assert validate_arkit52_weights(weights) is False

    def test_boundary_values(self):
        """Test boundary values (0.0 and 1.0)."""
        weights = [0.0] * 52
        assert validate_arkit52_weights(weights) is True

        weights = [1.0] * 52
        assert validate_arkit52_weights(weights) is True


class TestCreateEmptyArkit52:
    """Test create_empty_arkit52 function."""

    def test_creates_52_zeros(self):
        """Test that function creates array of 52 zeros."""
        weights = create_empty_arkit52()
        assert len(weights) == 52
        assert all(w == 0.0 for w in weights)

    def test_creates_new_instance(self):
        """Test that each call creates a new instance."""
        weights1 = create_empty_arkit52()
        weights2 = create_empty_arkit52()

        assert weights1 is not weights2

        # Modify one and ensure other is unchanged
        weights1[0] = 1.0
        assert weights2[0] == 0.0

    def test_valid_weights(self):
        """Test that created weights are valid."""
        weights = create_empty_arkit52()
        assert validate_arkit52_weights(weights) is True


class TestClamp01:
    """Test clamp01 utility function."""

    def test_values_in_range(self):
        """Test that values in range [0, 1] are unchanged."""
        assert clamp01(0.0) == 0.0
        assert clamp01(0.5) == 0.5
        assert clamp01(1.0) == 1.0
        assert clamp01(0.123) == 0.123
        assert clamp01(0.999) == 0.999

    def test_values_above_range(self):
        """Test that values > 1.0 are clamped to 1.0."""
        assert clamp01(1.1) == 1.0
        assert clamp01(2.0) == 1.0
        assert clamp01(100.0) == 1.0
        assert clamp01(float('inf')) == 1.0

    def test_values_below_range(self):
        """Test that values < 0.0 are clamped to 0.0."""
        assert clamp01(-0.1) == 0.0
        assert clamp01(-1.0) == 0.0
        assert clamp01(-100.0) == 0.0
        assert clamp01(float('-inf')) == 0.0

    def test_boundary_values(self):
        """Test exact boundary values."""
        assert clamp01(0.0) == 0.0
        assert clamp01(1.0) == 1.0

        # Just outside boundaries
        assert clamp01(-0.0001) == 0.0
        assert clamp01(1.0001) == 1.0

    def test_float_precision(self):
        """Test float precision edge cases."""
        # Very small positive number
        assert clamp01(1e-10) == 1e-10

        # Very close to 1.0
        assert clamp01(0.9999999) == 0.9999999

        # Just over 1.0
        assert clamp01(1.0 + 1e-10) == 1.0


class TestIntegration:
    """Integration tests using multiple utilities together."""

    def test_create_and_validate(self):
        """Test creating and validating empty weights."""
        weights = create_empty_arkit52()
        assert validate_arkit52_weights(weights) is True

    def test_modify_specific_blendshape(self):
        """Test modifying a specific blendshape by name."""
        weights = create_empty_arkit52()

        # Set jawOpen to 0.5
        jaw_index = arkit52_name_to_index("jawOpen")
        weights[jaw_index] = 0.5

        assert weights[jaw_index] == 0.5
        assert validate_arkit52_weights(weights) is True

    def test_clamp_and_validate(self):
        """Test clamping invalid values and validating."""
        weights = create_empty_arkit52()

        # Set some values out of range
        raw_values = [1.5, -0.5, 0.5, 2.0, -1.0]

        # Clamp and assign
        for i, val in enumerate(raw_values):
            weights[i] = clamp01(val)

        # Should now be valid
        assert validate_arkit52_weights(weights) is True

        # Check clamped values
        assert weights[0] == 1.0  # Was 1.5
        assert weights[1] == 0.0  # Was -0.5
        assert weights[2] == 0.5  # Unchanged
        assert weights[3] == 1.0  # Was 2.0
        assert weights[4] == 0.0  # Was -1.0

    def test_realistic_facial_expression(self):
        """Test creating a realistic facial expression."""
        weights = create_empty_arkit52()

        # Create a smile expression
        weights[arkit52_name_to_index("mouthSmileLeft")] = 0.7
        weights[arkit52_name_to_index("mouthSmileRight")] = 0.7
        weights[arkit52_name_to_index("jawOpen")] = 0.2

        assert validate_arkit52_weights(weights) is True

        # Verify only modified weights are non-zero
        non_zero_count = sum(1 for w in weights if w != 0.0)
        assert non_zero_count == 3

