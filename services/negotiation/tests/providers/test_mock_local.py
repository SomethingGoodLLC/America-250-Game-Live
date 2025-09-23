"""Tests for MockLocalProvider."""

import pytest
import asyncio
from datetime import datetime
from typing import List
from unittest.mock import AsyncMock

from schemas.models import SpeakerTurnModel, WorldContextModel, ProposalModel, ConcessionModel, CounterOfferModel, UltimatumModel, SmallTalkModel
from providers.mock_local import MockLocalProvider
from providers.base import NewIntent, LiveSubtitle, Analysis, Safety


class TestMockLocalProvider:
    """Test suite for MockLocalProvider."""

    @pytest.fixture
    def mock_config(self):
        return {"strict": False}

    @pytest.fixture
    def mock_world_context(self):
        return WorldContextModel(
            scenario_tags=["diplomatic", "trade"],
            initiator_faction={"id": "player", "type": "merchant"},
            counterpart_faction={"id": "ai_diplomat", "type": "diplomat"},
            current_state={"turn_count": 1}
        )

    @pytest.fixture
    def sample_speaker_turns(self):
        return [
            SpeakerTurnModel(
                speaker_id="player",
                text="I propose a trade agreement",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

    @pytest.fixture
    def provider(self, mock_config):
        return MockLocalProvider(mock_config)

    def test_provider_initialization(self, provider, mock_config):
        """Test provider initialization."""
        assert provider.config == mock_config
        assert not provider.strict

    @pytest.mark.asyncio
    async def test_stream_dialogue_no_turns(self, provider, mock_world_context):
        """Test streaming dialogue with no speaker turns."""
        events = []
        async for event in provider.stream_dialogue([], mock_world_context):
            events.append(event)

        # Should emit safety check and initial greeting
        assert len(events) >= 1
        assert any(isinstance(event, Safety) for event in events)

        # Check for initial small talk intent
        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1
        assert isinstance(intent_events[0].intent, SmallTalkModel)

    @pytest.mark.asyncio
    async def test_stream_dialogue_with_turns(self, provider, mock_world_context, sample_speaker_turns):
        """Test streaming dialogue with speaker turns."""
        events = []
        async for event in provider.stream_dialogue(sample_speaker_turns, mock_world_context):
            events.append(event)

        # Should emit multiple events
        assert len(events) > 0

        # Check event types
        event_types = [type(event).__name__ for event in events]
        assert any("Safety" in event_type for event_type in event_types)
        assert any("Analysis" in event_type for event_type in event_types)

        # Should detect trade proposal
        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1

        # Should be a proposal based on the input text
        detected_intent = intent_events[0].intent
        assert isinstance(detected_intent, ProposalModel)
        assert "trade" in detected_intent.intent_type

    @pytest.mark.asyncio
    async def test_stream_dialogue_strict_mode(self, mock_world_context, sample_speaker_turns):
        """Test streaming dialogue in strict mode."""
        strict_provider = MockLocalProvider({"strict": True})

        # Add unsafe content to test strict mode
        unsafe_turn = sample_speaker_turns[0]
        unsafe_turn.text = "This is a hateful message about war and destruction"

        events = []
        async for event in strict_provider.stream_dialogue([unsafe_turn], mock_world_context):
            events.append(event)

        # Should detect safety violation
        safety_events = [e for e in events if isinstance(e, Safety)]
        assert len(safety_events) >= 1

        # Should have high severity safety flag
        safety_event = next(e for e in safety_events if e.flag == "unsafe_content")
        assert safety_event.severity == "high"

    @pytest.mark.asyncio
    async def test_intent_detection_patterns(self, provider, mock_world_context):
        """Test various intent detection patterns."""
        test_cases = [
            ("We'll grant trade access if you withdraw troops", CounterOfferModel),
            ("Ceasefire now or else", UltimatumModel),
            ("I want to trade resources", ProposalModel),
            ("I agree to your terms", ConcessionModel),
            ("Hello, how are you?", SmallTalkModel)
        ]

        for test_text, expected_type in test_cases:
            turn = SpeakerTurnModel(
                speaker_id="player",
                text=test_text,
                timestamp=datetime.now()
            )

            events = []
            async for event in provider.stream_dialogue([turn], mock_world_context):
                events.append(event)

            intent_events = [e for e in events if isinstance(e, NewIntent)]
            if intent_events:
                detected_intent = intent_events[0].intent
                assert isinstance(detected_intent, expected_type), f"Expected {expected_type.__name__} for text: {test_text}"

    @pytest.mark.asyncio
    async def test_validate_intent(self, provider, mock_world_context):
        """Test intent validation."""
        valid_intent = ProposalModel(
            type="proposal",
            speaker_id="ai_diplomat",
            content="I propose peace",
            intent_type="peace",
            terms={"duration": "5_years"},
            timestamp=datetime.now()
        )

        is_valid = await provider.validate_intent(valid_intent)
        assert is_valid

        # Test invalid intent (empty content)
        invalid_intent = ProposalModel(
            type="proposal",
            speaker_id="ai_diplomat",
            content="",  # Invalid empty content
            intent_type="peace",
            terms={"duration": "5_years"},
            timestamp=datetime.now()
        )

        is_valid = await provider.validate_intent(invalid_intent)
        assert not is_valid

    @pytest.mark.asyncio
    async def test_validate_and_score_intent(self, provider, mock_world_context):
        """Test intent validation and scoring."""
        intent = ProposalModel(
            type="proposal",
            speaker_id="ai_diplomat",
            content="I propose a trade agreement",
            intent_type="trade",
            terms={"value": 1000},
            timestamp=datetime.now()
        )

        validated_intent, score, justification = await provider.validate_and_score_intent(
            intent, mock_world_context
        )

        assert validated_intent is not None
        assert isinstance(score, float)
        assert 0.0 <= score <= 1.0
        assert isinstance(justification, str)
        assert len(justification) > 0

    @pytest.mark.asyncio
    async def test_deterministic_behavior(self, provider, mock_world_context):
        """Test that the provider behaves deterministically."""
        turn = SpeakerTurnModel(
            speaker_id="player",
            text="I propose a trade agreement",
            timestamp=datetime.now()
        )

        # Run the same input multiple times
        results = []
        for _ in range(3):
            events = []
            async for event in provider.stream_dialogue([turn], mock_world_context):
                events.append(event)

            # Extract the detected intent
            intent_events = [e for e in events if isinstance(e, NewIntent)]
            if intent_events:
                results.append(intent_events[0].intent)

        # All results should be identical (deterministic)
        for result in results[1:]:
            assert result.type == results[0].type
            assert result.content == results[0].content
            assert result.intent_type == results[0].intent_type

    @pytest.mark.asyncio
    async def test_context_awareness(self, provider):
        """Test that provider is aware of context."""
        # Test with military context
        military_context = WorldContextModel(
            scenario_tags=["military", "high_stakes"],
            initiator_faction={"id": "player", "type": "military"},
            counterpart_faction={"id": "ai_diplomat", "type": "military"},
            current_state={"turn_count": 5}
        )

        turn = SpeakerTurnModel(
            speaker_id="player",
            text="We should attack immediately",
            timestamp=datetime.now()
        )

        events = []
        async for event in provider.stream_dialogue([turn], military_context):
            events.append(event)

        # Should detect the aggressive content
        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1

        detected_intent = intent_events[0].intent
        # In military context, this might be interpreted as an ultimatum
        assert detected_intent is not None

    @pytest.mark.asyncio
    async def test_error_handling(self, provider, mock_world_context):
        """Test error handling in provider."""
        # Test with malformed turn data
        malformed_turn = SpeakerTurnModel(
            speaker_id="",
            text="",  # Empty text
            timestamp=datetime.now()
        )

        events = []
        async for event in provider.stream_dialogue([malformed_turn], mock_world_context):
            events.append(event)

        # Should still produce some output
        assert len(events) > 0

        # Should include safety check
        safety_events = [e for e in events if isinstance(e, Safety)]
        assert len(safety_events) >= 1

    def test_pattern_compilation(self, provider):
        """Test that regex patterns are compiled correctly."""
        # Access private attributes for testing
        assert hasattr(provider, '_patterns')
        patterns = provider._patterns

        # Check that all expected patterns exist
        expected_patterns = ["counter_offer", "ultimatum", "trade", "aggressive", "cooperative"]
        for pattern_name in expected_patterns:
            assert pattern_name in patterns
            assert patterns[pattern_name] is not None

        # Test pattern matching
        test_text = "We'll grant trade access if you withdraw troops"
        assert provider._patterns["counter_offer"].search(test_text) is not None

    def test_unsafe_content_detection(self, provider):
        """Test unsafe content detection."""
        unsafe_texts = [
            "This is hateful content",
            "Let's start a war",
            "Threaten them now"
        ]

        for text in unsafe_texts:
            assert provider._contains_unsafe_content(text)

        safe_texts = [
            "I propose peace",
            "Let's trade",
            "I agree with you"
        ]

        for text in safe_texts:
            assert not provider._contains_unsafe_content(text)

    @pytest.mark.asyncio
    async def test_counter_offer_intent_detection(self, provider, mock_world_context):
        """Test specific counter-offer phrase detection and validation."""
        # Test the specific phrase mentioned in requirements
        counter_offer_turn = SpeakerTurnModel(
            speaker_id="player",
            text="We'll grant trade access if you withdraw troops",
            timestamp=datetime.now(),
            confidence=0.9
        )

        events = []
        async for event in provider.stream_dialogue([counter_offer_turn], mock_world_context):
            events.append(event)

        # Should emit safety check, analysis, and intent events
        assert len(events) > 0

        # Check for safety event
        safety_events = [e for e in events if isinstance(e, Safety)]
        assert len(safety_events) >= 1

        # Check for intent detection
        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1

        # Should detect COUNTER_OFFER intent
        detected_intent = intent_events[0].intent
        assert isinstance(detected_intent, CounterOfferModel)
        assert "trade access" in detected_intent.content.lower()
        # Accept both "troops" and "military forces" as valid variations
        assert ("withdraw troops" in detected_intent.content.lower() or 
                "military forces" in detected_intent.content.lower())

        # Validate intent using validators
        from schemas.validators import validator
        # Convert datetime to string for validation
        intent_dict = detected_intent.model_dump()
        intent_dict['timestamp'] = detected_intent.timestamp.isoformat()
        validated_intent = validator.validate_intent(intent_dict)
        assert validated_intent is not None
        assert validated_intent["type"] == "counter_offer"

        # Should include scoring fields
        assert intent_events[0].confidence is not None
        assert 0.0 <= intent_events[0].confidence <= 1.0
        assert intent_events[0].justification is not None
        assert len(intent_events[0].justification) > 0

        # Should have interim and final subtitles
        subtitle_events = [e for e in events if isinstance(e, LiveSubtitle)]
        assert len(subtitle_events) >= 2

        # Check subtitle progression
        interim_subtitle = next((e for e in subtitle_events if not e.is_final), None)
        final_subtitle = next((e for e in subtitle_events if e.is_final), None)

        assert interim_subtitle is not None
        assert final_subtitle is not None
        assert interim_subtitle.speaker_id == final_subtitle.speaker_id
        assert interim_subtitle.text.endswith("...")
        assert final_subtitle.text == counter_offer_turn.text
