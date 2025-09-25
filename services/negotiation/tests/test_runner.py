#!/usr/bin/env python3
"""Comprehensive test runner for AI Avatar Negotiation System."""

import asyncio
import json
import subprocess
import sys
import time
from pathlib import Path
import aiohttp
import websockets
from typing import Dict, Any


class NegotiationSystemTester:
    """Comprehensive tester for the AI Avatar Negotiation System."""

    def __init__(self):
        self.base_url = "http://127.0.0.1:8000"
        self.session_id = None
        self.websocket = None
        self.test_results = []

    def log_test(self, test_name: str, success: bool, message: str):
        """Log test result."""
        status = "✅ PASS" if success else "❌ FAIL"
        print(f"{status} {test_name}: {message}")
        self.test_results.append({
            "test": test_name,
            "success": success,
            "message": message
        })

    async def test_server_health(self):
        """Test basic server health."""
        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(f"{self.base_url}/health") as response:
                    if response.status == 200:
                        self.log_test("Server Health", True, "Server responding")
                        return True
                    else:
                        self.log_test("Server Health", False, f"HTTP {response.status}")
                        return False
        except Exception as e:
            self.log_test("Server Health", False, f"Connection failed: {e}")
            return False

    async def test_session_creation(self):
        """Test session creation."""
        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(
                    f"{self.base_url}/v1/session",
                    headers={"Content-Type": "application/x-yaml"},
                    data="model: mock_local"
                ) as response:
                    if response.status == 200:
                        session_data = await response.text()
                        if "session_id" in session_data:
                            self.session_id = session_data.split("session_id: ")[1].strip()
                            self.log_test("Session Creation", True, f"Created session {self.session_id}")
                            return True
                        else:
                            self.log_test("Session Creation", False, "No session_id in response")
                            return False
                    else:
                        self.log_test("Session Creation", False, f"HTTP {response.status}")
                        return False
        except Exception as e:
            self.log_test("Session Creation", False, f"Error: {e}")
            return False

    async def test_websocket_connection(self):
        """Test WebSocket connection and communication."""
        if not self.session_id:
            self.log_test("WebSocket Connection", False, "No session ID available")
            return False

        try:
            uri = f"ws://{self.base_url.replace('http://', '')}/v1/session/{self.session_id}/control"

            async with websockets.connect(uri) as websocket:
                self.websocket = websocket

                # Send test utterance
                test_message = {"type": "player_utterance", "text": "I propose a trade agreement"}
                await websocket.send(json.dumps(test_message))

                # Wait for response
                response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
                response_data = json.loads(response)

                if "type" in response_data:
                    self.log_test("WebSocket Connection", True, f"Received {response_data['type']}")
                    return True
                else:
                    self.log_test("WebSocket Connection", False, "Invalid response format")
                    return False

        except asyncio.TimeoutError:
            self.log_test("WebSocket Connection", False, "Timeout waiting for response")
            return False
        except Exception as e:
            self.log_test("WebSocket Connection", False, f"Error: {e}")
            return False

    async def test_webrtc_sdp_exchange(self):
        """Test WebRTC SDP offer/answer exchange."""
        if not self.session_id:
            self.log_test("WebRTC SDP Exchange", False, "No session ID available")
            return False

        try:
            # Create SDP offer (simulating browser)
            offer_sdp = {
                "sdp": """v=0
o=- 1234567890 1234567890 IN IP4 127.0.0.1
s=Test Negotiation
c=IN IP4 127.0.0.1
t=0 0
m=audio 9 UDP/TLS/RTP/SAVPF 111
a=rtcp:9 IN IP4 0.0.0.0
a=rtcp-mux
a=rtpmap:111 opus/48000/2
a=fmtp:111 minptime=10;useinbandfec=1
a=sendrecv
a=setup:actpass
a=ice-ufrag:test
a=ice-pwd:test
a=fingerprint:sha-256 test
a=candidate:1 1 UDP 2130706431 127.0.0.1 9 typ host""",
                "type": "offer"
            }

            async with aiohttp.ClientSession() as session:
                async with session.post(
                    f"{self.base_url}/v1/session/{self.session_id}/webrtc/offer",
                    json=offer_sdp
                ) as response:
                    if response.status == 200:
                        answer_data = await response.text()
                        if "type" in answer_data and "sdp" in answer_data:
                            self.log_test("WebRTC SDP Exchange", True, "Offer/answer exchange successful")
                            return True
                        else:
                            self.log_test("WebRTC SDP Exchange", False, "Invalid answer format")
                            return False
                    else:
                        self.log_test("WebRTC SDP Exchange", False, f"HTTP {response.status}")
                        return False

        except Exception as e:
            self.log_test("WebRTC SDP Exchange", False, f"Error: {e}")
            return False

    async def test_listener_adapters(self):
        """Test listener adapters."""
        try:
            from listeners.base import make_listener_from_env

            # Test local STT listener
            listener = make_listener_from_env()
            await listener.start()

            # Feed test audio
            test_audio = b"\x00" * 1024
            await listener.feed_pcm(test_audio, 1234567890)

            # Get final text
            final_text = await listener.final_text()
            assert isinstance(final_text, str)

            # Stream events
            events = []
            async for event in listener.stream_events():
                events.append(event)
                if len(events) >= 2:  # Stop after 2 events
                    break

            await listener.stop()

            self.log_test("Listener Adapters", True, f"Processed {len(events)} events")
            return True

        except Exception as e:
            self.log_test("Listener Adapters", False, f"Error: {e}")
            return False

    async def test_provider_integration(self):
        """Test provider integration."""
        try:
            from providers.mock_local import MockLocalProvider

            provider = MockLocalProvider({"strict": True})

            # Test dialogue streaming
            turns = [{"speaker": "PLAYER", "text": "I propose a trade agreement"}]
            world_context = {"scenario": "test"}
            guidelines = "Be diplomatic"

            events = []
            async for event in provider.stream_dialogue(turns, world_context, guidelines):
                events.append(event)

            # Should have at least one event
            if len(events) > 0:
                self.log_test("Provider Integration", True, f"Generated {len(events)} events")
                return True
            else:
                self.log_test("Provider Integration", False, "No events generated")
                return False

        except Exception as e:
            self.log_test("Provider Integration", False, f"Error: {e}")
            return False

    async def test_video_generation(self):
        """Test video generation."""
        try:
            from providers.video_sources.placeholder_loop import PlaceholderLoopVideoSource

            config = {
                "source_type": "placeholder",
                "resolution": (320, 240),
                "fps": 30
            }

            video_source = PlaceholderLoopVideoSource(config)
            assert video_source is not None

            self.log_test("Video Generation", True, "Placeholder video source created")
            return True

        except Exception as e:
            self.log_test("Video Generation", False, f"Error: {e}")
            return False

    async def run_all_tests(self):
        """Run all tests."""
        print("🚀 Starting comprehensive AI Avatar Negotiation System tests...")
        print("=" * 60)

        # Run tests in order
        tests = [
            self.test_server_health,
            self.test_session_creation,
            self.test_websocket_connection,
            self.test_webrtc_sdp_exchange,
            self.test_listener_adapters,
            self.test_provider_integration,
            self.test_video_generation
        ]

        passed = 0
        total = len(tests)

        for test in tests:
            try:
                if await test():
                    passed += 1
                print()
            except Exception as e:
                self.log_test(test.__name__, False, f"Exception: {e}")
                print()

        # Summary
        print("=" * 60)
        print("📊 TEST SUMMARY")
        print("=" * 60)
        print(f"Tests passed: {passed}/{total}")
        print(f"Success rate: {passed/total*100:.1f}%")

        if passed == total:
            print("🎉 ALL TESTS PASSED! The system is working correctly.")
        else:
            print("⚠️ Some tests failed. Check the logs above for details.")

        return passed == total


async def main():
    """Main test runner."""
    tester = NegotiationSystemTester()

    # Wait a moment for server to start
    print("⏳ Waiting for server to be ready...")
    await asyncio.sleep(2)

    success = await tester.run_all_tests()

    if success:
        print("\n🎯 Ready for production testing with real LLMs and video generation!")
    else:
        print("\n🔧 Fix the failing tests before proceeding.")

    return 0 if success else 1


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
