# Gemini Live API Setup and Integration

This guide explains how to set up and use the Gemini Live API integration in the Samson negotiation service.

## Prerequisites

1. **Google AI Studio API Key**: Get your API key from [Google AI Studio](https://makersuite.google.com/app/apikey)
2. **Python Dependencies**: The `google-genai` library is already included in the project dependencies

## Configuration

### Environment Variables

Set the following environment variables:

```bash
export GEMINI_API_KEY="your-api-key-here"
export LISTENER_TYPE="gemini_realtime"  # Use Gemini Live API
```

### Listener Configuration

The Gemini realtime listener supports these configuration options:

```yaml
# In session creation request
listener: gemini_realtime
model: mock_local  # or veo3 for video integration
world_context:
  scenario_tags: [trade_negotiation, colonial_period]
  initiator_faction: british_empire
  counterpart_faction: indigenous_confederation
```

## Features

### Real-Time Audio Processing

The Gemini Live API integration provides:

- **Bidirectional Audio Streaming**: Send and receive audio in real-time
- **Speech-to-Text**: Automatic transcription of spoken input  
- **Text-to-Speech**: Generated speech responses from Gemini
- **Contextual Understanding**: Diplomatic context-aware responses
- **Low Latency**: Optimized for real-time conversation

### Diplomatic Context

The implementation includes:

- **Specialized System Prompt**: Tuned for diplomatic negotiations
- **Professional Voice**: Uses "Aoede" voice for formal tone
- **Historical Context**: Supports colonial period (1607-1799) scenarios
- **Intent Detection**: Recognizes proposals, concessions, ultimatums, etc.

## Usage Examples

### Basic Session with Gemini Live

```bash
# Create a session with Gemini Live listener
curl -X POST http://localhost:8000/v1/session \
  -H "Content-Type: application/x-yaml" \
  -d "
model: mock_local
listener: gemini_realtime
world_context:
  scenario_tags: [trade_negotiation]
  initiator_faction: british_empire
  counterpart_faction: indigenous_confederation
"
```

### WebRTC Integration

The Gemini Live API integrates seamlessly with the existing WebRTC pipeline:

1. **Audio Input**: WebRTC captures microphone audio
2. **PCM Conversion**: Audio is converted to 16kHz mono PCM
3. **Gemini Processing**: Audio is streamed to Gemini Live API  
4. **Response Generation**: Gemini provides both text and audio responses
5. **WebRTC Output**: Audio responses are streamed back via WebRTC

### Web Interface Usage

1. Open `http://localhost:8000/` in your browser
2. Select "Gemini Realtime" as the listener type
3. Click "🚀 Create Session" 
4. Grant microphone permissions when prompted
5. Speak diplomatic phrases and receive AI responses

## Testing

### Test Script

Run the included test script to verify your setup:

```bash
cd /services/negotiation
export GEMINI_API_KEY="your-api-key"
python examples/gemini_live_test.py
```

### Expected Behavior

With a valid API key, you should see:

```
{"event": "Gemini Live API connection established", "level": "info", "model": "gemini-2.0-flash-exp"}
{"event": "Received event", "level": "info", "event": {"type": "subtitle", "text": "I understand you're interested in diplomatic negotiations.", "final": true}}
```

### Mock Mode

Without an API key, the listener falls back to mock mode:

```
{"event": "Running Gemini listener in mock mode", "level": "info"}
{"event": "Received event", "level": "info", "event": {"type": "subtitle", "text": "Processing diplomatic audio input...", "final": false}}
```

## Supported Models

- `gemini-2.0-flash-exp`: Experimental multimodal model (default)
- `gemini-1.5-flash`: Fast, lightweight model
- `gemini-1.5-pro`: High-quality model for complex negotiations

## Audio Formats

The integration handles:

- **Input**: 16kHz mono PCM from WebRTC
- **Chunking**: 100ms chunks (3200 bytes) for optimal latency
- **Encoding**: Base64 encoding for WebSocket transmission
- **Output**: Gemini-generated audio responses

## Troubleshooting

### Common Issues

1. **Import Error**: `ModuleNotFoundError: No module named 'google.genai'`
   - Solution: `pip install google-genai`

2. **Authentication Error**: `Invalid API key`
   - Check your API key from Google AI Studio
   - Verify the `GEMINI_API_KEY` environment variable

3. **Mock Mode Only**: Service falls back to mock responses
   - Verify API key is set correctly
   - Check network connectivity
   - Review service logs for error details

4. **Audio Not Processing**: No transcription events
   - Verify microphone permissions in browser
   - Check WebRTC connection status
   - Monitor audio input levels

### Debug Logging

Enable detailed logging:

```bash
export LOG_LEVEL=DEBUG
export LISTENER_TYPE=gemini_realtime
python main.py
```

### Network Requirements

- **Outbound HTTPS**: Access to Google AI APIs
- **WebSocket**: Persistent connection to Gemini Live
- **WebRTC**: Browser media permissions required

## Integration with Video

The Gemini Live API works alongside video generation:

```python
# Configure for video + audio integration
config = {
    "model": "veo3",  # Use Veo3 for video generation
    "listener": "gemini_realtime",  # Use Gemini for audio
    "avatar_style": "diplomatic"
}
```

This provides:
- **Synchronized A/V**: Video avatar with Gemini audio
- **Contextual Responses**: Video expressions match diplomatic intent
- **Real-time Generation**: Both video and audio generated live

## Performance Considerations

- **Latency**: ~200-500ms round-trip with Gemini Live
- **Bandwidth**: ~50kbps for audio streaming
- **Concurrency**: Each session maintains separate WebSocket
- **Caching**: Audio chunks buffered for smooth playback

## Security

- **API Key Protection**: Store keys as environment variables
- **Content Filtering**: Gemini includes built-in safety filters
- **Data Privacy**: Audio processed according to Google's privacy policy
- **Session Isolation**: Each negotiation session is independent
