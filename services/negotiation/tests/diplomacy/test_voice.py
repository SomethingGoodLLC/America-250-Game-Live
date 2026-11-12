"""Envoy voice: ElevenLabs pcm_24000 -> 24 kHz WAV, per-envoy voice table, visible failures, say backup,
and speech jobs that survive the client closing the socket."""

from __future__ import annotations

import asyncio
import base64
import json
import time
import wave
from pathlib import Path
from typing import Any

import httpx
import pytest
from fastapi.testclient import TestClient

from diplomacy import avatar


class _Resp:
    def __init__(self, status: int, content: bytes, ctype: str) -> None:
        self.status_code, self.content, self.headers = (
            status,
            content,
            {"content-type": ctype},
        )

    @property
    def text(self) -> str:
        return self.content.decode(errors="replace")

    def json(self) -> Any:
        return json.loads(self.content)


def _fake_post(
    monkeypatch: pytest.MonkeyPatch, resp: _Resp, seen: list[dict[str, Any]]
) -> None:
    class Client:
        def __init__(self, **_: Any) -> None: ...
        async def __aenter__(self) -> Client:
            return self

        async def __aexit__(self, *_: Any) -> None: ...
        async def post(self, url: str, **kw: Any) -> _Resp:
            seen.append({"url": url, **kw})
            return resp

    monkeypatch.setattr(httpx, "AsyncClient", Client)


def test_british_envoy_has_own_voice(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("ELEVENLABS_VOICE_ID", "DEFAULTVOICE")
    v = avatar.voice_for("british")
    assert v.source == "table" and v.voice_id != "DEFAULTVOICE" and v.say_voice
    assert avatar.voice_for("someone_else").voice_id == "DEFAULTVOICE"


def test_elevenlabs_writes_24k_mono_wav(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("ELEVENLABS_API_KEY", "k")
    monkeypatch.setenv("PA250_TTS", "elevenlabs")
    monkeypatch.setattr(avatar, "AUDIO_DIR", tmp_path)
    seen: list[dict[str, Any]] = []
    alignment = {"characters": list("Good day"), "character_start_times_seconds": [i * 0.1 for i in range(8)],
                 "character_end_times_seconds": [i * 0.1 + 0.1 for i in range(8)]}
    body = json.dumps({"audio_base64": base64.b64encode(b"\x01\x00" * 24000).decode(), "alignment": alignment})
    _fake_post(monkeypatch, _Resp(200, body.encode(), "application/json"), seen)
    out = asyncio.run(
        avatar.synthesize_line_audio("Good day, sir.", "j1", envoy_id="british")
    )
    assert out is not None and out[1] == "elevenlabs" and out[2] == alignment
    assert seen[0]["params"] == {"output_format": "pcm_24000"} and seen[0]["url"].endswith("/with-timestamps")
    assert avatar.voice_for("british").voice_id in seen[0]["url"]
    with wave.open(out[0], "rb") as w:
        assert (
            w.getframerate(),
            w.getnchannels(),
            w.getsampwidth(),
            w.getnframes(),
        ) == (24000, 1, 2, 24000)


def test_elevenlabs_failure_logs_status_and_falls_back_to_say(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setenv("ELEVENLABS_API_KEY", "k")
    monkeypatch.setenv("PA250_TTS", "auto")
    monkeypatch.setattr(avatar, "AUDIO_DIR", tmp_path)
    _fake_post(
        monkeypatch,
        _Resp(402, b'{"detail":{"code":"paid_plan_required"}}', "application/json"),
        [],
    )

    async def say_ok(text: str, wav: Path, voice: avatar.EnvoyVoice, previous_text: str | None = None) -> dict:
        with wave.open(str(wav), "wb") as w:
            (
                w.setnchannels(1),
                w.setsampwidth(2),
                w.setframerate(22050),
                w.writeframes(b"\x00\x00" * 2205),
            )
        return {}

    monkeypatch.setattr(avatar, "_tts_say", say_ok)
    seen_logs: list[tuple[str, dict[str, Any]]] = []
    monkeypatch.setattr(
        avatar.logger, "warning", lambda msg, **kw: seen_logs.append((msg, kw))
    )
    out = asyncio.run(
        avatar.synthesize_line_audio("Good day.", "j2", envoy_id="british")
    )
    assert out is not None and out[1] == "say"
    msg, kw = seen_logs[0]
    assert (
        msg == "ElevenLabs TTS failed"
        and kw["http_status"] == 402
        and "paid_plan_required" in kw["body"]
    )
    assert "k" not in kw.values()  # never log the key


def test_say_failure_logs_exit_code(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    async def run_fail(*cmd: str) -> tuple[int, str]:
        return 1, "Voice `Nobody' not found."

    monkeypatch.setattr(avatar, "_run", run_fail)
    seen_logs: list[tuple[str, dict[str, Any]]] = []
    monkeypatch.setattr(
        avatar.logger, "warning", lambda msg, **kw: seen_logs.append((msg, kw))
    )
    v = avatar.EnvoyVoice("x", "v", "m", say_voice="Nobody")
    assert asyncio.run(avatar._tts_say("hi", tmp_path / "a.wav", v)) is None
    assert seen_logs[0][0] == "say TTS failed" and seen_logs[0][1]["exit_code"] == 1


def test_speech_job_survives_socket_close(
    base_context: dict, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Close the control socket right after the proposal: the speech job must still run to completion."""
    import main

    finished: list[str] = []

    async def slow_render(
        text: str, job_id: str, portrait: Path | None = None, envoy_id: str = "default", previous_text: str | None = None
    ) -> avatar.AvatarLine:
        await asyncio.sleep(0.5)
        finished.append(envoy_id)
        return avatar.AvatarLine("/tmp/x.wav", None, "audio_only", [], "say", 1.0)

    monkeypatch.setattr(main, "render_avatar_line", slow_render)
    with TestClient(main.app) as client:
        sid = client.post(
            "/v1/session",
            json={
                "diplomacy_context": base_context,
                "envoy": {"name": "Gage", "title": "Gov"},
            },
        ).json()["session_id"]
        with client.websocket_connect(f"/v1/session/{sid}/control?format=json") as ws:
            ws.send_text(
                json.dumps(
                    {"type": "player_utterance", "text": "I propose a ceasefire."}
                )
            )
            while json.loads(ws.receive_text()).get("type") != "diplomacy_proposal":
                pass
        # socket closed: provider tasks were cancelled; the detached speech job must not be
        deadline = time.time() + 5
        while not finished and time.time() < deadline:
            client.get("/health")
            time.sleep(0.1)
    assert finished == [base_context["faction"]]


def test_elevenlabs_429_is_retried_not_spoken_by_say(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """A concurrency 429 must not hand one sentence of a reply to the macOS voice."""
    monkeypatch.setenv("ELEVENLABS_API_KEY", "k")
    monkeypatch.setenv("PA250_TTS", "auto")
    monkeypatch.setattr(avatar, "AUDIO_DIR", tmp_path)
    ok = json.dumps({"audio_base64": base64.b64encode(b"\x00\x20" * 2400).decode(), "alignment": None})
    replies = [_Resp(429, b'{"detail":{"code":"concurrent_limit_exceeded"}}', "application/json"),
               _Resp(200, ok.encode(), "application/json")]

    class Client:
        def __init__(self, **_: Any) -> None: ...
        async def __aenter__(self) -> Client:
            return self

        async def __aexit__(self, *_: Any) -> None: ...
        async def post(self, url: str, **kw: Any) -> _Resp:
            return replies.pop(0)

    monkeypatch.setattr(httpx, "AsyncClient", Client)

    async def no_sleep(_: float) -> None: ...

    monkeypatch.setattr(avatar.asyncio, "sleep", no_sleep)
    out = asyncio.run(avatar.synthesize_line_audio("Good day.", "j429", envoy_id="british"))
    assert out is not None and out[1] == "elevenlabs" and not replies


def test_tail_silence_is_trimmed_and_the_start_kept() -> None:
    import struct

    voiced = struct.pack("<2400h", *([8000, -8000] * 1200))  # 0.1 s
    pcm = b"\x00\x00" * 1200 + voiced + b"\x00\x00" * 12000  # 0.05 s lead, 0.5 s tail
    out = avatar.trim_tail_silence(pcm, 24000)
    assert out.startswith(b"\x00\x00" * 1200 + voiced)
    assert len(out) // 2 == 1200 + 2400 + int(avatar.TAIL_SILENCE_S * 24000)
    assert avatar.trim_tail_silence(b"\x00\x00" * 100, 24000) == b"\x00\x00" * 100  # all silence: untouched
