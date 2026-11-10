"""Player speech -> text for the Unreal mic button (local faster-whisper; audio never leaves this Mac).

POST /v1/stt with a 16-bit PCM WAV body returns {"text", "audio_seconds", "elapsed_s", "model"}.
The text is then sent by the client as an ordinary ``player_utterance``; nothing here touches campaign state.

Configuration (environment):
  PA250_STT_MODEL     faster-whisper model (default "base.en"; .env WHISPER_MODEL_SIZE is the old WebRTC path's)
  WHISPER_DEVICE      "cpu" (default)
  PA250_STT_LANGUAGE  "en" (default)
  PA250_STT_PREWARM=0 do not load the model at service start (tests)
"""

from __future__ import annotations

import asyncio
import io
import os
import threading
import time
import wave
from typing import Any

import numpy as np
import structlog

logger = structlog.get_logger(__name__)

WHISPER_RATE = 16000
MAX_SECONDS = 60.0
# Spelling hints for names the player will say (whisper otherwise writes "General Gauge").
VOCABULARY = (
    "General Gage, Governor, Massachusetts, Boston, Lexington, Concord, Cambridge, Menotomy, Charlestown, "
    "Roxbury, Dorchester, redcoats, regulars, militia, Continental, ceasefire, truce, parole, prisoners, "
    "safe passage, honours of war."
)
_model: Any = None
_model_name = ""
_load_lock = threading.Lock()


def model_name() -> str:
    return os.getenv("PA250_STT_MODEL") or "base.en"


def _get_model() -> Any:
    global _model, _model_name
    with _load_lock:
        if _model is None:
            from faster_whisper import WhisperModel

            t0 = time.monotonic()
            _model_name = model_name()
            _model = WhisperModel(
                _model_name,
                device=os.getenv("WHISPER_DEVICE", "cpu"),
                compute_type="int8",
            )
            logger.info(
                "STT model loaded",
                model=_model_name,
                load_s=round(time.monotonic() - t0, 2),
            )
    return _model


async def prewarm() -> None:
    try:
        await asyncio.to_thread(_get_model)
    except Exception as e:  # the mic button reports the failure when used
        logger.warning(
            "STT model failed to load",
            model=model_name(),
            error=f"{type(e).__name__}: {e}"[:400],
        )


def decode_wav(data: bytes) -> tuple[np.ndarray, float]:
    """16-bit PCM WAV (any rate, mono or stereo) -> float32 mono at 16 kHz, plus its duration."""
    with wave.open(io.BytesIO(data), "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"need 16-bit PCM WAV, got {8 * w.getsampwidth()}-bit")
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        pcm = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float32) / 32768.0
    if ch > 1:
        pcm = pcm.reshape(-1, ch).mean(axis=1)
    seconds = len(pcm) / float(rate or 1)
    if seconds > MAX_SECONDS:
        raise ValueError(f"clip is {seconds:.1f}s; the limit is {MAX_SECONDS:.0f}s")
    if rate != WHISPER_RATE and len(pcm):
        t_out = np.arange(int(len(pcm) * WHISPER_RATE / rate)) / WHISPER_RATE
        pcm = np.interp(t_out, np.arange(len(pcm)) / rate, pcm).astype(np.float32)
    return pcm, seconds


def _transcribe(audio: np.ndarray) -> str:
    segments, _info = _get_model().transcribe(
        audio,
        language=os.getenv("PA250_STT_LANGUAGE", "en"),
        beam_size=1,
        vad_filter=True,
        condition_on_previous_text=False,
        initial_prompt=VOCABULARY,
    )
    return " ".join(s.text.strip() for s in segments).strip()


async def transcribe_wav(data: bytes) -> dict[str, Any]:
    t0 = time.monotonic()
    audio, seconds = decode_wav(data)
    peak = float(np.abs(audio).max()) if len(audio) else 0.0
    if peak < 1e-3:  # macOS gives zeros when the app has no microphone permission
        logger.warning("STT got silence", audio_seconds=round(seconds, 2), peak=peak)
        return {
            "text": "",
            "audio_seconds": round(seconds, 2),
            "elapsed_s": 0.0,
            "model": model_name(),
            "silent": True,
        }
    text = await asyncio.to_thread(_transcribe, audio)
    elapsed = time.monotonic() - t0
    logger.info(
        "Player speech transcribed",
        text=text,
        audio_seconds=round(seconds, 2),
        peak=round(peak, 3),
        elapsed_s=round(elapsed, 2),
        model=model_name(),
    )
    return {
        "text": text,
        "audio_seconds": round(seconds, 2),
        "elapsed_s": round(elapsed, 2),
        "model": model_name(),
        "silent": False,
    }
