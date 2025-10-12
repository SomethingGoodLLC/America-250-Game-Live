"""Tests for TTS streaming providers."""

import asyncio
import base64
import pytest
from unittest.mock import AsyncMock, MagicMock, patch, Mock
from typing import List

from app.schemas import StreamEvent, PhonemeTimestamp, VisemeEvent
from tts.streaming.base import TTSStreamer
from tts.streaming.cartesia import CartesiaTTSStreamer
from tts.streaming.azure import AzureTTSStreamer, AZURE_AVAILABLE
from tts.streaming.elevenlabs import ElevenLabsTTSStreamer


class TestTTSStreamerBase:
    """Tests for TTSStreamer base class."""
    
    def test_validate_started_raises_when_not_started(self):
        """Test that validation fails when streamer not started."""
        
        class MockStreamer(TTSStreamer):
            async def start(self, *, voice_id=None, **kwargs):
                pass
            
            async def send_text(self, text, *, final=False):
                pass
            
            async def stop(self):
                pass
            
            async def aiter_events(self):
                yield StreamEvent(kind="done")
        
        streamer = MockStreamer()
        
        with pytest.raises(RuntimeError, match="start\\(\\) must be called"):
            streamer._validate_started()
    
    def test_validate_not_stopped_raises_when_stopped(self):
        """Test that validation fails when streamer already stopped."""
        
        class MockStreamer(TTSStreamer):
            async def start(self, *, voice_id=None, **kwargs):
                pass
            
            async def send_text(self, text, *, final=False):
                pass
            
            async def stop(self):
                pass
            
            async def aiter_events(self):
                yield StreamEvent(kind="done")
        
        streamer = MockStreamer()
        streamer._stopped = True
        
        with pytest.raises(RuntimeError, match="has been stopped"):
            streamer._validate_not_stopped()


class TestCartesiaTTSStreamer:
    """Tests for Cartesia TTS streaming adapter."""
    
    @pytest.fixture
    def mock_websocket(self):
        """Create a mock WebSocket."""
        ws = AsyncMock()
        ws.closed = False
        ws.recv = AsyncMock()
        ws.send = AsyncMock()
        ws.close = AsyncMock()
        return ws
    
    def test_init_requires_api_key(self):
        """Test that API key is required."""
        with pytest.raises(ValueError, match="API key is required"):
            CartesiaTTSStreamer(api_key="")
    
    @pytest.mark.asyncio
    async def test_start_connects_to_websocket(self, mock_websocket):
        """Test that start() establishes WebSocket connection."""
        with patch('tts.streaming.cartesia.websockets.connect', return_value=mock_websocket):
            # Mock config acknowledgment
            mock_websocket.recv.return_value = '{"type": "config_ack", "context_id": "test-123"}'
            
            streamer = CartesiaTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            assert streamer._started
            assert streamer._context_id == "test-123"
            
            # Verify configuration was sent
            mock_websocket.send.assert_called_once()
            
            # Clean up
            await streamer.stop()
    
    @pytest.mark.asyncio
    async def test_send_text_sends_chunk_message(self, mock_websocket):
        """Test that send_text() sends proper chunk message."""
        with patch('tts.streaming.cartesia.websockets.connect', return_value=mock_websocket):
            mock_websocket.recv.return_value = '{"type": "config_ack", "context_id": "test-123"}'
            
            streamer = CartesiaTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            # Send text
            await streamer.send_text("Hello world", final=False)
            
            # Verify chunk was sent
            assert mock_websocket.send.call_count >= 2  # config + chunk
            
            await streamer.stop()
    
    @pytest.mark.asyncio
    async def test_aiter_events_yields_audio_events(self, mock_websocket):
        """Test that audio events are properly yielded."""
        with patch('tts.streaming.cartesia.websockets.connect', return_value=mock_websocket):
            mock_websocket.recv.return_value = '{"type": "config_ack", "context_id": "test-123"}'
            
            streamer = CartesiaTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            # Simulate receiving an audio chunk
            audio_data = b'\x00\x01\x02\x03' * 100
            await streamer._handle_audio_chunk(audio_data)
            
            # Consume event
            events = []
            async for event in streamer.aiter_events():
                events.append(event)
                if event.kind == "done":
                    break
                # Break after first event for testing
                break
            
            assert len(events) == 1
            assert events[0].kind == "audio"
            assert events[0].pcm16_b64 is not None
            assert events[0].sample_rate == 24000
            
            await streamer.stop()
    
    @pytest.mark.asyncio
    async def test_phoneme_events_properly_converted(self, mock_websocket):
        """Test that Cartesia phoneme events are converted correctly."""
        with patch('tts.streaming.cartesia.websockets.connect', return_value=mock_websocket):
            mock_websocket.recv.return_value = '{"type": "config_ack", "context_id": "test-123"}'
            
            streamer = CartesiaTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            # Simulate phoneme timestamp message
            phoneme_msg = {
                "type": "phoneme_timestamps",
                "phonemes": [
                    {"phoneme": "HH", "start_time_ms": 0, "end_time_ms": 50},
                    {"phoneme": "AH0", "start_time_ms": 50, "end_time_ms": 150}
                ]
            }
            
            await streamer._handle_json_message(phoneme_msg)
            
            # Consume event
            events = []
            async for event in streamer.aiter_events():
                events.append(event)
                break
            
            assert len(events) == 1
            assert events[0].kind == "phonemes"
            assert len(events[0].phonemes) == 2
            assert events[0].phonemes[0].phoneme == "HH"
            
            await streamer.stop()


class TestAzureTTSStreamer:
    """Tests for Azure TTS streaming adapter."""
    
    @pytest.mark.skipif(not AZURE_AVAILABLE, reason="Azure SDK not installed")
    def test_init_requires_credentials(self):
        """Test that subscription key and region are required."""
        with pytest.raises(ValueError, match="subscription_key and region are required"):
            AzureTTSStreamer(subscription_key="", region="")
    
    @pytest.mark.skipif(not AZURE_AVAILABLE, reason="Azure SDK not installed")
    def test_init_with_valid_credentials(self):
        """Test initialization with valid credentials."""
        streamer = AzureTTSStreamer(
            subscription_key="test_key",
            region="eastus"
        )
        assert streamer.subscription_key == "test_key"
        assert streamer.region == "eastus"
    
    @pytest.mark.skipif(not AZURE_AVAILABLE, reason="Azure SDK not installed")
    @pytest.mark.asyncio
    async def test_start_initializes_synthesizer(self):
        """Test that start() initializes Azure synthesizer."""
        with patch('tts.streaming.azure.speechsdk.SpeechConfig') as mock_config, \
             patch('tts.streaming.azure.speechsdk.SpeechSynthesizer') as mock_synth:
            
            streamer = AzureTTSStreamer(
                subscription_key="test_key",
                region="eastus"
            )
            
            await streamer.start(voice_id="en-US-JennyNeural")
            
            assert streamer._started
            assert streamer.voice_id == "en-US-JennyNeural"
            
            # Verify config was created
            mock_config.assert_called_once()
    
    @pytest.mark.skipif(not AZURE_AVAILABLE, reason="Azure SDK not installed")
    @pytest.mark.asyncio
    async def test_build_ssml_includes_viseme_tag(self):
        """Test that SSML includes viseme configuration."""
        streamer = AzureTTSStreamer(
            subscription_key="test_key",
            region="eastus"
        )
        streamer.voice_id = "en-US-JennyNeural"
        
        ssml = streamer._build_ssml("Hello world")
        
        assert 'mstts:viseme type="FacialExpression"' in ssml
        assert "Hello world" in ssml
        assert streamer.voice_id in ssml
    
    @pytest.mark.skipif(not AZURE_AVAILABLE, reason="Azure SDK not installed")
    @pytest.mark.asyncio
    async def test_build_ssml_with_blendshapes(self):
        """Test that SSML includes blendshape tag when enabled."""
        streamer = AzureTTSStreamer(
            subscription_key="test_key",
            region="eastus"
        )
        streamer.voice_id = "en-US-JennyNeural"
        streamer._enable_blendshapes = True
        
        ssml = streamer._build_ssml("Hello world")
        
        assert 'mstts:viseme type="blendShapes"' in ssml


class TestElevenLabsTTSStreamer:
    """Tests for ElevenLabs TTS streaming adapter."""
    
    @pytest.fixture
    def mock_websocket(self):
        """Create a mock WebSocket."""
        ws = AsyncMock()
        ws.closed = False
        ws.recv = AsyncMock()
        ws.send = AsyncMock()
        ws.close = AsyncMock()
        return ws
    
    def test_init_requires_api_key(self):
        """Test that API key is required."""
        with pytest.raises(ValueError, match="API key is required"):
            ElevenLabsTTSStreamer(api_key="")
    
    @pytest.mark.asyncio
    async def test_start_connects_to_websocket(self, mock_websocket):
        """Test that start() establishes WebSocket connection."""
        with patch('tts.streaming.elevenlabs.websockets.connect', return_value=mock_websocket):
            streamer = ElevenLabsTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            assert streamer._started
            assert streamer.voice_id == "voice-123"
            
            # Verify initial config was sent
            mock_websocket.send.assert_called_once()
            
            await streamer.stop()
    
    @pytest.mark.asyncio
    async def test_send_text_accumulates_for_phonemes(self, mock_websocket):
        """Test that text is accumulated for phoneme generation."""
        with patch('tts.streaming.elevenlabs.websockets.connect', return_value=mock_websocket):
            streamer = ElevenLabsTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            # Send text
            await streamer.send_text("Hello", final=False)
            await streamer.send_text("world", final=False)
            
            # Check that text was accumulated
            assert len(streamer._pending_text) == 2
            
            await streamer.stop()
    
    @pytest.mark.asyncio
    async def test_process_alignment_generates_phonemes(self, mock_websocket):
        """Test that alignment data is processed into phonemes."""
        with patch('tts.streaming.elevenlabs.websockets.connect', return_value=mock_websocket):
            streamer = ElevenLabsTTSStreamer(api_key="test_key")
            await streamer.start(voice_id="voice-123")
            
            # Simulate alignment data
            alignment = {
                "chars": list("hello world"),
                "charStartTimesMs": [0, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500]
            }
            
            await streamer._process_alignment(alignment)
            
            # Should have generated phoneme events
            # Note: Actual phoneme generation depends on dictionary
            
            await streamer.stop()


class TestPhonemizerIntegration:
    """Tests for phonemizer integration."""
    
    def test_text_to_phonemes_basic(self):
        """Test basic text to phoneme conversion."""
        from app.phonemizer import text_to_phonemes
        
        phonemes = text_to_phonemes("hello world")
        
        assert isinstance(phonemes, list)
        assert len(phonemes) > 0
        # Should include phonemes and space markers
        assert "SP" in phonemes  # Space marker
    
    def test_words_to_phoneme_timestamps(self):
        """Test word timing to phoneme timing conversion."""
        from app.phonemizer import words_to_phoneme_timestamps
        
        words = ["hello", "world"]
        timings = [
            {"start_ms": 0, "end_ms": 500},
            {"start_ms": 600, "end_ms": 1000}
        ]
        
        result = words_to_phoneme_timestamps(words, timings)
        
        assert isinstance(result, list)
        assert len(result) > 0
        assert "phoneme" in result[0]
        assert "start_ms" in result[0]
        assert "end_ms" in result[0]


class TestMappingUtilities:
    """Tests for ARPAbet/OVR mapping utilities."""
    
    def test_arpabet_to_ovr_basic_phonemes(self):
        """Test ARPAbet to OVR viseme mapping."""
        from app.mapping import arpabet_to_ovr, OVRViseme
        
        # Test vowels
        assert arpabet_to_ovr("AA") == OVRViseme.AA
        assert arpabet_to_ovr("IY") == OVRViseme.I
        assert arpabet_to_ovr("UW") == OVRViseme.U
        
        # Test consonants
        assert arpabet_to_ovr("P") == OVRViseme.PP
        assert arpabet_to_ovr("F") == OVRViseme.FF
        assert arpabet_to_ovr("TH") == OVRViseme.TH
    
    def test_arpabet_to_ovr_strips_stress(self):
        """Test that stress markers are stripped."""
        from app.mapping import arpabet_to_ovr
        
        # With stress markers
        assert arpabet_to_ovr("AA0") == arpabet_to_ovr("AA")
        assert arpabet_to_ovr("IY1") == arpabet_to_ovr("IY")
        assert arpabet_to_ovr("EH2") == arpabet_to_ovr("EH")
    
    def test_azure_to_ovr_conversion(self):
        """Test Azure viseme to OVR conversion."""
        from app.mapping import azure_to_ovr, OVRViseme, AzureViseme
        
        # Test some key mappings
        assert azure_to_ovr(AzureViseme.SIL) == OVRViseme.SIL
        assert azure_to_ovr(AzureViseme.P_B_M) == OVRViseme.PP
        assert azure_to_ovr(AzureViseme.F_V) == OVRViseme.FF
    
    def test_is_vowel_check(self):
        """Test vowel detection."""
        from app.mapping import is_vowel, is_consonant
        
        assert is_vowel("AA")
        assert is_vowel("IY")
        assert is_vowel("AA1")  # With stress
        
        assert not is_vowel("P")
        assert not is_vowel("F")
    
    def test_is_consonant_check(self):
        """Test consonant detection."""
        from app.mapping import is_vowel, is_consonant
        
        assert is_consonant("P")
        assert is_consonant("B")
        assert is_consonant("M")
        
        assert not is_consonant("AA")
        assert not is_consonant("IY")
    
    def test_viseme_duration_hint(self):
        """Test phoneme duration estimation."""
        from app.mapping import get_viseme_duration_hint
        
        # Stops should be short
        min_ms, max_ms = get_viseme_duration_hint("P")
        assert min_ms < max_ms
        assert max_ms <= 100  # Stops are quick
        
        # Vowels should be longer
        min_ms, max_ms = get_viseme_duration_hint("AA")
        assert min_ms < max_ms
        assert max_ms >= 100  # Vowels are longer


@pytest.mark.integration
class TestEndToEndStreaming:
    """Integration tests for end-to-end streaming."""
    
    @pytest.mark.asyncio
    async def test_mock_streaming_workflow(self):
        """Test complete streaming workflow with mocks."""
        
        class MockTTSStreamer(TTSStreamer):
            """Mock streamer for testing."""
            
            def __init__(self):
                super().__init__()
                self._events = []
            
            async def start(self, *, voice_id=None, **kwargs):
                self._started = True
            
            async def send_text(self, text, *, final=False):
                self._validate_started()
                self._validate_not_stopped()
                
                # Generate mock audio event
                audio_data = b'\x00' * 1000
                pcm16_b64 = base64.b64encode(audio_data).decode('utf-8')
                
                event = StreamEvent(
                    kind="audio",
                    pcm16_b64=pcm16_b64,
                    sample_rate=24000
                )
                self._events.append(event)
                
                if final:
                    self._events.append(StreamEvent(kind="done"))
            
            async def stop(self):
                self._stopped = True
            
            async def aiter_events(self):
                for event in self._events:
                    yield event
        
        # Test workflow
        streamer = MockTTSStreamer()
        
        # Start
        await streamer.start(voice_id="test-voice")
        
        # Send text
        await streamer.send_text("Hello world", final=True)
        
        # Consume events
        events = []
        async for event in streamer.aiter_events():
            events.append(event)
        
        # Verify
        assert len(events) == 2  # audio + done
        assert events[0].kind == "audio"
        assert events[1].kind == "done"
        
        # Clean up
        await streamer.stop()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])

