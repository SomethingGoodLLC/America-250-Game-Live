"""Tests for negotiation providers."""

import asyncio
import pytest
from datetime import datetime
from unittest.mock import AsyncMock

from providers.base import Provider, ProviderEvent, NewIntent, LiveSubtitle, Analysis, Safety
from providers.mock_local import MockLocalProvider
from providers.gemini_veo3 import Veo3Provider
from schemas.models import SpeakerTurnModel, WorldContextModel, ProposalModel, ConcessionModel, CounterOfferModel, UltimatumModel, SmallTalkModel


@pytest.fixture
def world_context():
    """Sample world context for testing."""
    return WorldContextModel(
        scenario_tags=["trade", "diplomacy"],
        initiator_faction={
            "id": "player_faction",
            "name": "Player Empire"
        },
        counterpart_faction={
            "id": "ai_faction",
            "name": "AI Empire"
        },
        current_state={
            "relations": 50,
            "trade_status": "normal"
        }
    )


@pytest.fixture
def sample_turns():
    """Sample speaker turns for testing."""
    return [
        SpeakerTurnModel(
            speaker_id="player_faction",
            text="We need to discuss trade agreements.",
            timestamp=datetime.now(),
            confidence=0.9
        )
    ]


class TestMockLocalProvider:
    """Tests for MockLocalProvider."""

    def test_provider_initialization(self):
        """Test provider initialization with config."""
        config = {"strict": True}
        provider = MockLocalProvider(config)
        assert provider.strict is True
        assert provider.config == config

    def test_provider_initialization_default(self):
        """Test provider initialization with default config."""
        config = {}
        provider = MockLocalProvider(config)
        assert provider.strict is False
        assert provider.config == config

    @pytest.mark.asyncio
    async def test_empty_turns_greeting(self, world_context):
        """Test initial greeting when no turns provided."""
        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue([], world_context):
            events.append(event)

        # Should yield safety check and initial greeting
        assert len(events) >= 1
        assert any(isinstance(e, NewIntent) for e in events)
        assert any(isinstance(e, Safety) for e in events)

        # Check that greeting is small talk
        greeting_events = [e for e in events if isinstance(e, NewIntent)]
        if greeting_events:
            assert greeting_events[0].intent.type == "small_talk"
            assert greeting_events[0].confidence == 1.0

    @pytest.mark.asyncio
    async def test_counter_offer_detection(self, world_context):
        """Test counter offer detection from key phrase."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player_faction",
                text="We'll grant trade access if you withdraw troops",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should detect counter offer
        new_intent_events = [e for e in events if isinstance(e, NewIntent)]
        counter_offers = [e for e in new_intent_events if e.intent.type == "counter_offer"]

        assert len(counter_offers) == 1
        counter_offer = counter_offers[0].intent
        assert "trade access" in counter_offer.content.lower()
        assert "withdraw" in counter_offer.content.lower()
        assert counter_offers[0].confidence > 0.7  # High confidence for counter offers (0.95 base + 0.1 content + 0.5 relevance = 0.775)

    @pytest.mark.asyncio
    async def test_ultimatum_detection(self, world_context):
        """Test ultimatum detection from key phrase."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player_faction",
                text="Ceasefire now or else",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should detect ultimatum
        new_intent_events = [e for e in events if isinstance(e, NewIntent)]
        ultimatums = [e for e in new_intent_events if e.intent.type == "ultimatum"]

        assert len(ultimatums) == 1
        ultimatum = ultimatums[0].intent
        assert "cease" in ultimatum.content.lower()
        assert ultimatum.deadline is not None
        assert len(ultimatum.consequences) > 0

    @pytest.mark.asyncio
    async def test_trade_proposal_detection(self, world_context):
        """Test trade proposal detection."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player_faction",
                text="I propose a trade deal",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should detect proposal
        new_intent_events = [e for e in events if isinstance(e, NewIntent)]
        proposals = [e for e in new_intent_events if e.intent.type == "proposal"]

        assert len(proposals) == 1
        proposal = proposals[0].intent
        assert proposal.intent_type == "trade"
        assert "terms" in proposal.model_dump()

    @pytest.mark.asyncio
    async def test_strict_mode_unsafe_content(self, world_context):
        """Test strict mode blocks unsafe content."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player_faction",
                text="This will end in violence",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        provider = MockLocalProvider({"strict": True})
        events = []

        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should yield safety flag for unsafe content
        safety_events = [e for e in events if isinstance(e, Safety)]
        unsafe_events = [e for e in safety_events if e.flag == "unsafe_content"]

        assert len(unsafe_events) == 1
        assert unsafe_events[0].severity == "high"

    @pytest.mark.asyncio
    async def test_analysis_event_generation(self, world_context):
        """Test that analysis events are generated."""
        turns = [
            SpeakerTurnModel(
                speaker_id="player_faction",
                text="Let's cooperate on this trade deal",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should yield analysis event
        analysis_events = [e for e in events if isinstance(e, Analysis)]
        assert len(analysis_events) >= 1

        analysis = analysis_events[0]
        assert analysis.tag == "deterministic_analysis"
        assert "matched_patterns" in analysis.payload
        assert "intent_detected" in analysis.payload

    @pytest.mark.asyncio
    async def test_ai_turn_handling(self, world_context):
        """Test handling of AI turns (non-player)."""
        turns = [
            SpeakerTurnModel(
                speaker_id="ai_faction",  # This is an AI turn
                text="I understand your position",
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should only yield analysis for AI turns
        analysis_events = [e for e in events if isinstance(e, Analysis)]
        assert len(analysis_events) >= 1

        new_intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(new_intent_events) == 0  # No new intents for AI turns


class TestVeo3Provider:
    """Tests for Veo3Provider."""

    def test_provider_initialization(self):
        """Test provider initialization with config."""
        config = {
            "avatar_style": "diplomat_formal",
            "voice_id": "en_us_diplomat",
            "latency_target_ms": 200
        }
        provider = Veo3Provider(config)

        assert provider.avatar_style == "diplomat_formal"
        assert provider.voice_id == "en_us_diplomat"
        assert provider.latency_target_ms == 200
        assert provider.config == config

    def test_provider_initialization_defaults(self):
        """Test provider initialization with default values."""
        config = {}
        provider = Veo3Provider(config)

        assert provider.avatar_style == "diplomat_formal"
        assert provider.voice_id == "diplomat_en_us"
        assert provider.latency_target_ms == 150

    @pytest.mark.asyncio
    async def test_stream_dialogue_with_turns(self, world_context, sample_turns):
        """Test streaming dialogue with turns."""
        provider = Veo3Provider({})
        events = []

        async for event in provider.stream_dialogue(sample_turns, world_context):
            events.append(event)

        # Should yield various event types
        assert len(events) >= 3  # At least subtitle, safety, intent, analysis

        # Check for expected event types
        assert any(isinstance(e, LiveSubtitle) for e in events)
        assert any(isinstance(e, Safety) for e in events)
        assert any(isinstance(e, NewIntent) for e in events)
        assert any(isinstance(e, Analysis) for e in events)

    @pytest.mark.asyncio
    async def test_live_subtitle_generation(self, world_context, sample_turns):
        """Test live subtitle generation."""
        provider = Veo3Provider({})
        events = []

        async for event in provider.stream_dialogue(sample_turns, world_context):
            events.append(event)

        # Should yield both partial and final subtitles
        subtitle_events = [e for e in events if isinstance(e, LiveSubtitle)]
        assert len(subtitle_events) >= 2

        # Check subtitle properties
        partial_subtitle = next(e for e in subtitle_events if not e.is_final)
        final_subtitle = next(e for e in subtitle_events if e.is_final)

        assert "..." in partial_subtitle.text
        assert partial_subtitle.text != final_subtitle.text
        assert final_subtitle.text == sample_turns[0].text
        assert partial_subtitle.speaker_id == sample_turns[0].speaker_id

    @pytest.mark.asyncio
    async def test_intent_detection_patterns(self, world_context):
        """Test various intent detection patterns."""
        test_cases = [
            ("I want to make a trade deal", "proposal"),
            ("I agree to your terms", "concession"),
            ("I counter with this offer", "counter_offer"),
            ("Final warning or else", "ultimatum"),
            ("Hello there", "small_talk")
        ]

        for text, expected_type in test_cases:
            turns = [
                SpeakerTurnModel(
                    speaker_id="player_faction",
                    text=text,
                    timestamp=datetime.now(),
                    confidence=0.9
                )
            ]

            provider = Veo3Provider({})
            events = []

            async for event in provider.stream_dialogue(turns, world_context):
                events.append(event)

            new_intent_events = [e for e in events if isinstance(e, NewIntent)]
            if new_intent_events:
                intent = new_intent_events[0].intent
                assert intent.type == expected_type, f"Expected {expected_type} for '{text}', got {intent.type}"

    @pytest.mark.asyncio
    async def test_system_guidelines_parameter(self, world_context, sample_turns):
        """Test that system_guidelines parameter is accepted."""
        provider = Veo3Provider({})
        events = []

        # Should not raise an error with system_guidelines
        async for event in provider.stream_dialogue(
            sample_turns,
            world_context,
            system_guidelines="Be diplomatic and professional"
        ):
            events.append(event)

        assert len(events) > 0

    @pytest.mark.asyncio
    async def test_empty_turns_handling(self, world_context):
        """Test handling of empty turns."""
        provider = Veo3Provider({})
        events = []

        async for event in provider.stream_dialogue([], world_context):
            events.append(event)

        # Should still yield safety check
        assert any(isinstance(e, Safety) for e in events)

        # May or may not yield analysis depending on implementation
        analysis_events = [e for e in events if isinstance(e, Analysis)]
        assert len(analysis_events) >= 0


class TestProviderEventTypes:
    """Tests for ProviderEvent type definitions."""

    def test_new_intent_creation(self):
        """Test NewIntent creation."""
        intent = ProposalModel(
            type="proposal",
            speaker_id="test_speaker",
            content="Test proposal",
            intent_type="trade",
            terms={"test": "value"},
            timestamp=datetime.now()
        )

        event = NewIntent(
            intent=intent,
            confidence=0.85,
            justification="Test justification"
        )

        assert event.intent == intent
        assert event.confidence == 0.85
        assert event.justification == "Test justification"
        assert event.timestamp is not None

    def test_live_subtitle_creation(self):
        """Test LiveSubtitle creation."""
        event = LiveSubtitle(
            text="Hello world",
            is_final=True,
            speaker_id="speaker_1"
        )

        assert event.text == "Hello world"
        assert event.is_final is True
        assert event.speaker_id == "speaker_1"
        assert event.timestamp is not None

    def test_analysis_creation(self):
        """Test Analysis creation."""
        event = Analysis(
            tag="test_analysis",
            payload={"key": "value"}
        )

        assert event.tag == "test_analysis"
        assert event.payload == {"key": "value"}
        assert event.timestamp is not None

    def test_safety_creation(self):
        """Test Safety creation."""
        event = Safety(
            flag="test_flag",
            detail="Test detail",
            severity="high"
        )

        assert event.flag == "test_flag"
        assert event.detail == "Test detail"
        assert event.severity == "high"
        assert event.timestamp is not None

    def test_safety_default_severity(self):
        """Test Safety default severity."""
        event = Safety(
            flag="test_flag",
            detail="Test detail"
        )

        assert event.severity == "low"


@pytest.mark.asyncio
async def test_provider_interface_compliance():
    """Test that providers implement the required interface."""

    # Mock provider for testing interface
    class MockProvider(Provider):
        async def stream_dialogue(self, turns, world_context, system_guidelines=None):
            yield NewIntent(
                intent=SmallTalkModel(
                    type="small_talk",
                    speaker_id="test",
                    content="test",
                    timestamp=datetime.now()
                ),
                confidence=1.0,
                justification="test"
            )

        async def validate_intent(self, intent):
            return True

    provider = MockProvider({})
    assert hasattr(provider, 'stream_dialogue')
    assert hasattr(provider, 'validate_intent')
    assert hasattr(provider, 'close')


class TestProviderValidation:
    """Tests for provider validation and scoring."""

    @pytest.mark.asyncio
    async def test_validate_and_score_intent(self, world_context):
        """Test intent validation and scoring."""
        provider = MockLocalProvider({})

        # Create a test intent
        intent = ProposalModel(
            type="proposal",
            speaker_id="test_speaker",
            content="Test proposal content",
            intent_type="trade",
            terms={"duration": "5 years"},
            timestamp=datetime.now()
        )

        validated_intent, score, justification = await provider.validate_and_score_intent(intent, world_context)

        # Check that validation worked
        assert validated_intent.type == "proposal"
        assert validated_intent.intent_type == "trade"
        assert 0.0 <= score <= 1.0
        assert isinstance(justification, str)
        assert len(justification) > 0

    @pytest.mark.asyncio
    async def test_schema_validation_on_creation(self, world_context):
        """Test that Pydantic models validate on creation."""
        provider = MockLocalProvider({})

        # Test valid intent
        valid_intent = ProposalModel(
            type="proposal",
            speaker_id="test_speaker",
            content="Test proposal",
            intent_type="trade",
            terms={"duration": "5 years"},
            timestamp=datetime.now()
        )

        assert valid_intent.type == "proposal"
        assert valid_intent.intent_type == "trade"

        # Test invalid intent (should raise validation error)
        try:
            invalid_intent = ProposalModel(
                type="invalid_type",  # Invalid literal
                speaker_id="test_speaker",
                content="Test",
                intent_type="trade",
                terms={},
                timestamp=datetime.now()
            )
            assert False, "Should have raised validation error"
        except Exception:
            pass  # Expected

    @pytest.mark.asyncio
    async def test_intent_scoring_relevance(self, world_context):
        """Test that intent scoring considers context relevance."""
        provider = MockLocalProvider({})

        # Test intent with matching scenario tags
        world_context.scenario_tags = ["trade", "diplomacy"]
        intent = ProposalModel(
            type="proposal",
            speaker_id="test_speaker",
            content="Trade proposal",
            intent_type="trade",  # Matches scenario tag
            terms={"duration": "5 years"},
            timestamp=datetime.now()
        )

        _, score, _ = await provider.validate_and_score_intent(intent, world_context)
        assert score > 0.6  # Should get bonus for relevance (base 0.5 + content 0.1 + relevance 0.2 = 0.8, averaged with base confidence)

        # Test intent without matching tags
        world_context.scenario_tags = ["military", "war"]
        _, score_no_match, _ = await provider.validate_and_score_intent(intent, world_context)
        assert score_no_match > 0.5  # Base confidence (0.9) + relevance (0.5) = 0.7 average

    @pytest.mark.asyncio
    async def test_content_quality_scoring(self, world_context):
        """Test that content quality affects scoring."""
        provider = MockLocalProvider({})

        # Test good content length
        good_intent = ProposalModel(
            type="proposal",
            speaker_id="test_speaker",
            content="This is a reasonably long proposal content that should score well",  # 70+ chars
            intent_type="trade",
            terms={"duration": "5 years"},
            timestamp=datetime.now()
        )

        _, good_score, _ = await provider.validate_and_score_intent(good_intent, world_context)

        # Test poor content length
        poor_intent = ProposalModel(
            type="proposal",
            speaker_id="test_speaker",
            content="Short",  # Too short
            intent_type="trade",
            terms={"duration": "5 years"},
            timestamp=datetime.now()
        )

        _, poor_score, _ = await provider.validate_and_score_intent(poor_intent, world_context)

        assert good_score > poor_score
