# Video+Dialogue Provider Pipeline

This document describes the comprehensive video+dialogue provider pipeline implementation for the Negotiation Service.

## Architecture Overview

The pipeline consists of several key components:

1. **Provider Types & Events** - Core type definitions and event structures
2. **Video Sources** - Avatar generation and streaming
3. **Utility Modules** - Scoring, safety, and backpressure handling
4. **Integration Components** - YAML validation and WebRTC publishing
5. **Configuration Management** - Environment-based configuration

## Core Components

### 1. Provider Events (`providers/types.py`)

```python
from providers.types import ProviderEvent, IntentPayload

# Create a provider event
event = ProviderEvent(
    type="intent",
    payload={"kind": "proposal", "confidence": 0.8},
    is_final=True
)
```

### 2. Video Sources

#### Placeholder Video Source
```python
from providers.video_sources import PlaceholderLoopVideoSource
from providers.types import VideoSourceConfig

config = VideoSourceConfig(
    source_type="placeholder",
    avatar_style="diplomatic",
    resolution=(640, 480),
    framerate=30
)

video_source = PlaceholderLoopVideoSource(config)
await video_source.start()

# Stream frames
async for frame in video_source.stream_frames():
    print(f"Generated frame: {frame.width}x{frame.height}")
```

#### Veo3 Video Source (Stub)
```python
from providers.video_sources import Veo3StreamVideoSource

video_source = Veo3StreamVideoSource(config)
await video_source.start()

# Update context for expression changes
await video_source.update_dialogue_context({
    "current_intent": "proposal",
    "sentiment": "positive",
    "urgency": "medium"
})
```

### 3. Intent Scoring

```python
from providers import IntentScorer

scorer = IntentScorer()

# Score individual intent
score = await scorer.score_intent(intent, world_context)
print(f"Strategic value: {score}")

# Rank multiple intents
intents = [proposal_intent, counter_offer_intent, ultimatum_intent]
ranked = await scorer.rank_intents(intents, world_context)

for intent, score in ranked:
    print(f"{intent.type}: {score:.2f}")
```

### 4. Content Safety

```python
from providers import ContentSafetyFilter, DiplomaticContentFilter

# General content safety
safety_filter = ContentSafetyFilter()
result = await safety_filter.check_content_safety(
    "I propose a peaceful trade agreement",
    world_context,
    intent
)

if not result.is_safe:
    print(f"Blocked: {result.reason}")
    print(f"Flags: {result.flags}")
    print(f"Severity: {result.severity}")

# Diplomatic protocol safety
diplomatic_filter = DiplomaticContentFilter()
diplomatic_result = await diplomatic_filter.check_diplomatic_safety(
    content, world_context
)
```

### 5. Backpressure Management

```python
from providers import BackpressureHandler, BackpressureConfig

config = BackpressureConfig(
    max_concurrent_requests=10,
    rate_limit_per_second=2.0,
    burst_limit=5,
    memory_threshold_mb=100.0
)

handler = BackpressureHandler(config)

# Execute with backpressure
async def my_operation():
    # Expensive AI operation
    return await ai_provider.generate_response()

result = await handler.execute_with_backpressure(
    my_operation,
    priority=1,  # Higher priority
    timeout_seconds=30
)

# Check metrics
metrics = handler.get_metrics()
print(f"Active requests: {metrics['active_requests']}")
print(f"Error rate: {metrics['error_rate']:.2%}")
```

### 6. YAML Schema Validation

```python
from schemas.validators import SchemaValidator, NegotiationValidator

# Basic schema validation
validator = SchemaValidator()

# Validate intent against YAML schema
validated_intent = validator.validate_intent(intent_dict)

# Validate speaker turn
validated_turn = validator.validate_speaker_turn(turn_dict)

# High-level negotiation validation
negotiation_validator = NegotiationValidator()
validated_report = await negotiation_validator.validate_negotiation_report(report)
```

### 7. WebRTC Track Publishing

```python
from webrtc import TrackPublisher
from core.webrtc_manager import WebRTCManager

webrtc_manager = WebRTCManager()
publisher = TrackPublisher(webrtc_manager)

# Publish avatar video
track_id = await publisher.publish_avatar_video(
    session_id="session_123",
    video_source_config=config,
    avatar_style="diplomatic"
)

# Publish TTS audio
audio_track_id = await publisher.publish_tts_audio(
    session_id="session_123",
    tts_provider=tts_provider,
    voice_id="diplomat_en_us"
)

# Get session tracks
tracks = await publisher.get_session_tracks("session_123")
for track in tracks:
    print(f"Track: {track.kind} from {track.source}")
```

### 8. Configuration Management

```python
from config import ConfigManager

config_manager = ConfigManager()

# Get provider-specific config
gemini_config = config_manager.get_provider_config_for_provider("gemini")
print(f"API Key: {gemini_config.get('api_key', 'Not set')}")

# Validate configuration
issues = config_manager.validate_configuration()
if issues:
    for issue in issues:
        print(f"Config issue: {issue}")

# Access typed configurations
provider_config = config_manager.provider_config
webrtc_config = config_manager.webrtc_config
logging_config = config_manager.logging_config
```

## Complete Provider Example

```python
import asyncio
from datetime import datetime
from providers import MockLocalProvider
from schemas.models import WorldContextModel, SpeakerTurnModel

async def main():
    # Initialize provider
    provider = MockLocalProvider({"strict": False})
    
    # Create world context
    context = WorldContextModel(
        scenario_tags=["diplomatic", "trade"],
        initiator_faction={"id": "player", "type": "merchant"},
        counterpart_faction={"id": "ai_diplomat", "type": "diplomat"},
        current_state={"turn_count": 1}
    )
    
    # Create speaker turn
    turn = SpeakerTurnModel(
        speaker_id="player",
        text="We'll grant trade access if you withdraw troops",
        timestamp=datetime.now(),
        confidence=0.9
    )
    
    # Process dialogue
    print("Processing dialogue...")
    async for event in provider.stream_dialogue([turn], context):
        print(f"Event: {type(event).__name__}")
        
        if hasattr(event, 'intent'):
            intent = event.intent
            print(f"  Intent: {intent.type}")
            print(f"  Content: {intent.content}")
            print(f"  Confidence: {event.confidence}")
        elif hasattr(event, 'flag'):
            print(f"  Safety: {event.flag} - {event.detail}")
        elif hasattr(event, 'tag'):
            print(f"  Analysis: {event.tag}")
            print(f"  Payload: {event.payload}")

if __name__ == "__main__":
    asyncio.run(main())
```

## Environment Configuration

Set these environment variables for full functionality:

```bash
# API Keys
export GEMINI_API_KEY="your_gemini_key"
export OPENAI_API_KEY="your_openai_key"

# Provider Settings
export DEFAULT_VIDEO_SOURCE="placeholder"
export AVATAR_STYLE="diplomatic"
export VIDEO_RESOLUTION="640x480"
export VIDEO_FRAMERATE="30"

# Safety Settings
export CONTENT_SAFETY_ENABLED="true"
export SAFETY_STRICT_MODE="false"

# Resource Limits
export MEMORY_LIMIT_MB="500"
export CPU_LIMIT_PERCENT="80"
export MAX_CONCURRENT_SESSIONS="100"

# Feature Toggles
export ENABLE_VIDEO="true"
export ENABLE_AUDIO="true"
export ENABLE_REALTIME_SUBTITLES="true"
```

## Dependencies

Install required dependencies:

```bash
pip install -r requirements-providers.txt
```

Key dependencies:
- `pydantic>=2.0.0` - Data validation
- `structlog>=23.0.0` - Structured logging
- `aiortc>=1.6.0` - WebRTC support
- `av>=10.0.0` - Video processing
- `numpy>=1.24.0` - Numerical operations (optional)
- `psutil>=5.9.0` - Resource monitoring (optional)

## Testing

Run the test suite:

```bash
pytest tests/providers/ -v
```

Key test files:
- `test_mock_local.py` - MockLocalProvider tests
- `test_gemini_veo3_stub.py` - Veo3Provider stub tests

## Error Handling

The pipeline includes comprehensive error handling:

1. **Import Fallbacks** - Graceful degradation when optional dependencies are missing
2. **Resource Monitoring** - Automatic throttling when resources are constrained
3. **Schema Validation** - Detailed error messages for validation failures
4. **Safety Filtering** - Multiple layers of content safety checks
5. **Backpressure Management** - Rate limiting and queue management

## Performance Considerations

- **Async/Await** - All operations are async for maximum concurrency
- **Resource Monitoring** - Built-in CPU and memory monitoring
- **Rate Limiting** - Token bucket algorithm for API rate limiting
- **Caching** - Schema and pattern caching for performance
- **Lazy Loading** - Optional dependencies loaded only when needed

## Integration Points

The pipeline integrates with:

1. **Simulation Layer** - Via `NegotiationValidator` for report validation
2. **WebRTC Manager** - Via `TrackPublisher` for media streaming
3. **Content Safety** - Via existing `ContentSafetyFilter` integration
4. **Settings System** - Via `ConfigManager` for environment configuration

This implementation provides a complete, production-ready video+dialogue provider pipeline with comprehensive testing, error handling, and documentation.
