# SadTalker Portrait Animation Setup Guide

**Revolutionary War Diplomatic Avatar System**  
Optimized for Apple Silicon Macs (M1/M2/M3)

## Overview

This guide will help you set up SadTalker for generating lip-synced talking portraits from static images. Perfect for creating animated 18th-century diplomatic characters with ElevenLabs voice synthesis.

## Hardware Requirements

- **Apple Silicon Mac** (M1/M2/M3)
- **8GB RAM minimum** (16GB recommended)
- **10GB free disk space** (for models and generated videos)
- **macOS 12.0+**

## Installation Steps

### 1. Install PyTorch with MPS Support

MPS (Metal Performance Shaders) provides GPU acceleration on Apple Silicon:

```bash
pip3 install --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/cpu
```

### 2. Clone SadTalker Repository

```bash
cd ~/Projects  # or your preferred location
git clone https://github.com/OpenTalker/SadTalker.git
cd SadTalker
```

### 3. Install Dependencies

```bash
# Install ffmpeg via Homebrew
brew install ffmpeg

# Install Python requirements
pip install -r requirements.txt

# Install additional face processing libraries
pip install dlib
pip install face-alignment
```

### 4. Download Pre-trained Models

This downloads approximately 2GB of model files:

```bash
bash scripts/download_models.sh
```

### 5. Verify Installation

Test with a sample image and audio:

```bash
python inference.py \
  --driven_audio examples/driven_audio/bus_chinese.wav \
  --source_image examples/source_image/full_body_1.png \
  --result_dir ./test_output
```

If successful, you'll find generated video in `./test_output/`

## Configuration

### Environment Variables

Add to `/services/negotiation/.env`:

```bash
# SadTalker Configuration
SADTALKER_DIR=~/Projects/SadTalker
PORTRAIT_PATH=assets/avatars/portrait.jpg
SADTALKER_DEVICE=mps  # Apple Silicon GPU acceleration
SADTALKER_ENHANCE=true  # Use GFPGAN enhancement
SADTALKER_STILL_MODE=false  # Minimal head movement
```

### Portrait Images

Place your portrait images in `/services/negotiation/assets/avatars/`:

**Image Requirements:**
- Format: JPG or PNG
- Resolution: 512x512 minimum (1024x1024 recommended)
- Face clearly visible and centered
- Good lighting and focus
- Historical portraits work well (paintings or photographs)

## Usage

### Quick Test

```bash
cd /services/negotiation

# Test with a single portrait
python test_diplomatic_avatar.py quick

# Test Revolutionary War character
python test_diplomatic_avatar.py washington

# Test full negotiation dialogue
python test_diplomatic_avatar.py negotiation
```

### Python API

```python
from diplomatic_avatar import DiplomaticAvatar

# Initialize avatar with portrait
avatar = DiplomaticAvatar(
    portrait_path="assets/avatars/washington.jpg",
    sadtalker_dir="~/Projects/SadTalker",
    voice_id="YOUR_ELEVENLABS_VOICE_ID"
)

# Generate talking portrait
video = avatar.speak(
    text="We shall secure liberty for these colonies.",
    output_name="washington_speech",
    emotion="diplomatic",
    still_mode=True,  # For formal speeches
    enhance_video=True  # GFPGAN quality enhancement
)

print(f"Video saved to: {video}")
```

### Character Presets

Pre-configured Revolutionary War characters in `diplomatic_avatar.py`:

- **George Washington** - Commander-in-Chief
- **Benjamin Franklin** - Diplomat
- **Lord Cornwallis** - British General
- **Joseph Brant** - Mohawk Chief

```python
from diplomatic_avatar import create_character_avatar

washington = create_character_avatar("george_washington")
video = washington.speak("Your diplomatic text here")
```

## Performance Notes

### Generation Times (M1 Mac)

- **Base generation**: 30-45 seconds
- **With GFPGAN enhancement**: 45-60 seconds
- **Still mode**: 20-30 seconds (less head movement)

### Optimization Tips

1. **Use still_mode=True** for faster generation with minimal head movement
2. **Disable enhancement** (`enhance_video=False`) for speed
3. **Pre-generate videos** for frequently used dialogue
4. **Use lower resolution portraits** (512x512) for faster processing
5. **Batch process** multiple videos if needed

## Troubleshooting

### "SadTalker not found" Error

```bash
# Verify path in .env
echo $SADTALKER_DIR
# Should return: /Users/yourname/Projects/SadTalker

# Check if directory exists
ls ~/Projects/SadTalker
```

### "GFPGAN not found" Warning

GFPGAN enhancement is optional. Disable it if causing issues:

```bash
# In .env
SADTALKER_ENHANCE=false
```

### MPS/Metal Errors

If you get Metal-related errors, fall back to CPU:

```bash
# In .env
SADTALKER_DEVICE=cpu
```

### Low Quality Output

1. Use higher resolution source images (1024x1024+)
2. Enable GFPGAN enhancement
3. Ensure good quality audio from ElevenLabs
4. Check portrait has good lighting and focus

### Out of Memory Errors

```bash
# Reduce batch size (already set to 1 by default)
# Use CPU instead of MPS
SADTALKER_DEVICE=cpu

# Or use still mode for lower memory usage
SADTALKER_STILL_MODE=true
```

## Integration with Web Interface

The SadTalker avatar is integrated into the negotiation service web interface:

1. Start the service: `./scripts/start.sh`
2. Open http://localhost:8000
3. Select "SadTalker Avatar (Portrait Animation)" from model dropdown
4. Create session and test with diplomatic phrases

Videos are automatically generated and displayed in the avatar component.

## Advanced Configuration

### Custom Voice Settings

Configure ElevenLabs voices for different characters:

```python
REVOLUTIONARY_CHARACTERS = {
    "george_washington": {
        "portrait": "portraits/washington.jpg",
        "voice_id": "YOUR_VOICE_ID",  # Deep, authoritative
        "description": "Commander-in-Chief"
    },
    "benjamin_franklin": {
        "portrait": "portraits/franklin.jpg",
        "voice_id": "YOUR_VOICE_ID",  # Wise, diplomatic
        "description": "Diplomat to France"
    }
}
```

### Video Output Settings

Modify in `sadtalker_source.py`:

```python
# Change output directory
self.output_dir = Path("./my_custom_videos")

# Adjust device settings
self.device = "mps"  # or "cpu"

# Enable/disable enhancement
self.enhance = True  # or False
```

## Resources

- **SadTalker GitHub**: https://github.com/OpenTalker/SadTalker
- **ElevenLabs Voices**: https://elevenlabs.io/voice-library
- **Historical Portraits**: Use public domain images from museums/archives
- **MPS Documentation**: https://developer.apple.com/metal/pytorch/

## License

SadTalker is licensed under the Tsinghua University License. See their repository for details.

This integration is part of the Samson diplomatic negotiation system.


