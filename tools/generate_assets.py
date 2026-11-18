#!/usr/bin/env python3
"""Generate battle-feel audio/images from unreal/PA250Sim/data/generated_assets.json.

Run with the negotiation venv (python = services/negotiation/.venv/bin/python):
  python tools/generate_assets.py [--dry-run] [--force <id>] [--record-existing]

Reuses existing clients (do not copy):
  - tools/audio/gen_music.py (ElevenLabs key, SFX convert, decode/write helpers)
  - tools/art/build_battlefield_textures.py (OpenAI image generate, load_key)
  - services/negotiation TTS settings (pcm_24000, eleven_multilingual_v2) for voice

Keys: ELEVENLABS_API_KEY, OPENAI_API_KEY (env or services/negotiation/.env).
Never printed.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import sys
import wave
from datetime import date
from pathlib import Path
from types import ModuleType
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "unreal" / "PA250Sim" / "data"
CATALOG = DATA / "generated_assets.json"
MANIFEST = DATA / "asset_manifest.json"
ENV = ROOT / "services" / "negotiation" / ".env"
SRC_AUDIO = ROOT / "unreal" / "PlayAmerica250" / "SourceAudio"
GEN_TEX = ROOT / "unreal" / "PlayAmerica250" / "Content" / "Generated" / "Textures"


def _load_module(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def load_env_keys() -> None:
    if not ENV.exists():
        return
    for line in ENV.read_text().splitlines():
        if "=" not in line or line.strip().startswith("#"):
            continue
        k, v = line.split("=", 1)
        k, v = k.strip(), v.strip().strip('"').strip("'")
        if k in ("ELEVENLABS_API_KEY", "OPENAI_API_KEY") and k not in os.environ:
            os.environ[k] = v


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load_manifest() -> dict[str, Any]:
    if MANIFEST.exists():
        loaded: dict[str, Any] = json.loads(MANIFEST.read_text())
        return loaded
    return {
        "note": "Provenance for tools/generate_assets.py",
        "generated": [],
        "missing": [],
        "skipped_kinds": [],
    }


def save_manifest(m: dict[str, Any]) -> None:
    MANIFEST.write_text(json.dumps(m, indent=2) + "\n")


def write_silent_wav(path: Path, seconds: float = 0.5, sr: int = 44100) -> None:
    """Placeholder WAV when API key missing (dry assets for import wiring)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    n = int(seconds * sr)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(b"\x00\x00" * n)


def record_existing(entries: list[dict[str, Any]]) -> int:
    """Provenance for outputs whose generating run lost its manifest rows.

    (The 2026-11-16 audio run.)
    """
    manifest = load_manifest()
    have = {g.get("id") for g in manifest["generated"]}
    added = 0
    for e in entries:
        out = ROOT / e["output"]
        if e["id"] in have or not out.exists():
            continue
        settings = e.get("settings", {})
        manifest["generated"].append(
            {
                "id": e["id"],
                "kind": e["kind"],
                "model": settings.get("model_id") or settings.get("model") or "",
                "prompt": e["prompt"],
                "settings": settings,
                "output": e["output"],
                "date": date.fromtimestamp(out.stat().st_mtime).isoformat(),
                "sha256": sha256_file(out),
                "note": "recorded after the fact from the catalog (the generating "
                "run's manifest rows were lost); model and prompt are the "
                "catalog's, the file is the one on disk",
            }
        )
        added += 1
    save_manifest(manifest)
    print(f"generate_assets: recorded {added} existing outputs")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="append", default=[], help="regenerate id(s)")
    ap.add_argument(
        "--record-existing",
        action="store_true",
        help="add manifest rows (sha256, catalog prompt/model, file date) for "
        "outputs on disk that have none; no API calls",
    )
    args = ap.parse_args()

    load_env_keys()
    catalog = json.loads(CATALOG.read_text())
    entries = catalog.get("assets", [])
    if args.record_existing:
        return record_existing(entries)
    cap = int(catalog.get("max_generations_per_run", 60))
    force = set(args.force)

    el_key = bool(os.environ.get("ELEVENLABS_API_KEY"))
    oa_key = bool(os.environ.get("OPENAI_API_KEY"))

    planned_gen: list[dict[str, Any]] = []
    planned_skip: list[dict[str, Any]] = []
    for e in entries:
        out = ROOT / e["output"]
        kind = e["kind"]
        if e["id"] not in force and out.exists():
            planned_skip.append({**e, "reason": "exists"})
            continue
        if kind in ("sfx", "music", "voice") and not el_key:
            planned_skip.append({**e, "reason": "missing_ELEVENLABS_API_KEY"})
            continue
        if kind == "image" and not oa_key:
            planned_skip.append({**e, "reason": "missing_OPENAI_API_KEY"})
            continue
        planned_gen.append(e)

    print(
        f"generate_assets: {len(entries)} entries; would generate "
        f"{len(planned_gen)}, skip {len(planned_skip)}; cap={cap}"
    )
    for e in planned_gen[:20]:
        print(f"  GEN  {e['id']} -> {e['output']}")
    if len(planned_gen) > 20:
        print(f"  ... +{len(planned_gen) - 20} more")
    for e in planned_skip[:10]:
        print(f"  SKIP {e['id']} ({e.get('reason')})")
    if len(planned_skip) > 10:
        print(f"  ... +{len(planned_skip) - 10} more skips")

    if args.dry_run:
        return 0

    # Lazy-load helpers only when generating
    gen_music: ModuleType | None = None
    tex: ModuleType | None = None
    eleven: Any = None

    def eleven_client() -> Any:
        nonlocal eleven
        if eleven is None:
            from elevenlabs.client import ElevenLabs

            eleven = ElevenLabs(api_key=os.environ["ELEVENLABS_API_KEY"])
        return eleven

    manifest = load_manifest()
    if not el_key:
        if "sfx" not in manifest["skipped_kinds"]:
            manifest["skipped_kinds"].append("sfx")
        if "music" not in manifest["skipped_kinds"]:
            manifest["skipped_kinds"].append("music")
        if "voice" not in manifest["skipped_kinds"]:
            manifest["skipped_kinds"].append("voice")
    if not oa_key and "image" not in manifest["skipped_kinds"]:
        manifest["skipped_kinds"].append("image")

    generated = 0
    left: list[str] = []
    for e in planned_gen:
        if generated >= cap:
            left.append(e["id"])
            continue
        out = ROOT / e["output"]
        out.parent.mkdir(parents=True, exist_ok=True)
        kind = e["kind"]
        settings = e.get("settings", {})
        model = ""
        try:
            if kind == "sfx":
                if gen_music is None:
                    gen_music = _load_module(
                        "pa250_gen_music", ROOT / "tools" / "audio" / "gen_music.py"
                    )
                client = eleven_client()
                model = settings.get("model_id", "eleven_text_to_sound_v3")
                duration = float(settings.get("duration_seconds", 2.0))
                audio = client.text_to_sound_effects.convert(
                    text=e["prompt"],
                    duration_seconds=duration,
                    prompt_influence=float(settings.get("prompt_influence", 0.7)),
                    model_id=model,
                    output_format="mp3_44100_128",
                )
                tmp = ROOT / "tools" / "audio" / ".tmp_gen"
                tmp.mkdir(exist_ok=True)
                mp3 = tmp / f"{e['id']}.mp3"
                with mp3.open("wb") as f:
                    for chunk in audio:
                        f.write(chunk)
                x = gen_music.decode_mp3(mp3, 1)
                gen_music.write_wav(out, x)
                mp3.unlink(missing_ok=True)
            elif kind == "music":
                if gen_music is None:
                    gen_music = _load_module(
                        "pa250_gen_music", ROOT / "tools" / "audio" / "gen_music.py"
                    )
                client = eleven_client()
                model = settings.get("model_id", "music_v1")
                audio = client.music.compose(
                    prompt=e["prompt"],
                    music_length_ms=int(settings.get("music_length_ms", 30000)),
                    model_id=model,
                    output_format="mp3_44100_128",
                )
                tmp = ROOT / "tools" / "audio" / ".tmp_gen"
                tmp.mkdir(exist_ok=True)
                mp3 = tmp / f"{e['id']}.mp3"
                with mp3.open("wb") as f:
                    for chunk in audio:
                        f.write(chunk)
                x = gen_music.decode_mp3(mp3, 2)
                x = gen_music.make_loopable(x)
                gen_music.write_wav(out, x)
                mp3.unlink(missing_ok=True)
            elif kind == "voice":
                client = eleven_client()
                model = settings.get("model_id", "eleven_multilingual_v2")
                voice_id = settings["voice_id"]
                audio = client.text_to_speech.convert(
                    text=e["prompt"],
                    voice_id=voice_id,
                    model_id=model,
                    output_format="pcm_24000",
                )
                pcm = b"".join(
                    chunk if isinstance(chunk, (bytes, bytearray)) else chunk.read()
                    for chunk in audio
                )
                out.parent.mkdir(parents=True, exist_ok=True)
                with wave.open(str(out), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(24000)
                    w.writeframes(pcm)
            elif kind == "image":
                if tex is None:
                    tex = _load_module(
                        "pa250_bf_tex",
                        ROOT / "tools" / "art" / "build_battlefield_textures.py",
                    )
                    tex.load_key()
                model = settings.get(
                    "model",
                    os.environ.get(
                        "PA250_OPENAI_IMAGE_MODEL", "gpt-image-2.6-sunburst"
                    ),
                )
                size = settings.get("size", "1024x1024")
                extra = {}
                if settings.get("background") == "transparent":
                    extra["background"] = "transparent"
                tex.generate(e["prompt"], out, model=model, size=size, **extra)
            else:
                print(f"  unknown kind {kind} for {e['id']}", file=sys.stderr)
                continue
        except Exception as exc:  # noqa: BLE001 — log and continue
            print(f"  ERROR {e['id']}: {type(exc).__name__}", file=sys.stderr)
            left.append(e["id"])
            continue

        dig = sha256_file(out)
        row = {
            "id": e["id"],
            "kind": kind,
            "model": model,
            "prompt": e["prompt"],
            "settings": settings,
            "output": e["output"],
            "date": date.today().isoformat(),
            "sha256": dig,
        }
        if e.get("placeholder_note"):
            row["note"] = e["placeholder_note"]
        manifest["generated"] = [
            g for g in manifest["generated"] if g.get("id") != e["id"]
        ]
        manifest["generated"].append(row)
        save_manifest(
            manifest
        )  # after every asset: a crash or a later run cannot lose its provenance
        generated += 1
        print(f"  wrote {e['output']}")

    manifest["left_this_run"] = left
    if left:
        print(
            f"generate_assets: stopped at cap or errors; left={len(left)}: "
            f"{', '.join(left[:12])}..."
        )
    save_manifest(manifest)
    print(f"generate_assets: done generated={generated}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
