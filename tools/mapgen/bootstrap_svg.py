#!/usr/bin/env python3
"""One-off bootstrap of source/boston_1775.svg from unreal/PA250Sim/data/boston1775.json.

Province polygons are copied verbatim from the sim JSON except for six
PROVISIONAL edits (see POLYGON_OVERRIDES) that author the missing coastline /
land bridges. After bootstrapping, the SVG is the hand-editable source of
truth: this script refuses to overwrite it unless --force is given.

The SVG never carries map_index; build_map.py reads that from the sim JSON.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SIM_JSON = ROOT / "unreal" / "PA250Sim" / "data" / "boston1775.json"
SVG_OUT = HERE / "source" / "boston_1775.svg"

VIEWBOX = (0, 0, 1000, 800)

Pt = tuple[float, float]

# PROVISIONAL polygon edits (SVG is authoritative after bootstrap).
POLYGON_OVERRIDES: dict[str, list[Pt]] = {
    # Charlestown peninsula; neck runs west along the Charles mouth into Cambridge,
    # and touches Medford at its north-west corner.
    "charlestown": [
        (598, 322), (700, 300), (760, 380), (720, 445), (640, 452),
        (560, 475), (490, 478), (492, 462), (560, 452), (620, 425),
    ],
    # Boston near-island tied to Roxbury by Boston Neck.
    "boston": [
        (700, 480), (815, 470), (870, 520), (840, 600), (800, 625),
        (795, 652), (778, 652), (776, 625), (735, 600), (700, 540),
    ],
    # Dorchester clipped so it no longer overlaps Roxbury / Boston Neck.
    "dorchester": [
        (700, 700), (760, 700), (800, 650), (822, 641), (860, 720), (700, 740),
    ],
    # Lexington clipped to Medford's north/west edges (no sliver pieces after paint order).
    "lexington": [
        (400, 30), (580, 30), (620, 215), (522, 222), (506, 300), (500, 296), (420, 200),
    ],
    # Lincoln absorbs the sim-polygon gap between Lincoln, Menotomy and Cambridge.
    "lincoln": [(240, 40), (400, 30), (420, 200), (400, 320), (280, 230)],
    # Chelsea gains a Malden strip west to Medford (sim: "Malden folded in").
    "chelsea": [
        (625, 250), (700, 265), (760, 300), (880, 280), (900, 380),
        (820, 420), (765, 375), (740, 320), (680, 288), (625, 285),
    ],
}

# Authored water shapes (decorative; raster water is "everything not province").
SEA: list[Pt] = [(0, 0), (1000, 0), (1000, 800), (0, 800)]
BACK_BAY: list[Pt] = [
    (524, 500), (488, 508), (522, 575), (650, 598), (700, 613), (770, 636),
    (772, 624), (732, 600), (696, 540), (696, 457), (640, 462), (558, 484),
]
RIVERS: dict[str, list[Pt]] = {
    "charles_river": [
        (110, 468), (200, 495), (290, 515), (385, 522), (450, 512),
        (488, 506), (524, 500),
    ],
    "mystic_river": [(600, 250), (560, 300), (545, 345)],
}

# Interior road/ferry waypoints, endpoints included (hand-placed).
EDGE_PATHS: dict[str, list[Pt]] = {
    "concord-lincoln": [(110, 240), (265, 160), (320, 130)],
    "lincoln-lexington": [(320, 130), (410, 115), (490, 150)],
    "lexington-menotomy": [(490, 150), (470, 260), (480, 320)],
    "menotomy-cambridge": [(480, 320), (440, 375), (400, 430)],
    "menotomy-medford": [(480, 320), (497, 303), (506, 300), (540, 285), (580, 270)],
    "charlestown-medford": [(680, 385), (640, 345), (615, 322), (595, 300), (580, 270)],
    "charlestown-cambridge": [
        (680, 385), (680, 430), (620, 445), (560, 463), (495, 470), (440, 455), (400, 430),
    ],
    "cambridge-watertown": [(400, 430), (340, 480), (260, 500)],
    "cambridge-brookline": [(400, 430), (403, 512), (403, 540), (400, 570)],
    "brookline-roxbury": [(400, 570), (480, 600), (580, 650)],
    "roxbury-boston": [
        (580, 650), (700, 650), (786, 648), (787, 620), (770, 580), (755, 550),
    ],
    "roxbury-dorchester": [(580, 650), (700, 690), (760, 715)],
    "boston-charlestown": [(755, 550), (762, 490), (740, 462), (715, 440), (680, 385)],
    "boston-cambridge": [
        (755, 550), (698, 520), (620, 510), (540, 495), (514, 494),
        (470, 485), (430, 460), (400, 430),
    ],
    "boston-chelsea": [(755, 550), (790, 480), (830, 445), (850, 400), (830, 360)],
    "chelsea-medford": [(830, 360), (740, 300), (660, 275), (600, 270), (580, 270)],
}

TERRAIN_FILL = {
    "woods": "#bfc79a",
    "farmland": "#e3d9a8",
    "town": "#dcc9a0",
    "hills": "#cfc39a",
    "marsh": "#c9d0b0",
}


def fmt(v: float) -> str:
    return str(int(v)) if float(v).is_integer() else f"{v:g}"


def path_d(points: list[Pt], close: bool) -> str:
    parts = [f"M{fmt(points[0][0])},{fmt(points[0][1])}"]
    parts += [f"L{fmt(x)},{fmt(y)}" for x, y in points[1:]]
    if close:
        parts.append("Z")
    return " ".join(parts)


def build_svg(sim: dict) -> str:
    vb = " ".join(str(v) for v in VIEWBOX)
    out: list[str] = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{VIEWBOX[2]}" '
        f'height="{VIEWBOX[3]}" viewBox="{vb}">'
    )
    out.append("  <!-- Play America 250: Boston 1775 map source. Coordinates are SVG user units,")
    out.append("       origin top-left, y down. Layers (group ids): provinces, water, rivers, roads.")
    out.append("       Province order is paint order (later wins on overlap), matching map_index order.")
    out.append("       map_index is read from boston1775.json by build_map.py; it is not stored here.")
    out.append("       PROVISIONAL: coastline, necks, Back Bay and rivers are authored, not traced. -->")

    out.append('  <g id="water">')
    out.append(f'    <path id="sea" d="{path_d(SEA, True)}" fill="#b9c8c4"/>')
    out.append(f'    <path id="back_bay" d="{path_d(BACK_BAY, True)}" fill="#b9c8c4"/>')
    out.append("  </g>")

    out.append('  <g id="provinces">')
    for prov in sorted(sim["provinces"], key=lambda p: p["map_index"]):
        pid = prov["id"]
        pts = POLYGON_OVERRIDES.get(pid) or [(x, y) for x, y in prov["polygon"]]
        fill = TERRAIN_FILL.get(prov["terrain"][0], "#e3d9a8")
        note = "  <!-- PROVISIONAL override -->" if pid in POLYGON_OVERRIDES else ""
        out.append(
            f'    <path id="{pid}" d="{path_d(pts, True)}" fill="{fill}" '
            f'stroke="#5a3d22" stroke-width="2"/>{note}'
        )
    out.append("  </g>")

    out.append('  <g id="rivers">')
    for rid, pts in RIVERS.items():
        out.append(
            f'    <path id="{rid}" d="{path_d(pts, False)}" fill="none" '
            f'stroke="#7f9aa6" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'
        )
    out.append("  </g>")

    out.append('  <g id="roads">')
    for edge in sim["edges"]:
        eid = edge["id"]
        pts = EDGE_PATHS[eid]
        dash = ' stroke-dasharray="10 8"' if edge["type"] in ("ferry", "boat") else ""
        out.append(
            f'    <path id="{eid}" class="{edge["type"]}" d="{path_d(pts, False)}" '
            f'fill="none" stroke="#7a4a2a" stroke-width="3"{dash}/>'
        )
    out.append("  </g>")
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true", help="overwrite existing SVG")
    args = ap.parse_args()
    if SVG_OUT.exists() and not args.force:
        print(f"{SVG_OUT} exists; refusing to overwrite (use --force)", file=sys.stderr)
        return 1
    sim = json.loads(SIM_JSON.read_text())
    missing = [e["id"] for e in sim["edges"] if e["id"] not in EDGE_PATHS]
    if missing:
        print(f"no authored path for edges: {missing}", file=sys.stderr)
        return 1
    SVG_OUT.parent.mkdir(parents=True, exist_ok=True)
    SVG_OUT.write_text(build_svg(sim))
    print(f"wrote {SVG_OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
