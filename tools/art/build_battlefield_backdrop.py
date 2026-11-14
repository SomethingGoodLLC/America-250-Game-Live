"""Photographic horizon and ground variation for the battle levels.

Two things a tiling surface texture cannot give, each from OpenAI images
(PA250_OPENAI_IMAGE_MODEL, default gpt-image-2.6-sunburst, quality high):

  raw/Horizon_<n>.png               wood-edge photographs, sky cut out (RGBA, 2560x864)
  textures/HorizonTreeline.png      the panels joined into one strip that wraps (8192x1024,
                                    RGBA; alpha is the cut-out). Drawn on a ring round the
                                    field by build_levels.py, so the ground no longer ends in
                                    a bare skyline.
  raw/PastureAerial.png             a pasture from the air, about 55 m across
  textures/PastureMacro.png         the same as a tiling *variation* map: linear, 128 = no
                                    change. The meadow material multiplies the 3 m grass tile
                                    by it, so a field has greener and drier drifts like a
                                    real one instead of one flat tone.

  /opt/homebrew/bin/python3 tools/art/build_battlefield_backdrop.py [--force] [--process-only]

19 April 1775 followed an early spring in Middlesex (grass up, fruit trees in bloom), so the
woods are shown in first pale leaf with white pine among them. The treeline is a reading, not
a record of any one wood. OPENAI_API_KEY comes from services/negotiation/.env, never printed.
"""

from __future__ import annotations

import argparse
import os
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageMath, ImageStat

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_battlefield_textures as bt  # noqa: E402

OUT = bt.OUT
PANEL_SIZE = "2560x864"
STRIP = (8192, 1024)
OVERLAP = 220  # px of cross-fade between neighbouring panels (at panel resolution)

HORIZON_COMMON = (
    "Wide photograph, camera level and straight on from about 150 metres away across open "
    "ground, long lens so there is no perspective convergence. The wood runs straight across "
    "the whole frame from the left edge to the right edge without a gap, its foot on one level "
    "line near the bottom of the frame, and it is dense enough that no sky shows through below "
    "the crown line. Soft, even, slightly hazy daylight with no hard shadows and no sun glare. "
    "Natural muted colours, real photograph, sharp focus. No buildings, no people, no animals, "
    "no roads, no poles, no wires, no modern objects, no text. No grass foreground: the frame "
    "ends at the foot of the wood. The sky above the crowns is cut out: transparent background."
)

HORIZON: dict[str, str] = {
    "Horizon_1": "The edge of a Massachusetts woodlot in mid April: oaks, red maples and ashes in "
    "first pale yellow-green leaf, a few still bare grey crowns, three tall dark white "
    "pines standing above them, brushy undergrowth and a low dry fieldstone wall along "
    "the foot.",
    "Horizon_2": "The edge of an old New England hardwood forest in mid April: tall oaks, hickories "
    "and chestnuts with grey trunks and thin new pale-green leaves, red maple blossom "
    "haze, a tangle of brush and brambles at the foot.",
    "Horizon_3": "A stand of tall eastern white pines with a few birches and maples in early spring "
    "leaf in front of them, mid April in Massachusetts, dark green pine crowns of uneven "
    "height, a low fieldstone wall half hidden in brush along the foot.",
    "Horizon_4": "The edge of a swampy New England wood in mid April: red maples in dull red bloom, "
    "alders, willows in bright yellow-green new leaf, a few dark hemlocks, dry tan reeds "
    "and shrubs along the foot.",
    "Horizon_5": "A mixed New England wood on a low rise in mid April: oaks and maples in first pale "
    "leaf, scattered white pines, some bare grey ash crowns, crown line gently rising "
    "and falling, juniper and brush along the foot.",
    "Horizon_6": "The edge of a farm woodlot in Massachusetts in mid April: young maples, birches and "
    "oaks in fresh light green leaf with old tall oaks behind, two white pines, a dry "
    "fieldstone wall along the foot overgrown with brush.",
}

AERIAL_PROMPT = (
    "Aerial photograph looking straight down from sixty metres up at a rough, unimproved New "
    "England common pasture in mid April, the frame about fifty-five metres across. Uneven "
    "drifts of short new green grass, paler tan patches of last year's dead thatch, a few "
    "darker damp hollows, a few small scuffed bare-earth spots and faint meandering sheep "
    "tracks. The same kind of ground fills the whole square frame edge to edge. Flat overcast "
    "light, no shadows. No trees, no bushes, no rocks larger than a hand, no fences, no walls, "
    "no roads, no animals, no people, no buildings, no text. Real photograph, natural muted "
    "colours."
)


def _gen_panel(name: str, force: bool) -> None:
    raw = OUT / "raw" / f"{name}.png"
    if raw.exists() and not force:
        return
    bt.generate(
        HORIZON[name] + " " + HORIZON_COMMON,
        raw,
        size=PANEL_SIZE,
        background="transparent",
        output_format="png",
    )


def _bleed(img: Image.Image) -> Image.Image:
    """Carry edge colour out into the transparent pixels, so mips and filtering never pull
    in whatever the model left under zero alpha (a dark or pale fringe round every twig)."""
    r, g, b, a = img.split()
    solid = a.point(lambda v: 255 if v > 24 else 0)
    black = Image.new("L", img.size, 0)
    pm = [Image.composite(c, black, solid) for c in (r, g, b)]
    out = list(pm)
    filled = solid
    for radius in (2, 4, 8, 16, 32, 64, 128):
        cover = solid.filter(ImageFilter.GaussianBlur(radius))
        reach = cover.point(lambda v: 255 if v > 0 else 0)
        for i in range(3):
            blurred = pm[i].filter(ImageFilter.GaussianBlur(radius))
            fill = ImageMath.lambda_eval(
                lambda e: e["convert"](
                    e["min"](
                        e["float"](e["c"]) * 255.0 / e["max"](e["float"](e["m"]), 1.0),
                        255,
                    ),
                    "L",
                ),
                c=blurred,
                m=cover,
            )
            out[i] = Image.composite(out[i], fill, filled)
        filled = ImageChops.lighter(filled, reach)
    mean = [int(ImageStat.Stat(c, solid).mean[0]) for c in (r, g, b)]
    out = [
        Image.composite(c, Image.new("L", img.size, m), filled)
        for c, m in zip(out, mean, strict=False)
    ]
    return Image.merge("RGBA", (*out, a))


def _foot_row(alpha: Image.Image) -> int:
    """The lowest row that is still mostly opaque (the foot of the wood)."""
    w, h = alpha.size
    small = alpha.resize((64, h), Image.BOX)
    px = small.load()
    for y in range(h - 1, -1, -1):
        if sum(1 for x in range(64) if px[x, y] > 128) >= 58:
            return y
    return h - 1


def build_strip() -> None:
    panels = []
    for name in HORIZON:
        img = Image.open(OUT / "raw" / f"{name}.png").convert("RGBA")
        a = img.split()[3]
        top = next(
            (
                y
                for y in range(img.size[1])
                if a.crop((0, y, img.size[0], y + 1)).getextrema()[1] > 128
            ),
            0,
        )
        foot = _foot_row(a)
        cover = (
            sum(1 for v in a.resize((64, 64), Image.BOX).getdata() if v > 128) / 4096.0
        )
        print(
            f"{name}: {img.size[0]}x{img.size[1]} crown top row {top}, foot row {foot}, cover {cover:.2f}",
            flush=True,
        )
        if cover > 0.97:
            sys.exit(
                f"{name}: the sky was not cut out (opaque image); regenerate it with --force"
            )
        panels.append((img, top, foot))
    # One scale for all: every panel's foot sits on the strip's bottom edge, and the tallest
    # crown leaves a little clear sky at the top.
    ph = panels[0][0].size[1]
    foot = min(p[2] for p in panels)
    top = max(0, min(p[1] for p in panels) - 24)
    cropped = [img.crop((0, top, img.size[0], foot + 1)) for img, _t, _f in panels]
    w = cropped[0].size[0]
    h = cropped[0].size[1]
    total = len(cropped) * (w - OVERLAP)
    strip = Image.new("RGBA", (total, h), (0, 0, 0, 0))
    ramp = Image.new("L", (OVERLAP, 1))
    ramp.putdata(
        [
            int(round(255 * (t * t * (3 - 2 * t))))
            for t in ((i + 0.5) / OVERLAP for i in range(OVERLAP))
        ]
    )
    ramp = ramp.resize((OVERLAP, h))
    for i, img in enumerate(cropped):
        x0 = i * (w - OVERLAP)
        strip.paste(img.crop((OVERLAP, 0, w, h)), (x0 + OVERLAP, 0))
        # This panel's left edge fades in over the previous panel's right edge; for panel 0
        # that is the last panel, which is what makes the strip wrap.
        prev = cropped[i - 1].crop((w - OVERLAP, 0, w, h))
        strip.paste(Image.composite(img.crop((0, 0, OVERLAP, h)), prev, ramp), (x0, 0))
    out = _bleed(strip.resize(STRIP, Image.LANCZOS))
    out.save(OUT / "textures" / "HorizonTreeline.png")
    a = out.split()[3]
    edge = (
        sum(
            abs(a.getpixel((0, y)) - a.getpixel((STRIP[0] - 1, y)))
            for y in range(STRIP[1])
        )
        / STRIP[1]
    )
    inner = (
        sum(
            abs(a.getpixel((STRIP[0] // 3, y)) - a.getpixel((STRIP[0] // 3 + 1, y)))
            for y in range(STRIP[1])
        )
        / STRIP[1]
    )
    print(
        f"wrote HorizonTreeline.png {STRIP[0]}x{STRIP[1]}; crop rows {top}-{foot} of {ph}; "
        f"wrap alpha step {edge:.1f} vs interior {inner:.1f}",
        flush=True,
    )


def build_macro() -> None:
    raw = (
        Image.open(OUT / "raw" / "PastureAerial.png")
        .convert("RGB")
        .resize((1024, 1024), Image.LANCZOS)
    )
    img = bt.make_seamless(bt.flatten_lighting(raw, 0.35), band=0.18)
    # Linear-light ratio to the image mean, 0.5 = no change; fine grain removed (the 3 m tile
    # carries it) so only the drifts are left.
    img = img.filter(ImageFilter.GaussianBlur(1.2))
    lut = [((v / 255.0) ** 2.2) for v in range(256)]
    chans = []
    for c in img.split():
        data = [lut[v] for v in c.getdata()]
        mean = sum(data) / len(data)
        out = Image.new("L", c.size)
        out.putdata([max(0, min(255, int(round(127.5 * d / mean)))) for d in data])
        chans.append(out)
    res = Image.merge("RGB", chans)
    res.save(OUT / "textures" / "PastureMacro.png")
    ex = res.convert("L").getextrema()
    print(
        f"wrote PastureMacro.png 1024x1024 (linear ratio map, range {ex[0]}-{ex[1]}, 128 = neutral)",
        flush=True,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--process-only", action="store_true")
    ap.add_argument(
        "--only", default="", help="comma list of raw names to (re)generate"
    )
    a = ap.parse_args()
    bt.load_key()
    only = [n for n in a.only.split(",") if n]
    if not a.process_only:
        if "OPENAI_API_KEY" not in os.environ:
            sys.exit("OPENAI_API_KEY is not set (services/negotiation/.env)")
        jobs = [n for n in HORIZON if not only or n in only]
        with ThreadPoolExecutor(4) as pool:
            list(pool.map(lambda n: _gen_panel(n, a.force or n in only), jobs))
        aerial = OUT / "raw" / "PastureAerial.png"
        if (not only or "PastureAerial" in only) and (
            a.force or "PastureAerial" in only or not aerial.exists()
        ):
            bt.generate(AERIAL_PROMPT, aerial, size="2048x2048")
    (OUT / "textures").mkdir(parents=True, exist_ok=True)
    build_strip()
    build_macro()


if __name__ == "__main__":
    main()
