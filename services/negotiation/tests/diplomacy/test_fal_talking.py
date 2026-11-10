"""fal_talking backend, offline: pause-aware splitting, render cache (no fal call), cost estimate, timeout."""

from __future__ import annotations

import asyncio
import wave
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from diplomacy import avatar
from providers.video_sources import fal_talking_source as fts

RATE = 24000


def _speech(seconds_list: list[float], gap: float = 0.4) -> np.ndarray:
    """Tone bursts ("sentences") separated by silent gaps."""
    parts = []
    for s in seconds_list:
        t = np.arange(int(s * RATE)) / RATE
        parts += [
            (0.3 * np.sin(2 * np.pi * 180 * t) * 32767).astype("<i2"),
            np.zeros(int(gap * RATE), "<i2"),
        ]
    return np.concatenate(parts).reshape(-1, 1)


def _wav(path: Path, pcm: np.ndarray) -> Path:
    fts._write_wav(path, pcm, RATE)
    return path


def test_short_line_is_not_split() -> None:
    assert fts.split_points(_speech([5.0]), RATE, 30) == []


def test_long_line_splits_inside_pauses_under_limit() -> None:
    pcm = _speech([9.0, 9.0, 9.0, 9.0])  # ~37.6 s
    cuts = fts.split_points(pcm, RATE, 30)
    bounds = list(zip([0, *cuts], [*cuts, len(pcm)], strict=True))
    assert len(bounds) == 2 and all((b - a) / RATE <= 30 for a, b in bounds)
    for c in cuts:  # every cut lands in silence
        assert np.abs(pcm[c - 240 : c + 240]).max() == 0


def test_no_pause_hard_cuts_at_limit() -> None:
    pcm = _speech([70.0], gap=0)
    assert fts.split_points(pcm, RATE, 30) == [30 * RATE, 60 * RATE]


def test_unavailable_reasons(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("PA250_FAL", raising=False)
    monkeypatch.delenv("FAL_KEY", raising=False)
    assert fts.FalTalkingVideoSource().unavailable_reason() == "FAL_KEY not set"
    monkeypatch.setenv("FAL_KEY", "x")
    assert "unknown" in (
        fts.FalTalkingVideoSource(model="fal-ai/musetalk").unavailable_reason() or ""
    )
    monkeypatch.setenv("PA250_FAL", "0")
    assert fts.FalTalkingVideoSource().unavailable_reason() == "PA250_FAL=0"


class _FakeHandle:
    def __init__(self, out: dict[str, Any], delay: float) -> None:
        self.out, self.delay, self.cancelled = out, delay, False

    async def get(self, interval: float = 0.1) -> dict[str, Any]:
        await asyncio.sleep(self.delay)
        return self.out

    async def cancel(self) -> None:
        self.cancelled = True


class _FakeFal:
    calls: list[dict[str, Any]] = []
    uploads: list[str] = []
    handles: list[_FakeHandle] = []
    delay = 0.0

    def __init__(self, key: str | None = None) -> None: ...

    async def upload_file(self, path: Path, lifecycle: Any = None) -> str:
        _FakeFal.uploads.append(f"{Path(path).name}:{lifecycle.expires_in}")
        return f"https://fal.test/{Path(path).name}"

    async def submit(self, app: str, arguments: dict[str, Any]) -> _FakeHandle:
        _FakeFal.calls.append({"app": app, **arguments})
        h = _FakeHandle({"video": {"url": "https://fal.test/out.mp4"}}, _FakeFal.delay)
        _FakeFal.handles.append(h)
        return h


def _offline_fal(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    import fal_client

    _FakeFal.calls, _FakeFal.uploads, _FakeFal.handles, _FakeFal.delay = [], [], [], 0.0
    monkeypatch.setenv("FAL_KEY", "x")
    monkeypatch.setenv("PA250_FAL", "1")
    monkeypatch.setattr(fal_client, "AsyncClient", _FakeFal)
    monkeypatch.setattr(fts, "VIDEO_DIR", tmp_path)
    monkeypatch.setattr(fts, "SOURCE_CACHE", tmp_path / "source_cache.json")
    monkeypatch.setattr(fts, "RENDER_CACHE", tmp_path / "cache")

    class _Http:
        def __init__(self, **_: Any) -> None: ...
        async def __aenter__(self) -> _Http:
            return self

        async def __aexit__(self, *_: Any) -> None: ...
        async def get(self, url: str) -> Any:
            class R:
                content = b"mp4"

                def raise_for_status(self) -> None: ...

            return R()

    monkeypatch.setattr(fts.httpx, "AsyncClient", _Http)

    async def fake_ffmpeg(*args: str) -> tuple[int, str]:
        Path(args[-1]).write_bytes(b"muxed")
        return 0, ""

    async def fake_probe(path: str) -> dict[str, Any]:
        return {"video": ["h264"], "audio": ["aac"], "duration": 6.0}

    monkeypatch.setattr(fts, "_ffmpeg", fake_ffmpeg)
    monkeypatch.setattr(fts, "probe_streams", fake_probe)


def test_render_then_cache_hit_makes_no_fal_call(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    _offline_fal(monkeypatch, tmp_path)
    wav = _wav(tmp_path / "line.wav", _speech([5.6]))
    portrait = tmp_path / "p.jpg"
    portrait.write_bytes(b"jpeg")
    src = fts.FalTalkingVideoSource(model="veed/fabric-1.0")
    r1 = asyncio.run(src.render(str(wav), str(portrait), str(tmp_path / "a.mp4")))
    assert r1.path and not r1.cache_hit and r1.parts == 1
    assert (
        _FakeFal.calls[0]["resolution"] == "480p"
        and _FakeFal.calls[0]["app"] == "veed/fabric-1.0"
    )
    assert r1.est_cost_usd == pytest.approx(6.0 * 0.08)
    assert _FakeFal.uploads == ["p.jpg:1d", "part0.wav:1h"]  # short-lived uploads only

    r2 = asyncio.run(src.render(str(wav), str(portrait), str(tmp_path / "b.mp4")))
    assert (
        r2.cache_hit and r2.est_cost_usd == 0 and len(_FakeFal.calls) == 1
    )  # no second fal call
    assert (tmp_path / "b.mp4").read_bytes() == b"muxed"


def test_portrait_url_reused_from_source_cache(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    _offline_fal(monkeypatch, tmp_path)
    portrait = tmp_path / "p.jpg"
    portrait.write_bytes(b"jpeg")
    src = fts.FalTalkingVideoSource()
    for i, secs in enumerate([3.0, 4.0]):  # two different lines, same portrait
        wav = _wav(tmp_path / f"l{i}.wav", _speech([secs]))
        asyncio.run(src.render(str(wav), str(portrait), str(tmp_path / f"o{i}.mp4")))
    assert [u for u in _FakeFal.uploads if u.startswith("p.jpg")] == ["p.jpg:1d"]


def test_timeout_goes_audio_only_and_cancels_fal(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    _offline_fal(monkeypatch, tmp_path)
    _FakeFal.delay = 5.0
    wav = _wav(tmp_path / "line.wav", _speech([2.0]))

    async def fake_tts(
        text: str, job_id: str, envoy_id: str = "default", previous_text: str | None = None
    ) -> tuple[str, str]:
        return str(wav), "elevenlabs"

    monkeypatch.setattr(avatar, "synthesize_line_audio", fake_tts)
    monkeypatch.setattr(avatar, "VIDEO_DIR", tmp_path)
    monkeypatch.setenv("PA250_AVATAR", "fal")
    monkeypatch.setenv("PA250_AVATAR_TIMEOUT", "0.5")
    monkeypatch.setenv("PA250_VIDEO_WAIT", "1")
    line = asyncio.run(avatar.render_avatar_line("x", "j", portrait=wav))
    assert (
        line.backend == "audio_only"
        and line.video_file is None
        and line.audio_file == str(wav)
    )
    assert any("timed out" in n for n in line.notes)
    assert _FakeFal.handles and _FakeFal.handles[0].cancelled


def test_wav_is_never_sent_to_did_as_text(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """D-ID gets our audio (upload + script type audio), never text for its own voice."""
    seen: dict[str, Any] = {}

    class FakeDID:
        async def upload_audio(self, p: Path) -> str:
            seen["uploaded"] = p.name
            return "s3://audio"

        async def generate_video(self, **kw: Any) -> str:
            seen.update(kw)
            return str(kw["output_path"])

    import video_providers.did_video as did

    monkeypatch.setattr(did, "DIDVideoProvider", FakeDID)
    monkeypatch.setenv("DID_API_KEY", "k")
    monkeypatch.setenv("PA250_DID", "1")
    wav = _wav(tmp_path / "line.wav", _speech([1.0]))
    out = asyncio.run(
        avatar._did_video(str(wav), tmp_path / "p.jpg", tmp_path / "o.mp4")
    )
    assert (
        out
        and seen["uploaded"] == "line.wav"
        and seen["audio_url"] == "s3://audio"
        and "text" not in seen
    )


def test_wav_roundtrip(tmp_path: Path) -> None:
    pcm = _speech([0.5])
    p = _wav(tmp_path / "a.wav", pcm)
    back, rate, ch = fts._read_wav(str(p))
    assert rate == RATE and ch == 1 and np.array_equal(back, pcm)
    with wave.open(str(p)) as w:
        assert w.getsampwidth() == 2


def test_voice_first_uses_only_cached_video(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Default (no PA250_VIDEO_WAIT): audio goes out at once; no fal call; a cached render is used if present."""
    _offline_fal(monkeypatch, tmp_path)
    wav = _wav(tmp_path / "line.wav", _speech([2.0]))
    portrait = tmp_path / "p.jpg"
    portrait.write_bytes(b"jpeg")

    async def fake_tts(
        text: str, job_id: str, envoy_id: str = "default", previous_text: str | None = None
    ) -> tuple[str, str]:
        return str(wav), "elevenlabs"

    monkeypatch.setattr(avatar, "synthesize_line_audio", fake_tts)
    monkeypatch.setattr(avatar, "VIDEO_DIR", tmp_path)
    monkeypatch.setenv("PA250_AVATAR", "auto")
    monkeypatch.delenv("PA250_VIDEO_WAIT", raising=False)
    line = asyncio.run(avatar.render_avatar_line("x", "j1", portrait=portrait))
    assert line.backend == "audio_only" and not _FakeFal.calls

    asyncio.run(
        fts.FalTalkingVideoSource().render(
            str(wav), str(portrait), str(tmp_path / "r.mp4")
        )
    )
    calls = len(_FakeFal.calls)
    line = asyncio.run(avatar.render_avatar_line("x", "j2", portrait=portrait))
    assert (
        line.backend == "fal_talking"
        and line.video_file
        and len(_FakeFal.calls) == calls
    )
