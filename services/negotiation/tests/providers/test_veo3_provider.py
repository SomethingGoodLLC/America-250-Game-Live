"""Tests for Veo3Provider implementation."""

import pytest
import asyncio
from datetime import datetime
from unittest.mock import Mock, AsyncMock

from schemas.models import SpeakerTurnModel, WorldContextModel
from providers.gemini_veo3 import Veo3Provider


class TestVeo3Provider:
    """Test suite for Veo3Provider."""

    def test_constructor_defaults(self):
        """Test constructor with default parameters."""
        provider = Veo3Provider()

        assert provider.avatar_style == "colonial_diplomat"
        assert provider.voice_id == "en_male_01"
        assert provider.latency_target_ms == 800
        assert provider.use_veo3 is False
        assert provider.video_source is not None
        assert provider.stt_provider is None
        assert provider.tts_provider is None

    def test_constructor_custom_parameters(self):
        """Test constructor with custom parameters."""
        mock_video_source = Mock()
        mock_stt = Mock()
        mock_tts = Mock()

        provider = Veo3Provider(
            avatar_style="modern_diplomat",
            voice_id="en_female_02",
            latency_target_ms=500,
            use_veo3=True,
            video_source=mock_video_source,
            stt_provider=mock_stt,
            tts_provider=mock_tts
        )

        assert provider.avatar_style == "modern_diplomat"
        assert provider.voice_id == "en_female_02"
        assert provider.latency_target_ms == 500
        assert provider.use_veo3 is True
        assert provider.video_source is mock_video_source
        assert provider.stt_provider is mock_stt
        assert provider.tts_provider is mock_tts

    def test_build_system_prompt(self):
        """Test system prompt building."""
        provider = Veo3Provider()

        world_context = WorldContextModel(
            scenario_tags=['diplomatic', 'trade'],
            initiator_faction={'id': 'player_faction', 'name': 'Player Empire'},
            counterpart_faction={'id': 'ai_faction', 'name': 'AI Empire'},
            current_state={'war_score': 50, 'borders': ['north', 'south']}
        )

        prompt = provider._build_system_prompt(world_context, "Be diplomatic and respectful.")

        assert "AI Diplomatic Envoy" in prompt
        assert "Formal, period-appropriate (1607–1799), concise" in prompt
        assert "ai_faction" in prompt
        assert "player_faction" in prompt
        assert "war_score" in prompt
        assert "Be diplomatic and respectful" in prompt

    def test_split_into_clauses(self):
        """Test text splitting into clauses."""
        provider = Veo3Provider()

        text = "I propose a trade agreement. This will benefit both parties. Do you agree?"
        clauses = provider._split_into_clauses(text)

        assert len(clauses) >= 1
        assert "I propose a trade agreement" in clauses
        assert "This will benefit both parties" in clauses

    @pytest.mark.asyncio
    async def test_stream_dialogue_basic_flow(self):
        """Test basic stream dialogue flow."""
        provider = Veo3Provider()

        turns = [
            SpeakerTurnModel(
                speaker_id='player_1',
                text='I propose a trade agreement for resources.',
                timestamp=datetime.now(),
                confidence=0.9
            )
        ]

        world_context = WorldContextModel(
            scenario_tags=['diplomatic', 'trade'],
            initiator_faction={'id': 'player_faction', 'name': 'Player Empire'},
            counterpart_faction={'id': 'ai_faction', 'name': 'AI Empire'},
            current_state={'war_score': 50}
        )

        events = []
        async for event in provider.stream_dialogue(turns, world_context):
            events.append(event)

        # Should have subtitle events, intent event, and analysis event
        event_types = [e.type for e in events]
        assert "subtitle" in event_types
        assert "intent" in event_types
        assert "analysis" in event_types

        # Check that we have both interim and final subtitles
        subtitle_events = [e for e in events if e.type == "subtitle"]
        assert len(subtitle_events) >= 2  # At least interim and final

        # Check intent event structure
        intent_events = [e for e in events if e.type == "intent"]
        assert len(intent_events) == 1
        intent_event = intent_events[0]
        assert "intent" in intent_event.payload
        assert "confidence" in intent_event.payload

    @pytest.mark.asyncio
    async def test_mock_function_call(self):
        """Test mock function calling."""
        provider = Veo3Provider()

        # Test trade proposal
        result = await provider._mock_function_call(
            "I propose a trade agreement for resources.",
            "system prompt"
        )

        assert "PROPOSAL" in result
        assert "trade" in result.lower()

        # Test concession
        result = await provider._mock_function_call(
            "I agree to your terms.",
            "system prompt"
        )

        assert "CONCESSION" in result

        # Test ultimatum
        result = await provider._mock_function_call(
            "Accept now or face consequences!",
            "system prompt"
        )

        assert "ULTIMATUM" in result

    def test_yaml_system_prompt_structure(self):
        """Test that system prompt has correct YAML structure."""
        provider = Veo3Provider()

        world_context = WorldContextModel(
            scenario_tags=['test'],
            initiator_faction={'id': 'test_player'},
            counterpart_faction={'id': 'test_ai'},
            current_state={'war_score': 0}
        )

        prompt_yaml = provider._build_system_prompt(world_context)

        # Should be valid YAML
        import yaml
        parsed = yaml.safe_load(prompt_yaml)

        assert "system" in parsed
        assert "world" in parsed
        assert "rules" in parsed
        assert parsed["system"]["role"] == "AI Diplomatic Envoy"
        assert parsed["world"]["counterpart_faction_id"] == "test_ai"
        assert parsed["world"]["player_faction_id"] == "test_player"
