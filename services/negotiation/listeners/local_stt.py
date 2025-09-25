"""Local STT listener using faster-whisper for real-time transcription."""

import asyncio
import time
from typing import AsyncIterator, Dict, Any
try:
    import numpy as np
    from faster_whisper import WhisperModel
    HAS_WHISPER = True
except ImportError:
    HAS_WHISPER = False
    np = None
    WhisperModel = None

from .base import Listener


class LocalSTTListener(Listener):
    """Local STT listener using faster-whisper.

    Provides real-time transcription with VAD (Voice Activity Detection).
    """

    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.model = None
        self.vad_model = None
        self.is_speaking = False
        self.audio_buffer = []
        self.sample_rate = 16000  # 16kHz mono
        self.chunk_duration_ms = 100  # Process every 100ms
        self.chunk_samples = int(self.sample_rate * self.chunk_duration_ms / 1000)
        self.transcript_buffer = []
        self.event_queue = asyncio.Queue()
        self.running = False

    async def start(self) -> None:
        """Initialize the Whisper model and VAD."""
        if not HAS_WHISPER:
            print("Warning: faster-whisper not available. Using mock mode.")
            self.running = True
            asyncio.create_task(self._mock_stream())
            return

        try:
            # Load Whisper model (small for speed, tiny for even faster)
            model_size = self.config.get("model_size", "tiny")
            self.model = WhisperModel(model_size, device="cpu", compute_type="int8")

            # Load VAD model
            try:
                import torch
                from silero_vad import load_silero_vad
                self.vad_model = load_silero_vad()
            except ImportError:
                # Fallback: simple energy-based VAD
                self.vad_model = "energy"

            self.running = True
            # Start background processing
            asyncio.create_task(self._process_audio())
        except Exception as e:
            print(f"Error initializing LocalSTTListener: {e}")
            # Fallback to mock mode
            self.running = True
            asyncio.create_task(self._mock_stream())

    async def stop(self) -> None:
        """Stop processing and clean up."""
        self.running = False
        if self.event_queue:
            await self.event_queue.put({"type": "stop"})

    async def feed_pcm(self, pcm_bytes: bytes, ts_ms: int) -> None:
        """Feed PCM audio data to the listener."""
        if not self.running:
            return

        if not HAS_WHISPER or np is None:
            # In mock mode, just acknowledge the audio
            return

        # Convert bytes to numpy array (16-bit signed PCM)
        audio_data = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32) / 32768.0

        # Add to buffer
        self.audio_buffer.append(audio_data)

        # Process when we have enough data
        if len(self.audio_buffer) * self.chunk_samples >= self.sample_rate:
            await self._process_chunk()

    async def final_text(self) -> str:
        """Get final transcript when user stops speaking."""
        if not HAS_WHISPER:
            return "Mock local STT final transcript"
            
        # Force process any remaining audio
        if self.audio_buffer:
            await self._process_chunk()

        # Wait a moment for final processing
        await asyncio.sleep(0.1)

        # Return concatenated transcript
        return " ".join(self.transcript_buffer).strip()

    async def stream_events(self) -> AsyncIterator[Dict[str, Any]]:
        """Stream real-time events."""
        while self.running:
            try:
                event = await asyncio.wait_for(self.event_queue.get(), timeout=0.1)
                if event.get("type") == "stop":
                    break
                yield event
            except asyncio.TimeoutError:
                continue
            except Exception as e:
                print(f"Error in stream_events: {e}")
                break

    async def _process_chunk(self) -> None:
        """Process accumulated audio chunk."""
        if not self.audio_buffer or not self.model or not HAS_WHISPER:
            return

        try:
            # Concatenate and flatten audio buffer
            audio_array = np.concatenate(self.audio_buffer)
            self.audio_buffer.clear()

            # Skip if too short
            if len(audio_array) < 1000:  # Less than 100ms
                return

            # Check VAD
            if not await self._is_speech(audio_array):
                return

            # Transcribe
            segments, info = await asyncio.get_event_loop().run_in_executor(
                None, self.model.transcribe, audio_array, {"language": "en"}
            )

            for segment in segments:
                text = segment.text.strip()
                if text:
                    self.transcript_buffer.append(text)
                    await self.event_queue.put({
                        "type": "subtitle",
                        "text": text,
                        "final": False,
                        "confidence": float(info.language_probability) if info.language_probability else 0.8
                    })

        except Exception as e:
            print(f"Error processing audio chunk: {e}")

    async def _is_speech(self, audio_data) -> bool:
        """Check if audio contains speech using VAD."""
        if not HAS_WHISPER or np is None:
            return True  # Assume speech in mock mode
            
        try:
            if self.vad_model == "energy":
                # Simple energy-based VAD
                energy = np.sqrt(np.mean(audio_data**2))
                return energy > 0.01  # Threshold for speech
            else:
                # Use silero VAD
                import torch
                audio_tensor = torch.from_numpy(audio_data).unsqueeze(0)
                return bool(self.vad_model(audio_tensor))

        except Exception:
            # Fallback: assume speech if audio has reasonable amplitude
            return np.max(np.abs(audio_data)) > 0.01

    async def _process_audio(self) -> None:
        """Background task to process audio buffer."""
        while self.running:
            try:
                await asyncio.sleep(0.05)  # Process every 50ms
                if self.audio_buffer:
                    await self._process_chunk()
            except Exception as e:
                print(f"Error in _process_audio: {e}")

    async def _mock_stream(self) -> None:
        """Mock streaming for testing when Whisper is not available."""
        while self.running:
            try:
                await asyncio.sleep(2.0)
                # Simulate subtitle events
                await self.event_queue.put({
                    "type": "subtitle",
                    "text": "Mock local STT processing audio...",
                    "final": False,
                    "confidence": 0.8
                })
            except Exception as e:
                print(f"Error in mock stream: {e}")


# Re-export for convenience
__all__ = ["LocalSTTListener"]
