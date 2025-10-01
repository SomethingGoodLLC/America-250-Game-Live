"""
Wav2Lip Lip-Sync Generator
Fast lip-sync video generation (~5-10 seconds per video)
Optimized for Apple Silicon
"""

import os
import sys
import subprocess
import time
from pathlib import Path
from typing import Optional
import structlog

logger = structlog.get_logger(__name__)


class Wav2LipGenerator:
    """
    Fast lip-sync video generator using Wav2Lip
    Processing time: ~5-10 seconds (vs 20 minutes for SadTalker)
    """
    
    def __init__(
        self,
        wav2lip_dir: str = "~/Projects/Wav2Lip",
        checkpoint: str = "checkpoints/wav2lip_gan.pth",
        device: str = "mps"  # mps for Apple Silicon, cpu, or cuda
    ):
        """
        Initialize Wav2Lip generator
        
        Args:
            wav2lip_dir: Path to Wav2Lip installation
            checkpoint: Path to pre-trained model weights
            device: Device to run on (mps/cpu/cuda)
        """
        self.wav2lip_dir = Path(wav2lip_dir).expanduser().absolute()
        self.checkpoint_path = self.wav2lip_dir / checkpoint
        self.device = device
        
        # Verify installation
        self.inference_script = self.wav2lip_dir / "inference.py"
        if not self.inference_script.exists():
            raise FileNotFoundError(
                f"Wav2Lip not found at {self.wav2lip_dir}. "
                f"Please clone: git clone https://github.com/Rudrabha/Wav2Lip.git"
            )
        
        # Check for model
        if not self.checkpoint_path.exists():
            logger.warning(
                "Wav2Lip model not found. Download from: "
                "https://github.com/Rudrabha/Wav2Lip/releases/download/models/wav2lip_gan.pth",
                path=str(self.checkpoint_path)
            )
        
        logger.info(
            "Wav2Lip generator initialized",
            wav2lip_dir=str(self.wav2lip_dir),
            device=device,
            model_ready=self.checkpoint_path.exists()
        )
    
    def generate(
        self,
        portrait_path: Path,
        audio_path: Path,
        output_path: Optional[Path] = None,
        face_det_batch_size: int = 16,
        wav2lip_batch_size: int = 128,
        resize_factor: int = 1,
        fps: int = 25
    ) -> Path:
        """
        Generate lip-synced video
        
        Args:
            portrait_path: Path to portrait image or video
            audio_path: Path to audio file (WAV preferred)
            output_path: Optional output path (auto-generated if None)
            face_det_batch_size: Face detection batch size (higher = faster but more memory)
            wav2lip_batch_size: Wav2Lip batch size (higher = faster but more memory)
            resize_factor: Resize factor for speedup (1 = no resize)
            fps: Output video FPS
            
        Returns:
            Path to generated video file
        """
        if not self.checkpoint_path.exists():
            raise FileNotFoundError(
                f"Model not found at {self.checkpoint_path}. "
                "Download it first: https://github.com/Rudrabha/Wav2Lip/releases/download/models/wav2lip_gan.pth"
            )
        
        # Create output path if not provided
        if output_path is None:
            output_dir = Path("diplomatic_videos/wav2lip")
            output_dir.mkdir(parents=True, exist_ok=True)
            timestamp = int(time.time())
            output_path = output_dir / f"wav2lip_{timestamp}.mp4"
        
        output_path = Path(output_path).absolute()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Get Python from Wav2Lip venv - MUST use this for correct numpy version
        venv_python = self.wav2lip_dir / ".venv" / "bin" / "python"
        if not venv_python.exists():
            raise FileNotFoundError(
                f"Wav2Lip venv not found at {venv_python}. "
                f"Run: cd {self.wav2lip_dir} && uv venv"
            )
        python_exe = str(venv_python)
        
        # Build command
        cmd = [
            python_exe,
            str(self.inference_script),
            "--checkpoint_path", str(self.checkpoint_path),
            "--face", str(portrait_path.absolute()),
            "--audio", str(audio_path.absolute()),
            "--outfile", str(output_path),
            "--face_det_batch_size", str(face_det_batch_size),
            "--wav2lip_batch_size", str(wav2lip_batch_size),
            "--resize_factor", str(resize_factor),
            "--fps", str(fps),
            "--nosmooth",  # Faster processing
        ]
        
        logger.info(
            "Starting Wav2Lip video generation",
            portrait=str(portrait_path),
            audio=str(audio_path),
            output=str(output_path),
            device=self.device
        )
        
        # Run Wav2Lip
        try:
            result = subprocess.run(
                cmd,
                cwd=str(self.wav2lip_dir),
                capture_output=True,
                text=True,
                timeout=60  # 60 second timeout for fast generation
            )
            
            if result.returncode != 0:
                logger.error(
                    "Wav2Lip generation failed",
                    returncode=result.returncode,
                    stderr=result.stderr[:500]
                )
                raise RuntimeError(f"Wav2Lip failed: {result.stderr[:200]}")
            
            if not output_path.exists():
                raise FileNotFoundError(f"Output video not generated at {output_path}")
            
            logger.info(
                "Wav2Lip video generated successfully",
                output_path=str(output_path),
                size_mb=output_path.stat().st_size / 1024 / 1024
            )
            
            return output_path
            
        except subprocess.TimeoutExpired:
            logger.error("Wav2Lip generation timed out after 60 seconds")
            raise
        except Exception as e:
            logger.error("Wav2Lip generation error", error=str(e))
            raise


def quick_test():
    """Quick test of Wav2Lip generator"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Test Wav2Lip generator")
    parser.add_argument("--portrait", required=True, help="Path to portrait image")
    parser.add_argument("--audio", required=True, help="Path to audio file")
    parser.add_argument("--output", help="Output video path")
    
    args = parser.parse_args()
    
    generator = Wav2LipGenerator()
    
    output = generator.generate(
        portrait_path=Path(args.portrait),
        audio_path=Path(args.audio),
        output_path=Path(args.output) if args.output else None
    )
    
    print(f"\n✅ Video generated: {output}")
    print(f"📁 Size: {output.stat().st_size / 1024:.1f} KB")
    print(f"🎬 Open with: open {output}\n")


if __name__ == "__main__":
    quick_test()

