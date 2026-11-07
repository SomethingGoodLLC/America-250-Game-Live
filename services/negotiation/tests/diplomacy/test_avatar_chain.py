"""Avatar preference chain: MuseTalk -> D-ID (if keyed) -> audio-only."""

import asyncio
from pathlib import Path

import pytest

from diplomacy import avatar
from providers.video_sources.musetalk_source import MuseTalkVideoSource


def test_musetalk_unavailable_without_install(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("MUSETALK_HOME", raising=False)
    src = MuseTalkVideoSource()
    assert not src.available()
    assert "MUSETALK_HOME" in (src.unavailable_reason() or "")
    assert asyncio.run(src.render_line("a.wav", "p.jpg", "o.mp4")) is None


def _fake_audio(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    wav = tmp_path / "x.wav"
    wav.write_bytes(b"RIFF")

    async def fake(text: str, job_id: str) -> str:
        return str(wav)

    monkeypatch.setattr(avatar, "synthesize_line_audio", fake)


def test_falls_back_to_audio_only(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    _fake_audio(monkeypatch, tmp_path)
    monkeypatch.setenv("PA250_AVATAR", "auto")
    monkeypatch.delenv("MUSETALK_HOME", raising=False)
    monkeypatch.delenv("DID_API_KEY", raising=False)
    line = asyncio.run(avatar.render_avatar_line("Good day.", "job1"))
    assert line.backend == "audio_only" and line.video_file is None and line.audio_file


def test_musetalk_preferred_then_did(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    _fake_audio(monkeypatch, tmp_path)
    monkeypatch.setenv("PA250_AVATAR", "auto")
    calls = []

    async def mt_ok(self, audio: str, portrait: str, out: str):  # noqa: ANN001
        calls.append("musetalk")
        return out

    async def did_ok(text: str, portrait: Path, out: Path):
        calls.append("did")
        return str(out)

    monkeypatch.setattr(MuseTalkVideoSource, "unavailable_reason", lambda self: None)
    monkeypatch.setattr(MuseTalkVideoSource, "render_line", mt_ok)
    monkeypatch.setattr(avatar, "_did_video", did_ok)
    assert asyncio.run(avatar.render_avatar_line("x", "j")).backend == "musetalk"

    async def mt_fail(self, audio: str, portrait: str, out: str):  # noqa: ANN001
        return None

    monkeypatch.setattr(MuseTalkVideoSource, "render_line", mt_fail)
    assert asyncio.run(avatar.render_avatar_line("x", "j")).backend == "did"
    assert calls == ["musetalk", "did"][: len(calls)]


def test_no_audio_means_no_avatar(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("PA250_TTS", "off")
    line = asyncio.run(avatar.render_avatar_line("hello", "j"))
    assert line.backend == "none" and line.audio_file is None
