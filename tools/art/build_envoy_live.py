#!/usr/bin/env python3
"""The envoy as a living man: a real period portrait -> photoreal seated figure with alpha, plus closed eyes.

  services/negotiation/.venv/bin/python tools/art/build_envoy_live.py [--step live|blink|cut|all] [--force]

Source: John Singleton Copley, "General Thomas Gage" (c. 1768-88), Yale Center for British Art, CC0
(SourceArt/envoy/gage_copley_original.jpg, from Wikimedia Commons).
Steps (each output is kept; a step is skipped when its file exists unless --force):
  live  image edit (--provider openai gpt-image-2 by default; gemini or fal kontext): the painting's sitter as a photograph, seated, facing the lens, plain backdrop
        -> SourceArt/envoy/gage_live_raw.png
  blink the same edit model on the live image: only the eyes close -> gage_blink_raw.png
  cut   BiRefNet-portrait (local rembg if installed, else fal-ai/birefnet): alpha matte of the live image; the same matte is applied to the blink image, and
        colour is bled outward under the matte so warped edges never show the backdrop
        -> gage_live.png, gage_blink.png (RGBA, same size)
FAL_KEY comes from services/negotiation/.env and is never printed. Each fal call is logged with its model.
"""
from __future__ import annotations

import argparse
import io
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "unreal/PlayAmerica250/SourceArt/envoy"
SRC = OUT / "gage_copley_original.jpg"

LIVE_PROMPT = (
    "Turn this 18th-century oil painting into a photorealistic photograph of the same man, alive today in 1775. "
    "Keep his exact face, age, features and grey powdered hair tied back, and his red British general's coat with "
    "dark blue lapels and gold lace, white neck stock and buff waistcoat. He sits upright in a chair facing the "
    "camera squarely, head level and straight, looking directly into the lens, mouth closed, composed and "
    "serious. Shown from the waist up, arms relaxed at his sides with hands out of frame below, whole head and "
    "hair in frame with clear space above and at both sides. Warm candlelight from the front, soft firelight rim "
    "from behind. Real skin texture and pores, real cloth, 85mm portrait photograph, sharp focus. Plain flat "
    "uniform dark grey studio backdrop, nothing else in frame."
)
EYES = ((509, 333), (611, 326))  # eye centres in gage_live_raw.png (1024x1536); build_levels.py FACE_BRITISH
BLINK_PROMPT = (
    "The same photograph, unchanged in every way, except that both of his eyes are gently closed, eyelids "
    "fully shut. Keep the head position, face, expression, mouth, hair, clothing, lighting and background "
    "exactly identical."
)


def _fal(model: str, args: dict) -> dict:
    import fal_client

    print(f"fal call: {model}", flush=True)
    return fal_client.subscribe(model, arguments=args)


def _upload(path: Path) -> str:
    import fal_client

    return fal_client.upload_file(str(path))


def _fetch(url: str, dest: Path) -> None:
    from PIL import Image

    with urllib.request.urlopen(url) as r:
        Image.open(io.BytesIO(r.read())).save(dest)
    print("wrote", dest)


OPENAI_MODEL = os.environ.get("PA250_OPENAI_IMAGE_MODEL", "gpt-image-2")
GEMINI_MODEL = os.environ.get("PA250_GEMINI_IMAGE_MODEL", "gemini-3-pro-image")


def _edit(prompt: str, src: Path, dest: Path, seed: int, provider: str) -> None:
    """One image edit: 'openai' (OPENAI_API_KEY, 2:3 output), 'gemini' (GEMINI_API_KEY) or 'fal' (flux-pro/kontext)."""
    if provider == "fal":
        r = _fal("fal-ai/flux-pro/kontext", {"prompt": prompt, "image_url": _upload(src), "aspect_ratio": "3:4",
                                             "output_format": "png", "seed": seed})
        return _fetch(r["images"][0]["url"], dest)
    import base64
    import json

    from PIL import Image

    if provider == "openai":
        from openai import OpenAI

        print(f"openai call: {OPENAI_MODEL}", flush=True)
        img = Image.open(src).convert("RGB")
        img.thumbnail((1536, 1536))
        buf = io.BytesIO()
        img.save(buf, "PNG")
        buf.name = "source.png"
        buf.seek(0)
        r = OpenAI().images.edit(model=OPENAI_MODEL, image=buf, prompt=prompt, size="1024x1536", quality="high")
        dest.write_bytes(base64.b64decode(r.data[0].b64_json))
        return print("wrote", dest)
    # REST, not the SDK: the pinned google-genai (1.38) has no image_config for the aspect ratio.
    print(f"gemini call: {GEMINI_MODEL}", flush=True)
    img = Image.open(src).convert("RGB")
    img.thumbnail((2048, 2048))
    buf = io.BytesIO()
    img.save(buf, "PNG")
    body = {"contents": [{"parts": [{"text": prompt},
                                    {"inline_data": {"mime_type": "image/png", "data": base64.b64encode(buf.getvalue()).decode()}}]}],
            "generationConfig": {"responseModalities": ["IMAGE"], "imageConfig": {"aspectRatio": "3:4", "imageSize": "2K"}}}
    req = urllib.request.Request(
        f"https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL}:generateContent",
        data=json.dumps(body).encode(), headers={"x-goog-api-key": os.environ["GEMINI_API_KEY"], "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            resp = json.load(r)
    except urllib.error.HTTPError as e:
        sys.exit(f"gemini HTTP {e.code}: {e.read().decode(errors='replace')[:400]}")
    for cand in resp.get("candidates", []):
        for part in cand.get("content", {}).get("parts", []):
            data = (part.get("inlineData") or part.get("inline_data") or {}).get("data")
            if data:
                Image.open(io.BytesIO(base64.b64decode(data))).save(dest)
                return print("wrote", dest)
    sys.exit(f"gemini returned no image: {json.dumps(resp)[:400]}")


def step_live(force: bool, seed: int, provider: str) -> None:
    dest = OUT / "gage_live_raw.png"
    if dest.exists() and not force:
        return print("kept", dest)
    _edit(LIVE_PROMPT, SRC, dest, seed, provider)


def step_blink(force: bool, seed: int, provider: str) -> None:
    dest = OUT / "gage_blink_raw.png"
    if dest.exists() and not force:
        return print("kept", dest)
    _edit(BLINK_PROMPT, OUT / "gage_live_raw.png", dest, seed, provider)


def step_cut(force: bool) -> None:
    import numpy as np
    from PIL import Image, ImageFilter

    matte_path = OUT / "gage_matte.png"
    live = Image.open(OUT / "gage_live_raw.png").convert("RGB")
    if not matte_path.exists() or force:
        try:  # local BiRefNet (pip install "rembg[cpu]"; first run downloads a 973 MB model to ~/.rembg)
            from rembg import new_session, remove

            remove(live, session=new_session("birefnet-portrait"), only_mask=True).convert("L").save(matte_path)
        except ImportError:
            r = _fal("fal-ai/birefnet/v2", {"image_url": _upload(OUT / "gage_live_raw.png"), "model": "Portrait",
                                            "output_format": "png", "refine_foreground": True})
            with urllib.request.urlopen(r["image"]["url"]) as resp:
                Image.open(io.BytesIO(resp.read())).convert("RGBA").resize(live.size).getchannel("A").save(matte_path)
        print("wrote", matte_path)
    alpha = Image.open(matte_path).convert("L")
    solid = np.asarray(alpha) > 200
    for raw, name in (("gage_live_raw.png", "gage_live.png"), ("gage_blink_raw.png", "gage_blink.png")):
        if not (OUT / raw).exists():
            print("missing", raw, "(skipped)")
            continue
        im = Image.open(OUT / raw).convert("RGB").resize(live.size)
        if raw != "gage_live_raw.png":
            # only the eyes may differ from the live image: paste feathered eye patches onto it
            from PIL import ImageDraw

            m = Image.new("L", live.size, 0)
            for ex, ey in EYES:
                ImageDraw.Draw(m).ellipse([ex - 46, ey - 30, ex + 46, ey + 28], fill=255)
            im = Image.composite(im, live, m.filter(ImageFilter.GaussianBlur(7)))
        # bleed: outside the solid figure, replace colour by a blurred copy of the figure's own colours
        rgb = np.asarray(im).astype(np.float32)
        acc, wsum = rgb * solid[..., None], solid.astype(np.float32)
        out = rgb.copy()
        filled = solid.copy()
        for radius in (2, 4, 8, 16, 32):
            a = np.asarray(Image.fromarray(np.clip(acc, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius))).astype(np.float32)
            w = np.asarray(Image.fromarray((wsum * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius))).astype(np.float32) / 255
            new = (~filled) & (w > 0.02)
            out[new] = (a / np.maximum(w, 1e-3)[..., None])[new]
            filled |= new
        rgba = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).convert("RGBA")
        rgba.putalpha(alpha)
        rgba.save(OUT / name)
        print("wrote", OUT / name, rgba.size)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--step", default="all", choices=["live", "blink", "cut", "all"])
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--seed", type=int, default=1775)
    ap.add_argument("--provider", default="openai", choices=["openai", "gemini", "fal"])
    a = ap.parse_args()
    if not SRC.exists():
        sys.exit(f"missing source portrait: {SRC}")
    env = ROOT / "services/negotiation/.env"
    if env.exists():
        for line in env.read_text().splitlines():
            for key in ("FAL_KEY", "GEMINI_API_KEY", "OPENAI_API_KEY"):
                if line.startswith(key + "=") and key not in os.environ:
                    os.environ[key] = line.split("=", 1)[1].strip().strip('"')
    if a.step in ("live", "all"):
        step_live(a.force, a.seed, a.provider)
    if a.step in ("blink", "all"):
        step_blink(a.force, a.seed, a.provider)
    if a.step in ("cut", "all"):
        step_cut(a.force)


if __name__ == "__main__":
    main()
