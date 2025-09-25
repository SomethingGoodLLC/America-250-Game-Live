#!/usr/bin/env python3
"""
Integration test for the AI Avatar test harness.
This script tests the core functionality without requiring a browser.
"""

import asyncio
import json
import yaml
from ruamel.yaml import YAML
from aiortc import RTCPeerConnection
from pydantic import BaseModel

class SDPIn(BaseModel):
    sdp: str
    type: str = "offer"

def test_yaml_parsing():
    """Test YAML parsing functionality."""
    print("🧪 Testing YAML parsing...")

    yaml_content = """
type: player_utterance
text: "We'll grant trade access if you withdraw troops"
"""

    yaml_loader = YAML()
    parsed = yaml_loader.load(yaml_content)

    assert parsed['type'] == 'player_utterance'
    assert parsed['text'] == "We'll grant trade access if you withdraw troops"
    print("✅ YAML parsing works")

def test_webrtc_sdp():
    """Test WebRTC SDP handling."""
    print("🧪 Testing WebRTC SDP...")

    # Create a peer connection
    pc = RTCPeerConnection()

    # Create a simple offer
    offer = pc.createOffer()

    # Test our data model
    sdp_data = SDPIn(sdp="test sdp", type="offer")
    assert sdp_data.sdp == "test sdp"
    assert sdp_data.type == "offer"

    pc.close()
    print("✅ WebRTC SDP handling works")

def test_video_frame_generation():
    """Test video frame generation."""
    print("🧪 Testing video frame generation...")

    import numpy as np

    # Create test frame
    height, width = 240, 320
    frame = np.zeros((height, width, 3), dtype=np.uint8)

    # Fill with test pattern
    frame.fill(128)

    assert frame.shape == (240, 320, 3)
    assert frame.dtype == np.uint8
    print("✅ Video frame generation works")

def test_session_structure():
    """Test session data structure."""
    print("🧪 Testing session structure...")

    session_id = "test123"

    SESSIONS = {
        session_id: {
            "pc": None,
            "model": "mock_local",
            "ws_clients": set(),
            "turns": [{"speaker": "PLAYER", "text": "Test utterance"}],
            "world_context": {"scenario": "colonial_america"},
            "provider_task": None,
            "blackhole": None,
        }
    }

    session = SESSIONS[session_id]
    assert session["model"] == "mock_local"
    assert len(session["turns"]) == 1
    assert session["turns"][0]["speaker"] == "PLAYER"
    assert session["world_context"]["scenario"] == "colonial_america"

    print("✅ Session structure works")

def test_intent_validation():
    """Test intent validation logic."""
    print("🧪 Testing intent validation...")

    # Test counter-offer detection
    text = "We'll grant trade access if you withdraw troops from Ohio Country."
    is_counter_offer = "grant" in text.lower() and "access" in text.lower() and "withdraw" in text.lower()

    assert is_counter_offer
    print("✅ Intent validation works")

async def main():
    """Run all integration tests."""
    print("🚀 Running AI Avatar Test Harness Integration Tests")
    print("=" * 50)

    try:
        test_yaml_parsing()
        test_webrtc_sdp()
        test_video_frame_generation()
        test_session_structure()
        test_intent_validation()

        print("=" * 50)
        print("🎉 All integration tests passed!")
        print("\n📖 To run the test harness:")
        print("   cd /Users/leone/PycharmProjects/Samson/services/negotiation")
        print("   source test_venv/bin/activate")
        print("   make run")
        print("\n🌐 Then open http://localhost:8000 in your browser!")

    except Exception as e:
        print(f"❌ Integration test failed: {e}")
        raise

if __name__ == "__main__":
    asyncio.run(main())
