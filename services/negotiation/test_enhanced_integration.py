#!/usr/bin/env python3
"""
Enhanced integration test for the AI Avatar test harness.
Tests the improved functionality and provider integration.
"""

import asyncio
import json
from datetime import datetime
from ruamel.yaml import YAML
from enhanced_harness import EnhancedMockProvider, EnhancedVideoSource

async def test_enhanced_provider():
    """Test the enhanced mock provider functionality."""
    print("🧪 Testing Enhanced Mock Provider...")

    provider = EnhancedMockProvider({"strict": True})
    
    # Test turns
    turns = [
        {
            "speaker": "PLAYER",
            "text": "We'll grant trade access if you withdraw troops from Ohio Country.",
            "timestamp": datetime.now().isoformat()
        }
    ]
    
    world_context = {
        "scenario": "colonial_america",
        "year": 1755,
        "initiator_faction": {"id": "player", "name": "Colonial Delegation"},
        "counterpart_faction": {"id": "ai_diplomat", "name": "British Crown Representative"}
    }

    events = []
    async for event in provider.stream_dialogue(turns, world_context, "Test guidelines"):
        events.append(event)
        print(f"  📨 Event: {event['type']}")
        if len(events) >= 10:  # Increase limit to ensure we get all events
            break

    # Verify we got the expected event types
    event_types = [event["type"] for event in events]
    expected_types = ["safety", "analysis", "subtitle", "intent"]
    
    for expected_type in expected_types:
        assert expected_type in event_types, f"Missing event type: {expected_type}"

    print("✅ Enhanced Mock Provider works correctly")
    return events

async def test_enhanced_video_source():
    """Test the enhanced video source."""
    print("🧪 Testing Enhanced Video Source...")

    source = EnhancedVideoSource("diplomatic")
    await source.start()

    # Generate a few frames
    frame_generator = source.frames()
    frames = []
    
    for i in range(3):
        frame = await frame_generator.__anext__()
        frames.append(frame)
        assert frame.shape == (480, 640, 3), f"Unexpected frame shape: {frame.shape}"
        assert frame.dtype.name == 'uint8', f"Unexpected frame dtype: {frame.dtype}"

    print("✅ Enhanced Video Source generates frames correctly")
    return frames

def test_intent_generation():
    """Test intent generation logic."""
    print("🧪 Testing Intent Generation...")

    provider = EnhancedMockProvider()

    # Test counter-offer detection
    counter_offer_text = "We'll grant trade access if you withdraw troops"
    intent1 = provider._generate_intent(counter_offer_text, {})
    assert intent1["kind"] == "COUNTER_OFFER", f"Expected COUNTER_OFFER, got {intent1['kind']}"
    assert "trade_access" in str(intent1["offer"]), "Counter-offer should include trade access"

    # Test ultimatum detection
    ultimatum_text = "Ceasefire now or else we'll declare war"
    intent2 = provider._generate_intent(ultimatum_text, {})
    assert intent2["kind"] == "ULTIMATUM", f"Expected ULTIMATUM, got {intent2['kind']}"
    assert "ceasefire" in str(intent2["demand"]), "Ultimatum should include ceasefire demand"

    # Test default proposal
    neutral_text = "Let's discuss our mutual interests"
    intent3 = provider._generate_intent(neutral_text, {})
    assert intent3["kind"] == "PROPOSAL", f"Expected PROPOSAL, got {intent3['kind']}"

    print("✅ Intent generation logic works correctly")

def test_keyword_extraction():
    """Test keyword extraction."""
    print("🧪 Testing Keyword Extraction...")

    provider = EnhancedMockProvider()

    text = "We propose a trade agreement and military alliance"
    keywords = provider._extract_keywords(text)
    
    expected_keywords = ["trade", "agreement", "alliance"]
    for keyword in expected_keywords:
        assert keyword in keywords, f"Expected keyword '{keyword}' not found in {keywords}"

    print("✅ Keyword extraction works correctly")

def test_sentiment_analysis():
    """Test sentiment analysis."""
    print("🧪 Testing Sentiment Analysis...")

    provider = EnhancedMockProvider()

    # Test positive sentiment
    positive_text = "We offer peace and cooperation"
    sentiment1 = provider._analyze_sentiment(positive_text)
    assert sentiment1 == "positive", f"Expected positive sentiment, got {sentiment1}"

    # Test negative sentiment
    negative_text = "We refuse your demands and threaten war"
    sentiment2 = provider._analyze_sentiment(negative_text)
    assert sentiment2 == "negative", f"Expected negative sentiment, got {sentiment2}"

    # Test neutral sentiment
    neutral_text = "The weather is nice today"
    sentiment3 = provider._analyze_sentiment(neutral_text)
    assert sentiment3 == "neutral", f"Expected neutral sentiment, got {sentiment3}"

    print("✅ Sentiment analysis works correctly")

async def test_yaml_serialization():
    """Test YAML serialization of events."""
    print("🧪 Testing YAML Serialization...")

    yaml = YAML()
    
    # Test event serialization
    event = {
        "type": "intent",
        "payload": {
            "kind": "PROPOSAL",
            "offer": {"trade": True},
            "confidence": 0.85
        },
        "timestamp": datetime.now().isoformat()
    }

    # Serialize to YAML
    from io import StringIO
    buf = StringIO()
    yaml.dump(event, buf)
    yaml_str = buf.getvalue()

    # Deserialize back
    parsed = yaml.load(yaml_str)
    
    assert parsed["type"] == event["type"]
    assert parsed["payload"]["kind"] == event["payload"]["kind"]
    assert parsed["payload"]["confidence"] == event["payload"]["confidence"]

    print("✅ YAML serialization works correctly")

async def main():
    """Run all enhanced integration tests."""
    print("🚀 Running Enhanced AI Avatar Test Harness Integration Tests")
    print("=" * 60)

    try:
        # Test enhanced provider
        events = await test_enhanced_provider()
        print(f"📊 Generated {len(events)} events from provider")

        # Test enhanced video source
        frames = await test_enhanced_video_source()
        print(f"🎬 Generated {len(frames)} video frames")

        # Test intent generation
        test_intent_generation()

        # Test keyword extraction
        test_keyword_extraction()

        # Test sentiment analysis
        test_sentiment_analysis()

        # Test YAML serialization
        await test_yaml_serialization()

        print("=" * 60)
        print("🎉 All enhanced integration tests passed!")
        print("\n📖 To run the enhanced test harness:")
        print("   cd /Users/leone/PycharmProjects/Samson/services/negotiation")
        print("   source test_venv/bin/activate")
        print("   make run")
        print("\n🌐 Then open http://localhost:8000 in your browser!")
        print("\n✨ Enhanced Features:")
        print("   • Realistic avatar animations with expressions")
        print("   • Proper provider event streaming")
        print("   • Advanced intent detection with confidence scoring")
        print("   • Real-time subtitle generation")
        print("   • Comprehensive safety and analysis events")
        print("   • Structured logging with JSON output")
        print("   • Enhanced UI with statistics and better UX")

    except Exception as e:
        print(f"❌ Enhanced integration test failed: {e}")
        import traceback
        traceback.print_exc()
        raise

if __name__ == "__main__":
    asyncio.run(main())
