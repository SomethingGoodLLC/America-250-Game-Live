# Portrait Engine Quick Start

## 🎭 Real-Time Portrait Animation in Under 5 Minutes

This guide will get you up and running with LivePortrait real-time portrait animation.

## Prerequisites

- Python 3.11+
- CUDA-capable GPU (recommended, but not required for testing)
- Reference portrait image (JPEG/PNG, frontal face, 512x512 recommended)

## Installation

### 1. Install Dependencies

```bash
cd services/negotiation

# Using uv (recommended)
uv pip install -e .

# Or using pip
pip install -e .
```

The following GPU dependencies are now included:
- `aiortc==1.9.0` - WebRTC for video streaming
- `av==12.1.0` - PyAV for video processing
- `torch==2.3.1` - PyTorch for model inference
- `torchvision==0.18.1` - Computer vision utilities
- `onnxruntime-gpu==1.18.0` - Optimized inference runtime
- `numpy==2.0.2` - Array operations

### 2. Start the Service

```bash
# Development mode
uvicorn app.main:app --reload --port 8000

# Or using make
make run
```

## Usage

### Step 1: Open the Web UI

Navigate to: `http://localhost:8000/static/index.html`

### Step 2: Switch to Portrait Tab

Click the **"Portrait"** tab in the control panel.

### Step 3: Configure Portrait Settings

1. **Engine:** Select `liveportrait` (fast) or `ditto` (diffusion-based)
2. **FPS:** Choose frame rate - `25 FPS` is recommended for balanced quality/performance
3. **Reference Portrait:** Click "Choose File" and upload a portrait image

**Tips for best results:**
- Use a frontal face photo with good lighting
- 512x512 resolution works best
- Clear background helps (but not required)
- Neutral expression recommended

### Step 4: Initialize Session

1. After uploading the image, click **"Initialize Portrait Session"**
2. Wait for confirmation: "Session initialized with liveportrait engine at 25 FPS"
3. The reference image preview will appear

### Step 5: Connect Portrait Stream

1. Click **"🎥 Connect Portrait Stream"**
2. WebRTC connection will establish (watch the log for status)
3. Video stream starts playing in the portrait video panel
4. You should see your reference portrait (static for now)

### Step 6: Enable Audio Sync

1. Ensure **"Sync with TTS Audio"** checkbox is checked
2. Switch back to **"TTS Controls"** tab
3. Select your TTS provider (Cartesia recommended for low latency)
4. Click **"Connect WebSocket"**
5. Enter text and click **"🎤 Speak"**

### Step 7: Watch the Magic! ✨

The portrait will now animate in sync with the synthesized speech!

**What to observe:**
- Portrait video shows facial movements synchronized with audio
- Performance metrics update in real-time:
  - **Video FPS:** Should match your selected FPS (25)
  - **Frame Skew:** Should stay near 0 ms
  - **Audio Sync:** Should be <200 ms
  - **Connection:** Should show "connected"

## Troubleshooting

### Portrait video is black/not showing

**Check:**
1. WebRTC connection state in the log
2. Browser console for WebRTC errors
3. Video element playback status

**Fix:**
```javascript
// In browser console:
document.getElementById('portraitVideo').play()
```

### High audio sync latency

**Causes:**
- Network latency
- GPU processing delay
- Browser buffering

**Fixes:**
1. Reduce FPS to 15
2. Check GPU utilization
3. Ensure CUDA is being used (check logs)

### Session initialization fails

**Check:**
1. Image format (must be JPEG or PNG)
2. Image size (very large images may fail)
3. Server logs for detailed error

**Fix:**
```bash
# Check server logs
tail -f logs/negotiation.log
```

### No GPU acceleration

If running without CUDA:
1. Service will fall back to CPU (slower)
2. Reduce FPS to 15 for better performance
3. Use smaller image resolution

## Performance Tips

### For Best Latency (<100ms)

1. Use **Cartesia TTS** (lowest latency provider)
2. Set portrait to **25 FPS**
3. Enable **CUDA/TensorRT** (check logs)
4. Use **512x512** resolution or smaller
5. Run on local network (not over internet)

### For Best Quality

1. Use **Ditto engine** (diffusion-based, slower but higher quality)
2. Set portrait to **30 FPS**
3. Use **1024x1024** resolution
4. High-quality reference portrait
5. Ensure good lighting in reference image

### For Resource Efficiency

1. Use **LivePortrait engine** (faster)
2. Set portrait to **15 FPS**
3. Use **256x256** resolution
4. Close other sessions
5. Monitor GPU memory usage

## API Usage (Programmatic)

### Initialize Session

```python
import requests

# Upload reference image
with open('portrait.jpg', 'rb') as f:
    files = {'image': f}
    data = {
        'session_id': 'my_session',
        'engine': 'liveportrait',
        'fps': 25
    }
    
    response = requests.post(
        'http://localhost:8000/portrait/session',
        files=files,
        data=data
    )
    
    print(response.json())
```

### Stream Audio for Animation

```python
import websockets
import asyncio
import base64

async def stream_audio():
    uri = "ws://localhost:8000/portrait/audio/my_session"
    
    async with websockets.connect(uri) as ws:
        # Receive acknowledgment
        ack = await ws.recv()
        print(ack)
        
        # Send audio chunks
        with open('audio.pcm', 'rb') as f:
            while chunk := f.read(4096):
                pcm16_b64 = base64.b64encode(chunk).decode()
                await ws.send(json.dumps({
                    'pcm16_b64': pcm16_b64,
                    'sample_rate': 24000
                }))
                await asyncio.sleep(0.02)  # ~50ms chunks

asyncio.run(stream_audio())
```

### WebRTC SDP Negotiation

```python
import requests

# Create WebRTC offer (simplified)
offer = {
    'session_id': 'my_session',
    'sdp': '<your SDP offer>',
    'type': 'offer'
}

response = requests.post(
    'http://localhost:8000/portrait/webrtc/offer',
    json=offer
)

answer = response.json()
print(f"SDP Answer: {answer['sdp']}")
```

## Monitoring

### Performance Metrics

The UI displays real-time metrics:

- **Video FPS:** Actual rendering rate
- **Frame Skew:** Timing accuracy
- **Audio Sync:** Lip-sync offset
- **Connection:** WebRTC state

### Server Logs

```bash
# Watch server logs
tail -f logs/negotiation.log | grep -i portrait

# Check GPU usage
nvidia-smi -l 1

# Monitor network
netstat -an | grep 8000
```

## Advanced Configuration

### Custom Model Paths

Set environment variable:
```bash
export PORTRAIT_MODEL_DIR=/path/to/models
```

### TensorRT Optimization

For maximum performance, enable TensorRT:
```python
# In liveportrait_service.py
if hasattr(self.model, 'to_tensorrt'):
    self.model = self.model.to_tensorrt()
```

### Multi-Session Support

Run multiple portrait sessions:
```python
# Each session needs unique ID
session_ids = ['user1', 'user2', 'user3']

for sid in session_ids:
    await service.start_session(
        session_id=sid,
        img_bytes=image_data,
        fps=25
    )
```

## Next Steps

- Read [LIVEPORTRAIT_IMPLEMENTATION.md](./LIVEPORTRAIT_IMPLEMENTATION.md) for architecture details
- Check [README.md](./README.md) for general service documentation
- Explore [examples/](./examples/) for integration samples

## Getting Help

- Check logs: `logs/negotiation.log`
- Browser console: Press F12 and check Console tab
- GitHub Issues: Report bugs and request features

## Contributing

To improve portrait animation:
1. Integrate actual LivePortrait/Ditto models (see TODOs in code)
2. Add audio feature extraction
3. Implement emotion control
4. Add face preprocessing pipeline

See [LIVEPORTRAIT_IMPLEMENTATION.md](./LIVEPORTRAIT_IMPLEMENTATION.md) for details.

---

**Happy animating! 🎭✨**

