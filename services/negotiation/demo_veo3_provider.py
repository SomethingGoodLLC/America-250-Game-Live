#!/usr/bin/env python3
"""Demonstration script for Veo3Provider functionality."""

import asyncio
import sys
from datetime import datetime
from typing import List

# Add the project root to path for imports
sys.path.insert(0, '/Users/leone/PycharmProjects/Samson')

from services.negotiation.schemas.models import SpeakerTurnModel, WorldContextModel
from services.negotiation.providers.gemini_veo3 import Veo3Provider


async def demo_veo3_provider():
    """Demonstrate the Veo3Provider functionality."""
    print("🎭 Veo3Provider Demonstration")
    print("=" * 50)

    # Create provider with default settings
    provider = Veo3Provider(
        avatar_style="colonial_diplomat",
        voice_id="en_male_01",
        latency_target_ms=800,
        use_veo3=False  # Using placeholder video source for demo
    )

    print("✅ Provider created successfully")
    print(f"   - Avatar style: {provider.avatar_style}")
    print(f"   - Voice ID: {provider.voice_id}")
    print(f"   - Latency target: {provider.latency_target_ms}ms")
    print(f"   - Use Veo3: {provider.use_veo3}")
    print()

    # Create test conversation
    turns: List[SpeakerTurnModel] = [
        SpeakerTurnModel(
            speaker_id="player_1",
            text="I propose a trade agreement that will benefit both our empires. We offer valuable resources in exchange for military access.",
            timestamp=datetime.now(),
            confidence=0.95
        )
    ]

    world_context = WorldContextModel(
        scenario_tags=["diplomatic", "trade", "military"],
        initiator_faction={
            "id": "player_empire",
            "name": "Player Empire",
            "traits": {"expansionist": True, "diplomatic": True}
        },
        counterpart_faction={
            "id": "ai_empire",
            "name": "AI Empire",
            "traits": {"defensive": True, "resource_rich": True}
        },
        current_state={
            "war_score": 25,
            "borders": ["northern_border", "eastern_frontier"],
            "trade_balance": -100
        }
    )

    system_guidelines = "Maintain formal diplomatic tone. Be strategic but cooperative."

    print("📝 Test Scenario:")
    print(f"   - Player text: {turns[0].text}")
    print(f"   - World context: {world_context.scenario_tags}")
    print(f"   - Guidelines: {system_guidelines}")
    print()

    print("🎬 Streaming dialogue events...")
    print("-" * 30)

    event_count = 0
    async for event in provider.stream_dialogue([turn.model_dump() for turn in turns], world_context.model_dump(), system_guidelines):
        event_count += 1
        timestamp = event.timestamp.strftime("%H:%M:%S.%f")[:-3]

        if event.type == "subtitle":
            print(f"[{timestamp}] 📺 SUBTITLE: {event.payload['text'][:50]}...")
            if event.payload.get('is_final'):
                print(f"           → FINAL: {event.payload['text']}")

        elif event.type == "intent":
            intent = event.payload['intent']
            confidence = event.payload['confidence']
            print(f"[{timestamp}] 🎯 INTENT: {intent.get('type', 'unknown').upper()}")
            print(f"           → Content: {intent.get('content', '')[:50]}...")
            print(f"           → Confidence: {confidence:.2f}")

        elif event.type == "safety":
            print(f"[{timestamp}] 🛡️  SAFETY: {event.payload['flag']} - {event.payload['detail']}")

        elif event.type == "analysis":
            print(f"[{timestamp}] 📊 ANALYSIS: {event.payload['tag']}")

        # Simulate real-time processing delay
        await asyncio.sleep(0.1)

    print()
    print("📈 Summary:")
    print(f"   - Total events processed: {event_count}")
    print(f"   - Video source: {'Veo3 API' if provider.use_veo3 else 'Placeholder'}")
    print(f"   - Processing completed in ~{provider.latency_target_ms}ms target")

    print()
    print("🎉 Demonstration completed successfully!")


if __name__ == "__main__":
    try:
        asyncio.run(demo_veo3_provider())
    except KeyboardInterrupt:
        print("\n⏹️  Demonstration interrupted")
    except Exception as e:
        print(f"\n❌ Error during demonstration: {e}")
        import traceback
        traceback.print_exc()
