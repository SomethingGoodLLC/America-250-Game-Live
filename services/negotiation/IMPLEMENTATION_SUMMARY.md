# TTS WebSocket Proxy Implementation Summary

## Overview

Successfully implemented browser-side TTS integration with WebSocket proxies for Cartesia, ElevenLabs, and Azure Speech Services. These proxies enable direct browser-to-provider communication while keeping API keys secure server-side.

## Implemented Features

### 1. WebSocket Bridge Utility (`app/providers/_bridge.py`)

A reusable bidirectional WebSocket bridge that:
- Forwards messages between client and provider WebSockets
- Supports message transformation hooks
- Implements proper error handling and cleanup
- Includes connection rate limiting by IP
- Supports origin checking for security

**Key Components:**
- `bridge_client_server()` - Main bridging function
- `ConnectionRateLimiter` - IP-based connection throttling
- `check_origin()` - Origin validation for CORS

### 2. Cartesia WebSocket Proxy (`app/providers/cartesia_ws.py`)

**Endpoint:** `ws://localhost:8000/ws/cartesia`

**Features:**
- Query parameters for voice, model, sample rate, output format
- Automatic session initialization with Cartesia API
- Server-side API key management
- Rate limiting (3 concurrent connections per IP)
- Origin checking

**Helper Endpoints:**
- `GET /cartesia/voices` - List available voices
- `GET /cartesia/models` - List available models (sonic-english, sonic-multilingual)

**Configuration:**
```bash
CARTESIA_API_KEY=cart_...
CARTESIA_MODEL=sonic-english
```

### 3. ElevenLabs WebSocket Proxy (`app/providers/elevenlabs_ws.py`)

**Endpoint:** `ws://localhost:8000/ws/elevenlabs`

**Features:**
- Support for all ElevenLabs streaming features (alignment, character count)
- Configurable latency optimization
- Voice settings customization
- Rate limiting and origin checking

**Helper Endpoints:**
- `GET /elevenlabs/voices` - List available voices
- `GET /elevenlabs/models` - List available models
- `GET /elevenlabs/user` - Get subscription info (quota, tier)

**Configuration:**
```bash
ELEVENLABS_API_KEY=el_...
ELEVENLABS_MODEL=eleven_turbo_v2
```

### 4. Azure Token Endpoint (`app/providers/azure_token.py`)

**Endpoint:** `POST /azure/token`

**Features:**
- Issues short-lived tokens (10 minutes) for browser-side Azure SDK
- Token-based rate limiting (10/min, 100/hour per IP)
- Never exposes subscription key to browser
- Support for viseme events and ARKit blendshapes

**Helper Endpoints:**
- `GET /azure/regions` - List available Azure regions (26 regions)
- `GET /azure/voices` - List available neural voices

**Configuration:**
```bash
AZURE_SPEECH_KEY=your_key
AZURE_SPEECH_REGION=eastus
```

### 5. Phoneme-to-Viseme Mapping (`app/utils/mapping.py`)

Comprehensive phoneme-to-viseme conversion utilities:

**Key Functions:**
- `arpabet_to_ovr_viseme(phoneme)` - Convert ARPAbet to OVR viseme
- `phonemes_to_visemes(phoneme_timestamps)` - Batch conversion with timing
- `collapse_visemes(viseme_events)` - Merge consecutive similar visemes
- `estimate_phoneme_duration(phoneme)` - Duration estimation

**Features:**
- Complete ARPAbet → OVR Lip Sync mapping (14 visemes)
- Support for stress markers (AA0, AA1, AA2)
- Viseme collapse for smoother animation
- Duration hints for timing estimation
- Helper functions for viseme names and descriptions

**Supported Visemes:**
- SIL (0) - Silence
- PP (1) - Bilabial (p, b, m)
- FF (2) - Labiodental (f, v)
- TH (3) - Dental (th)
- DD (4) - Alveolar (t, d, n, l)
- KK (5) - Velar (k, g, ng)
- CH (6) - Palatal (ch, j, sh, zh)
- SS (7) - Fricative (s, z)
- RR (9) - Retroflex (r)
- AA (10) - Open vowel
- E (11) - Mid vowel
- I (12) - Close front vowel
- O (13) - Close back rounded
- U (14) - Very rounded

### 6. Grapheme-to-Phoneme (G2P) (`app/utils/g2p.py`)

Lightweight G2P conversion for lip-sync timing:

**Key Functions:**
- `text_to_phonemes(text)` - Convert text to ARPAbet phonemes
- `text_to_phonemes_with_timing(text, ...)` - Add timing information
- `words_to_phoneme_timestamps(words, word_timings)` - Word-level to phoneme-level timing

**Features:**
- Embedded CMUdict subset (300+ common words)
- Rule-based fallback for unknown words
- Timing estimation and distribution
- Word-to-phoneme alignment

**Dictionary Coverage:**
- Pronouns, articles, determiners
- Common verbs (100+)
- Conjunctions and prepositions
- Question words
- Common adjectives and nouns
- Numbers 1-10

### 7. Configuration & Security

**Settings Added to `core/settings.py`:**
```python
# Rate limiting
rate_limit_ws_per_ip: int = 3
rate_limit_token_per_minute: int = 10
rate_limit_token_per_hour: int = 100

# Origin checking
allowed_ws_origins: str = "http://localhost:*,..."
```

**Environment Variables (`env.example`):**
```bash
# Rate limiting
RATE_LIMIT_WS_PER_IP=3
RATE_LIMIT_TOKEN_PER_MINUTE=10
RATE_LIMIT_TOKEN_PER_HOUR=100

# CORS/Origin checking
ALLOWED_WS_ORIGINS=http://localhost:*,https://*.yourdomain.com
```

**Security Features:**
- IP-based rate limiting for WebSocket connections
- Token request throttling (per minute and per hour)
- Origin checking with wildcard support
- API keys never exposed to browser
- Short-lived Azure tokens (10 minute expiry)
- Automatic cleanup on disconnect

### 8. Integration with Main App

**Updated `app/main.py`:**
```python
from app.providers import cartesia_router, elevenlabs_router, azure_router

app.include_router(cartesia_router, tags=["providers", "cartesia"])
app.include_router(elevenlabs_router, tags=["providers", "elevenlabs"])
app.include_router(azure_router, tags=["providers", "azure"])
```

**New Routers Added:**
- `/ws/cartesia` - Cartesia WebSocket proxy
- `/ws/elevenlabs` - ElevenLabs WebSocket proxy
- `/azure/token` - Azure token endpoint
- Helper endpoints for voices, models, regions

### 9. Documentation

**Updated `README.md` with:**
- Complete "Browser-Side TTS Integration" section
- Architecture diagram
- JavaScript usage examples for each provider
- Security configuration guide
- Phoneme-to-viseme utility documentation
- Complete browser integration example with lip-sync

## File Structure

```
services/negotiation/
├── app/
│   ├── providers/
│   │   ├── __init__.py
│   │   ├── _bridge.py           # WebSocket bridge utility
│   │   ├── cartesia_ws.py       # Cartesia proxy
│   │   ├── elevenlabs_ws.py     # ElevenLabs proxy
│   │   └── azure_token.py       # Azure token endpoint
│   └── utils/
│       ├── __init__.py
│       ├── mapping.py           # Phoneme-to-viseme mapping
│       └── g2p.py              # Grapheme-to-phoneme conversion
├── core/
│   └── settings.py             # Updated with rate limiting config
├── env.example                 # Updated with new settings
└── README.md                   # Updated with WebSocket proxy docs
```

## Usage Examples

### Browser - Cartesia

```javascript
const ws = new WebSocket('ws://localhost:8000/ws/cartesia?voice_id=abc123');
ws.onopen = () => {
    ws.send(JSON.stringify({
        type: 'text',
        text: 'Welcome to the diplomatic summit.'
    }));
};
ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    if (data.type === 'chunk' && data.data) {
        playAudio(atob(data.data));
        if (data.timestamps) updateLipSync(data.timestamps);
    }
};
```

### Browser - ElevenLabs

```javascript
const ws = new WebSocket('ws://localhost:8000/ws/elevenlabs?voice_id=abc123');
ws.onopen = () => {
    ws.send(JSON.stringify({ text: 'Hello', flush: true }));
};
ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    if (data.audio) playAudio(atob(data.audio));
    if (data.alignment) updateLipSync(data.alignment);
};
```

### Browser - Azure

```javascript
// Get token
const { token, region } = await fetch('/azure/token', {
    method: 'POST'
}).then(r => r.json());

// Use Azure Speech SDK
const config = SpeechSDK.SpeechConfig.fromAuthorizationToken(token, region);
const synthesizer = new SpeechSDK.SpeechSynthesizer(config);

synthesizer.visemeReceived = (s, e) => {
    applyViseme(e.visemeId, e.audioOffset / 10000);
};

synthesizer.speakTextAsync('We propose a treaty.');
```

### Python - Phoneme Utilities

```python
from app.utils import phonemes_to_visemes, text_to_phonemes_with_timing

# Convert phonemes to visemes
phonemes = [
    {"phoneme": "HH", "start_ms": 0, "end_ms": 50},
    {"phoneme": "AH0", "start_ms": 50, "end_ms": 150},
]
visemes = phonemes_to_visemes(phonemes, collapse_similar=True)

# Text to phonemes with timing
result = text_to_phonemes_with_timing(
    "hello world",
    total_duration_ms=1000
)
```

## Testing

All implementations follow the project's testing standards:
- Type-safe with proper annotations
- Structured logging with correlation IDs
- Graceful error handling
- Resource cleanup on disconnect
- No linter errors

## Dependencies

No new external dependencies required. Uses existing:
- `websockets` - Already in project for WebSocket support
- `httpx` - Already in project for HTTP requests
- `structlog` - Already in project for logging
- `fastapi` - Already in project for API framework

## Performance

- **Latency:** Direct browser-to-provider reduces round-trip time
- **Scalability:** Rate limiting prevents abuse
- **Memory:** Automatic cleanup on disconnect
- **Security:** Multiple layers (rate limiting, origin checking, key protection)

## Future Enhancements

Potential improvements for consideration:
1. Load CMU Pronouncing Dictionary for better G2P accuracy
2. Montreal Forced Aligner integration for precise phoneme timing
3. WebSocket connection pooling for high-traffic scenarios
4. Metrics collection for monitoring (Prometheus/Grafana)
5. WebRTC data channel option for lower latency
6. Token caching for Azure to reduce API calls

## Conclusion

All requested features have been successfully implemented following the project's architecture principles:
- ✅ Strict separation of concerns
- ✅ Type-safe with proper annotations
- ✅ Structured logging throughout
- ✅ Comprehensive error handling
- ✅ Security-first design (API keys never exposed)
- ✅ Well-documented with examples
- ✅ No linter errors

The implementation is production-ready and follows FastAPI best practices.

