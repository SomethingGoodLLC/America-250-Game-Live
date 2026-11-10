"""POST /v1/stt (mic button): WAV in, text out; silence and bad input are reported, never crash."""

from __future__ import annotations

import io
import wave

import numpy as np
import pytest
from fastapi.testclient import TestClient

from diplomacy import stt


def _wav(seconds: float, rate: int = 48000, amp: float = 0.3) -> bytes:
    t = np.arange(int(seconds * rate)) / rate
    pcm = (amp * np.sin(2 * np.pi * 220 * t) * 32767).astype("<i2")
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        (
            w.setnchannels(1),
            w.setsampwidth(2),
            w.setframerate(rate),
            w.writeframes(pcm.tobytes()),
        )
    return buf.getvalue()


def test_decode_resamples_to_16k() -> None:
    audio, secs = stt.decode_wav(_wav(2.0, 48000))
    assert (
        audio.dtype == np.float32
        and abs(secs - 2.0) < 1e-6
        and abs(len(audio) - 32000) <= 1
    )


def test_endpoint_transcribes(monkeypatch: pytest.MonkeyPatch) -> None:
    import main

    monkeypatch.setattr(stt, "_transcribe", lambda audio: "I propose a ceasefire.")
    with TestClient(main.app) as c:
        r = c.post("/v1/stt", content=_wav(1.5), headers={"Content-Type": "audio/wav"})
    assert (
        r.status_code == 200
        and r.json()["text"] == "I propose a ceasefire."
        and not r.json()["silent"]
    )


def test_silence_is_flagged_not_transcribed(monkeypatch: pytest.MonkeyPatch) -> None:
    import main

    def boom(audio: np.ndarray) -> str:
        raise AssertionError("silence must not reach the model")

    monkeypatch.setattr(stt, "_transcribe", boom)
    with TestClient(main.app) as c:
        r = c.post("/v1/stt", content=_wav(1.0, amp=0.0))
    assert r.status_code == 200 and r.json()["silent"] and r.json()["text"] == ""


def test_bad_input_is_422() -> None:
    import main

    with TestClient(main.app) as c:
        assert c.post("/v1/stt", content=b"not a wav").status_code == 422
        assert c.post("/v1/stt", content=_wav(61.0, rate=8000)).status_code == 422
