#!/usr/bin/env python3
"""Test script for Gemini Live API integration.

This script demonstrates how to use the GeminiRealtimeListener with actual
Gemini Live API calls for diplomatic negotiations.

Usage:
    export GEMINI_API_KEY="your-api-key"
    python examples/gemini_live_test.py
"""

import asyncio
import os
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.append(str(Path(__file__).parent.parent))

from listeners.gemini_realtime import GeminiRealtimeListener
import structlog

# Configure logging
structlog.configure(
    processors=[
        structlog.stdlib.filter_by_level,
        structlog.stdlib.add_logger_name,
        structlog.stdlib.add_log_level,
        structlog.stdlib.PositionalArgumentsFormatter(),
        structlog.processors.TimeStamper(fmt="iso"),
        structlog.processors.StackInfoRenderer(),
        structlog.processors.format_exc_info,
        structlog.processors.JSONRenderer()
    ],
    context_class=dict,
    logger_factory=structlog.stdlib.LoggerFactory(),
    wrapper_class=structlog.stdlib.BoundLogger,
    cache_logger_on_first_use=True,
)

logger = structlog.get_logger(__name__)


async def test_gemini_live_basic():
    """Test basic Gemini Live API connection and streaming."""
    logger.info("Starting Gemini Live API test")
    
    config = {
        "api_key": os.getenv("GEMINI_API_KEY"),
        "model": "gemini-2.0-flash-exp"
    }
    
    if not config["api_key"]:
        logger.warning("GEMINI_API_KEY not set, testing in mock mode")
    
    listener = GeminiRealtimeListener(config)
    
    try:
        # Start the listener
        await listener.start()
        logger.info("Gemini listener started")
        
        # Simulate some audio input (in real usage, this would come from WebRTC)
        test_audio = b'\x00' * 3200  # 100ms of silence
        await listener.feed_pcm(test_audio, int(asyncio.get_event_loop().time() * 1000))
        
        # Listen for events for a few seconds
        logger.info("Listening for events...")
        event_count = 0
        async for event in listener.stream_events():
            logger.info("Received event", event=event)
            event_count += 1
            
            # Stop after receiving a few events or timeout
            if event_count >= 3:
                break
                
        logger.info("Test completed", events_received=event_count)
        
    finally:
        await listener.stop()
        logger.info("Gemini listener stopped")


async def test_diplomatic_conversation():
    """Test a diplomatic conversation scenario."""
    logger.info("Starting diplomatic conversation test")
    
    config = {
        "api_key": os.getenv("GEMINI_API_KEY"),
        "model": "gemini-2.0-flash-exp"
    }
    
    listener = GeminiRealtimeListener(config)
    
    try:
        await listener.start()
        
        # Simulate a diplomatic conversation
        diplomatic_phrases = [
            "We propose a trade agreement between our nations.",
            "The terms you've suggested require careful consideration.", 
            "We're willing to negotiate on the territorial boundaries.",
            "This ultimatum is unacceptable to our delegation.",
            "Perhaps we can find a mutually beneficial compromise."
        ]
        
        for i, phrase in enumerate(diplomatic_phrases):
            logger.info("Simulating phrase", phrase=phrase, index=i)
            
            # In real usage, this would be actual audio data
            # For testing, we simulate with silence
            test_audio = b'\x00' * 6400  # 200ms of silence
            await listener.feed_pcm(test_audio, int(asyncio.get_event_loop().time() * 1000))
            
            # Wait for response
            await asyncio.sleep(2.0)
            
            # Check for events
            try:
                async for event in listener.stream_events():
                    logger.info("Diplomatic response", event=event)
                    # Process first event and move to next phrase
                    break
            except asyncio.TimeoutError:
                logger.info("No response received for phrase", phrase=phrase)
                
        logger.info("Diplomatic conversation test completed")
        
    finally:
        await listener.stop()


async def main():
    """Main test function."""
    logger.info("=== Gemini Live API Test Suite ===")
    
    # Test 1: Basic connection and streaming
    logger.info("Test 1: Basic connection test")
    await test_gemini_live_basic()
    
    await asyncio.sleep(1.0)
    
    # Test 2: Diplomatic conversation
    logger.info("Test 2: Diplomatic conversation test")
    await test_diplomatic_conversation()
    
    logger.info("=== All tests completed ===")


if __name__ == "__main__":
    asyncio.run(main())
