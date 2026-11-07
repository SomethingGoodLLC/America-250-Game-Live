#!/usr/bin/env python3
"""Render the parchment-style 1775 Boston campaign map.

Source: unreal/PA250Sim/data/boston1775.json.

Usage:
  services/negotiation/.venv/bin/python scripts/gen_campaign_map.py \\
    [--out PATH] [--preview PATH]
Needs Pillow + numpy (negotiation service venv). Deterministic (seeded).
Geography only; ownership tint, armies and markers are drawn by
UPA250MapWidget. Image space = data map space (1000x700) * SCALE, so
polygon coordinates map 1:1 after scaling.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "unreal" / "PA250Sim" / "data" / "boston1775.json"
SCALE = 2
PARCHMENT = (226, 207, 162)
LAND = (214, 192, 140)
SEA = (150, 172, 170)
SEA_DEEP = (120, 148, 152)
INK = (62, 44, 28)
FONTS = [
    "/System/Library/Fonts/Supplemental/Georgia.ttf",
    "/System/Library/Fonts/Supplemental/Baskerville.ttc",
]
FONTS_I = [
    "/System/Library/Fonts/Supplemental/Georgia Italic.ttf",
] + FONTS


def font(paths, size):
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def sc(pts):
    return [(x * SCALE, y * SCALE) for x, y in pts]


def parchment_noise(w, h, rng):
    base = rng.normal(0, 1, (h // 8 + 1, w // 8 + 1))
    scaled = ((base - base.min()) / (np.ptp(base)) * 255).astype("uint8")
    img = Image.fromarray(scaled).resize((w, h), Image.BICUBIC)
    fine = Image.fromarray(rng.integers(0, 255, (h, w), dtype="uint8"))
    mix = (
        np.asarray(img, dtype=np.float32) * 0.7
        + np.asarray(fine, dtype=np.float32) * 0.3
    )
    return (mix - mix.mean()) / 255.0


def tint(rgb, noise, amount):
    arr = np.empty(noise.shape + (3,), dtype=np.float32)
    for i in range(3):
        arr[..., i] = rgb[i] + noise * amount
    return Image.fromarray(np.clip(arr, 0, 255).astype("uint8"), "RGB")


def dashed(draw, pts, fill, width, dash=14, gap=10):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:], strict=False):
        length = math.hypot(x1 - x0, y1 - y0)
        n = max(1, int(length // (dash + gap)))
        for i in range(n + 1):
            a = i * (dash + gap) / length
            b = min(1.0, a + dash / length)
            if a >= 1:
                break
            draw.line(
                [
                    (x0 + (x1 - x0) * a, y0 + (y1 - y0) * a),
                    (x0 + (x1 - x0) * b, y0 + (y1 - y0) * b),
                ],
                fill=fill,
                width=width,
            )


def render(data: dict) -> Image.Image:
    m = data["map"]
    W, H = int(m["width"] * SCALE), int(m["height"] * SCALE)
    rng = np.random.default_rng(1775)
    noise = parchment_noise(W, H, rng)

    sea = tint(SEA, noise, 26)
    land_img = tint(LAND, noise, 30)
    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    md.polygon(sc(m["land"]), fill=255)
    for w in m.get("water", []):
        md.polygon(sc(w["points"]), fill=0)
    for r in m.get("rivers", []):
        md.line(
            sc(r["points"]),
            fill=0,
            width=int(r["width"] * SCALE),
            joint="curve",
        )
    mask = mask.filter(ImageFilter.GaussianBlur(2))
    img = Image.composite(land_img, sea, mask)

    d = ImageDraw.Draw(img, "RGBA")
    # Coastal wave hatching: offset contours as faint rings in the sea.
    for k, a in enumerate((70, 45, 25)):
        grow = mask.filter(
            ImageFilter.MaxFilter(2 * (14 + 16 * k) + 1)
        ).filter(ImageFilter.GaussianBlur(1))
        grow_arr = np.asarray(grow, dtype=np.int16) > 128
        mask_arr = np.asarray(mask, dtype=np.int16) > 128
        ring = Image.fromarray((grow_arr ^ mask_arr).astype("uint8") * a)
        img.paste(Image.new("RGB", (W, H), SEA_DEEP), (0, 0), ring)

    # Province borders (dashed) and the Neck via Boston's narrow strip.
    for p in data["provinces"]:
        pts = sc(p["polygon"])
        dashed(d, pts + [pts[0]], INK + (150,), 3)

    # Roads
    by_id = {p["id"]: p for p in data["provinces"]}
    for road in m.get("roads", []):
        line = [
            tuple(v * SCALE for v in by_id[pid]["label"]) for pid in road
        ]
        d.line(line, fill=(96, 62, 34, 220), width=5, joint="curve")
        dashed(d, line, (236, 220, 180, 230), 2, dash=10, gap=8)

    # Labels
    f_name = font(FONTS, 30)
    f_small = font(FONTS_I, 20)
    for p in data["provinces"]:
        x, y = (v * SCALE for v in p["label"])
        d.ellipse(
            [x - 7, y - 7, x + 7, y + 7],
            fill=INK + (255,),
            outline=(240, 226, 190, 255),
            width=2,
        )
        name = p["name"].upper()
        tw = d.textlength(name, font=f_name)
        d.text(
            (x - tw / 2 + 2, y + 18 + 2),
            name,
            font=f_name,
            fill=(240, 226, 190, 200),
        )
        d.text((x - tw / 2, y + 18), name, font=f_name, fill=INK + (255,))
    for w in m.get("water", []) + m.get("rivers", []):
        pts = w["points"]
        cx = sum(p[0] for p in pts) / len(pts) * SCALE
        cy = sum(p[1] for p in pts) / len(pts) * SCALE
        txt = w["name"]
        tw = d.textlength(txt, font=f_small)
        d.text(
            (cx - tw / 2, cy - 12),
            txt,
            font=f_small,
            fill=(54, 78, 92, 230),
        )
    d.text(
        (W - 360, H - 150),
        "Massachusetts Bay",
        font=font(FONTS_I, 34),
        fill=(54, 78, 92, 220),
    )
    d.text(
        (W - 280, 120),
        "Boston Harbor",
        font=font(FONTS_I, 28),
        fill=(54, 78, 92, 200),
    )

    # Title cartouche
    f_title = font(FONTS, 46)
    title = m.get("title", "Boston, 1775")
    tw = d.textlength(title, font=f_title)
    box = [30, 28, 30 + tw + 60, 28 + 84]
    d.rounded_rectangle(
        box,
        radius=10,
        fill=(234, 216, 172, 235),
        outline=INK + (255,),
        width=4,
    )
    d.rounded_rectangle(
        [box[0] + 7, box[1] + 7, box[2] - 7, box[3] - 7],
        radius=6,
        outline=INK + (200,),
        width=1,
    )
    d.text((box[0] + 30, box[1] + 15), title, font=f_title, fill=INK + (255,))

    # Compass rose
    cx, cy, r = 130, H - 130, 78
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=INK + (200,), width=3)
    for ang, ln in ((0, r + 26), (90, r), (180, r), (270, r)):
        a = math.radians(ang - 90)
        tip = (cx + math.cos(a) * ln, cy + math.sin(a) * ln)
        left = (cx + math.cos(a + 1.6) * 12, cy + math.sin(a + 1.6) * 12)
        right = (cx + math.cos(a - 1.6) * 12, cy + math.sin(a - 1.6) * 12)
        d.polygon([tip, left, right], fill=INK + (230,))
    d.text((cx - 9, cy - r - 64), "N", font=font(FONTS, 34), fill=INK + (255,))

    # Vignette + aged edges
    yy, xx = np.mgrid[0:H, 0:W]
    v = np.sqrt(
        ((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2
    )
    shade = np.clip(1 - 0.35 * np.clip(v - 0.55, 0, 1) ** 1.5, 0, 1)
    arr = np.asarray(img.convert("RGB"), dtype=np.float32) * shade[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype("uint8"), "RGB")


def main() -> None:
    ap = argparse.ArgumentParser()
    default_out = (
        ROOT
        / "unreal"
        / "PlayAmerica250"
        / "Content"
        / "PA250"
        / "Source"
        / "campaign_map.png"
    )
    default_preview = ROOT / "docs" / "preview" / "campaign_map.png"
    ap.add_argument("--out", default=str(default_out))
    ap.add_argument("--preview", default=str(default_preview))
    args = ap.parse_args()
    data = json.loads(DATA.read_text())
    img = render(data)
    preview = img.resize(
        (img.width // 2, img.height // 2), Image.LANCZOS
    )
    for target, im in ((args.out, img), (args.preview, preview)):
        Path(target).parent.mkdir(parents=True, exist_ok=True)
        im.save(target, optimize=True)
        print("wrote", target, im.size)


if __name__ == "__main__":
    main()
