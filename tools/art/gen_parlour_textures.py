"""Procedural textures for the diplomacy parlour (original art, seeded, deterministic).

  services/negotiation/.venv/bin/python tools/art/gen_parlour_textures.py

Writes power-of-two PNGs (mips generate on import) to unreal/PlayAmerica250/SourceArt/parlour/:
  floor_planks   wide pine boards, tiles every 200 cm (world-aligned)
  wallpaper      green damask, half-drop repeat, 100 cm (world-aligned)
  paint_cream    off-white panel paint, 100 cm (world-aligned)
  mahogany       furniture / mantel wood, 100 cm (world-aligned)
  marble         fireplace surround, 100 cm (world-aligned)
  rug            Turkey-carpet style rug, mapped once (UV)
  curtain        red wool drape with folds, mapped once (UV)
  firebrick      sooty firebox brick, 100 cm (world-aligned)
  canvas         linen weave + craquelure, multiplied over the envoy's portrait (UV)
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parents[2] / "unreal" / "PlayAmerica250" / "SourceArt" / "parlour"
RNG = np.random.default_rng(1775)


def noise(size: int, cells: int, seed: int) -> np.ndarray:
    """Tileable value noise in [0, 1] (wrap-padded random grid, bicubic upsample, crop)."""
    r = np.random.default_rng(seed).random((cells, cells))
    pad = np.pad(r, 2, mode="wrap")
    img = Image.fromarray((pad * 255).astype(np.uint8)).resize(
        ((cells + 4) * size // cells,) * 2, Image.BICUBIC)
    a = np.asarray(img, dtype=np.float64) / 255.0
    o = 2 * size // cells
    return a[o:o + size, o:o + size]


def fbm(size: int, seed: int, octaves: tuple[int, ...] = (4, 8, 16, 32)) -> np.ndarray:
    acc, w = np.zeros((size, size)), 0.0
    for i, c in enumerate(octaves):
        amp = 0.5 ** i
        acc += amp * noise(size, c, seed + 31 * i)
        w += amp
    return acc / w


def save(name: str, rgb: np.ndarray) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).save(OUT / f"{name}.png")
    print("wrote", OUT / f"{name}.png")


def lerp(a: tuple[float, float, float], b: tuple[float, float, float], t: np.ndarray) -> np.ndarray:
    a_, b_ = np.array(a, dtype=np.float64), np.array(b, dtype=np.float64)
    return a_ + (b_ - a_) * t[..., None]


def floor_planks(size: int = 1024) -> None:
    boards = 10  # 20 cm boards over a 200 cm tile, running along V
    bw = size // boards
    y = np.arange(size)[:, None] / size
    img = np.zeros((size, size, 3))
    grain = fbm(size, 11, (2, 4, 64, 128))
    for b in range(boards):
        x0, x1 = b * bw, (b + 1) * bw
        # two butt joints per board at random heights (wrapping)
        joints = sorted(RNG.random(2))
        seg = np.searchsorted(joints, y[:, 0]) % 2
        tone = np.where(seg[:, None] == 0, RNG.uniform(0.85, 1.1), RNG.uniform(0.85, 1.1))
        xs = np.arange(x1 - x0)[None, :] / bw
        rings = 0.5 + 0.5 * np.sin((xs * 9 + grain[:, x0:x1] * 14 + b) * np.pi)
        t = 0.35 * rings + 0.65 * grain[:, x0:x1]
        col = lerp((120, 78, 42), (176, 126, 74), t) * tone[..., None]
        for j in joints:  # joint line
            jy = int(j * size)
            col[max(0, jy - 1):jy + 1] *= 0.45
        col[:, :2] *= 0.4  # board seam
        img[:, x0:x1] = col
    img *= (0.85 + 0.3 * fbm(size, 12, (3, 6)))[..., None]  # wear
    save("floor_planks", img)


def wallpaper(size: int = 1024) -> None:
    base = lerp((34, 62, 48), (44, 76, 58), fbm(size, 21))
    motif = Image.new("L", (size // 2, size // 2), 0)
    d = ImageDraw.Draw(motif)
    m = size // 2
    c = m // 2
    # symmetric damask flourish: central bud, leaves, scrolls (drawn on the left, mirrored)
    for (cx, cy, rx, ry) in [(c, c, m * 0.06, m * 0.16), (c, c - m * 0.2, m * 0.04, m * 0.07),
                             (c, c + m * 0.22, m * 0.05, m * 0.05)]:
        d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255)
    for k in range(6):
        a = np.pi * (0.25 + 0.1 * k)
        x, y = c - m * 0.18 * np.cos(a) - m * 0.06, c - m * 0.25 * np.sin(a - 0.6)
        d.ellipse([x - m * 0.05, y - m * 0.025, x + m * 0.05, y + m * 0.025], fill=255)
    d.arc([c - m * 0.38, c - m * 0.3, c - m * 0.02, c + m * 0.3], 100, 260, fill=255, width=max(2, m // 90))
    arr = np.asarray(motif, dtype=np.float64)
    arr = np.maximum(arr, arr[:, ::-1])  # mirror
    tile = Image.fromarray(arr.astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    full = Image.new("L", (size, size), 0)
    full.paste(tile, (0, 0))
    full.paste(tile, (m, m // 2))
    full.paste(tile, (m, -m // 2))
    mask = np.asarray(full, dtype=np.float64) / 255.0
    img = base * (1 - 0.35 * mask[..., None]) + np.array([92, 120, 88]) * (0.35 * mask[..., None])
    img *= (0.92 + 0.16 * fbm(size, 22, (32, 64)))[..., None]
    save("wallpaper", img)


def paint_cream(size: int = 512) -> None:
    n = fbm(size, 31, (4, 16, 64))
    streak = noise(size, 8, 32)[:, :1].repeat(size, 1) * 0.3 + 0.7 * n
    save("paint_cream", lerp((205, 192, 162), (226, 216, 190), streak))


def mahogany(size: int = 512) -> None:
    g = fbm(size, 41, (4, 8, 32, 128))
    # stretch along V: grain streaks run the length of the board
    g = np.asarray(Image.fromarray((g[: size // 8] * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC)) / 255.0
    x = np.arange(size)[None, :] / size
    rings = 0.5 + 0.5 * np.sin((x * 5 + g * 3) * 2 * np.pi)
    fine = fbm(size, 42, (64, 256))
    t = 0.55 * rings + 0.3 * g + 0.15 * fine
    save("mahogany", lerp((54, 22, 13), (118, 54, 30), t))


def marble(size: int = 512) -> None:
    g = fbm(size, 51, (2, 4, 8, 16, 32))
    x = np.arange(size)[None, :] / size
    y = np.arange(size)[:, None] / size
    veins = np.abs(np.sin((x * 3 + y * 2 + g * 6) * np.pi)) ** 0.2
    t = 0.75 * veins + 0.25 * fbm(size, 52, (8, 32))
    save("marble", lerp((120, 116, 110), (232, 228, 220), t))


def firebrick(size: int = 512) -> None:
    rows, cols = 8, 4
    img = np.zeros((size, size, 3))
    rh, cw = size // rows, size // cols
    soot = fbm(size, 61, (2, 4, 8))
    for r in range(rows):
        off = (cw // 2) * (r % 2)
        for k in range(-1, cols + 1):
            x0 = k * cw + off
            tone = RNG.uniform(0.7, 1.15)
            xs, xe = max(0, x0 + 3), min(size, x0 + cw - 3)
            if xs < xe:
                img[r * rh + 3:(r + 1) * rh - 3, xs:xe] = np.array([110, 50, 34]) * tone
    img[img.sum(axis=2) == 0] = (52, 46, 40)  # mortar
    img *= (0.25 + 0.75 * (1 - soot))[..., None]  # soot darkens the middle
    save("firebrick", img)


def rug(size: int = 1024) -> None:
    yy, xx = np.mgrid[0:size, 0:size] / size
    field = (128, 26, 26)
    img = np.zeros((size, size, 3)) + np.array(field)
    edge = np.minimum(np.minimum(xx, 1 - xx), np.minimum(yy, 1 - yy))
    navy, gold, ivory = np.array((28, 34, 66)), np.array((190, 140, 52)), np.array((214, 200, 168))
    img[edge < 0.11] = navy
    img[(edge < 0.13) & (edge >= 0.11)] = gold
    img[edge < 0.025] = gold
    # border lozenges
    band = (edge < 0.11) & (edge > 0.035)
    lz = (np.abs(((xx + yy) * 18) % 2 - 1) + np.abs(((xx - yy) * 18) % 2 - 1)) < 0.6
    img[band & lz] = ivory
    # central medallion (octagon) and corner spandrels
    cx, cy = np.abs(xx - 0.5), np.abs(yy - 0.5)
    oct_ = np.maximum(np.maximum(cx, cy), (cx + cy) / 1.4)
    img[(oct_ < 0.2) & (edge >= 0.13)] = navy
    img[(oct_ < 0.17)] = gold * 0.85
    img[(oct_ < 0.13)] = field
    img[(np.maximum(cx, cy) < 0.04)] = ivory
    # small field motifs
    fm = ((np.abs(((xx * 10) % 1) - 0.5) + np.abs(((yy * 10) % 1) - 0.5)) < 0.12) & (oct_ > 0.22) & (edge > 0.15)
    img[fm] = gold * 0.75
    img *= (0.82 + 0.3 * fbm(size, 71, (32, 128, 256)))[..., None]  # pile and wear
    save("rug", img)


def curtain(size: int = 512) -> None:
    x = np.arange(size)[None, :] / size
    folds = 0.5 + 0.5 * np.sin(x * 2 * np.pi * 7 + 0.8 * np.sin(x * 2 * np.pi * 3))
    shade = (0.45 + 0.55 * folds).repeat(size, 0)
    weave = 0.9 + 0.2 * fbm(size, 81, (64, 128, 256))
    save("curtain", np.array((132, 22, 28)) * (shade * weave)[..., None])


def canvas(size: int = 512) -> None:
    """Painting overlay (multiplied over the portrait): linen weave + fine craquelure, near white."""
    y, x = np.mgrid[0:size, 0:size] / size
    weave = 0.5 + 0.25 * np.sin(x * 2 * np.pi * 96) + 0.25 * np.sin(y * 2 * np.pi * 96)
    n = fbm(size, 91, (8, 32, 128))
    cells = noise(size, 24, 92)
    crack = np.clip(1 - np.abs(np.sin(cells * 2 * np.pi * 3)) * 12, 0, 1)  # thin network of lines
    v = 0.9 + 0.06 * weave + 0.04 * n - 0.1 * crack
    save("canvas", np.stack([v * 255] * 3, axis=-1))


if __name__ == "__main__":
    for f in (floor_planks, wallpaper, paint_cream, mahogany, marble, firebrick, rug, curtain, canvas):
        f()
