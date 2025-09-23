"""Tests for Veo3Provider (stub implementation)."""

import pytest
import asyncio
from datetime import datetime
from typing import List
from unittest.mock import AsyncMock, patch

from schemas.models import SpeakerTurnModel, WorldContextModel, ProposalModel, ConcessionModel, CounterOfferModel, UltimatumModel, SmallTalkModel
from providers.gemini_veo3 import Veo3Provider
from providers.base import NewIntent, LiveSubtitle, Analysis, Safety


class TestVeo3Provider:
    """Test suite for Veo3Provider stub implementation."""

    @pytest.fixture
    def mock_config(self):
        return {
            "api_key": "test_key",
            "llm_model": "gemini-2.5-pro",
            "veo_model": "gemini-veo3",
            "avatar_style": "diplomatic",
            "voice_id": "diplomat_en_us",
            "latency_target_ms": 150
        }

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
                text="I propose a trade agreement with mutual benefits",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

    @pytest.fixture
    def provider(self, mock_config):
        return Veo3Provider(
            avatar_style=mock_config.get("avatar_style", "diplomatic"),
            voice_id=mock_config.get("voice_id", "diplomat_en_us"),
            latency_target_ms=mock_config.get("latency_target_ms", 150),
            use_veo3=False  # Use placeholder mode for testing
        )

    def test_provider_initialization(self, provider, mock_config):
        """Test provider initialization."""
        assert provider.config == mock_config
        assert provider.api_key == "test_key"
        assert provider.llm_model == "gemini-2.5-pro"
        assert provider.veo_model == "gemini-veo3"
        assert provider.avatar_style == "diplomatic"
        assert provider.voice_id == "diplomat_en_us"
        assert provider.latency_target_ms == 150

    def test_mock_function_schemas(self, provider):
        """Test that mock function schemas are properly defined."""
        schemas = provider._mock_function_schemas
        assert "detect_intent" in schemas

        detect_intent_schema = schemas["detect_intent"]
        assert detect_intent_schema["name"] == "detect_intent"
        assert detect_intent_schema["description"] == "Detect diplomatic intent from conversation"

        properties = detect_intent_schema["parameters"]["properties"]
        assert "intent_type" in properties
        assert "content" in properties
        assert "confidence" in properties
        assert "terms" in properties

        required = detect_intent_schema["parameters"]["required"]
        assert "intent_type" in required
        assert "content" in required
        assert "confidence" in required

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

        # Should include live subtitles
        subtitle_events = [e for e in events if isinstance(e, LiveSubtitle)]
        assert len(subtitle_events) >= 2  # Initial partial and final

        # Check subtitle progression
        partial_subtitle = subtitle_events[0]
        final_subtitle = subtitle_events[1]

        assert not partial_subtitle.is_final
        assert final_subtitle.is_final
        assert partial_subtitle.text.endswith("...")
        assert final_subtitle.text == sample_speaker_turns[0].text
        assert partial_subtitle.speaker_id == final_subtitle.speaker_id

        # Should detect intent
        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1

        detected_intent = intent_events[0].intent
        assert detected_intent is not None

    @pytest.mark.asyncio
    async def test_stream_dialogue_no_turns(self, provider, mock_world_context):
        """Test streaming dialogue with no turns."""
        events = []
        async for event in provider.stream_dialogue([], mock_world_context):
            events.append(event)

        # Should still emit safety check
        assert len(events) > 0
        safety_events = [e for e in events if isinstance(e, Safety)]
        assert len(safety_events) >= 1

    @pytest.mark.asyncio
    async def test_mock_intent_detection(self, provider, mock_world_context):
        """Test mock intent detection patterns."""
        test_cases = [
            ("trade", ProposalModel),
            ("concede", ConcessionModel),
            ("counter", CounterOfferModel),
            ("or else", UltimatumModel)
        ]

        for keyword, expected_type in test_cases:
            turn = SpeakerTurnModel(
                speaker_id="player",
                text=f"I want to {keyword} with you",
                timestamp=datetime.now()
            )

            events = []
            async for event in provider.stream_dialogue([turn], mock_world_context):
                events.append(event)

            intent_events = [e for e in events if isinstance(e, NewIntent)]
            if intent_events:
                detected_intent = intent_events[0].intent
                assert isinstance(detected_intent, expected_type), f"Expected {expected_type.__name__} for keyword: {keyword}"

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

        # Test invalid intent (too long content)
        invalid_intent = ProposalModel(
            type="proposal",
            speaker_id="ai_diplomat",
            content="A" * 2000,  # Too long
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
        assert "pattern" in justification.lower()

    @pytest.mark.asyncio
    async def test_processing_delay_simulation(self, provider, mock_world_context, sample_speaker_turns):
        """Test that processing delays are simulated."""
        start_time = datetime.now()

        events = []
        async for event in provider.stream_dialogue(sample_speaker_turns, mock_world_context):
            events.append(event)

        end_time = datetime.now()
        processing_time = (end_time - start_time).total_seconds()

        # Should take some time due to simulated delays
        assert processing_time > 0.1  # At least 100ms

        # Should have subtitle events with time gaps
        subtitle_events = [e for e in events if isinstance(e, LiveSubtitle)]
        assert len(subtitle_events) >= 2

    @pytest.mark.asyncio
    async def test_error_handling(self, provider, mock_world_context):
        """Test error handling in provider."""
        # Test with empty context
        empty_context = WorldContextModel(
            scenario_tags=[],
            initiator_faction={},
            counterpart_faction={},
            current_state={}
        )

        turn = SpeakerTurnModel(
            speaker_id="player",
            text="Test message",
            timestamp=datetime.now()
        )

        events = []
        async for event in provider.stream_dialogue([turn], empty_context):
            events.append(event)

        # Should still produce output despite empty context
        assert len(events) > 0

    @pytest.mark.asyncio
    async def test_close_method(self, provider):
        """Test provider cleanup."""
        # Should not raise any exceptions
        await provider.close()
        assert True  # If we get here, close() worked

    def test_configuration_access(self, provider):
        """Test configuration access methods."""
        assert provider.config["api_key"] == "test_key"
        assert provider.config["avatar_style"] == "diplomatic"
        assert provider.config["latency_target_ms"] == 150

    @pytest.mark.asyncio
    async def test_multiple_turn_processing(self, provider, mock_world_context):
        """Test processing multiple speaker turns."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player",
                text="I propose a trade",
                timestamp=datetime.now()
            ),
            SpeakerTurnModel(
                speaker_id="ai_diplomat",
                text="I accept your proposal",
                timestamp=datetime.now()
            ),
            SpeakerTurnModel(
                speaker_id="player",
                text="Let's finalize the deal",
                timestamp=datetime.now()
            )
        ]

        events = []
        async for event in provider.stream_dialogue(turns, mock_world_context):
            events.append(event)

        # Should process all turns
        assert len(events) > 0

        # Should include analysis event with turn count
        analysis_events = [e for e in events if isinstance(e, Analysis)]
        assert len(analysis_events) >= 1

        # Check analysis payload
        analysis_payload = analysis_events[0].payload
        assert "turn_count" in analysis_payload
        assert analysis_payload["turn_count"] == len(turns)

    @pytest.mark.asyncio
    async def test_intent_confidence_scoring(self, provider, mock_world_context):
        """Test that intent confidence is properly scored."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player",
                text="I definitely want to trade with you",
                timestamp=datetime.now(),
                confidence=0.95
            )
        ]

        events = []
        async for event in provider.stream_dialogue(turns, mock_world_context):
            events.append(event)

        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1

        detected_intent = intent_events[0].intent

        # Mock detection should assign reasonable confidence
        if hasattr(detected_intent, 'confidence'):
            assert 0.0 <= detected_intent.confidence <= 1.0

        # Provider confidence should also be reasonable
        assert 0.0 <= intent_events[0].confidence <= 1.0

    @pytest.mark.asyncio
    async def test_ultimatum_intent_detection(self, provider, mock_world_context):
        """Test ultimatum intent detection and validation."""
        # Test the specific phrase mentioned in requirements
        ultimatum_turn = SpeakerTurnModel(
            speaker_id="player",
            text="Ceasefire now or else",
            timestamp=datetime.now(),
            confidence=0.9
        )

        events = []
        async for event in provider.stream_dialogue([ultimatum_turn], mock_world_context, "Test guidelines"):
            events.append(event)

        # Should emit safety check, analysis, and intent events
        assert len(events) > 0

        # Check for safety event
        safety_events = [e for e in events if isinstance(e, Safety)]
        assert len(safety_events) >= 1

        # Should detect ULTIMATUM intent
        intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(intent_events) >= 1

        detected_intent = intent_events[0].intent
        assert isinstance(detected_intent, UltimatumModel)
        assert "ceasefire" in detected_intent.content.lower()
        assert "or else" in detected_intent.content.lower()

        # Validate intent using validators
        from schemas.validators import validator
        # Convert datetime to string for validation
        intent_dict = detected_intent.model_dump()
        intent_dict['timestamp'] = detected_intent.timestamp.isoformat()
        validated_intent = validator.validate_intent(intent_dict)
        assert validated_intent is not None
        assert validated_intent["type"] == "ultimatum"

        # Should include scoring fields
        assert intent_events[0].confidence is not None
        assert 0.0 <= intent_events[0].confidence <= 1.0
        assert intent_events[0].justification is not None
        assert len(intent_events[0].justification) > 0
        assert "pattern" in intent_events[0].justification.lower()

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
        assert final_subtitle.text == ultimatum_turn.text
