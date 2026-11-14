"""Photoreal PBR surface textures for the battle levels (reusable material library).

Each material is one OpenAI image (PA250_OPENAI_IMAGE_MODEL, default gpt-image-2.6-sunburst)
of a plain surface with no structure larger than the tile, then made into a tiling PBR set:

  raw/<Name>.png            the model's image, kept so the processing can be rerun offline
  textures/<Name>_BaseColor.png   sRGB; lighting flattened, edges cross-blended to tile
  textures/<Name>_Normal.png      linear, OpenGL +Y (Unreal: Flip Green Channel)
  textures/<Name>_Roughness.png   linear
  textures/WaterRipple_Normal.png generated here without the API (river water)

Boards, shingles, bricks and stones are real geometry in the Blender kit
(tools/art/battlefield_kit.py); these images only carry the surface, so a tile never has
to line up with a course. Ground textures are top-down and world-aligned in Unreal.

  /opt/homebrew/bin/python3 tools/art/build_battlefield_textures.py [--only Name,...] [--force] [--process-only]

OPENAI_API_KEY comes from services/negotiation/.env and is never printed. Each call is
logged with its model.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image, ImageChops, ImageEnhance, ImageFilter, ImageMath, ImageStat

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "unreal/PlayAmerica250/SourceArt/battlefield"
MODEL = os.environ.get("PA250_OPENAI_IMAGE_MODEL", "gpt-image-2.6-sunburst")
SIZE = 1024

COMMON = (
    "Photograph for a PBR texture library: perfectly flat, evenly lit by soft overcast daylight, "
    "no shadows, no highlights, no vignette, no perspective, no border, no text, no objects. "
    "The surface fills the whole square frame edge to edge at a constant scale and the same "
    "density everywhere, so it can be tiled. Sharp focus, natural muted colours, true to a "
    "real historical New England building or field of the 1770s."
)

# name: (base roughness, normal strength, prompt). Roughness is the surface's mean value.
MATERIALS: dict[str, tuple[float, float, str]] = {
    # --- building surfaces (about 1.2 m across; grain runs left to right)
    "ClapboardWeathered": (0.86, 1.0,
        "Extreme close-up of one single unpainted pine board, weathered silver-grey to grey-brown "
        "by decades of sun and rain, raised horizontal grain running left to right, fine checks, "
        "a few small tight knots. One continuous board face only: no gaps between boards, no nails."),
    "ClapboardOchre": (0.78, 0.8,
        "Extreme close-up of one single wooden board painted with old hand-mixed yellow-ochre "
        "linseed-oil paint, matt, chalky and sun-faded, thin in places so horizontal wood grain "
        "running left to right shows through, fine hairline crazing. One continuous board face "
        "only: no gaps between boards, no nails, no bare patches larger than a coin."),
    "ClapboardRed": (0.8, 0.8,
        "Extreme close-up of one single wooden board painted with old iron-oxide red 'Spanish "
        "brown' linseed-oil paint, matt, weathered and slightly faded, horizontal wood grain "
        "running left to right showing through the thin paint, fine hairline crazing. One "
        "continuous board face only: no gaps between boards, no nails."),
    "PaintedTrim": (0.7, 0.6,
        "Extreme close-up of one single wooden board painted with old off-white lead paint, "
        "matt, slightly yellowed and weathered, faint brush marks and horizontal wood grain "
        "running left to right, a few hairline cracks. One continuous painted face only: no "
        "gaps, no nails, no large flakes."),
    "ShingleCedar": (0.9, 1.0,
        "Extreme close-up of the face of one single hand-split wooden roof shingle, weathered "
        "dark silver-grey with brown streaks, coarse riven grain running top to bottom, traces "
        "of dry lichen. One continuous wood face only: no gaps, no edges of other shingles."),
    "OakWeathered": (0.9, 1.0,
        "Extreme close-up of a hand-split chestnut fence rail, weathered grey-brown, rough riven "
        "surface with long splintery grain running left to right, small cracks, a little green "
        "lichen. One continuous wood face only: no gaps, no bark, no edges."),
    "PlankDoor": (0.75, 0.9,
        "Extreme close-up of an old hand-planed oak board, dark brown, oiled and worn, straight "
        "grain running top to bottom, plane marks, small dents. One continuous wood face only: "
        "no gaps, no nails, no hardware."),
    "Fieldstone": (0.88, 1.0,
        "Extreme close-up of the face of a New England granite fieldstone: grey with pale tan "
        "and rust mottling, fine speckled crystal texture, patches of pale grey-green crustose "
        "lichen. One continuous stone face only: no cracks between stones, no mortar, no moss "
        "clumps."),
    "BrickHandmade": (0.9, 0.9,
        "Extreme close-up of the face of one handmade wood-fired clay brick, soft orange-red "
        "with darker burnt patches, sandy pitted surface, tiny pebbles and creases from the "
        "mould. One continuous brick face only: no mortar joints, no edges of other bricks."),
    "LimeMortar": (0.95, 0.7,
        "Extreme close-up of old weathered lime mortar: pale grey-buff, coarse sand and small "
        "shell and grit inclusions, slightly crumbly. One continuous surface only: no bricks, "
        "no stones, no cracks wider than a hair."),
    # --- ground, seen straight down (about 3 m across)
    "PastureSpring": (0.93, 1.0,
        "Straight-down photograph from two metres up of a rough grazed town common in New England "
        "in mid April: short uneven new green grass coming through flattened pale tan dead thatch "
        "from last year, small bare earth specks, a few dry leaves and clover. About three "
        "metres across. No flowers, no stones larger than a thumb, no footprints, no paths."),
    "RoadDirt": (0.85, 1.0,
        "Straight-down photograph from two metres up of an unpaved 18th-century country road "
        "surface: damp packed brown earth and sandy gravel, small embedded pebbles, a few shallow "
        "hoof prints and scuffs, bits of straw. About three metres across. No wheel ruts, no "
        "puddles, no grass, no tyre tracks, no footprints of shoes."),
    "TrampledTurf": (0.9, 1.0,
        "Straight-down photograph from two metres up of trampled ground at the edge of a country "
        "road in April: scuffed dark earth with patches of short flattened grass and dead tan "
        "thatch, a few pebbles. About three metres across. No wheel ruts, no puddles, no "
        "footprints of shoes, no paths."),
    "FieldPloughed": (0.95, 1.0,
        "Straight-down photograph from two metres up of a field ploughed by oxen and harrowed "
        "last autumn, in April: weathered dark brown clods and crumbly soil, old crop stubble and "
        "scattered small stones. About three metres across. No furrow lines, no plants, no "
        "footprints, no tracks."),
}


# (saturation, brightness, contrast) applied to the base colour. The model's paint and brick
# come out more vivid than weathered 18th-century earth pigments and soft-fired brick; its
# weathered wood is darker than silvered cedar and pine read in daylight; and its pasture has
# stark white thatch that turns to noise when the tile repeats across a field.
ADJUST: dict[str, tuple[float, float, float]] = {
    "BrickHandmade": (0.62, 0.82, 1.0),
    "ClapboardOchre": (0.72, 0.9, 1.0),
    "ClapboardRed": (0.8, 0.9, 1.0),
    "ClapboardWeathered": (1.0, 1.12, 1.0),
    "ShingleCedar": (1.0, 1.3, 0.9),
    "PastureSpring": (1.35, 0.95, 0.62),
    "Fieldstone": (1.1, 0.8, 1.0),
    "TrampledTurf": (1.0, 0.95, 0.8),
    "FieldPloughed": (1.0, 1.0, 0.85),
}
# Ground is seen at a grazing angle under a low sun, where a full-strength normal map from
# luminance turns grass into crumpled foil.
NORMAL_SCALE: dict[str, float] = {"PastureSpring": 0.3, "TrampledTurf": 0.45, "FieldPloughed": 0.6, "RoadDirt": 0.55}


def water_ripple_normal(dest: Path, n: int = 512) -> None:
    """Tiling ripple normal for still river water (no API call): a sum of whole-period sines."""
    import math
    import random
    rng = random.Random(1775)
    waves = [(rng.choice((-1, 1)) * rng.randint(2, 9), rng.choice((-1, 1)) * rng.randint(2, 9),
              rng.uniform(0, 6.283), 1.0 / rng.uniform(1.0, 3.0)) for _ in range(14)]
    height = Image.new("L", (n, n))
    px = height.load()
    for y in range(n):
        for x in range(n):
            h = sum(a * math.sin(6.2832 * (fx * x + fy * y) / n + ph) for fx, fy, ph, a in waves)
            px[x, y] = max(0, min(255, int(128 + h * 22)))
    normal_from_height(height, 0.5).save(dest)
    print(f"wrote {dest.name}", flush=True)


def load_key() -> None:
    env = ROOT / "services/negotiation/.env"
    if "OPENAI_API_KEY" in os.environ or not env.exists():
        return
    for line in env.read_text().splitlines():
        if line.startswith("OPENAI_API_KEY="):
            os.environ["OPENAI_API_KEY"] = line.split("=", 1)[1].strip().strip('"').strip("'")


def generate(prompt: str, dest: Path, model: str = MODEL, size: str = f"{SIZE}x{SIZE}", **extra) -> None:
    """One image from the OpenAI Images API, saved as PNG. `extra` goes into the request body."""
    print(f"openai call: {model} -> {dest.name}", flush=True)
    body = {"model": model, "prompt": prompt, "size": size, "quality": "high", "n": 1, **extra}
    req = urllib.request.Request(
        "https://api.openai.com/v1/images/generations", data=json.dumps(body).encode(),
        headers={"Authorization": "Bearer " + os.environ["OPENAI_API_KEY"], "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as exc:
        sys.exit(f"openai {model} failed: HTTP {exc.code} {exc.read()[:400].decode(errors='replace')}")
    item = data["data"][0]
    dest.parent.mkdir(parents=True, exist_ok=True)
    if item.get("b64_json"):
        dest.write_bytes(base64.b64decode(item["b64_json"]))
    else:
        with urllib.request.urlopen(item["url"], timeout=300) as resp:
            dest.write_bytes(resp.read())


def _wrap(img: Image.Image, pad: int) -> Image.Image:
    """The tile with `pad` wrapped pixels on every side, so filters see across the seam."""
    w, h = img.size
    big = Image.new(img.mode, (w + 2 * pad, h + 2 * pad))
    for ox in (-w, 0, w):
        for oy in (-h, 0, h):
            big.paste(img, (pad + ox, pad + oy))
    return big


def _blur_wrapped(img: Image.Image, radius: float) -> Image.Image:
    pad = min(img.size[0], int(radius * 3) + 2)
    big = _wrap(img, pad).filter(ImageFilter.GaussianBlur(radius))
    return big.crop((pad, pad, pad + img.size[0], pad + img.size[1]))


def _ramp(n: int, band: float) -> list[int]:
    """0 at both borders, 255 from `band` inwards (smoothstep)."""
    out = []
    for i in range(n):
        t = min(1.0, min(i, n - 1 - i) / max(1.0, band * n))
        out.append(int(round(255 * t * t * (3 - 2 * t))))
    return out


def make_seamless(img: Image.Image, band: float = 0.14) -> Image.Image:
    """Cross-blend each axis with the half-rolled image: near a border the rolled copy (which is
    continuous there) takes over; the rolled copy's own seam lies in the middle, where the
    original is used."""
    w, h = img.size
    mx = Image.new("L", (w, 1))
    mx.putdata(_ramp(w, band))
    img = Image.composite(img, ImageChops.offset(img, w // 2, 0), mx.resize((w, h)))
    my = Image.new("L", (1, h))
    my.putdata(_ramp(h, band))
    return Image.composite(img, ImageChops.offset(img, 0, h // 2), my.resize((w, h)))


def flatten_lighting(img: Image.Image, radius_frac: float = 0.22) -> Image.Image:
    """Remove slow brightness drift (the model's leftover lighting), which is what makes a
    tile read as a repeating patch: divide by the heavily blurred luminance."""
    lum = img.convert("L")
    low = _blur_wrapped(lum, img.size[0] * radius_frac)
    mean = ImageStat.Stat(lum).mean[0]
    bands = []
    for band in img.split():
        f = ImageMath.lambda_eval(
            lambda a: a["convert"](a["min"](a["max"](a["float"](a["c"]) * mean / a["max"](a["float"](a["low"]), 8.0), 0), 255), "L"),
            c=band, low=low)
        bands.append(f)
    return Image.merge("RGB", bands)


def normal_from_height(height: Image.Image, strength: float) -> Image.Image:
    """Tangent normal (OpenGL +Y) from a height image by wrapped central differences.
    Only X and Y matter to Unreal's normal compression; Z is written as the usual 255."""
    pad = 2
    big = _wrap(height.filter(ImageFilter.GaussianBlur(0.6)), pad)
    k = 2.2 * strength
    dx = big.filter(ImageFilter.Kernel((3, 3), (0, 0, 0, k, 0, -k, 0, 0, 0), scale=1, offset=128))
    # Image rows run downwards; +Y of the normal is up the image.
    dy = big.filter(ImageFilter.Kernel((3, 3), (0, -k, 0, 0, 0, 0, 0, k, 0), scale=1, offset=128))
    w, h = height.size
    box = (pad, pad, pad + w, pad + h)
    return Image.merge("RGB", (dx.crop(box), dy.crop(box), Image.new("L", (w, h), 255)))


def process(name: str) -> None:
    rough_base, n_strength, _prompt = MATERIALS[name]
    raw = Image.open(OUT / "raw" / f"{name}.png").convert("RGB")
    if raw.size != (SIZE, SIZE):
        raw = raw.resize((SIZE, SIZE), Image.LANCZOS)
    base = make_seamless(flatten_lighting(raw))
    raw_lum = base.convert("L")  # relief from the unadjusted image
    if name in ADJUST:
        sat, val, con = ADJUST[name]
        base = ImageEnhance.Brightness(ImageEnhance.Color(base).enhance(sat)).enhance(val)
        base = ImageEnhance.Contrast(base).enhance(con)
    tex = OUT / "textures"
    tex.mkdir(parents=True, exist_ok=True)
    base.save(tex / f"{name}_BaseColor.png")
    lum = base.convert("L")
    normal_from_height(raw_lum, n_strength * NORMAL_SCALE.get(name, 1.0)).save(tex / f"{name}_Normal.png")
    # Crevices (darker than their surroundings) are rougher, worn high spots a little smoother.
    detail = ImageChops.subtract(_blur_wrapped(lum, 6), lum, scale=1, offset=128)
    rough = detail.point(lambda v: max(0, min(255, int(round(255 * rough_base + (v - 128) * 1.1)))))
    rough.save(tex / f"{name}_Roughness.png")
    # Seam check: mean absolute step across the wrap, against the mean step inside the tile.
    px = base.convert("L")
    w, h = px.size
    edge = sum(abs(px.getpixel((0, y)) - px.getpixel((w - 1, y))) for y in range(h)) / h
    inner = sum(abs(px.getpixel((w // 3, y)) - px.getpixel((w // 3 + 1, y))) for y in range(h)) / h
    print(f"processed {name}: wrap step {edge:.1f} vs interior step {inner:.1f}", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma list of material names")
    ap.add_argument("--force", action="store_true", help="regenerate images that already exist")
    ap.add_argument("--process-only", action="store_true", help="no API calls; rebuild PBR sets from raw/")
    a = ap.parse_args()
    names = [n for n in a.only.split(",") if n] or list(MATERIALS)
    unknown = [n for n in names if n not in MATERIALS]
    if unknown:
        sys.exit("unknown materials: " + ", ".join(unknown))
    load_key()
    for name in names:
        raw = OUT / "raw" / f"{name}.png"
        if not a.process_only and (a.force or not raw.exists()):
            if "OPENAI_API_KEY" not in os.environ:
                sys.exit("OPENAI_API_KEY is not set (services/negotiation/.env)")
            generate(MATERIALS[name][2] + " " + COMMON, raw)
        if not raw.exists():
            sys.exit(f"missing raw image: {raw}")
        process(name)
    if not a.only:
        water_ripple_normal(OUT / "textures" / "WaterRipple_Normal.png")


if __name__ == "__main__":
    main()
