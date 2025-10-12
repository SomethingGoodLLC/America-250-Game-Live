#!/usr/bin/env python3
"""Example WebSocket client for TTS streaming with backpressure.

This demonstrates how to connect to the /ws/stream endpoint and:
1. Initialize a streaming session with provider and voice
2. Send text messages for synthesis
3. Receive audio and timing events
4. Handle stop commands and cleanup

Usage:
    python websocket_stream_client.py --provider cartesia --voice-id "voice-123"
"""

import asyncio
import argparse
import base64
import json
import wave
from pathlib import Path
from typing import Optional

import websockets
from websockets.client import WebSocketClientProtocol


async def stream_tts(
    url: str,
    provider: str,
    voice_id: str,
    mode: str = "phonemes",
    sample_rate: int = 24000
):
    """Connect to WebSocket and stream TTS.
    
    Args:
        url: WebSocket URL (e.g., "ws://localhost:8000/ws/stream")
        provider: TTS provider ("cartesia", "azure", "elevenlabs")
        voice_id: Voice identifier
        mode: Streaming mode ("phonemes", "viseme", "blendshapes")
        sample_rate: Audio sample rate
    """
    audio_chunks = []
    phoneme_events = []
    
    async with websockets.connect(url) as websocket:
        print(f"Connected to {url}")
        
        # Step 1: Send initialization config
        config = {
            "provider": provider,
            "voice_id": voice_id,
            "mode": mode,
            "sample_rate": sample_rate
        }
        
        print(f"Sending config: {config}")
        await websocket.send(json.dumps(config))
        
        # Receive acknowledgment
        response = await websocket.recv()
        response_data = json.loads(response)
        
        if response_data.get("type") == "config_ack":
            print(f"✓ {response_data['message']}")
        else:
            print(f"✗ Unexpected response: {response_data}")
            return
        
        # Step 2: Start event receiver task
        async def receive_events():
            """Receive and process events from server."""
            nonlocal audio_chunks, phoneme_events
            
            while True:
                try:
                    message = await websocket.recv()
                    event = json.loads(message)
                    
                    if event["kind"] == "audio":
                        # Decode and store audio
                        if event.get("pcm16_b64"):
                            audio_data = base64.b64decode(event["pcm16_b64"])
                            audio_chunks.append(audio_data)
                            print(f"  ♪ Received audio chunk: {len(audio_data)} bytes")
                    
                    elif event["kind"] == "phonemes":
                        # Store phoneme timing
                        phonemes = event.get("phonemes", [])
                        phoneme_events.extend(phonemes)
                        print(f"  📝 Received {len(phonemes)} phonemes")
                    
                    elif event["kind"] == "viseme":
                        # Handle viseme event
                        viseme_id = event.get("viseme", {}).get("viseme_id")
                        offset = event.get("viseme", {}).get("audio_offset_ms")
                        print(f"  👄 Viseme {viseme_id} at {offset}ms")
                    
                    elif event["kind"] == "blendshapes":
                        # Handle blendshape frames
                        frames = event.get("blendshapes", [])
                        print(f"  🎭 Received {len(frames)} blendshape frames")
                    
                    elif event["kind"] == "done":
                        print("  ✓ Synthesis complete")
                        break
                    
                    elif event["kind"] == "error":
                        print(f"  ✗ Error: {event.get('error')}")
                        break
                
                except websockets.exceptions.ConnectionClosed:
                    print("  Connection closed")
                    break
                except Exception as e:
                    print(f"  Error receiving: {e}")
                    break
        
        # Start receiver in background
        receiver_task = asyncio.create_task(receive_events())
        
        # Step 3: Send text messages
        texts = [
            "Hello, this is a test of the streaming TTS system.",
            "The system supports multiple providers including Cartesia, Azure, and ElevenLabs.",
            "Backpressure is automatically managed to prevent memory issues."
        ]
        
        for i, text in enumerate(texts, 1):
            print(f"\nSending text {i}/{len(texts)}: '{text[:50]}...'")
            await websocket.send(json.dumps({"text": text}))
            await asyncio.sleep(1)  # Simulate pacing
        
        # Optional: Send stop command
        # await websocket.send(json.dumps({"command": "stop"}))
        
        # Wait for all events to be received
        print("\nWaiting for synthesis to complete...")
        await receiver_task
        
        # Step 4: Save results
        if audio_chunks:
            output_path = Path("output_stream.wav")
            combined_audio = b''.join(audio_chunks)
            
            with wave.open(str(output_path), 'wb') as wav_file:
                wav_file.setnchannels(1)  # Mono
                wav_file.setsampwidth(2)  # 16-bit
                wav_file.setframerate(sample_rate)
                wav_file.writeframes(combined_audio)
            
            print(f"\n✓ Saved audio to {output_path}")
            print(f"  Duration: {len(combined_audio) / (sample_rate * 2):.2f}s")
            print(f"  Size: {len(combined_audio)} bytes")
        
        if phoneme_events:
            print(f"✓ Received {len(phoneme_events)} phoneme events")
            
            # Print first few phonemes
            print("\nSample phonemes:")
            for phoneme in phoneme_events[:5]:
                print(f"  {phoneme}")


async def test_backpressure(url: str, provider: str, voice_id: str):
    """Test backpressure handling by sending many rapid messages.
    
    This tests the server's ability to handle slow clients by dropping
    non-critical events (phonemes, visemes) while preserving audio.
    """
    print("\n=== Testing Backpressure ===\n")
    
    async with websockets.connect(url) as websocket:
        # Initialize
        config = {
            "provider": provider,
            "voice_id": voice_id,
            "mode": "phonemes",
            "sample_rate": 24000
        }
        await websocket.send(json.dumps(config))
        
        # Wait for ack
        await websocket.recv()
        print("✓ Connected")
        
        # Send many text messages rapidly
        long_text = " ".join([
            "This is a very long text that will generate many events."
        ] * 20)
        
        print(f"Sending large text ({len(long_text)} chars)...")
        await websocket.send(json.dumps({"text": long_text}))
        
        # Receive events slowly to simulate slow client
        event_count = {"audio": 0, "phonemes": 0, "other": 0}
        
        async for message in websocket:
            await asyncio.sleep(0.1)  # Slow consumer
            
            try:
                event = json.loads(message)
                kind = event.get("kind")
                
                if kind == "audio":
                    event_count["audio"] += 1
                elif kind == "phonemes":
                    event_count["phonemes"] += 1
                elif kind == "done":
                    break
                else:
                    event_count["other"] += 1
                
            except Exception as e:
                print(f"Error: {e}")
                break
        
        print(f"\n✓ Received events:")
        print(f"  Audio: {event_count['audio']}")
        print(f"  Phonemes: {event_count['phonemes']}")
        print(f"  Other: {event_count['other']}")
        print("\nNote: Some phoneme events may have been dropped due to backpressure")


async def test_stop_command(url: str, provider: str, voice_id: str):
    """Test stop command functionality."""
    print("\n=== Testing Stop Command ===\n")
    
    async with websockets.connect(url) as websocket:
        # Initialize
        config = {
            "provider": provider,
            "voice_id": voice_id,
            "mode": "phonemes"
        }
        await websocket.send(json.dumps(config))
        await websocket.recv()
        print("✓ Connected")
        
        # Start receiving events
        async def receiver():
            count = 0
            async for message in websocket:
                event = json.loads(message)
                print(f"  Received: {event.get('kind')}")
                count += 1
                if event.get("kind") in ("done", "error"):
                    break
            return count
        
        receiver_task = asyncio.create_task(receiver())
        
        # Send text
        await websocket.send(json.dumps({"text": "This is a test message."}))
        await asyncio.sleep(0.5)
        
        # Send stop command
        print("Sending stop command...")
        await websocket.send(json.dumps({"command": "stop"}))
        
        # Wait for completion
        event_count = await receiver_task
        print(f"\n✓ Stopped after {event_count} events")


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="WebSocket TTS streaming client example"
    )
    parser.add_argument(
        "--url",
        default="ws://localhost:8000/ws/stream",
        help="WebSocket URL"
    )
    parser.add_argument(
        "--provider",
        choices=["cartesia", "azure", "elevenlabs"],
        default="cartesia",
        help="TTS provider"
    )
    parser.add_argument(
        "--voice-id",
        required=True,
        help="Voice identifier"
    )
    parser.add_argument(
        "--mode",
        choices=["phonemes", "viseme", "blendshapes"],
        default="phonemes",
        help="Streaming mode"
    )
    parser.add_argument(
        "--test",
        choices=["basic", "backpressure", "stop", "all"],
        default="basic",
        help="Test to run"
    )
    
    args = parser.parse_args()
    
    # Run selected test
    if args.test == "basic":
        asyncio.run(stream_tts(
            url=args.url,
            provider=args.provider,
            voice_id=args.voice_id,
            mode=args.mode
        ))
    
    elif args.test == "backpressure":
        asyncio.run(test_backpressure(
            url=args.url,
            provider=args.provider,
            voice_id=args.voice_id
        ))
    
    elif args.test == "stop":
        asyncio.run(test_stop_command(
            url=args.url,
            provider=args.provider,
            voice_id=args.voice_id
        ))
    
    elif args.test == "all":
        print("=== Running All Tests ===\n")
        asyncio.run(stream_tts(args.url, args.provider, args.voice_id, args.mode))
        asyncio.run(test_backpressure(args.url, args.provider, args.voice_id))
        asyncio.run(test_stop_command(args.url, args.provider, args.voice_id))


if __name__ == "__main__":
    main()

