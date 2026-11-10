"""Lip-sync flipbook for the envoy's portrait, built from talking-portrait renders we already have.

  services/negotiation/.venv/bin/python tools/art/build_envoy_lipsync_atlas.py [clip.mp4 ...]

Live speech cannot wait for a video render (fal takes 35-90 s per line), so the painting lip-syncs from a
4x4 atlas: cell 0 is the mouth at rest, cells 1..15 open progressively wider. While the envoy's WAV plays,
the game picks the cell from the voice level every 1/30 s (APA250DiplomacyGameMode::PollVoice).

Every cell shows the same head (a steady rest frame); only a feathered mouth-and-jaw patch changes, so the
head never jitters. Patches come from fal Fabric renders of this portrait, aligned to the rest frame by
phase correlation (translation). Writes unreal/PlayAmerica250/SourceArt/parlour/envoy_lipsync_atlas.png
and a preview sheet next to it.
"""

from __future__ import annotations

import sys
from pathlib import Path

import av
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
VIDEO = ROOT / "services" / "negotiation" / "generated_video"
OUT = ROOT / "unreal" / "PlayAmerica250" / "SourceArt" / "parlour"
DEFAULT_CLIPS = [VIDEO / "talking_portrait_test.mp4", VIDEO / "pa250_311ec92e_1794226338579.mp4"]

# Measured on fal Fabric 480p renders of assets/avatars/british_general_frame.jpg (864x480 frames).
W, H = 864, 480
FACE_BOX = (330, 140, 540, 400)  # x0, y0, x1, y1: region used to align heads
MOUTH = (432, 302)  # mouth centre
APERTURE_R = (34, 22)  # ellipse radii where the open mouth (dark interior or teeth) is counted
PATCH_R = (62, 54)  # ellipse radii of the composited mouth+jaw patch
PATCH_FEATHER = 10.0
CROP_X = (round(W * 0.29), round(W * 0.71))  # the material's 3:4 crop (PORTRAIT_CROP 0.42)
COLS = ROWS = 4


def load(clips: list[Path]) -> np.ndarray:
    frames = []
    for c in clips:
        with av.open(str(c)) as f:
            frames += [fr.to_ndarray(format="rgb24") for fr in f.decode(video=0)]
    return np.stack(frames)


def gray(a: np.ndarray) -> np.ndarray:
    return a.astype(np.float32) @ np.array([0.299, 0.587, 0.114], dtype=np.float32)


def shift_to(ref: np.ndarray, img: np.ndarray) -> tuple[int, int]:
    """Integer (dy, dx) that moves img onto ref, by phase correlation over the face box."""
    x0, y0, x1, y1 = FACE_BOX
    win = np.outer(np.hanning(y1 - y0), np.hanning(x1 - x0)).astype(np.float32)
    a, b = gray(ref[y0:y1, x0:x1]) * win, gray(img[y0:y1, x0:x1]) * win
    r = np.fft.fft2(a) * np.conj(np.fft.fft2(b))
    corr = np.fft.ifft2(r / (np.abs(r) + 1e-6)).real
    dy, dx = np.unravel_index(np.argmax(corr), corr.shape)
    hh, ww = corr.shape
    return (dy - hh if dy > hh // 2 else dy), (dx - ww if dx > ww // 2 else dx)


def aperture(img: np.ndarray, region: np.ndarray) -> float:
    """Open-mouth area: pixels that are neither lip nor skin (both saturated warm): dark interior or pale teeth."""
    rgb = img.astype(np.float32) / 255.0
    v = rgb.max(axis=2)
    s = (v - rgb.min(axis=2)) / np.maximum(v, 1e-6)
    gap = (v < 0.3) | ((s < 0.28) & (v > 0.45))
    return float(gap[region].sum())


def ellipse_mask(r: tuple[int, int], feather: float) -> np.ndarray:
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).ellipse([MOUTH[0] - r[0], MOUTH[1] - r[1], MOUTH[0] + r[0], MOUTH[1] + r[1]], fill=255)
    if feather:
        m = m.filter(ImageFilter.GaussianBlur(feather))
    return np.asarray(m, dtype=np.float32) / 255.0


def main() -> None:
    clips = [Path(p) for p in sys.argv[1:]] or DEFAULT_CLIPS
    frames = load(clips)
    print(f"{len(frames)} frames from {len(clips)} clip(s)")
    region = ellipse_mask(APERTURE_R, 0) > 0.5

    # Rest frame: the median head position with the most closed mouth.
    ref0 = frames[len(frames) // 2]
    shifts = np.array([shift_to(ref0, f) for f in frames])
    med = np.median(shifts, axis=0)
    near = np.where(np.abs(shifts - med).sum(axis=1) <= 4)[0]
    ap0 = np.array([aperture(np.roll(f, tuple(int(v) for v in (med - sh)), axis=(0, 1)), region)
                    for f, sh in zip(frames, shifts, strict=True)])
    rest_i = int(near[np.argmin(ap0[near])])
    rest = frames[rest_i]
    print(f"rest frame {rest_i} (closed mouth, median pose)")

    aligned, openness = [], []
    for f in frames:
        dy, dx = shift_to(rest, f)
        a = np.roll(f, (dy, dx), axis=(0, 1))
        aligned.append(a)
        openness.append(aperture(a, region))
    openness_arr = np.array(openness)
    order = np.argsort(openness_arr)
    lo, hi = openness_arr[rest_i], np.percentile(openness_arr, 97)
    picks = [rest_i]
    for q in np.linspace(0, 1, COLS * ROWS)[1:]:
        target = lo + q * (hi - lo)
        cand = [int(i) for i in np.argsort(np.abs(openness_arr - target)) if int(i) not in picks]
        picks.append(cand[0])
    print("openness per cell:", [round(float(openness_arr[i]), 1) for i in picks])

    patch = ellipse_mask(PATCH_R, PATCH_FEATHER)[..., None]
    cx0, cx1 = CROP_X
    cw, ch = cx1 - cx0, H
    atlas = Image.new("RGB", (cw * COLS, ch * ROWS))
    for k, i in enumerate(picks):
        cell = rest.astype(np.float32) * (1 - patch) + aligned[i].astype(np.float32) * patch
        img = Image.fromarray(np.clip(cell, 0, 255).astype(np.uint8)).crop((cx0, 0, cx1, H))
        atlas.paste(img, ((k % COLS) * cw, (k // COLS) * ch))
    OUT.mkdir(parents=True, exist_ok=True)
    atlas.save(OUT / "envoy_lipsync_atlas.png")
    print(f"wrote {OUT / 'envoy_lipsync_atlas.png'} {atlas.size} ({COLS}x{ROWS} cells of {cw}x{ch})")
    mouths = Image.new("RGB", (160 * COLS * ROWS // 2, 120 * 2))
    for k in range(COLS * ROWS):
        c = atlas.crop(((k % COLS) * cw + MOUTH[0] - cx0 - 80, (k // COLS) * ch + MOUTH[1] - 70,
                        (k % COLS) * cw + MOUTH[0] - cx0 + 80, (k // COLS) * ch + MOUTH[1] + 50))
        mouths.paste(c, ((k % (COLS * ROWS // 2)) * 160, (k // (COLS * ROWS // 2)) * 120))
    mouths.save(OUT / "envoy_lipsync_mouths_preview.png")


if __name__ == "__main__":
    main()
