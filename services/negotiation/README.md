# Samson Negotiation Service

A production-ready FastAPI service for diplomatic negotiations with WebRTC A/V streaming and real-time intent detection.

## Features

### Core Capabilities
- **REST API** for session management and negotiation reports
- **YAML-first protocol** with `application/x-yaml` content type support
- **WebSocket** for real-time control messages
- **WebRTC** for audio/video streaming with avatar support
- **Real-time Intent Detection** with structured diplomatic analysis
- **Content Safety** filtering with rule-based and provider support
- **Session Management** with automatic cleanup and timeouts
- **Structured Logging** with correlation IDs and comprehensive monitoring

### Provider System
- **Pluggable Architecture** for negotiation analysis providers
- **MockLocalProvider** - Deterministic state machine for testing and development
- **Veo3Provider** - Complete video+dialogue pipeline with Gemini Veo3 integration
- **Video Avatar Support** - Real-time avatar generation with placeholder and Veo3 modes
- **Structured Events** - Type-safe event system (`NewIntent`, `LiveSubtitle`, `Analysis`, `Safety`)
- **Schema Validation** - Automatic Pydantic model validation with confidence scoring
- **Backpressured Streaming** - Real-time subtitle generation with partial/final events

### Audio/Video Processing
- **STT Interfaces** - Pluggable Speech-to-Text (faster-whisper, cloud providers)
- **TTS Interfaces** - Pluggable Text-to-Speech (Coqui XTTS, cloud providers)
- **Avatar Support** - Video avatar generation with lipsync capabilities

### Development & Deployment
- **Comprehensive Testing** - 38+ tests with 95%+ coverage including edge cases
- **Docker Support** - Complete containerization with docker-compose
- **Type Safety** - Full mypy/pyright compliance with proper type annotations
- **Performance Optimized** - Sub-second response times with efficient pattern matching

## Quick Start

### Using UV (Recommended)

```bash
# Install dependencies (production)
make install

# Install development dependencies
make dev

# Run tests
make test

# Run the test harness server
make run
```

**Note**: This project now uses **uv** exclusively for package management. All dependencies are defined in `pyproject.toml` with automatic virtual environment management.

### Turn-Key Browser Test Harness

**🚀 Quickest way to test your AI avatar today!**

```bash
# Start the test harness server
cd services/negotiation
uv run uvicorn simple_test_harness:app --host 127.0.0.1 --port 8000 --reload

# Alternative: Use the Makefile
make run
```

Open http://localhost:8000 in your browser for an instant test environment featuring:

#### **🎭 Live Negotiation System**
- **Real-time WebSocket** communication for instant feedback
- **YAML-first protocol** with `js-yaml` integration for proper data handling
- **Live subtitles** with final/partial indicators
- **Intent detection** with structured diplomatic analysis
- **Content safety** filtering and validation

#### **🎮 Interactive Testing**
- **Model Selection**: Switch between `mock_local` (deterministic) and `veo3` (advanced)
- **Custom Utterances**: Type or paste diplomatic messages to test AI responses
- **Session Management**: Create, monitor, and end negotiation sessions
- **Real-time Status**: Connection state, WebRTC ICE state, and message flow

#### **💻 Technical Features**
- **WebSocket Control**: Real-time bidirectional communication
- **Proper YAML Parsing**: Uses `js-yaml` library for robust data handling
- **Cross-browser Support**: Works in modern browsers with WebRTC support
- **Mobile Friendly**: Responsive design with touch controls

#### **🔧 Usage**
1. **Create Session**: Select model and click "Create Session"
2. **Grant Microphone**: Allow browser microphone access for audio input
3. **Send Utterance**: Type diplomatic messages or use test phrases
4. **Watch Results**: See avatar video, live subtitles, and detected intents
5. **Monitor Status**: Real-time connection and processing status

#### **📋 Test Scenarios**
- **Counter-Offers**: "We'll grant trade access if you withdraw troops"
- **Ultimatums**: "Ceasefire now or else we'll declare war!"
- **Proposals**: "I propose we establish a fair trade agreement"
- **Custom Messages**: Type any diplomatic text for testing

#### **🔍 Expected Outputs**
- **🟢 Subtitles**: Live speech-to-text with confidence indicators
- **📜 Intents**: Structured diplomatic intents (Proposal, CounterOffer, Ultimatum)
- **⚠️ Safety**: Content safety validation results
- **📊 Analysis**: Detailed diplomatic analysis with scoring
  - **Custom message input** for testing your own scenarios
- **Real-time intent detection** with confidence scores and detailed rationale
- **Keyword extraction** and sentiment analysis
- **Streaming subtitle generation** showing partial and final transcriptions

#### **🔧 Professional Development Tools**
- **Live event logs** with color-coded event types (intents, safety, analysis, subtitles)
- **Real-time statistics** tracking sessions, messages, intents, and connection time
- **Model switching** between Enhanced Mock (deterministic) and Veo3 (advanced)
- **Comprehensive diagnostics** for microphone and WebRTC testing
- **Log export functionality** for debugging and analysis
- **Structured JSON logging** with timestamps and correlation IDs

#### **🛡️ Safety & Validation**
- **Content safety screening** with configurable severity levels
- **Schema validation** against YAML diplomatic protocols
- **Error handling** with graceful degradation
- **Session management** with automatic cleanup

Perfect for:
- **Demonstrating** the complete negotiation pipeline to stakeholders
- **Testing** avatar video generation and diplomatic intent detection
- **Debugging** negotiation flow and provider integration in real-time
- **Validating** safety filters and content screening
- **Developing** new diplomatic scenarios and testing edge cases

### Using Docker

```bash
# Build and run with Docker Compose
docker-compose up -d

# Or build manually
docker build -t samson-negotiation-service .
docker run -p 8000:8000 samson-negotiation-service
```

**Note**: The Docker setup uses uv for dependency management as defined in the Dockerfile. The `pyproject.toml` file is used for all dependency resolution.

## API Endpoints

### Session Management
- `POST /v1/session` - Create a new negotiation session
- `POST /v1/session/{id}/end` - End a session and get report
- `GET /v1/session/{id}/report` - Get session report

### WebRTC
- `POST /v1/session/{id}/webrtc/offer` - Handle WebRTC SDP offer

### Control
- `WS /v1/session/{id}/control` - WebSocket for control messages

### Development
- `POST /v1/session/{id}/proposed-intents` - Inject intents (dev only)
- `GET /health` - Health check

## Configuration

Copy `.env.example` to `.env` and configure:

```bash
cp .env.example .env
```

### Package Management with UV

This project uses **uv** for fast, reliable Python package management:

- **Dependencies**: All defined in `pyproject.toml`
- **Virtual Environment**: Automatically managed by uv (`.venv/`)
- **Lock File**: `uv.lock` ensures reproducible builds
- **Development Tools**: Integrated linting, formatting, and type checking

#### Key UV Commands

```bash
# Install all dependencies
uv sync

# Install with development dependencies
uv sync --dev

# Run commands in the uv environment
uv run python script.py
uv run pytest
uv run uvicorn app:main

# Add new dependencies
uv add package-name
uv add --dev dev-package-name
```

## Project Structure

```
services/negotiation/
├── app/                    # FastAPI application
│   └── main.py            # Main FastAPI app with all endpoints
├── core/                   # Core business logic
│   ├── session_manager.py # Session lifecycle management
│   ├── webrtc_manager.py  # WebRTC connection handling
│   ├── content_safety.py  # Content filtering and safety
│   ├── yaml_middleware.py # YAML protocol support
│   └── logging_config.py  # Structured logging setup
├── providers/             # Negotiation analysis providers
│   ├── base.py           # Abstract provider interface
│   ├── mock_local.py     # Deterministic local provider
│   ├── gemini_veo3.py    # Complete Veo3 video+dialogue pipeline
│   ├── video_sources/    # Video avatar generation
│   │   ├── base.py       # Video source interface
│   │   ├── placeholder_loop.py # Placeholder video source
│   │   └── veo3_stream.py      # Veo3 API video source
│   └── README.md         # Provider documentation
├── stt/                   # Speech-to-Text interfaces
│   ├── base.py           # STT provider interface
│   └── faster_whisper.py # faster-whisper implementation
├── tts/                   # Text-to-Speech interfaces
│   ├── base.py           # TTS provider interface
│   └── xtts.py           # Coqui XTTS implementation
├── schemas/               # Pydantic models and validation
│   ├── models.py         # Generated from YAML schemas
│   └── generate_models.py # Schema generation script
├── tests/                 # Comprehensive test suite (38+ tests)
│   ├── test_providers.py      # Provider functionality tests
│   ├── test_provider_edge_cases.py # Edge case and performance tests
│   ├── test_integration.py    # End-to-end integration tests
│   ├── test_content_safety.py # Content filtering tests
│   └── test_session_manager.py # Session management tests
├── Dockerfile            # Docker configuration
├── docker-compose.yml    # Docker Compose setup
├── Makefile             # Development commands (uv-based)
├── pyproject.toml       # Python project configuration and dependencies
├── uv.lock             # Dependency lock file for reproducible builds
└── .env.example        # Environment configuration template
```

## Development

### Provider System

The negotiation service uses a pluggable provider architecture for diplomatic intent detection:

#### Adding New Providers

1. **Implement the Provider Interface**:
   ```python
   from providers.base import Provider, ProviderEvent
   
   class MyProvider(Provider):
       async def stream_dialogue(self, turns, world_context, system_guidelines):
           # Implement your logic here
           yield ProviderEvent(
               type="intent",
               payload={
                   "intent": my_intent,
                   "confidence": 0.9,
                   "justification": "Analysis reasoning..."
               }
           )
   ```

2. **Add to Module**: Update `providers/__init__.py` to export your provider
3. **Add Tests**: Create comprehensive tests following the existing pattern
4. **Update Configuration**: Add any required configuration parameters

#### Veo3Provider Features

The **Veo3Provider** offers a complete video+dialogue pipeline:

- **🎬 Video Avatar Generation**: Supports both Veo3 API and placeholder modes
- **📺 Live Subtitle Streaming**: Progressive clause-by-clause subtitle delivery
- **🎯 Intent Detection**: YAML-based function calling with schema validation
- **🛡️ Content Safety**: Integrated safety screening and filtering
- **📊 Scoring & Analytics**: Context-aware confidence scoring
- **⚡ Backpressure Control**: Real-time performance with bounded queues
- **🔄 Dependency Injection**: Pluggable STT/TTS provider support

```python
# Example usage
provider = Veo3Provider(
    avatar_style="colonial_diplomat",
    voice_id="en_male_01",
    latency_target_ms=800,
    use_veo3=False  # True for Veo3 API, False for placeholder
)

async for event in provider.stream_dialogue(turns, world_context, guidelines):
    if event.type == "subtitle":
        print(f"Subtitle: {event.payload['text']}")
    elif event.type == "intent":
        print(f"Intent: {event.payload['intent']['type']}")
```

#### Provider Event Types

- **`ProviderEvent`**: Base event with type, payload, and timestamp
  - **`"intent"`**: Diplomatic intent with confidence and justification
  - **`"subtitle"`**: Real-time subtitle text with finality flag  
  - **`"analysis"`**: Structured analysis data with tag and payload
  - **`"safety"`**: Safety validation with flag, detail, and severity

#### Key Phrase Detection (MockLocalProvider)

The mock provider uses deterministic pattern matching:
- `"We'll grant trade access if you withdraw troops"` → `CounterOffer`
- `"Ceasefire now or else"` → `Ultimatum`  
- Trade keywords → `Proposal`
- Aggressive language → `Ultimatum`
- Cooperative language → `Concession`
- Default → `SmallTalk`

### Adding STT/TTS Providers

1. **Implement Interface**: 
   - `STTProvider` for Speech-to-Text
   - `TTSProvider` for Text-to-Speech
2. **Add to Module**: Update respective `__init__.py`
3. **Configuration**: Add provider-specific settings
4. **Testing**: Add comprehensive test coverage

## Testing

The service includes a comprehensive test suite with 38+ tests covering functionality, edge cases, and performance:

```bash
# Run all tests
make test

# Run with coverage report
uv run pytest --cov=. --cov-report=html

# Run specific test categories
uv run pytest tests/test_providers.py              # Provider functionality (26 tests)
uv run pytest tests/test_provider_edge_cases.py    # Edge cases & performance (12 tests)
uv run pytest tests/test_integration.py            # End-to-end integration
uv run pytest tests/test_content_safety.py         # Content filtering

# Run with verbose output
uv run pytest -v

# Run performance tests only
uv run pytest -k "performance" -v
```

### Test Coverage

- **Provider System**: 26 core functionality tests + 12 edge case tests + Veo3Provider tests
- **Integration**: End-to-end API and WebSocket testing
- **Content Safety**: Comprehensive safety filtering validation
- **Session Management**: Session lifecycle and cleanup testing
- **Video Sources**: Avatar generation and streaming tests
- **Edge Cases**: Empty inputs, malformed data, concurrent usage, memory efficiency
- **Performance**: Response time consistency and pattern matching efficiency

### Quality Gates

✅ **95%+ Test Coverage** (pytest with coverage reporting)  
✅ **Type Safety** (mypy with strict configuration)  
✅ **Code Quality** (ruff linting + black formatting)  
✅ **Performance** (sub-second response times)  
✅ **Error Handling** (graceful degradation)  
✅ **Schema Compliance** (Pydantic validation)  
✅ **Dependency Management** (uv with lock file for reproducibility)

## Deployment

### Docker

```bash
# Build image
docker build -t samson-negotiation-service .

# Run with compose
docker-compose up -d
```

The Dockerfile uses uv for fast, reproducible builds:
- Dependencies installed from `pyproject.toml`
- Multi-stage build for optimized image size
- Automatic virtual environment management

### Production

- Set `DEBUG=false` in environment
- Use proper STUN/TURN servers
- Configure logging appropriately
- Set up monitoring and health checks
- Ensure `uv.lock` is committed for reproducible deployments

## Architecture

The service follows a clean, layered architecture designed for deterministic grand-strategy gameplay:

### System Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Godot Game    │    │  Negotiation     │    │   Simulation    │
│   Engine        │◄──►│   Service        │◄──►│   Manager       │
│ (UI/A-V Only)   │    │ (FastAPI+WebRTC) │    │ (Authoritative) │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### Layer Separation

- **🎮 Simulation Layer**: Authoritative, deterministic game state (external Godot process)
- **🎙️ Service Layer**: This FastAPI service handling A/V streaming and negotiation analysis
- **🔌 Provider Layer**: Pluggable negotiation analysis (Mock, Gemini Veo3, etc.)
- **📋 Protocol Layer**: Versioned YAML schemas for all cross-process communication

### Key Principles

1. **Deterministic Contract**: Service only proposes intents; Simulation validates and applies
2. **Schema-First**: All communication uses versioned YAML schemas (`application/x-yaml`)
3. **Pluggable Design**: Providers, STT, TTS can be swapped without code changes
4. **Real-time Streaming**: WebRTC for A/V, WebSocket for control messages
5. **Safety First**: Content filtering and validation at multiple layers

### Data Flow

```
Audio/Video Input → STT → Provider Analysis → Intent Detection → 
Schema Validation → Confidence Scoring → Safety Filtering → 
Negotiation Report → Simulation Validation → Game State Update
```

### Provider Architecture

```python
# Structured event system
ProviderEvent(type="intent", payload={"intent": {...}, "confidence": 0.9})
ProviderEvent(type="subtitle", payload={"text": "...", "is_final": True})
ProviderEvent(type="analysis", payload={"tag": "summary", "result": {...}})
ProviderEvent(type="safety", payload={"flag": "content_check", "is_safe": True})
```

The provider system enables:
- **Real-time Processing**: Streaming analysis with backpressure support
- **Video Avatar Integration**: Seamless avatar generation and streaming
- **Confidence Scoring**: Context-aware scoring based on relevance and quality  
- **Safety Validation**: Multi-layer content filtering and validation
- **Extensibility**: Easy addition of new analysis providers
- **YAML Protocol**: Schema-compliant diplomatic intent generation
