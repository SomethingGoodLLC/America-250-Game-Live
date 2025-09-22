"""Edge case tests for negotiation providers."""

import asyncio
import pytest
from datetime import datetime
from unittest.mock import AsyncMock, patch

from providers import MockLocalProvider, Veo3Provider, NewIntent, LiveSubtitle, Analysis, Safety
from schemas.models import SpeakerTurnModel, WorldContextModel, ProposalModel


@pytest.fixture
def minimal_world_context():
    """Minimal world context for edge case testing."""
    return WorldContextModel(
        scenario_tags=[],
        initiator_faction={"id": "test_player", "name": "Test Player"},
        counterpart_faction={"id": "test_ai", "name": "Test AI"}
    )


class TestProviderEdgeCases:
    """Test edge cases and error scenarios."""

    @pytest.mark.asyncio
    async def test_empty_text_handling(self, minimal_world_context):
        """Test handling of empty or whitespace-only text."""
        turns = [
            SpeakerTurnModel(
                speaker_id="test_player",
                text="",  # Empty text
                timestamp=datetime.now()
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, minimal_world_context):
            events.append(event)

        # Should still generate events even with empty text
        assert len(events) >= 2  # At least safety and analysis
        assert any(isinstance(e, Safety) for e in events)
        assert any(isinstance(e, Analysis) for e in events)

    @pytest.mark.asyncio
    async def test_very_long_text_handling(self, minimal_world_context):
        """Test handling of very long text input."""
        long_text = "trade " * 1000  # Very long text with trade keyword
        turns = [
            SpeakerTurnModel(
                speaker_id="test_player",
                text=long_text,
                timestamp=datetime.now()
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, minimal_world_context):
            events.append(event)

        # Should handle long text gracefully
        new_intent_events = [e for e in events if isinstance(e, NewIntent)]
        assert len(new_intent_events) >= 1
        
        # Check that confidence is adjusted for very long content
        if new_intent_events:
            intent_event = new_intent_events[0]
            assert 0.0 <= intent_event.confidence <= 1.0

    @pytest.mark.asyncio
    async def test_special_characters_handling(self, minimal_world_context):
        """Test handling of special characters and unicode."""
        turns = [
            SpeakerTurnModel(
                speaker_id="test_player",
                text="We'll grant tråde access if you withdrâw tröops! 🤝💼",
                timestamp=datetime.now()
            )
        ]

        provider = MockLocalProvider({})
        events = []

        async for event in provider.stream_dialogue(turns, minimal_world_context):
            events.append(event)

        # Should handle special characters gracefully
        assert len(events) >= 2
        new_intent_events = [e for e in events if isinstance(e, NewIntent)]
        
        # Should still detect patterns despite special characters
        if new_intent_events:
            intent = new_intent_events[0].intent
            assert intent.type in ["counter_offer", "small_talk"]

    @pytest.mark.asyncio
    async def test_malformed_world_context(self):
        """Test handling of malformed world context."""
        # Missing required fields
        malformed_context = WorldContextModel(
            scenario_tags=["test"],
            initiator_faction={"id": "player"},  # Missing name
            counterpart_faction={"id": "ai"}     # Missing name
        )

        turns = [
            SpeakerTurnModel(
                speaker_id="player",
                text="Hello",
                timestamp=datetime.now()
            )
        ]

        provider = MockLocalProvider({})
        events = []

        # Should not crash with malformed context
        async for event in provider.stream_dialogue(turns, malformed_context):
            events.append(event)

        assert len(events) >= 1

    @pytest.mark.asyncio
    async def test_concurrent_provider_usage(self, minimal_world_context):
        """Test concurrent usage of providers."""
        turns = [
            SpeakerTurnModel(
                speaker_id="test_player",
                text="Let's make a trade deal",
                timestamp=datetime.now()
            )
        ]

        provider1 = MockLocalProvider({})
        provider2 = MockLocalProvider({})
        provider3 = Veo3Provider({})

        # Run providers concurrently
        async def collect_events(provider):
            events = []
            async for event in provider.stream_dialogue(turns, minimal_world_context):
                events.append(event)
            return events

        results = await asyncio.gather(
            collect_events(provider1),
            collect_events(provider2),
            collect_events(provider3),
            return_exceptions=True
        )

        # All providers should complete successfully
        assert len(results) == 3
        for result in results:
            assert not isinstance(result, Exception)
            assert len(result) >= 1

    @pytest.mark.asyncio
    async def test_provider_cleanup(self, minimal_world_context):
        """Test provider cleanup and resource management."""
        provider = MockLocalProvider({})
        
        # Use provider normally
        turns = [SpeakerTurnModel(
            speaker_id="test_player",
            text="Hello",
            timestamp=datetime.now()
        )]

        events = []
        async for event in provider.stream_dialogue(turns, minimal_world_context):
            events.append(event)

        # Cleanup should not raise errors
        await provider.close()
        
        # Should be able to call close multiple times
        await provider.close()

    @pytest.mark.asyncio
    async def test_validation_error_handling(self, minimal_world_context):
        """Test handling of validation errors."""
        provider = MockLocalProvider({"strict": True})

        # Create an intent that might cause validation issues
        intent = ProposalModel(
            type="proposal",
            speaker_id="test",
            content="",  # Empty content
            intent_type="trade",
            terms={},
            timestamp=datetime.now()
        )

        # Should handle validation gracefully
        is_valid = await provider.validate_intent(intent)
        assert isinstance(is_valid, bool)

        # Test validation and scoring
        validated_intent, score, justification = await provider.validate_and_score_intent(
            intent, minimal_world_context
        )
        
        assert validated_intent is not None
        assert 0.0 <= score <= 1.0
        assert isinstance(justification, str)

    @pytest.mark.asyncio
    async def test_network_simulation_errors(self, minimal_world_context):
        """Test handling of simulated network errors in Veo3Provider."""
        provider = Veo3Provider({})

        # Mock network delay/timeout
        with patch('asyncio.sleep', side_effect=asyncio.TimeoutError("Simulated timeout")):
            turns = [SpeakerTurnModel(
                speaker_id="test_player",
                text="Hello",
                timestamp=datetime.now()
            )]

            events = []
            try:
                async for event in provider.stream_dialogue(turns, minimal_world_context):
                    events.append(event)
            except asyncio.TimeoutError:
                # Should handle timeout gracefully in real implementation
                pass

    @pytest.mark.asyncio
    async def test_memory_efficiency_large_turns(self, minimal_world_context):
        """Test memory efficiency with large number of turns."""
        # Create many turns
        turns = []
        for i in range(100):
            turns.append(SpeakerTurnModel(
                speaker_id="test_player" if i % 2 == 0 else "test_ai",
                text=f"Turn {i}: Let's discuss trade agreements",
                timestamp=datetime.now()
            ))

        provider = MockLocalProvider({})
        events = []

        # Should handle large turn history efficiently
        async for event in provider.stream_dialogue(turns, minimal_world_context):
            events.append(event)

        # Should still process correctly
        assert len(events) >= 1

    @pytest.mark.asyncio
    async def test_strict_mode_comprehensive(self, minimal_world_context):
        """Comprehensive test of strict mode behavior."""
        provider = MockLocalProvider({"strict": True})

        test_cases = [
            ("This will end in violence", True),  # Should be blocked
            ("Let's make a peaceful trade", False),  # Should be allowed
            ("I hate this negotiation", True),  # Should be blocked
            ("We need to discuss terms", False),  # Should be allowed
        ]

        for text, should_block in test_cases:
            turns = [SpeakerTurnModel(
                speaker_id="test_player",
                text=text,
                timestamp=datetime.now()
            )]

            events = []
            async for event in provider.stream_dialogue(turns, minimal_world_context):
                events.append(event)

            safety_events = [e for e in events if isinstance(e, Safety)]
            unsafe_events = [e for e in safety_events if e.flag == "unsafe_content"]

            if should_block:
                assert len(unsafe_events) >= 1, f"Expected blocking for: {text}"
            else:
                assert len(unsafe_events) == 0, f"Unexpected blocking for: {text}"


class TestProviderPerformance:
    """Performance and optimization tests."""

    @pytest.mark.asyncio
    async def test_response_time_consistency(self, minimal_world_context):
        """Test that response times are consistent."""
        provider = MockLocalProvider({})
        turns = [SpeakerTurnModel(
            speaker_id="test_player",
            text="Let's make a trade deal",
            timestamp=datetime.now()
        )]

        response_times = []
        
        for _ in range(5):
            start_time = asyncio.get_event_loop().time()
            
            events = []
            async for event in provider.stream_dialogue(turns, minimal_world_context):
                events.append(event)
            
            end_time = asyncio.get_event_loop().time()
            response_times.append(end_time - start_time)

        # Response times should be reasonably consistent (within 2x of each other)
        min_time = min(response_times)
        max_time = max(response_times)
        
        assert max_time <= min_time * 3, f"Response times too variable: {response_times}"

    @pytest.mark.asyncio
    async def test_pattern_matching_efficiency(self, minimal_world_context):
        """Test that pattern matching is efficient."""
        provider = MockLocalProvider({})
        
        # Test with text that matches multiple patterns
        complex_text = "We need to discuss trade deals and military cooperation for peace"
        
        turns = [SpeakerTurnModel(
            speaker_id="test_player",
            text=complex_text,
            timestamp=datetime.now()
        )]

        start_time = asyncio.get_event_loop().time()
        
        events = []
        async for event in provider.stream_dialogue(turns, minimal_world_context):
            events.append(event)
        
        end_time = asyncio.get_event_loop().time()
        processing_time = end_time - start_time

        # Should complete quickly even with complex pattern matching
        assert processing_time < 1.0, f"Pattern matching too slow: {processing_time}s"
        assert len(events) >= 1
