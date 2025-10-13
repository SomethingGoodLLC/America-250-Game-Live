#!/usr/bin/env python3
"""
Example demonstrating Audio2Face integration with ARKit-52 blendshapes.

This script shows how to:
1. Use ARKit-52 utilities
2. Create and validate blendshape arrays
3. Connect to NVIDIA Audio2Face (or use stub mode)
4. Stream audio and receive blendshape frames

Run with: python examples/a2f_integration_example.py
"""

import asyncio
import os
from pathlib import Path

from app.facial import (
    ARKIT52,
    ARKIT52_INDEX,
    Audio2FaceClient,
    Audio2FaceStubClient,
    Audio2FaceConfig,
    BlendshapeFrame,
    create_empty_arkit52,
    validate_arkit52_weights,
    clamp01,
)


def demo_arkit52_utilities():
    """Demonstrate ARKit-52 utility functions."""
    print("=" * 60)
    print("ARKit-52 Utilities Demo")
    print("=" * 60)
    
    # 1. Show all blendshape names
    print(f"\n1. Total blendshapes: {len(ARKIT52)}")
    print(f"   First 5: {ARKIT52[:5]}")
    print(f"   Last 5: {ARKIT52[-5:]}")
    
    # 2. Map name to index
    print(f"\n2. Blendshape indices:")
    for name in ["browInnerUp", "jawOpen", "mouthSmileLeft", "tongueOut"]:
        idx = ARKIT52_INDEX[name]
        print(f"   {name:20s} → index {idx}")
    
    # 3. Create neutral face
    neutral = create_empty_arkit52()
    print(f"\n3. Neutral face: {len(neutral)} weights, all zeros = {all(w == 0.0 for w in neutral)}")
    
    # 4. Create smile expression
    smile = create_empty_arkit52()
    smile[ARKIT52_INDEX["mouthSmileLeft"]] = 0.8
    smile[ARKIT52_INDEX["mouthSmileRight"]] = 0.8
    smile[ARKIT52_INDEX["jawOpen"]] = 0.2
    
    print(f"\n4. Smile expression:")
    print(f"   mouthSmileLeft = {smile[ARKIT52_INDEX['mouthSmileLeft']]}")
    print(f"   mouthSmileRight = {smile[ARKIT52_INDEX['mouthSmileRight']]}")
    print(f"   jawOpen = {smile[ARKIT52_INDEX['jawOpen']]}")
    print(f"   Valid = {validate_arkit52_weights(smile)}")
    
    # 5. Demonstrate clamping
    print(f"\n5. Clamping values:")
    values = [-0.5, 0.0, 0.5, 1.0, 1.5]
    for val in values:
        clamped = clamp01(val)
        print(f"   {val:5.1f} → {clamped:4.1f}")


async def demo_a2f_stub_client():
    """Demonstrate Audio2Face stub client (no NVIDIA service required)."""
    print("\n" + "=" * 60)
    print("Audio2Face Stub Client Demo")
    print("=" * 60)
    
    # Configure stub client
    config = Audio2FaceConfig(
        host="127.0.0.1",
        port=50051,
        model_id="audio2face_3d",
        sample_rate=16000,
    )
    
    # Create stub client (works without NVIDIA A2F service)
    client = Audio2FaceStubClient(config)
    
    print("\n1. Connecting to stub client...")
    await client.connect()
    print("   ✓ Connected (stub mode - no actual A2F service needed)")
    
    # Generate dummy audio chunks (silent audio)
    print("\n2. Streaming audio chunks...")
    
    async def dummy_audio_generator():
        """Generate 10 chunks of silent audio."""
        chunk_size = 1600  # 100ms at 16kHz
        for i in range(10):
            # Create silent PCM16 audio
            chunk = b'\x00\x00' * chunk_size
            yield chunk
            await asyncio.sleep(0.1)  # Simulate real-time audio
    
    # Stream audio and receive blendshape frames
    frame_count = 0
    print("   Receiving blendshape frames...")
    
    async for frame in client.stream_audio(dummy_audio_generator()):
        frame_count += 1
        
        if frame_count <= 3:  # Show first 3 frames
            # Find non-zero weights
            non_zero = [(i, w) for i, w in enumerate(frame.weights) if w > 0.01]
            
            print(f"\n   Frame {frame.frame_index}:")
            print(f"     Timestamp: {frame.timestamp_ms:.2f}ms")
            print(f"     Active blendshapes: {len(non_zero)}")
            
            if non_zero:
                for idx, weight in non_zero[:3]:  # Show first 3 active
                    name = ARKIT52[idx]
                    print(f"       {name}: {weight:.3f}")
    
    print(f"\n   ✓ Received {frame_count} frames")
    
    # Disconnect
    await client.disconnect()
    print("\n3. Disconnected")


async def demo_a2f_real_client():
    """Demonstrate real Audio2Face client (requires NVIDIA A2F service)."""
    print("\n" + "=" * 60)
    print("Audio2Face Real Client Demo")
    print("=" * 60)
    
    # Check if A2F service is configured
    a2f_host = os.getenv("A2F_GRPC_HOST", "127.0.0.1")
    a2f_port = int(os.getenv("A2F_GRPC_PORT", "50051"))
    
    print(f"\nConfiguration:")
    print(f"  Host: {a2f_host}")
    print(f"  Port: {a2f_port}")
    
    config = Audio2FaceConfig(
        host=a2f_host,
        port=a2f_port,
        model_id=os.getenv("A2F_MODEL_ID", "audio2face_3d"),
        sample_rate=16000,
    )
    
    client = Audio2FaceClient(config)
    
    try:
        print("\nAttempting connection to NVIDIA Audio2Face service...")
        await client.connect()
        print("✓ Connected to real A2F service!")
        
        # You would stream real audio here
        # See dummy_audio_generator() above for example
        
        await client.disconnect()
        
    except Exception as e:
        print(f"⚠ Could not connect to NVIDIA A2F service: {e}")
        print("\nTo use real Audio2Face:")
        print("1. Install NVIDIA Audio2Face Microservice")
        print("2. Configure .env with A2F_GRPC_HOST and A2F_GRPC_PORT")
        print("3. Run the service before executing this script")
        print("\nFor development, use stub mode (see demo_a2f_stub_client above)")


async def main():
    """Run all demos."""
    # Demo 1: ARKit-52 utilities
    demo_arkit52_utilities()
    
    # Demo 2: A2F stub client (always works)
    await demo_a2f_stub_client()
    
    # Demo 3: Real A2F client (optional, requires service)
    await demo_a2f_real_client()
    
    print("\n" + "=" * 60)
    print("Demo Complete!")
    print("=" * 60)
    print("\nNext steps:")
    print("- Check /a2f/health endpoint: http://localhost:8000/a2f/health")
    print("- List blendshapes: http://localhost:8000/a2f/blendshapes")
    print("- Try WebSocket streaming: ws://localhost:8000/a2f/stream?use_stub=true")
    print("- Read the docs: services/negotiation/README.md (Audio2Face section)")


if __name__ == "__main__":
    asyncio.run(main())

