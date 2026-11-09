#!/usr/bin/env python3
"""Prepare campaign-map army illustrations from the supplied art pack (Pillow only).

Usage:
  /opt/homebrew/bin/python3 -I tools/art/prepare_army_sprites.py <pack_dir> <out_dir>

For each sprite this:
  1. Drops alpha dust (alpha < ALPHA_LO -> 0) and snaps the ~98% body alpha to opaque.
  2. Optionally keeps a horizontal slice (drops the marching sheet's lone straggler).
  3. Crops to the opaque bounding box, so no transparent padding becomes geometry
     or a hit target.
  4. Resizes in premultiplied space (no colour fringe from transparent texels).
  5. Bleeds edge colour into fully transparent texels, so filtering and mips cannot
     pull the pack's saturated red/yellow/black matte colours into a halo.
  6. Pads to a power-of-two canvas (top-left aligned) so the engine builds full mips.

Prints one line per sprite with the content rectangle as UV (for the C++ art table).

Also writes two generated textures (no source art):
  army_ground_oval.png  soft parchment oval under an army illustration, so game armies
                        separate from the soldiers printed on the 1775 engraving;
  town_wash_disc.png    soft white disc, tinted per owner in game and washed over a
                        town like period hand-colouring (marks the town, not a border).
"""

from __future__ import annotations

import math
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageFilter

ALPHA_LO = 24
ALPHA_HI = 232
MAX_W = 1024
MAX_H = 512
MARGIN = 4


@dataclass(frozen=True)
class Sprite:
    src: str
    out: str
    # Optional [x0, x1) slice of the source (source pixels), applied before the crop.
    keep_x: tuple[int, int] | None = None


SPRITES = (
    # Despite its file name, this sheet shows provincial regulars (per the supplier).
    Sprite("Provincial_Militia.png", "army_provincial_regulars.png"),
    # Opaque columns: 225-1534 (the column), 1576-1805 (a lone straggler); cut the gap.
    Sprite("Marching_Infantry.png", "army_british_marching.png", keep_x=(0, 1555)),
    Sprite("Firing_Infantry.png", "army_british_firing.png"),
)


def clean_alpha(im: Image.Image) -> Image.Image:
    r, g, b, a = im.split()
    a = a.point(lambda v: 0 if v < ALPHA_LO else (255 if v >= ALPHA_HI else v))
    return Image.merge("RGBA", (r, g, b, a))


def premultiply(im: Image.Image) -> Image.Image:
    px = list(im.getdata())
    out = [(r * a // 255, g * a // 255, b * a // 255, a) for r, g, b, a in px]
    res = Image.new("RGBA", im.size)
    res.putdata(out)
    return res


def unpremultiply(im: Image.Image) -> Image.Image:
    out = []
    for r, g, b, a in im.getdata():
        if a == 0:
            out.append((0, 0, 0, 0))
        else:
            out.append(
                (
                    min(255, r * 255 // a),
                    min(255, g * 255 // a),
                    min(255, b * 255 // a),
                    a,
                )
            )
    res = Image.new("RGBA", im.size)
    res.putdata(out)
    return res


def bleed(im: Image.Image, passes: int = 6) -> Image.Image:
    """Fill RGB of alpha==0 texels with nearby opaque colour; alpha is untouched."""
    w, h = im.size
    data = list(im.getdata())
    filled = [a > 0 for (_, _, _, a) in data]
    for k in range(passes):
        radius = 2 ** (k + 1)
        weight = Image.new("L", (w, h))
        weight.putdata([255 if f else 0 for f in filled])
        pre = Image.new("RGB", (w, h))
        pre.putdata(
            [
                (r, g, b) if f else (0, 0, 0)
                for (r, g, b, _), f in zip(data, filled, strict=True)
            ]
        )
        pre_b = list(pre.filter(ImageFilter.BoxBlur(radius)).getdata())
        wt_b = list(weight.filter(ImageFilter.BoxBlur(radius)).getdata())
        new_filled = filled[:]
        for i, f in enumerate(filled):
            if f:
                continue
            wv = wt_b[i]
            if wv == 0:
                continue
            r, g, b = pre_b[i]
            s = 255.0 / wv
            data[i] = (
                min(255, int(r * s)),
                min(255, int(g * s)),
                min(255, int(b * s)),
                data[i][3],
            )
            new_filled[i] = True
        filled = new_filled
        if all(filled):
            break
    # Anything still unreached takes the mean opaque colour.
    opaque = [(r, g, b) for (r, g, b, a) in data if a > 0]
    if opaque:
        mr = sum(p[0] for p in opaque) // len(opaque)
        mg = sum(p[1] for p in opaque) // len(opaque)
        mb = sum(p[2] for p in opaque) // len(opaque)
        data = [
            d if f else (mr, mg, mb, d[3]) for d, f in zip(data, filled, strict=True)
        ]
    res = Image.new("RGBA", (w, h))
    res.putdata(data)
    return res


def process(pack: Path, out_dir: Path, sp: Sprite) -> str:
    src = Image.open(pack / sp.src).convert("RGBA")
    im = clean_alpha(src)
    if sp.keep_x:
        x0, x1 = sp.keep_x
        mask = Image.new("L", im.size, 0)
        mask.paste(255, (x0, 0, x1, im.height))
        r, g, b, a = im.split()
        a = Image.composite(a, Image.new("L", im.size, 0), mask)
        im = Image.merge("RGBA", (r, g, b, a))
    bbox = im.getchannel("A").getbbox()
    if bbox is None:
        raise SystemExit(f"{sp.src}: no opaque pixels")
    x0, y0, x1, y1 = bbox
    x0, y0 = max(0, x0 - MARGIN), max(0, y0 - MARGIN)
    x1, y1 = min(im.width, x1 + MARGIN), min(im.height, y1 + MARGIN)
    im = im.crop((x0, y0, x1, y1))
    cw, ch = im.size
    scale = min(MAX_W / cw, MAX_H / ch, 1.0)
    tw, th = max(1, round(cw * scale)), max(1, round(ch * scale))
    im = unpremultiply(premultiply(im).resize((tw, th), Image.LANCZOS))
    # Resampling can re-introduce faint alpha; drop it again so no dust survives.
    im = clean_alpha(im)
    canvas = Image.new("RGBA", (MAX_W, MAX_H), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    canvas = bleed(canvas)
    out_dir.mkdir(parents=True, exist_ok=True)
    canvas.save(out_dir / sp.out, optimize=True)
    return (
        f"{sp.out}: src_bbox=({x0},{y0},{x1},{y1}) content={tw}x{th} "
        f"canvas={MAX_W}x{MAX_H} "
        f"uv_rect=({tw / MAX_W:.5f},{th / MAX_H:.5f}) aspect={tw / th:.4f}"
    )


def ground_oval(out_dir: Path, w: int = 256, h: int = 128) -> str:
    """Parchment oval; alpha 1 inside 55% of the radius easing to 0 at the rim."""
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            dx = (x + 0.5 - w / 2) / (w / 2)
            dy = (y + 0.5 - h / 2) / (h / 2)
            t = max(0.0, min(1.0, (1.0 - math.hypot(dx, dy)) / 0.45))
            px[x, y] = (236, 224, 194, round(255 * t * t * (3 - 2 * t)))
    out_dir.mkdir(parents=True, exist_ok=True)
    im.save(out_dir / "army_ground_oval.png", optimize=True)
    return f"army_ground_oval.png: generated {w}x{h}"


def town_wash_disc(out_dir: Path, n: int = 256) -> str:
    """White disc; alpha 1 inside 45% of the radius easing to 0 at the rim."""
    im = Image.new("RGBA", (n, n))
    px = im.load()
    for y in range(n):
        for x in range(n):
            r = math.hypot((x + 0.5 - n / 2) / (n / 2), (y + 0.5 - n / 2) / (n / 2))
            t = max(0.0, min(1.0, (1.0 - r) / 0.55))
            px[x, y] = (255, 255, 255, round(255 * t * t * (3 - 2 * t)))
    out_dir.mkdir(parents=True, exist_ok=True)
    im.save(out_dir / "town_wash_disc.png", optimize=True)
    return f"town_wash_disc.png: generated {n}x{n}"


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    pack, out_dir = Path(argv[1]), Path(argv[2])
    for sp in SPRITES:
        print(process(pack, out_dir, sp))
    print(ground_oval(out_dir))
    print(town_wash_disc(out_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
