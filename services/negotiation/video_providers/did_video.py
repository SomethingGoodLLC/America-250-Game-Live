"""
D-ID Video Avatar Provider
Fast, reliable talking avatar generation (2-5 seconds)
Production-ready cloud service
"""

import os
import asyncio
import aiohttp
from pathlib import Path
from typing import Optional
import structlog

logger = structlog.get_logger(__name__)


class DIDVideoProvider:
    """
    D-ID talking avatar provider
    https://www.d-id.com/
    
    Pricing: ~$0.30 per video minute
    Speed: 2-5 seconds generation time
    Quality: Professional, photorealistic lip-sync
    """
    
    def __init__(self, api_key: Optional[str] = None):
        """
        Initialize D-ID provider
        
        Args:
            api_key: D-ID API key (or set DID_API_KEY env var)
        """
        self.api_key = api_key or os.getenv("DID_API_KEY")
        if not self.api_key:
            raise ValueError("DID_API_KEY not set. Get one at https://studio.d-id.com/account-settings")
        
        self.base_url = "https://api.d-id.com"
        self.headers = {
            "Authorization": f"Basic {self.api_key}",
            "Content-Type": "application/json"
        }
        
        logger.info("D-ID provider initialized")
    
    async def generate_video(
        self,
        text: str = None,
        audio_url: str = None,
        image_url: str = None,
        output_path: Optional[Path] = None,
        voice_id: str = "en-US-JennyNeural"
    ) -> str:
        """
        Generate talking avatar video from text or audio
        
        Args:
            text: Text to speak (uses D-ID's TTS) - RECOMMENDED
            audio_url: URL to audio file (must be HTTPS and publicly accessible)
            image_url: URL to portrait image (can be local file path)
            output_path: Optional local path to save video
            voice_id: D-ID voice ID (default: en-US-JennyNeural)
            
        Returns:
            URL to generated video
        """
        if not text and not audio_url:
            raise ValueError("Must provide either text or audio_url")
        
        # Upload local image if needed
        if image_url and not image_url.startswith("http"):
            # Local file path - need to upload it
            image_url = await self._upload_image(Path(image_url))
        
        logger.info("Starting D-ID video generation", text=text[:50] if text else None, 
                   audio=audio_url, image=image_url)
        
        # Create talk with text (D-ID's TTS) or audio URL
        if text:
            script_config = {
                "type": "text",
                "input": text,
                "provider": {
                    "type": "microsoft",
                    "voice_id": voice_id
                }
            }
        else:
            script_config = {
                "type": "audio",
                "audio_url": audio_url
            }
        
        create_payload = {
            "source_url": image_url,
            "script": script_config,
            "config": {
                "fluent": True,
                "pad_audio": 0
            }
        }
        
        async with aiohttp.ClientSession() as session:
            # Step 1: Create the talk
            async with session.post(
                f"{self.base_url}/talks",
                headers=self.headers,
                json=create_payload
            ) as response:
                if response.status != 201:
                    error_text = await response.text()
                    raise RuntimeError(f"D-ID API error: {response.status} - {error_text}")
                
                data = await response.json()
                talk_id = data["id"]
                logger.info("D-ID talk created", talk_id=talk_id)
            
            # Step 2: Poll for completion
            max_wait = 120  # 120 seconds max (D-ID free tier can be slow)
            poll_interval = 2  # Check every 2 seconds
            elapsed = 0
            
            logger.info("⏳ D-ID video generation started - this may take 20-40 seconds on free tier...")
            
            while elapsed < max_wait:
                async with session.get(
                    f"{self.base_url}/talks/{talk_id}",
                    headers=self.headers
                ) as response:
                    if response.status != 200:
                        raise RuntimeError(f"D-ID status check failed: {response.status}")
                    
                    data = await response.json()
                    status = data["status"]
                    
                    logger.info("D-ID status", talk_id=talk_id, status=status, elapsed=elapsed)
                    
                    # Log progress every 10 seconds
                    if elapsed % 10 == 0 and elapsed > 0:
                        logger.info(f"⏳ Still waiting for D-ID... ({elapsed}s elapsed)")
                    
                    if status == "done":
                        result_url = data["result_url"]
                        logger.info("D-ID video ready", url=result_url)
                        
                        # Download if output_path specified
                        if output_path:
                            await self._download_video(session, result_url, output_path)
                            return str(output_path)
                        
                        return result_url
                    
                    elif status == "error":
                        error = data.get("error", {}).get("description", "Unknown error")
                        raise RuntimeError(f"D-ID generation failed: {error}")
                    
                    # Still processing
                    await asyncio.sleep(poll_interval)
                    elapsed += poll_interval
            
            raise TimeoutError(f"D-ID video generation timed out after {max_wait}s")
    
    async def _upload_image(self, image_path: Path) -> str:
        """Upload local image to D-ID and return URL"""
        logger.info("Uploading image to D-ID", path=str(image_path))
        
        if not image_path.exists():
            raise FileNotFoundError(f"Image not found: {image_path}")
        
        async with aiohttp.ClientSession() as session:
            # Upload image
            with open(image_path, 'rb') as f:
                data = aiohttp.FormData()
                data.add_field('image', f, filename=image_path.name,
                             content_type='image/jpeg')
                
                async with session.post(
                    f"{self.base_url}/images",
                    headers={"Authorization": f"Basic {self.api_key}"},
                    data=data
                ) as response:
                    if response.status != 201:
                        error = await response.text()
                        raise RuntimeError(f"Failed to upload image: {response.status} - {error}")
                    
                    result = await response.json()
                    url = result["url"]
                    logger.info("Image uploaded to D-ID", url=url)
                    return url
    
    async def _download_video(self, session: aiohttp.ClientSession, url: str, path: Path):
        """Download video from URL to local path"""
        logger.info("Downloading D-ID video", url=url, path=str(path))
        
        path.parent.mkdir(parents=True, exist_ok=True)
        
        async with session.get(url) as response:
            if response.status != 200:
                raise RuntimeError(f"Failed to download video: {response.status}")
            
            with open(path, 'wb') as f:
                f.write(await response.read())
        
        logger.info("Video downloaded", size=path.stat().st_size)


async def test_did():
    """Test D-ID integration"""
    import sys
    
    if len(sys.argv) < 3:
        print("Usage: python did_video.py <audio_url> <image_url>")
        print("Example: python did_video.py https://example.com/audio.wav https://example.com/portrait.jpg")
        sys.exit(1)
    
    audio_url = sys.argv[1]
    image_url = sys.argv[2]
    
    provider = DIDVideoProvider()
    
    output = Path("test_did_output.mp4")
    video_url = await provider.generate_video(audio_url, image_url, output)
    
    print(f"\n✅ Video generated!")
    print(f"URL: {video_url}")
    if output.exists():
        print(f"Local: {output}")
        print(f"Size: {output.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    asyncio.run(test_did())


