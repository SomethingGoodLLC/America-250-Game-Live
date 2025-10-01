"""
Revolutionary War Diplomatic Avatar System
Integrates ElevenLabs TTS + SadTalker lip-sync for Mac Silicon
Optimized for Apple Silicon using MPS (Metal Performance Shaders)
"""

import os
import sys
import subprocess
from pathlib import Path
from typing import Optional, Dict
import tempfile
import structlog

logger = structlog.get_logger(__name__)

# Check for ElevenLabs availability
try:
    from elevenlabs import generate, save, set_api_key
    ELEVENLABS_AVAILABLE = True
except ImportError:
    ELEVENLABS_AVAILABLE = False
    logger.warning("ElevenLabs not available. Install with: pip install elevenlabs")


class DiplomaticAvatar:
    """
    Animated portrait generator for Revolutionary War characters
    Optimized for Apple Silicon Macs using SadTalker
    """
    
    def __init__(
        self,
        portrait_path: str,
        sadtalker_dir: str = "~/Projects/SadTalker",
        elevenlabs_api_key: Optional[str] = None,
        voice_id: Optional[str] = None
    ):
        """
        Initialize diplomatic avatar
        
        Args:
            portrait_path: Path to 18th-century portrait image
            sadtalker_dir: Path to SadTalker installation
            elevenlabs_api_key: ElevenLabs API key (or set ELEVENLABS_API_KEY env var)
            voice_id: ElevenLabs voice ID for this character
        """
        self.portrait_path = Path(portrait_path).expanduser().absolute()
        self.sadtalker_dir = Path(sadtalker_dir).expanduser().absolute()
        
        # Verify portrait exists
        if not self.portrait_path.exists():
            raise FileNotFoundError(f"Portrait not found: {self.portrait_path}")
        
        # Verify SadTalker installation
        if not self.sadtalker_dir.exists():
            logger.warning(
                f"SadTalker not found at {self.sadtalker_dir}\n"
                f"Clone it: git clone https://github.com/OpenTalker/SadTalker.git"
            )
        
        # Setup ElevenLabs
        if not ELEVENLABS_AVAILABLE:
            logger.warning("ElevenLabs not available - TTS will not work")
        else:
            api_key = elevenlabs_api_key or os.getenv("ELEVENLABS_API_KEY")
            if not api_key:
                raise ValueError("ElevenLabs API key required")
            set_api_key(api_key)
            self.voice_id = voice_id
        
        # Output directory
        self.output_dir = Path("./diplomatic_videos")
        self.output_dir.mkdir(exist_ok=True)
        
        logger.info(
            "Initialized DiplomaticAvatar",
            portrait=str(self.portrait_path),
            sadtalker_dir=str(self.sadtalker_dir),
            has_elevenlabs=ELEVENLABS_AVAILABLE
        )
    
    def speak(
        self,
        text: str,
        output_name: Optional[str] = None,
        emotion: str = "neutral",
        still_mode: bool = False,
        enhance_video: bool = True
    ) -> Path:
        """
        Generate talking portrait video from text
        
        Args:
            text: Diplomatic dialogue text
            output_name: Custom output filename (auto-generated if None)
            emotion: 'neutral', 'diplomatic', 'angry', 'fearful'
            still_mode: If True, minimal head movement (formal speeches)
            enhance_video: Use GFPGAN to enhance quality
            
        Returns:
            Path to generated MP4 video
        """
        logger.info("Generating speech with ElevenLabs", text=text[:50])
        
        if not ELEVENLABS_AVAILABLE:
            raise RuntimeError("ElevenLabs not available - cannot generate speech")
        
        # Generate audio with ElevenLabs
        audio_data = generate(
            text=text,
            voice=self.voice_id,
            model="eleven_multilingual_v2"
        )
        
        # Save to temp file
        with tempfile.NamedTemporaryFile(
            suffix=".mp3",
            delete=False,
            dir=self.output_dir
        ) as temp_audio:
            save(audio_data, temp_audio.name)
            audio_path = Path(temp_audio.name)
        
        logger.info("Audio saved", path=str(audio_path))
        logger.info("Animating portrait with SadTalker...")
        
        # Generate video with SadTalker
        video_path = self._run_sadtalker(
            audio_path,
            still_mode=still_mode,
            enhance=enhance_video
        )
        
        # Cleanup temp audio
        audio_path.unlink()
        
        # Rename output if custom name provided
        if output_name:
            final_path = self.output_dir / f"{output_name}.mp4"
            video_path.rename(final_path)
            video_path = final_path
        
        logger.info("Video generated successfully", path=str(video_path))
        return video_path
    
    def _run_sadtalker(
        self,
        audio_path: Path,
        still_mode: bool = False,
        enhance: bool = True
    ) -> Path:
        """
        Run SadTalker inference
        """
        # Build SadTalker command
        cmd = [
            sys.executable,
            str(self.sadtalker_dir / "inference.py"),
            "--driven_audio", str(audio_path),
            "--source_image", str(self.portrait_path),
            "--result_dir", str(self.output_dir / "sadtalker_results"),
        ]
        
        if still_mode:
            cmd.append("--still")
        
        if enhance:
            cmd.extend(["--enhancer", "gfpgan"])
        
        # Apple Silicon optimization
        cmd.extend([
            "--device", "mps",  # Use Metal Performance Shaders
            "--batch_size", "1"  # Conservative for Mac
        ])
        
        logger.info("Running SadTalker", command=" ".join(cmd))
        
        # Run SadTalker
        result = subprocess.run(
            cmd,
            cwd=self.sadtalker_dir,
            capture_output=True,
            text=True
        )
        
        if result.returncode != 0:
            logger.error("SadTalker failed", stderr=result.stderr)
            raise RuntimeError(
                f"SadTalker failed:\n{result.stderr}"
            )
        
        # Find generated video
        results_dir = self.output_dir / "sadtalker_results"
        videos = list(results_dir.glob("**/*.mp4"))
        
        if not videos:
            raise RuntimeError("No video generated by SadTalker")
        
        return videos[-1]  # Return most recent


# Character presets for Revolutionary War
REVOLUTIONARY_CHARACTERS = {
    "george_washington": {
        "portrait": "portraits/washington.jpg",
        "voice_id": "YOUR_WASHINGTON_VOICE_ID",
        "description": "Commander-in-Chief, Continental Army"
    },
    "benjamin_franklin": {
        "portrait": "portraits/franklin.jpg",
        "voice_id": "YOUR_FRANKLIN_VOICE_ID",
        "description": "American Diplomat to France"
    },
    "lord_cornwallis": {
        "portrait": "portraits/cornwallis.jpg",
        "voice_id": "YOUR_CORNWALLIS_VOICE_ID",
        "description": "British General"
    },
    "joseph_brant": {
        "portrait": "portraits/brant.jpg",
        "voice_id": "YOUR_BRANT_VOICE_ID",
        "description": "Mohawk Chief, British Ally"
    }
}


def create_character_avatar(character_name: str) -> DiplomaticAvatar:
    """
    Quick character setup
    """
    if character_name not in REVOLUTIONARY_CHARACTERS:
        raise ValueError(
            f"Unknown character. Available: {list(REVOLUTIONARY_CHARACTERS.keys())}"
        )
    
    char_data = REVOLUTIONARY_CHARACTERS[character_name]
    
    return DiplomaticAvatar(
        portrait_path=char_data["portrait"],
        voice_id=char_data["voice_id"]
    )


