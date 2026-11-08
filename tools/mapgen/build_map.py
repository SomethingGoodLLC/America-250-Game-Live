#!/usr/bin/env python3
"""Play America 250 M4 mapgen: boston_1775.svg + boston1775.json -> map textures and data.

Usage:
  build_map.py            build outputs (runs --check first; aborts on errors)
  build_map.py --check    validate SVG against the sim JSON and the rasterized map (read-only)
  build_map.py --preview  write tools/mapgen/out/preview.png (parchment-wash approximation)
  build_map.py --overlay  if tools/mapgen/reference/ holds a scan, write out/overlay.png

Conventions
  * map_index comes only from the sim JSON; the SVG never stores it.
  * UV origin is top-left, v grows downward (Unreal texture space).
  * ProvinceID.png: R = map_index & 0xFF, G = map_index >> 8, B = 255 on land; water = (0,0,0).
    No anti-aliasing; later provinces in SVG/map_index order win overlaps.
  * ProvinceIdLookup.bin: uint32 LE width, uint32 LE height, then uint16 LE map_index per pixel
    (row-major, top row first; 0 = water).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as ndi
from svgelements import SVG, Close, Group, Line, Move
from svgelements import Path as SvgPath

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SIM_JSON = ROOT / "unreal" / "PA250Sim" / "data" / "boston1775.json"
SVG_PATH = HERE / "source" / "boston_1775.svg"
REFERENCE_DIR = HERE / "reference"
OUT_DIR = HERE / "out"
MAP_DIR = ROOT / "unreal" / "PlayAmerica250" / "Content" / "Map" / "Boston1775"
SOURCE_OUT = MAP_DIR / "Source"
RAW_OUT = MAP_DIR / "Raw"

LONG_SIDE = 4096
BORDER_NORM_PX = 48.0
COAST_NORM_PX = 64.0
ADJ_TOL_PX = 3  # raster adjacency tolerance (dilation iterations)
LAND_TYPES = {"road", "neck", "bridge"}
WATER_TYPES = {"ferry", "boat"}
SCHEMA = 1
SPECKLE_MAX_PX = 64  # enclosed water pockets smaller than this are rasterization seams

Point = tuple[float, float]


# --------------------------------------------------------------------------- loading


@dataclass
class SvgData:
    viewbox: tuple[float, float, float, float]
    layers: dict[str, dict[str, list[Point]]]  # group id -> element id -> polyline points


def load_sim() -> dict[str, Any]:
    with SIM_JSON.open() as f:
        return json.load(f)  # type: ignore[no-any-return]


def path_points(path: SvgPath) -> list[Point]:
    pts: list[Point] = []
    for seg in path:
        if isinstance(seg, Close):
            continue
        if isinstance(seg, (Move, Line)):
            if seg.end is not None:
                pts.append((float(seg.end.x), float(seg.end.y)))
        else:  # curves: flatten
            for i in range(1, 25):
                p = seg.point(i / 24)
                pts.append((float(p.x), float(p.y)))
    return pts


def load_svg(path: Path = SVG_PATH) -> SvgData:
    svg = SVG.parse(str(path), reify=True)
    vb = svg.viewbox
    if vb is None:
        raise SystemExit(f"{path}: missing viewBox")
    layers: dict[str, dict[str, list[Point]]] = {}

    def walk(group: Group, layer: str | None) -> None:
        for el in group:
            if isinstance(el, Group):
                gid = el.values.get("id")
                walk(el, gid if layer is None else layer)
            elif layer is not None:
                eid = el.values.get("id")
                if eid is None:
                    continue
                p = el if isinstance(el, SvgPath) else SvgPath(el)
                layers.setdefault(layer, {})[eid] = path_points(p)

    walk(svg, None)
    return SvgData((vb.x, vb.y, vb.width, vb.height), layers)


# ------------------------------------------------------------------------- raster


@dataclass
class Raster:
    width: int
    height: int
    ids: np.ndarray  # uint16 (H, W) map_index, 0 = water
    scale: tuple[float, float, float, float]  # sx, sy, vbx, vby

    def to_px(self, pt: Point) -> tuple[float, float]:
        sx, sy, vx, vy = self.scale
        return ((pt[0] - vx) * sx, (pt[1] - vy) * sy)

    def to_uv(self, pt: Point) -> tuple[float, float]:
        px, py = self.to_px(pt)
        return (px / self.width, py / self.height)


def make_raster(svg: SvgData, sim: dict[str, Any], long_side: int = LONG_SIDE) -> Raster:
    vx, vy, vw, vh = svg.viewbox
    if vw >= vh:
        width, height = long_side, max(1, round(long_side * vh / vw))
    else:
        width, height = max(1, round(long_side * vw / vh)), long_side
    sx, sy = width / vw, height / vh
    r = Raster(width, height, np.zeros((height, width), np.uint16), (sx, sy, vx, vy))
    provs = sorted(sim["provinces"], key=lambda p: p["map_index"])
    for prov in provs:
        pts = svg.layers.get("provinces", {}).get(prov["id"])
        if not pts or len(pts) < 3:
            continue
        mask_img = Image.new("L", (width, height), 0)
        ImageDraw.Draw(mask_img).polygon([r.to_px(p) for p in pts], fill=1, outline=1)
        mask = np.asarray(mask_img, dtype=bool)
        r.ids[mask] = prov["map_index"]
    despeckle(r)
    return r


def despeckle(r: Raster, max_px: int = SPECKLE_MAX_PX) -> None:
    """Assign tiny enclosed water slivers (rasterization seams) to their majority land neighbour."""
    lab, n = ndi.label(r.ids == 0)
    if n == 0:
        return
    sizes = np.bincount(lab.ravel(), minlength=n + 1)
    for comp in np.nonzero(sizes[1:] < max_px)[0] + 1:
        comp_mask = lab == comp
        bb = bbox_of(comp_mask)
        assert bb is not None
        y0, y1, x0, x1 = max(0, bb[0] - 2), min(r.height, bb[1] + 2), max(0, bb[2] - 2), min(r.width, bb[3] + 2)
        sub = comp_mask[y0:y1, x0:x1]
        ring = ndi.binary_dilation(sub, structure=np.ones((3, 3), bool)) & ~sub
        vals = r.ids[y0:y1, x0:x1][ring]
        vals = vals[vals != 0]
        if vals.size:
            counts = np.bincount(vals)
            r.ids[comp_mask] = int(np.argmax(counts))  # lowest index wins ties


def province_image(r: Raster) -> np.ndarray:
    rgb = np.zeros((r.height, r.width, 3), np.uint8)
    land = r.ids != 0
    rgb[..., 0] = (r.ids & 0xFF).astype(np.uint8)
    rgb[..., 1] = (r.ids >> 8).astype(np.uint8)
    rgb[..., 2] = np.where(land, 255, 0).astype(np.uint8)
    return rgb


def bbox_of(mask: np.ndarray) -> tuple[int, int, int, int] | None:
    ys, xs = np.nonzero(mask)
    if ys.size == 0:
        return None
    return int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1


def border_distance(r: Raster, indices: list[int]) -> tuple[np.ndarray, dict[int, np.ndarray]]:
    """Per-province distance (px) to the nearest pixel of another id (water included).

    The canvas edge is not a border for the BorderDist texture. Returns the combined float32
    map (0 on water) and per-province crops (y0, x0, dist) used for anchors, where the canvas
    edge does count as a border.
    """
    bd = np.zeros(r.ids.shape, np.float32)
    crops: dict[int, Any] = {}
    for idx in indices:
        full = r.ids == idx
        bb = bbox_of(full)
        if bb is None:
            continue
        y0, y1, x0, x1 = bb
        y0, y1, x0, x1 = max(0, y0 - 1), min(r.height, y1 + 1), max(0, x0 - 1), min(r.width, x1 + 1)
        crop = full[y0:y1, x0:x1]
        d = ndi.distance_transform_edt(crop).astype(np.float32)
        sub = bd[y0:y1, x0:x1]
        sub[crop] = d[crop]
        # anchors treat the canvas edge as a border so poles never sit on the map edge
        if y0 == 0 or x0 == 0 or y1 == r.height or x1 == r.width:
            pad = np.pad(crop, 1, constant_values=False)
            d = ndi.distance_transform_edt(pad)[1:-1, 1:-1].astype(np.float32)
        crops[idx] = (y0, x0, np.where(crop, d, 0.0).astype(np.float32))
    return bd, crops


def coast_distance(r: Raster) -> np.ndarray:
    water = r.ids == 0
    d = ndi.distance_transform_edt(water).astype(np.float32)
    d[~water] = 0.0
    return d


def to_gray8(d: np.ndarray, norm_px: float) -> np.ndarray:
    return np.rint(np.clip(d / norm_px, 0.0, 1.0) * 255.0).astype(np.uint8)


def pole_of(crop: tuple[int, int, np.ndarray], excl: tuple[float, float, float] | None = None
            ) -> tuple[int, int, float]:
    y0, x0, d = crop
    if excl is not None:
        ex, ey, er = excl
        yy, xx = np.indices(d.shape)
        d = np.where((xx + x0 - ex) ** 2 + (yy + y0 - ey) ** 2 < er * er, 0.0, d)
    k = int(np.argmax(d))
    yy2, xx2 = np.unravel_index(k, d.shape)
    return int(xx2) + x0, int(yy2) + y0, float(d[yy2, xx2])


# ------------------------------------------------------------------ presentation


def rounded(v: float) -> float:
    return round(float(v), 6)


def build_presentation(
    r: Raster, svg: SvgData, sim: dict[str, Any], crops: dict[int, Any]
) -> dict[str, Any]:
    provs: list[dict[str, Any]] = []
    for prov in sorted(sim["provinces"], key=lambda p: p["map_index"]):
        idx = prov["map_index"]
        mask = r.ids == idx
        bb = bbox_of(mask)
        if bb is None:
            continue
        y0, y1, x0, x1 = bb
        px, py, pd = pole_of(crops[idx])
        label = (px, py)
        # token anchor: best separate local pole (>= 0.8 pole radius away, >= 0.6 pole depth)
        tx, ty, td = pole_of(crops[idx], (px, py, 0.8 * pd))
        token = (tx, ty) if td >= 0.6 * pd else label
        provs.append(
            {
                "id": prov["id"],
                "map_index": idx,
                "label_anchor_uv": [rounded((label[0] + 0.5) / r.width), rounded((label[1] + 0.5) / r.height)],
                "token_anchor_uv": [rounded((token[0] + 0.5) / r.width), rounded((token[1] + 0.5) / r.height)],
                "pole_border_dist_px": rounded(pd),
                "bounds_uv": {
                    "u0": rounded(x0 / r.width),
                    "v0": rounded(y0 / r.height),
                    "u1": rounded(x1 / r.width),
                    "v1": rounded(y1 / r.height),
                },
                "area_px": int(mask.sum()),
            }
        )
    edges: list[dict[str, Any]] = []
    roads = svg.layers.get("roads", {})
    for e in sim["edges"]:
        pts = roads.get(e["id"], [])
        edges.append(
            {
                "id": e["id"],
                "a": e["a"],
                "b": e["b"],
                "type": e["type"],
                "polyline_uv": [[rounded(u), rounded(v)] for u, v in (r.to_uv(p) for p in pts)],
            }
        )
    rivers = [
        {"id": rid, "polyline_uv": [[rounded(u), rounded(v)] for u, v in (r.to_uv(p) for p in pts)]}
        for rid, pts in svg.layers.get("rivers", {}).items()
    ]
    return {
        "schema_version": SCHEMA,
        "scenario": sim["scenario"],
        "source_svg": "tools/mapgen/source/boston_1775.svg",
        "uv_origin": "top-left, v down",
        "texture_size": {"width": r.width, "height": r.height},
        "svg_viewbox": list(svg.viewbox),
        "normalization_px": {"border_dist": BORDER_NORM_PX, "coast_dist": COAST_NORM_PX},
        "provinces": provs,
        "edges": edges,
        "rivers": rivers,
    }


def lookup_bytes(r: Raster) -> bytes:
    header = np.array([r.width, r.height], dtype="<u4").tobytes()
    return header + r.ids.astype("<u2").tobytes()


def png_bytes_rgb(arr: np.ndarray) -> bytes:
    import io

    buf = io.BytesIO()
    Image.fromarray(arr, "RGB").save(buf, format="PNG", compress_level=6)
    return buf.getvalue()


def png_bytes_gray(arr: np.ndarray) -> bytes:
    import io

    buf = io.BytesIO()
    Image.fromarray(arr, "L").save(buf, format="PNG", compress_level=6)
    return buf.getvalue()


@dataclass
class Outputs:
    files: dict[Path, bytes]
    raster: Raster
    border: np.ndarray
    coast: np.ndarray
    crops: dict[int, Any]
    presentation: dict[str, Any]


def build_outputs(svg: SvgData, sim: dict[str, Any]) -> Outputs:
    r = make_raster(svg, sim)
    indices = sorted(p["map_index"] for p in sim["provinces"])
    border, crops = border_distance(r, indices)
    coast = coast_distance(r)
    pres = build_presentation(r, svg, sim, crops)
    files = {
        SOURCE_OUT / "ProvinceID.png": png_bytes_rgb(province_image(r)),
        SOURCE_OUT / "BorderDist.png": png_bytes_gray(to_gray8(border, BORDER_NORM_PX)),
        SOURCE_OUT / "CoastDist.png": png_bytes_gray(to_gray8(coast, COAST_NORM_PX)),
        RAW_OUT / "ProvinceIdLookup.bin": lookup_bytes(r),
        RAW_OUT / "map_presentation.json": (json.dumps(pres, indent=2) + "\n").encode(),
    }
    return Outputs(files, r, border, coast, crops, pres)


def write_outputs(out: Outputs) -> None:
    for path, data in out.files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"  wrote {path.relative_to(ROOT)} ({len(data):,} bytes)")


# ------------------------------------------------------------------------- checks


def sample_polyline(r: Raster, pts: list[Point]) -> list[tuple[int, int]]:
    px = [r.to_px(p) for p in pts]
    out: list[tuple[int, int]] = []
    for (ax, ay), (bx, by) in zip(px, px[1:]):
        n = max(1, int(max(abs(bx - ax), abs(by - ay))))
        for i in range(n + 1):
            t = i / n
            x = min(r.width - 1, max(0, int(round(ax + (bx - ax) * t))))
            y = min(r.height - 1, max(0, int(round(ay + (by - ay) * t))))
            out.append((x, y))
    return out


def dilate(mask: np.ndarray, it: int) -> np.ndarray:
    return ndi.binary_dilation(mask, structure=np.ones((3, 3), bool), iterations=it)


def run_checks(svg: SvgData, sim: dict[str, Any], out: Outputs | None = None) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warns: list[str] = []
    provs = {p["id"]: p for p in sim["provinces"]}
    edges = {e["id"]: e for e in sim["edges"]}

    # layers present
    for layer in ("provinces", "water", "rivers", "roads"):
        if layer not in svg.layers:
            errors.append(f"SVG missing layer group '{layer}'")

    # map_index sanity (from sim only)
    idxs = [p["map_index"] for p in sim["provinces"]]
    if len(set(idxs)) != len(idxs):
        errors.append("duplicate map_index in sim JSON")
    if any(i <= 0 or i > 0xFFFF for i in idxs):
        errors.append("map_index outside 1..65535 in sim JSON")

    # ids match sim
    svg_prov = set(svg.layers.get("provinces", {}))
    if svg_prov != set(provs):
        errors.append(
            f"province ids differ: missing in SVG {sorted(set(provs) - svg_prov)}, "
            f"extra in SVG {sorted(svg_prov - set(provs))}"
        )
    svg_roads = set(svg.layers.get("roads", {}))
    if svg_roads != set(edges):
        errors.append(
            f"road ids differ: missing in SVG {sorted(set(edges) - svg_roads)}, "
            f"extra in SVG {sorted(svg_roads - set(edges))}"
        )
    if errors:
        return errors, warns

    for pid, pts in svg.layers["provinces"].items():
        if len(pts) < 3:
            errors.append(f"province '{pid}' has <3 vertices")
    for eid, pts in svg.layers["roads"].items():
        if len(pts) < 2:
            errors.append(f"road '{eid}' has <2 points")
    if errors:
        return errors, warns

    if out is None:
        out = build_outputs(svg, sim)
    r = out.raster
    idx_of = {pid: p["map_index"] for pid, p in provs.items()}
    id_of = {v: k for k, v in idx_of.items()}

    # deviation from sim polygons (informational; SVG is authoritative after bootstrap)
    scale = (1.0, 1.0, svg.viewbox[0], svg.viewbox[1])
    vw, vh = int(svg.viewbox[2]), int(svg.viewbox[3])
    for pid, p in provs.items():
        a = Image.new("L", (vw, vh), 0)
        ImageDraw.Draw(a).polygon([(x - scale[2], y - scale[3]) for x, y in p["polygon"]], fill=1, outline=1)
        b = Image.new("L", (vw, vh), 0)
        ImageDraw.Draw(b).polygon(
            [(x - scale[2], y - scale[3]) for x, y in svg.layers["provinces"][pid]], fill=1, outline=1
        )
        am, bm = np.asarray(a, bool), np.asarray(b, bool)
        iou = float((am & bm).sum()) / max(1, float((am | bm).sum()))
        if iou < 0.4:
            errors.append(f"province '{pid}' SVG polygon deviates from sim polygon (IoU {iou:.2f})")
        elif iou < 0.999:
            warns.append(f"PROVISIONAL: '{pid}' polygon edited vs sim JSON (IoU {iou:.2f})")

    # raster component checks
    present = set(np.unique(r.ids).tolist()) - {0}
    for pid, idx in idx_of.items():
        if idx not in present:
            errors.append(f"province '{pid}' (map_index {idx}) has no pixels after rasterization")
            continue
        _, n = ndi.label(r.ids == idx)  # 4-connectivity
        if n != 1:
            errors.append(f"province '{pid}' rasterizes to {n} connected components (expected 1)")
        if int((r.ids == idx).sum()) < 2000:
            errors.append(f"province '{pid}' has implausibly few pixels")
    if present - set(idx_of.values()):
        errors.append("raster contains ids not in sim JSON")
    _, n_water = ndi.label(r.ids == 0)
    if n_water < 1:
        errors.append("no water pixels")
    elif n_water > 1:
        sizes = np.bincount(ndi.label(r.ids == 0)[0].ravel())[1:]
        big = sorted(sizes.tolist(), reverse=True)
        warns.append(f"PROVISIONAL: water has {n_water} components (px: {big[:4]}); small pockets are enclosed marsh/basin")

    # JSON label inside own raster province (informational)
    for pid, p in provs.items():
        lx, ly = r.to_px(tuple(p["label"]))  # type: ignore[arg-type]
        xi, yi = min(r.width - 1, int(lx)), min(r.height - 1, int(ly))
        if r.ids[yi, xi] != p["map_index"]:
            warns.append(f"sim label of '{pid}' falls in '{id_of.get(int(r.ids[yi, xi]), 'water')}' after overlap resolution")

    # adjacency / water contact
    water = r.ids == 0
    water_near: dict[str, bool] = {}
    for pid, idx in idx_of.items():
        m = r.ids == idx
        bb = bbox_of(m)
        assert bb is not None
        y0, y1, x0, x1 = bb
        pad = ADJ_TOL_PX + 1
        y0, y1, x0, x1 = max(0, y0 - pad), min(r.height, y1 + pad), max(0, x0 - pad), min(r.width, x1 + pad)
        water_near[pid] = bool((dilate(m[y0:y1, x0:x1], ADJ_TOL_PX) & water[y0:y1, x0:x1]).any())

    def adjacent(a: str, b: str) -> bool:
        ma = r.ids == idx_of[a]
        bb = bbox_of(ma)
        assert bb is not None
        y0, y1, x0, x1 = bb
        pad = ADJ_TOL_PX + 1
        y0, y1, x0, x1 = max(0, y0 - pad), min(r.height, y1 + pad), max(0, x0 - pad), min(r.width, x1 + pad)
        return bool((dilate(ma[y0:y1, x0:x1], ADJ_TOL_PX) & (r.ids[y0:y1, x0:x1] == idx_of[b])).any())

    for eid, e in edges.items():
        a, b, typ = e["a"], e["b"], e["type"]
        pts = svg.layers["roads"][eid]
        samples = sample_polyline(r, pts)
        owners = [int(r.ids[y, x]) for x, y in samples]
        ia, ib = idx_of[a], idx_of[b]
        if owners[0] not in (ia, ib) or owners[-1] not in (ia, ib) or owners[0] == owners[-1]:
            errors.append(
                f"edge '{eid}' endpoints land in "
                f"{id_of.get(owners[0], 'water')}/{id_of.get(owners[-1], 'water')}, expected {a}/{b}"
            )
        if typ in LAND_TYPES:
            if not adjacent(a, b):
                errors.append(f"land edge '{eid}' ({typ}): {a} and {b} are not adjacent in raster")
            stray = sorted({id_of.get(o, "water") for o in owners} - {a, b})
            if stray:
                errors.append(f"land edge '{eid}' path crosses {stray}")
        elif typ in WATER_TYPES:
            for pid in (a, b):
                if not water_near[pid]:
                    errors.append(f"{typ} edge '{eid}': province '{pid}' does not touch water")
            n_w = sum(1 for o in owners if o == 0)
            if n_w < 5:
                errors.append(f"{typ} edge '{eid}' path has only {n_w} water samples")
            stray = sorted({id_of.get(o, "water") for o in owners} - {a, b, "water"})
            if stray:
                errors.append(f"{typ} edge '{eid}' path crosses land of {stray}")
        else:
            errors.append(f"edge '{eid}' has unknown type '{typ}'")

    # Lexington must stay inland (slice requirement)
    lex_idx = idx_of.get("lexington")
    if lex_idx is not None:
        land_dist = ndi.distance_transform_edt(r.ids != 0)
        lx, ly, _ = pole_of(out.crops[lex_idx])
        if land_dist[ly, lx] < 200:
            errors.append(f"lexington pole is only {land_dist[ly, lx]:.0f}px from water; it must be inland")
    # Boston must be near-island: only land neighbour is Roxbury via the neck
    if "boston" in idx_of:
        m = r.ids == idx_of["boston"]
        ring = dilate(m, ADJ_TOL_PX) & ~m
        nbrs = sorted({id_of[int(v)] for v in np.unique(r.ids[ring]) if v != 0})
        if nbrs != ["dorchester", "roxbury"] and nbrs != ["roxbury"]:
            errors.append(f"boston land neighbours {nbrs}; expected only roxbury (neck)")

    # presentation consistency
    for p in out.presentation["provinces"]:
        for k in ("label_anchor_uv", "token_anchor_uv"):
            u, v = p[k]
            if not (0 <= u <= 1 and 0 <= v <= 1):
                errors.append(f"{p['id']} {k} outside UV range")
            elif r.ids[min(r.height - 1, int(v * r.height)), min(r.width - 1, int(u * r.width))] != p["map_index"]:
                errors.append(f"{p['id']} {k} not inside its province")

    # lookup consistency with ProvinceID image
    img = province_image(r)
    dec = img[..., 0].astype(np.uint16) | (img[..., 1].astype(np.uint16) << 8)
    dec = np.where(img[..., 2] == 255, dec, 0)
    if not np.array_equal(dec, r.ids):
        errors.append("ProvinceID image does not decode back to map_index grid")

    # on-disk outputs staleness
    for path, data in out.files.items():
        if not path.exists():
            warns.append(f"output missing (run build): {path.relative_to(ROOT)}")
        elif hashlib.sha256(path.read_bytes()).digest() != hashlib.sha256(data).digest():
            errors.append(f"output stale vs SVG/sim (re-run build): {path.relative_to(ROOT)}")
    return errors, warns


# ------------------------------------------------------------------------ preview


def load_font(size: int) -> Any:
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def noise_field(h: int, w: int, cell: int, rng: np.random.Generator) -> np.ndarray:
    small = rng.random((max(2, h // cell), max(2, w // cell))).astype(np.float32)
    img = Image.fromarray((small * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    return np.asarray(img, np.float32) / 255.0


def make_preview(out: Outputs, svg: SvgData, sim: dict[str, Any], long_side: int = 2048, labels: bool = True) -> Image.Image:
    r = out.raster
    rng = np.random.default_rng(1775)
    h, w = r.height, r.width
    land = r.ids != 0
    terrain = {p["map_index"]: p["terrain"][0] for p in sim["provinces"]}
    tint = {
        "woods": (-14, -4, -16), "farmland": (4, 4, -4), "town": (0, -6, -10),
        "hills": (-6, -6, -12), "marsh": (-12, -2, -8),
    }
    parchment = np.array([233, 217, 178], np.float32)
    img = np.broadcast_to(parchment, (h, w, 3)).copy()
    for idx, t in terrain.items():
        img[r.ids == idx] += np.array(tint.get(t, (0, 0, 0)), np.float32)

    # paper mottling (fixed seed => deterministic)
    mott = 0.55 * noise_field(h, w, 256, rng) + 0.3 * noise_field(h, w, 64, rng) + 0.15 * noise_field(h, w, 16, rng)
    img *= (0.9 + 0.2 * mott)[..., None]

    # water wash: pale grey-blue, darker toward the shore
    shore = np.clip(1.0 - out.coast / COAST_NORM_PX, 0.0, 1.0) ** 1.6
    water_base = np.array([176, 194, 190], np.float32)
    water_dark = np.array([128, 156, 160], np.float32)
    wash = water_base[None, None, :] * (1 - shore[..., None]) + water_dark[None, None, :] * shore[..., None]
    wash *= (0.94 + 0.12 * mott)[..., None]
    img = np.where(land[..., None], img, wash)

    # land: inner shading near coasts/borders (watercolor edge pooling)
    edge = np.clip(1.0 - out.border / (BORDER_NORM_PX * 0.6), 0.0, 1.0) ** 2
    img[land] *= (1.0 - 0.16 * edge[land])[..., None]

    # ink: province borders and coast line
    ink = np.array([92, 62, 36], np.float32)
    line = land & (out.border <= 2.5)
    line = dilate(line, 1) & land
    img[line] = img[line] * 0.35 + ink * 0.65

    # vignette
    yy, xx = np.indices((h, w), dtype=np.float32)
    vig = 1.0 - 0.28 * (((xx / w - 0.5) ** 2 + (yy / h - 0.5) ** 2) * 2.0) ** 1.4
    img *= vig[..., None]

    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    scale = long_side / max(w, h)
    tw, th = round(w * scale), round(h * scale)
    pil = pil.resize((tw, th), Image.LANCZOS)
    d = ImageDraw.Draw(pil)

    def px(pt: Point) -> tuple[float, float]:
        u, v = r.to_uv(pt)
        return (u * tw, v * th)

    for pts in svg.layers.get("rivers", {}).values():
        d.line([px(p) for p in pts], fill=(112, 142, 156), width=max(2, tw // 450), joint="curve")
    for e in sim["edges"]:
        pts = [px(p) for p in svg.layers["roads"][e["id"]]]
        col = (110, 66, 38) if e["type"] not in WATER_TYPES else (84, 100, 120)
        wdt = max(2, tw // 600)
        if e["type"] in WATER_TYPES:
            for a, b in zip(pts, pts[1:]):
                n = max(1, int(np.hypot(b[0] - a[0], b[1] - a[1]) / 14))
                for i in range(n):
                    if i % 2 == 0:
                        t0, t1 = i / n, (i + 1) / n
                        d.line(
                            [(a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0),
                             (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)],
                            fill=col, width=wdt,
                        )
        else:
            d.line(pts, fill=col, width=wdt, joint="curve")
    if not labels:
        # UE display texture: in-engine TextRender labels/tokens draw on top, so
        # baking them here would print every name twice.
        return pil
    font = load_font(max(12, tw // 110))
    for p in out.presentation["provinces"]:
        x, y = p["label_anchor_uv"][0] * tw, p["label_anchor_uv"][1] * th
        name = next(s["name"] for s in sim["provinces"] if s["id"] == p["id"])
        bb = d.textbbox((0, 0), name, font=font)
        d.text((x - (bb[2] - bb[0]) / 2, y - (bb[3] - bb[1]) / 2), name, fill=(58, 36, 20), font=font)
        tx, ty = p["token_anchor_uv"][0] * tw, p["token_anchor_uv"][1] * th
        rr = max(3, tw // 400)
        d.ellipse([tx - rr, ty - rr, tx + rr, ty + rr], outline=(140, 40, 30), width=2)
    return pil


def find_reference() -> Path | None:
    if not REFERENCE_DIR.is_dir():
        return None
    for ext in ("png", "jpg", "jpeg", "tif", "tiff", "webp"):
        found = sorted(REFERENCE_DIR.glob(f"*.{ext}"))
        if found:
            return found[0]
    return None


def make_overlay(out: Outputs, ref: Path, long_side: int = 2048) -> Image.Image:
    r = out.raster
    scale = long_side / max(r.width, r.height)
    tw, th = round(r.width * scale), round(r.height * scale)
    scan = Image.open(ref).convert("RGB").resize((tw, th), Image.LANCZOS)
    line = (out.border <= 2.5) & (r.ids != 0)
    line_img = Image.fromarray((line * 255).astype(np.uint8), "L").resize((tw, th), Image.BILINEAR)
    mask = np.asarray(line_img) > 40
    arr = np.asarray(scan).copy()
    arr[mask] = (0.35 * arr[mask] + 0.65 * np.array([220, 30, 30])).astype(np.uint8)
    return Image.fromarray(arr, "RGB")


# --------------------------------------------------------------------------- main


def main() -> int:
    ap = argparse.ArgumentParser(description="PA250 Boston 1775 map generator")
    ap.add_argument("--check", action="store_true", help="validate only (read-only)")
    ap.add_argument("--preview", action="store_true", help="write out/preview.png")
    ap.add_argument("--overlay", action="store_true", help="write out/overlay.png from reference/ scan")
    args = ap.parse_args()

    sim = load_sim()
    if not SVG_PATH.exists():
        print(f"missing {SVG_PATH}; run bootstrap_svg.py", file=sys.stderr)
        return 1
    svg = load_svg()

    if args.check:
        errors, warns = run_checks(svg, sim)
        for w in warns:
            print(f"WARN  {w}")
        for e in errors:
            print(f"ERROR {e}")
        print(f"mapgen --check: {'FAIL' if errors else 'PASS'} ({len(errors)} errors, {len(warns)} warnings)")
        return 1 if errors else 0

    out = build_outputs(svg, sim)
    if args.preview or args.overlay:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        if args.preview:
            p = OUT_DIR / "preview.png"
            make_preview(out, svg, sim).save(p)
            print(f"wrote {p.relative_to(ROOT)}")
            p = OUT_DIR / "preview_display.png"
            make_preview(out, svg, sim, labels=False).save(p)
            print(f"wrote {p.relative_to(ROOT)}")
        if args.overlay:
            ref = find_reference()
            if ref is None:
                print("overlay skipped: no scan in tools/mapgen/reference/")
            else:
                p = OUT_DIR / "overlay.png"
                make_overlay(out, ref).save(p)
                print(f"wrote {p.relative_to(ROOT)} (scan: {ref.name})")
        return 0

    # default: build (check first, without staleness complaints about files we are about to write)
    errors, _ = run_checks(svg, sim, out)
    errors = [e for e in errors if not e.startswith("output stale")]
    if errors:
        for e in errors:
            print(f"ERROR {e}")
        print("build aborted: fix errors (see --check)")
        return 1
    write_outputs(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
