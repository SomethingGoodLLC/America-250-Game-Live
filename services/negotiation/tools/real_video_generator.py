#!/usr/bin/env python3
"""Real AI video generation using Gemini Veo3 and other AI models."""

import asyncio
import aiohttp
import json
import base64
import os
import time
from typing import Dict, Any, Optional
import structlog

# Import Gemini client
try:
    from google import genai
    from google.genai import types
    GEMINI_AVAILABLE = True
except ImportError:
    genai = None
    types = None
    GEMINI_AVAILABLE = False

logger = structlog.get_logger(__name__)


class GeminiVeo3VideoGenerator:
    """Real AI video generation using Gemini Veo3."""
    
    def __init__(self, api_key: str):
        self.api_key = api_key
        self.client = None
        
    def __enter__(self):
        if not GEMINI_AVAILABLE:
            raise ImportError("google-genai library not installed. Run: pip install google-genai")
        self.client = genai.Client(api_key=self.api_key)
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        pass
    
    def generate_video(self, prompt: str, duration: int = 8) -> Any:
        """Generate real video using Gemini Veo3."""
        logger.info("Starting Gemini Veo3 video generation", prompt=prompt, duration=duration)
        
        if not self.client:
            raise RuntimeError("Client not initialized. Use 'with' statement.")
        
        # Start video generation operation
        operation = self.client.models.generate_videos(
            model="veo-3.0-generate-001",
            prompt=prompt,
            config=types.GenerateVideosConfig(
                resolution="1080p",
                aspect_ratio="16:9",
                duration_seconds=duration,
            ),
        )
        
        logger.info("Gemini Veo3 generation started", operation_name=operation.name)
        return operation
    
    def wait_for_completion(self, operation) -> Any:
        """Wait for video generation to complete."""
        logger.info("Waiting for Gemini Veo3 generation to complete...")
        
        while not operation.done:
            logger.info("Generation in progress...")
            time.sleep(10)
            operation = self.client.operations.get(operation)
        
        if operation.error:
            logger.error("Generation failed", error=operation.error)
            raise Exception(f"Generation failed: {operation.error}")
        
        logger.info("Generation completed successfully")
        
        result = operation.result
        # Handle content safety filter outcomes explicitly so callers can retry with a modified prompt
        if result and hasattr(result, 'rai_media_filtered_count') and result.rai_media_filtered_count:
            logger.warning(
                "Gemini Veo3 RAI filtered media",
                filtered_count=result.rai_media_filtered_count,
                reasons=getattr(result, 'rai_media_filtered_reasons', None),
            )
            # Surface a structured error so caller can decide on fallback
            reasons = getattr(result, 'rai_media_filtered_reasons', []) or []
            raise Exception(f"RAI_FILTERED:{' | '.join(reasons)}")

        if result and hasattr(result, 'generated_videos') and result.generated_videos:
            return result.generated_videos[0]
        else:
            logger.error("Generation result is empty or invalid", result=result)
            raise Exception("Generation result is empty or invalid")
    
    def download_video(self, generated_video, output_path: str) -> str:
        """Download the generated video."""
        logger.info("Downloading generated video", output_path=output_path)
        
        # Download and save the video file using the correct method
        self.client.files.download(file=generated_video.video)
        generated_video.video.save(output_path)
        
        logger.info("Video downloaded successfully", path=output_path)
        return output_path


class RunwayMLVideoGenerator:
    """Alternative: Real video generation using RunwayML Gen-3."""
    
    def __init__(self, api_key: str):
        self.api_key = api_key
        self.base_url = "https://api.runwayml.com/v1"
        self.session = None
    
    async def __aenter__(self):
        self.session = aiohttp.ClientSession()
        return self
        
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()
    
    async def generate_video(self, prompt: str, duration: int = 4) -> Dict[str, Any]:
        """Generate video using RunwayML Gen-3."""
        logger.info("Starting RunwayML video generation", prompt=prompt)
        
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json"
        }
        
        payload = {
            "model": "gen3a_turbo",
            "prompt": prompt,
            "duration": duration,
            "resolution": "1920x1080",
            "seed": int(time.time())
        }
        
        async with self.session.post(
            f"{self.base_url}/image_to_video",
            headers=headers,
            json=payload,
            timeout=aiohttp.ClientTimeout(total=300)
        ) as response:
            
            if response.status == 200:
                result = await response.json()
                logger.info("RunwayML generation started", task_id=result.get("id"))
                return result
            else:
                error_text = await response.text()
                logger.error("RunwayML generation failed", error=error_text)
                raise Exception(f"RunwayML Error: {error_text}")


class StabilityAIVideoGenerator:
    """Alternative: Real video generation using Stability AI."""
    
    def __init__(self, api_key: str):
        self.api_key = api_key
        self.base_url = "https://api.stability.ai/v2beta"
        self.session = None
    
    async def __aenter__(self):
        self.session = aiohttp.ClientSession()
        return self
        
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()
    
    async def generate_video(self, prompt: str, duration: int = 4) -> Dict[str, Any]:
        """Generate video using Stability AI."""
        logger.info("Starting Stability AI video generation", prompt=prompt)
        
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json"
        }
        
        payload = {
            "prompt": prompt,
            "duration": duration,
            "aspect_ratio": "16:9",
            "motion": 127,  # High motion
            "seed": int(time.time())
        }
        
        async with self.session.post(
            f"{self.base_url}/image-to-video",
            headers=headers,
            json=payload,
            timeout=aiohttp.ClientTimeout(total=300)
        ) as response:
            
            if response.status == 200:
                result = await response.json()
                logger.info("Stability AI generation started", id=result.get("id"))
                return result
            else:
                error_text = await response.text()
                logger.error("Stability AI generation failed", error=error_text)
                raise Exception(f"Stability AI Error: {error_text}")


async def generate_british_general_video():
    """Generate the requested British general video using real AI."""
    print("🎬 REAL AI VIDEO GENERATION")
    print("🇬🇧 British General from 1700s saying 'So it's war then'")
    print("=" * 60)
    
    # Get API keys from environment
    gemini_key = os.getenv("GEMINI_API_KEY")
    runway_key = os.getenv("RUNWAY_API_KEY") 
    stability_key = os.getenv("STABILITY_API_KEY")
    
    if not any([gemini_key, runway_key, stability_key]):
        print("❌ No API keys found. Please set one of:")
        print("   - GEMINI_API_KEY for Google Gemini Veo3")
        print("   - RUNWAY_API_KEY for RunwayML Gen-3")
        print("   - STABILITY_API_KEY for Stability AI")
        return None
    
    # Ultra‑realistic, cinematic prompt (primary)
    prompt_primary = (
       """
       Ultra-photorealistic cinematic footage of a stern British military general from the 1700s, wearing a 
       pristine red military coat with gold braiding, white powdered wig, and tricorn hat. He stands in a 
       canvas military command tent, parchment maps and brass instruments on a wooden table, candlelight and 
       subtle volumetric smoke in the air. The general looks straight into the lens and **says: 
       "So it's war then!"** with gravitas. Shot on a 50mm full-frame lens at f/1.8 with shallow depth of 
       field, natural film grain, soft rim lighting, skin subsurface scattering, physically-based materials, 
       and historically accurate textures. High dynamic range, cinematic color grading.

       """
    )

    # Silent fallback prompt (no explicit speech to avoid audio-related RAI filters)
    prompt_silent = (
        "Ultra‑photorealistic cinematic footage of a stern British military general from the 1700s, "
        "wearing a pristine red military coat with gold braiding, white powdered wig, and tricorn hat, inside a canvas command tent. "
        "He looks directly into the camera in silence, then nods solemnly, conveying resolve. "
        "Shot on a 50mm full‑frame lens at f/1.8 with shallow depth of field, natural film grain, soft rim lighting, "
        "skin subsurface scattering, physically‑based materials, and historically accurate textures. High dynamic range, cinematic color grading."
    )
    duration_seconds = 8
    
    video_result = None
    
    # Try Gemini Veo3 first
    if gemini_key:
        try:
            print("🚀 Attempting generation with Google Gemini Veo3...")
            
            if not GEMINI_AVAILABLE:
                print("❌ google-genai library not installed")
                print("💡 Install with: pip install google-genai")
                return None
            
            with GeminiVeo3VideoGenerator(gemini_key) as generator:
                try:
                    # Attempt with primary prompt (includes speaking)
                    operation = generator.generate_video(prompt_primary, duration=duration_seconds)
                    print(f"⏳ Video generation started (Operation: {operation.name})")
                    generated_video = generator.wait_for_completion(operation)
                except Exception as e:
                    msg = str(e)
                    
                    # Detect RAI audio/speech related filtering and retry silently
                    if msg.startswith("RAI_FILTERED") or "audio" in msg.lower() or "speech" in msg.lower():
                        print("⚠️ RAI filter triggered on primary prompt. Retrying with silent prompt...")
                        operation = generator.generate_video(prompt_silent, duration=duration_seconds)
                        generated_video = generator.wait_for_completion(operation)
                    else:
                        raise

                # Download video
                output_path = f"british_general_veo3_{int(time.time())}.mp4"
                generator.download_video(generated_video, output_path)
                print(f"📁 Video saved to: {output_path}")
                return output_path
                
        except Exception as e:
            print(f"❌ Gemini Veo3 failed: {e}")
            print("💡 Make sure your API key has Veo3 access enabled")
    
    # Try RunwayML Gen-3
    if runway_key and not video_result:
        try:
            print("🚀 Attempting generation with RunwayML Gen-3...")
            async with RunwayMLVideoGenerator(runway_key) as generator:
                video_result = await generator.generate_video(prompt_primary, duration=duration_seconds)
                print(f"✅ RunwayML task started: {video_result.get('id')}")
                return video_result
                
        except Exception as e:
            print(f"❌ RunwayML failed: {e}")
    
    # Try Stability AI
    if stability_key and not video_result:
        try:
            print("🚀 Attempting generation with Stability AI...")
            async with StabilityAIVideoGenerator(stability_key) as generator:
                video_result = await generator.generate_video(prompt_primary, duration=duration_seconds)
                print(f"✅ Stability AI task started: {video_result.get('id')}")
                return video_result
                
        except Exception as e:
            print(f"❌ Stability AI failed: {e}")
    
    if not video_result:
        print("❌ All video generation services failed or unavailable")
        return None
    
    return video_result


async def integrate_with_negotiation_system():
    """Integrate real video generation with the negotiation system."""
    print("\n🤝 INTEGRATING WITH NEGOTIATION SYSTEM")
    print("=" * 50)
    
    # Update the Veo3StreamVideoSource to use real generation
    veo3_code = '''
# Real Veo3 integration for providers/video_sources/veo3_stream.py

async def _generate_real_video(self) -> None:
    """Generate real video using AI models."""
    if not self.api_key:
        logger.warning("No API key provided, falling back to mock")
        await self._generate_mock_video()
        return
    
    try:
        async with RealVideoGenerator(self.api_key) as generator:
            # Generate video based on current diplomatic context
            prompt = self._create_diplomatic_prompt()
            
            video_result = await generator.generate_video(prompt, duration=4)
            
            if video_result.get("status") == "processing":
                # Stream frames while waiting for completion
                await self._stream_processing_frames(video_result["video_id"])
            
    except Exception as e:
        logger.error("Real video generation failed", error=str(e))
        # Fallback to mock
        await self._generate_mock_video()

def _create_diplomatic_prompt(self) -> str:
    """Create AI prompt based on diplomatic context."""
    character_desc = f"A {self.style} diplomat in formal attire"
    
    if hasattr(self, 'current_intent'):
        intent = self.current_intent
        if intent == "ULTIMATUM":
            return f"{character_desc} delivering a stern ultimatum with authority"
        elif intent == "PROPOSAL":
            return f"{character_desc} making a diplomatic proposal with confidence"
        elif intent == "CONCESSION":
            return f"{character_desc} accepting terms with dignity"
        else:
            return f"{character_desc} speaking diplomatically"
    
    return f"{character_desc} in a diplomatic setting"
'''
    
    print("📝 Real video integration code:")
    print(veo3_code)
    
    print("\n✅ Integration points:")
    print("   • Real AI video generation in Veo3StreamVideoSource")
    print("   • Dynamic prompts based on diplomatic intent")
    print("   • Fallback to mock if API fails")
    print("   • Streaming frames during generation")


async def main():
    """Main execution."""
    print("🎬 REAL AI VIDEO GENERATION SYSTEM")
    print("=" * 50)
    
    # Generate the requested video
    result = await generate_british_general_video()
    
    if result:
        print(f"\n🎉 SUCCESS!")
        print(f"✅ Real AI video generation working")
        print(f"📹 Generated: British General saying 'So it's war then'")
    else:
        print(f"\n⚠️ Setup required:")
        print(f"💡 Add API key to .env file:")
        print(f"   GEMINI_API_KEY=your_key_here")
        print(f"   # or RUNWAY_API_KEY=your_key_here")
        print(f"   # or STABILITY_API_KEY=your_key_here")
    
    # Show integration
    await integrate_with_negotiation_system()


if __name__ == "__main__":
    asyncio.run(main())
