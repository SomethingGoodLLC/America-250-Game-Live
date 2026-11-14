"""Play America 250 | colonial battlefield kit (Blender 4.5 / bpy 4.5.3, Python 3.11).

Reusable exterior buildings, fences, walls and props for the battle levels, as editable
geometry. One asset = one static mesh with named material slots and UCX collision hulls.

  blender -b --factory-startup --python tools/art/battlefield_kit.py -- [--only Name,...] [--no-render]
  <bpy venv>/bin/python tools/art/battlefield_kit.py [--only Name,...] [--no-render]

Output (unreal/PlayAmerica250/SourceArt/battlefield):
  <Asset>.blend, <Asset>.glb (visual, textures embedded), ue5/SM_<Asset>.fbx (+ UCX_ hulls),
  previews/<Asset>_{front,side,iso}.png, kit_manifest.txt (JSON: triangles, bounds, hulls, slots)

Conventions: metres, Z up, front = -Y, origin = ground level at the footprint centre.
Unreal (FBX, scene conversion on): Blender (x, y, z) m -> (100x, -100y, 100z) cm, so the
front faces +Y there.

Surfaces come from tools/art/build_battlefield_textures.py (textures/<Name>_{BaseColor,
Normal,Roughness}.png). Boards, shingles, bricks and stones are geometry; UVs are metres
divided by the material's tile size, with a random offset per piece.

History notes (what is documented, what is a reading of it) are on each recipe.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "unreal/PlayAmerica250/SourceArt/battlefield"
SEED = 1775

# name: (tile size in metres, texture grain direction 'u' | 'v' | None, flat colour, roughness)
MATERIALS = {
    "ClapboardWeathered": (0.6, "u", (0.42, 0.39, 0.35), 0.86),
    "ClapboardOchre": (0.6, "u", (0.66, 0.52, 0.27), 0.78),
    "ClapboardRed": (0.6, "u", (0.43, 0.17, 0.12), 0.80),
    "PaintedTrim": (0.6, "u", (0.80, 0.77, 0.68), 0.70),
    "ShingleCedar": (0.5, "v", (0.33, 0.31, 0.28), 0.90),
    "OakWeathered": (0.6, "u", (0.40, 0.36, 0.30), 0.90),
    "PlankDoor": (0.6, "v", (0.24, 0.16, 0.10), 0.75),
    "Fieldstone": (0.7, None, (0.45, 0.44, 0.40), 0.88),
    "BrickHandmade": (0.3, None, (0.55, 0.28, 0.20), 0.90),
    "LimeMortar": (0.5, None, (0.62, 0.59, 0.52), 0.95),
    "RoadDirt": (1.6, None, (0.30, 0.22, 0.14), 0.85),
    "Glass": (1.0, None, (0.035, 0.045, 0.05), 0.12),
    "Iron": (1.0, None, (0.05, 0.045, 0.04), 0.55),
    "DarkVoid": (1.0, None, (0.02, 0.018, 0.016), 0.95),
}
UNTEXTURED = ("Glass", "Iron", "DarkVoid")
UP = Vector((0, 0, 1))


# ----------------------------------------------------------------------------- mesh builder


class Builder:
    """Accumulates faces with explicit winding (no normal recalculation, so open lap boards
    and shingles keep their outward side) and per-piece UVs."""

    def __init__(self, name: str, seed: int):
        self.name = name
        self.rng = random.Random(seed)
        self.v: list[tuple] = []
        self.f: list[tuple] = []
        self.mi: list[int] = []
        self.uv: list[list] = []
        self.smooth: list[bool] = []
        self.slots: list[str] = []
        self.hulls: list[tuple[list, list]] = []

    def slot(self, mat: str) -> int:
        if mat not in MATERIALS:
            raise KeyError(mat)
        if mat not in self.slots:
            self.slots.append(mat)
        return self.slots.index(mat)

    def offset(self):
        return (self.rng.random() * 7.0, self.rng.random() * 7.0)

    def face(self, pts, mat, hint, uvs, smooth=False):
        """One polygon. `hint` is any vector on the outward side; winding is fixed to match."""
        pts = [Vector(p) for p in pts]
        n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
        if n.dot(Vector(hint)) < 0:
            pts.reverse()
            uvs = list(reversed(uvs))
        base = len(self.v)
        self.v.extend(tuple(p) for p in pts)
        self.f.append(tuple(range(base, base + len(pts))))
        self.mi.append(self.slot(mat))
        self.uv.append([tuple(u) for u in uvs])
        self.smooth.append(smooth)

    def _uv(self, mat, along, across, off):
        """UV for a point `along` metres down the grain and `across` metres over it."""
        tile, grain, _c, _r = MATERIALS[mat]
        if grain == "v":
            return (across / tile + off[0], along / tile + off[1])
        return (along / tile + off[0], across / tile + off[1])

    def box(self, center, size, mat, basis=None, grain=None, off=None, skip=()):
        """Oriented box. `grain` = index of the local axis the wood grain follows (default:
        the longest). `skip` = local face ids ('-x', '+z', ...) left out (hidden sides)."""
        c = Vector(center)
        ax = [Vector(a).normalized() for a in (basis or ((1, 0, 0), (0, 1, 0), (0, 0, 1)))]
        g = max(range(3), key=lambda i: size[i]) if grain is None else grain
        off = off or self.offset()
        for axis in range(3):
            i, j = [k for k in range(3) if k != axis]
            for sign in (-1, 1):
                if (("+" if sign > 0 else "-") + "xyz"[axis]) in skip:
                    continue
                pts, uvs = [], []
                for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    local = [0.0, 0.0, 0.0]
                    local[axis] = sign * size[axis] / 2
                    local[i] = a * size[i] / 2
                    local[j] = b * size[j] / 2
                    pts.append(c + ax[0] * local[0] + ax[1] * local[1] + ax[2] * local[2])
                    if g == i:
                        uvs.append(self._uv(mat, local[i], local[j] + local[axis], off))
                    elif g == j:
                        uvs.append(self._uv(mat, local[j], local[i] + local[axis], off))
                    else:  # end grain
                        uvs.append(self._uv(mat, local[i], local[j], off))
                self.face(pts, mat, ax[axis] * sign, uvs)

    def beam(self, a, b, width, depth, mat, off=None):
        """Box from a to b; grain along its length."""
        a, b = Vector(a), Vector(b)
        z = (b - a).normalized()
        helper = UP if abs(z.z) < 0.9 else Vector((0, 1, 0))
        x = helper.cross(z).normalized()
        y = z.cross(x)
        self.box((a + b) / 2, (width, depth, (b - a).length), mat, (x, y, z), grain=2, off=off)

    def convex(self, verts, faces, mat, smooth=False, off=None):
        """Convex polyhedron: outward = away from the centroid. UVs are planar by dominant axis."""
        vs = [Vector(v) for v in verts]
        cen = sum(vs, Vector()) / len(vs)
        off = off or self.offset()
        tile = MATERIALS[mat][0]
        for face in faces:
            pts = [vs[i] for i in face]
            fc = sum(pts, Vector()) / len(pts)
            n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
            if n.dot(fc - cen) < 0:
                n = -n
            k = max(range(3), key=lambda q: abs(n[q]))
            i, j = ((1, 2), (0, 2), (0, 1))[k]
            self.face(pts, mat, fc - cen, [(p[i] / tile + off[0], p[j] / tile + off[1]) for p in pts], smooth)

    def stone(self, center, half, mat="Fieldstone", jitter=0.12, yaw=None, tilt=0.0, power=None):
        """Fieldstone: a subdivided cube pushed toward a superellipsoid (48 triangles), with
        every vertex knocked about and the whole stone tipped, flat shaded so the breaks
        read as the faces of a split or weathered rock. `power` < 0.5 = blocky, 1 = round."""
        r = self.rng
        yaw = r.uniform(0, math.pi) if yaw is None else yaw
        power = r.uniform(0.38, 0.7) if power is None else power
        cy, sy = math.cos(yaw), math.sin(yaw)
        tx, ty = (r.uniform(-tilt, tilt), r.uniform(-tilt, tilt)) if tilt else (0.0, 0.0)
        skew = [1.0 + r.uniform(-jitter, jitter) for _ in range(3)]
        grid = {}
        verts, faces = [], []

        def vid(p):
            if p not in grid:
                d = Vector(p)
                e = Vector([math.copysign(abs(q) ** power, q) for q in d.normalized()])
                e = e * (1.0 + r.uniform(-jitter, jitter))
                x, y, z = e.x * half[0] * skew[0], e.y * half[1] * skew[1], e.z * half[2] * skew[2]
                z += x * tx + y * ty
                grid[p] = len(verts)
                verts.append(Vector(center) + Vector((x * cy - y * sy, x * sy + y * cy, z)))
            return grid[p]

        for axis in range(3):
            i, j = [k for k in range(3) if k != axis]
            for sign in (-1, 1):
                for a in (-1, 0):
                    for b in (-1, 0):
                        quad = []
                        for da, db in ((0, 0), (1, 0), (1, 1), (0, 1)):
                            p = [0, 0, 0]
                            p[axis] = sign
                            p[i] = a + da
                            p[j] = b + db
                            quad.append(vid(tuple(p)))
                        faces.append(quad)
        base = len(self.v)
        cen = Vector(center)
        off = self.offset()
        tile = MATERIALS[mat][0]
        self.v.extend(tuple(v) for v in verts)
        for quad in faces:
            pts = [verts[q] for q in quad]
            n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
            fc = sum(pts, Vector()) / 4
            idx = list(quad)
            if n.dot(fc - cen) < 0:
                idx.reverse()
                n = -n
            k = max(range(3), key=lambda q: abs(n[q]))
            i, j = ((1, 2), (0, 2), (0, 1))[k]
            self.f.append(tuple(base + q for q in idx))
            self.mi.append(self.slot(mat))
            self.uv.append([(verts[q][i] / tile + off[0], verts[q][j] / tile + off[1]) for q in idx])
            self.smooth.append(False)

    # collision ---------------------------------------------------------------
    def hull_box(self, center, size, basis=None):
        c = Vector(center)
        ax = [Vector(a).normalized() for a in (basis or ((1, 0, 0), (0, 1, 0), (0, 0, 1)))]
        vs = [tuple(c + ax[0] * size[0] * x / 2 + ax[1] * size[1] * y / 2 + ax[2] * size[2] * z / 2)
              for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
        self.hulls.append((vs, [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]))

    def hull_convex(self, verts, faces):
        self.hulls.append(([tuple(v) for v in verts], [tuple(f) for f in faces]))


# ----------------------------------------------------------------------------- wall helpers


class Wall:
    """A vertical wall plane from 2D point a to b; outward is to the right of a->b.
    s = metres along the wall, off = metres outward from the siding's back plane."""

    def __init__(self, a, b):
        self.a = Vector((a[0], a[1], 0))
        d = Vector((b[0] - a[0], b[1] - a[1], 0))
        self.length = d.length
        self.d = d.normalized()
        self.n = Vector((self.d.y, -self.d.x, 0))

    def p(self, s, off, z):
        return self.a + self.d * s + self.n * off + UP * z

    def box(self, b: Builder, s, off, z, size, mat, grain=None, skip=()):
        """size = (along the wall, outward depth, height)."""
        b.box(self.p(s, off, z), size, mat, (self.d, self.n, UP), grain=grain, skip=skip)


BODY_INSET = 0.07  # the solid body sits this far behind the siding's back plane (window reveal)


def _roofline(top, s):
    """Height of a polyline [(s, z), ...] at s."""
    if s <= top[0][0]:
        return top[0][1]
    for (s0, z0), (s1, z1) in zip(top, top[1:], strict=False):
        if s <= s1:
            return z0 + (z1 - z0) * (s - s0) / max(s1 - s0, 1e-9)
    return top[-1][1]


def _above(top, z, length):
    """Intervals of s where the roofline is above z."""
    cuts = [0.0, length]
    for (s0, z0), (s1, z1) in zip(top, top[1:], strict=False):
        if (z0 - z) * (z1 - z) < 0:
            cuts.append(s0 + (s1 - s0) * (z - z0) / (z1 - z0))
        cuts.append(s0)
    cuts = sorted({min(length, max(0.0, c)) for c in cuts})
    out = []
    for lo, hi in zip(cuts, cuts[1:], strict=False):
        if hi - lo > 1e-4 and _roofline(top, (lo + hi) / 2) > z:
            if out and abs(out[-1][1] - lo) < 1e-6:
                out[-1] = (out[-1][0], hi)
            else:
                out.append((lo, hi))
    return out


def _minus(intervals, holes):
    out = []
    for lo, hi in intervals:
        for h0, h1 in sorted(holes):
            if h1 <= lo or h0 >= hi:
                continue
            if h0 > lo:
                out.append((lo, h0))
            lo = max(lo, h1)
        if hi > lo:
            out.append((lo, hi))
    return [(a, b) for a, b in out if b - a > 0.02]


def clapboards(b: Builder, wall: Wall, z0, z1, mat, holes=(), top=None, exposure=0.11):
    """Lapped riven clapboards, each a tilted face and its butt, in boards of 1.1-1.7 m
    (period clapboards were short; every board gets its own piece of grain)."""
    top = top or [(0, z1), (wall.length, z1)]
    zmax = max(z for _s, z in top)
    lap, back = 0.020, 0.004
    z = z0
    while z < zmax - 0.01:
        zb = min(z + exposure, zmax)
        cuts = sorted({z, zb, *[h[k] for h in holes for k in (2, 3) if z < h[k] < zb]})
        for za, zc in zip(cuts, cuts[1:], strict=False):
            zm = (za + zc) / 2
            spans = _minus(_above(top, zm, wall.length), [(h[0], h[1]) for h in holes if h[2] < zm < h[3]])
            for lo, hi in spans:
                s = lo
                while s < hi - 1e-6:
                    e = min(hi, s + b.rng.uniform(1.1, 1.7))
                    if hi - e < 0.35:
                        e = hi
                    off = b.offset()
                    # how far out the face is at this height within the full course
                    o_lo = back + (lap - back) * (zb - za) / exposure
                    o_hi = back + (lap - back) * (zb - zc) / exposure
                    pts = [wall.p(s, o_lo, za), wall.p(e, o_lo, za), wall.p(e, o_hi, zc), wall.p(s, o_hi, zc)]
                    uvs = [b._uv(mat, q, w, off) for q, w in ((s, za), (e, za), (e, zc), (s, zc))]
                    b.face(pts, mat, wall.n, uvs)
                    if za == z:  # butt under the lower edge
                        pts = [wall.p(s, 0, za), wall.p(e, 0, za), wall.p(e, o_lo, za), wall.p(s, o_lo, za)]
                        b.face(pts, mat, -UP, [b._uv(mat, q, w, off) for q, w in ((s, 0), (e, 0), (e, o_lo), (s, o_lo))])
                    s = e
        z = zb


def vertical_boards(b: Builder, wall: Wall, z0, z1, mat, holes=(), top=None, width=0.27):
    """Barn siding: wide vertical boards with open joints, each standing slightly proud or shy."""
    top = top or [(0, z1), (wall.length, z1)]
    s = 0.0
    while s < wall.length - 1e-6:
        e = min(wall.length, s + width * b.rng.uniform(0.75, 1.2))
        if wall.length - e < 0.1:
            e = wall.length
        proud = b.rng.uniform(0.004, 0.016)
        off = b.offset()
        zs = sorted({z0, *[h[k] for h in holes for k in (2, 3) if h[0] < (s + e) / 2 < h[1]]})
        tops = (_roofline(top, s + 0.004), _roofline(top, e - 0.004))
        zs.append(None)
        for za, zc in zip(zs, zs[1:], strict=False):
            mid = ((za + (zc if zc is not None else min(tops))) / 2)
            if any(h[0] < (s + e) / 2 < h[1] and h[2] < mid < h[3] for h in holes):
                continue
            t0, t1 = (zc, zc) if zc is not None else tops
            if min(t0, t1) - za < 0.02:
                continue
            pts = [wall.p(s + 0.004, proud, za), wall.p(e - 0.004, proud, za),
                   wall.p(e - 0.004, proud, t1), wall.p(s + 0.004, proud, t0)]
            # grain runs up the board: swap the roles of s and z
            uvs = [b._uv(mat, w, q, off) for q, w in ((s, za), (e, za), (e, t1), (s, t0))]
            b.face(pts, mat, wall.n, uvs)
        s = e


def window(b: Builder, wall: Wall, s, z, ww, wh, cols=4, rows=6, trim="PaintedTrim"):
    """Double-hung sash with small panes (12-over-12 by default), plank casing and sill.
    Returns the hole (s0, s1, z0, z1) the siding must leave."""
    wall.box(b, s, -BODY_INSET + 0.012, z, (ww, 0.012, wh), "Glass")
    cas = 0.105
    depth = BODY_INSET + 0.040
    mid = (-BODY_INSET + 0.040) / 2
    for sg in (-1, 1):
        wall.box(b, s + sg * (ww / 2 + cas / 2), mid, z, (cas, depth, wh + 0.02), trim, grain=2)
    wall.box(b, s, mid, z + wh / 2 + 0.065, (ww + 2 * cas + 0.05, depth + 0.02, 0.13), trim)
    wall.box(b, s, mid + 0.03, z - wh / 2 - 0.035, (ww + 2 * cas + 0.08, depth + 0.07, 0.07), trim)
    so = -BODY_INSET + 0.035  # sash plane
    for sg in (-1, 1):
        wall.box(b, s + sg * (ww / 2 - 0.022), so, z, (0.044, 0.035, wh), trim, grain=2)
        wall.box(b, s, so, z + sg * (wh / 2 - 0.022), (ww - 0.088, 0.035, 0.044), trim)
    wall.box(b, s, so + 0.006, z, (ww - 0.088, 0.04, 0.04), trim)  # meeting rail
    for c in range(1, cols):
        wall.box(b, s - ww / 2 + ww * c / cols, so - 0.004, z, (0.02, 0.024, wh - 0.088), trim, grain=2)
    for r in range(1, rows):
        if r * 2 == rows:
            continue
        wall.box(b, s, so - 0.004, z - wh / 2 + wh * r / rows, (ww - 0.088, 0.024, 0.02), trim)
    return (s - ww / 2 - cas, s + ww / 2 + cas, z - wh / 2 - 0.07, z + wh / 2 + 0.13)


def door(b: Builder, wall: Wall, s, z0, dw, dh, trim="PaintedTrim", leaves=1, head="flat", mat="PlankDoor"):
    """Board-and-batten or panelled door with casing; `head` = flat entablature or pediment."""
    n = max(leaves, 1) * max(3, round(dw / leaves / 0.25))
    for i in range(n):
        ps = s - dw / 2 + dw * (i + 0.5) / n
        wall.box(b, ps, -BODY_INSET + 0.03, z0 + dh / 2, (dw / n - 0.006, 0.05, dh), mat, grain=2)
    for zz in (0.25, dh - 0.25, dh / 2):
        for leaf in range(leaves):
            lw = dw / leaves
            wall.box(b, s - dw / 2 + lw * (leaf + 0.5), -BODY_INSET + 0.062, z0 + zz, (lw - 0.08, 0.022, 0.12), mat)
    # wrought latch and strap hinges
    for leaf in range(leaves):
        lw = dw / leaves
        edge = s - dw / 2 + lw * leaf + (0.06 if leaf % 2 == 0 else lw - 0.06)
        for zz in (0.25, dh - 0.25):
            wall.box(b, edge + (0.17 if leaf % 2 == 0 else -0.17), -BODY_INSET + 0.078, z0 + zz, (0.4, 0.01, 0.035), "Iron")
    wall.box(b, s + (dw / 2 - 0.12 if leaves == 1 else 0.07), -BODY_INSET + 0.085, z0 + dh * 0.48, (0.035, 0.03, 0.16), "Iron")
    cas = 0.15
    depth = BODY_INSET + 0.055
    mid = (-BODY_INSET + 0.055) / 2
    for sg in (-1, 1):
        wall.box(b, s + sg * (dw / 2 + cas / 2), mid, z0 + dh / 2, (cas, depth, dh), trim, grain=2)
    top = z0 + dh
    wall.box(b, s, mid, top + 0.09, (dw + 2 * cas + 0.04, depth + 0.02, 0.18), trim)
    wall.box(b, s, mid + 0.03, top + 0.21, (dw + 2 * cas + 0.16, depth + 0.08, 0.06), trim)
    hole_top = top + 0.24
    if head == "pediment":
        half = dw / 2 + cas + 0.08
        rise = half * 0.42
        for sg in (-1, 1):
            a = wall.p(s + sg * half, mid + 0.03, top + 0.24)
            c = wall.p(s, mid + 0.03, top + 0.24 + rise)
            b.beam(a, c, 0.07, depth + 0.08, trim)
        tri = [wall.p(s - half, 0.01, top + 0.24), wall.p(s + half, 0.01, top + 0.24), wall.p(s, 0.01, top + 0.24 + rise)]
        off = b.offset()
        b.face(tri, trim, wall.n, [b._uv(trim, -half, 0, off), b._uv(trim, half, 0, off), b._uv(trim, 0, rise, off)])
        hole_top = top + 0.24 + rise + 0.05
        # flush board behind the pediment, so the siding's square cut-out is closed
        wall.box(b, s, 0.0, (top + 0.24 + hole_top) / 2, (dw + 2 * cas, 0.016, hole_top - top - 0.24), trim, skip=("-y",))
    return (s - dw / 2 - cas, s + dw / 2 + cas, z0 - 0.02, hole_top)


def corner_boards(b: Builder, pts, z0, z1, trim, width=0.13):
    for x, y in pts:
        b.box((x, y, (z0 + z1) / 2), (width + 0.03, width + 0.03, z1 - z0), trim, grain=2)


def foundation(b: Builder, w, d, h, sink=0.25):
    """Dry-laid fieldstone underpinning: one course of large stones on a dark backing."""
    b.box((0, 0, (h - sink) / 2), (w - 0.16, d - 0.16, h + sink), "DarkVoid", skip=("-z",))
    walls = [Wall((-w / 2, -d / 2), (w / 2, -d / 2)), Wall((w / 2, -d / 2), (w / 2, d / 2)),
             Wall((w / 2, d / 2), (-w / 2, d / 2)), Wall((-w / 2, d / 2), (-w / 2, -d / 2))]
    for wl in walls:
        s = 0.0
        while s < wl.length - 0.05:
            ln = min(b.rng.uniform(0.45, 0.95), wl.length - s)
            hh = h * b.rng.uniform(0.92, 1.08)
            b.stone(wl.p(s + ln / 2, -0.06, hh / 2 - 0.06), (ln / 2 + 0.01, 0.15, hh / 2 + 0.06),
                    yaw=math.atan2(wl.d.y, wl.d.x), jitter=0.07, power=0.3)
            s += ln


def chimney(b: Builder, x, y, z0, z1, sx, sy):
    """Brick stack in running bond over a mortar core, with a two-course corbelled cap."""
    b.box((x, y, (z0 + z1) / 2), (sx - 0.02, sy - 0.02, z1 - z0), "LimeMortar", skip=("-z",))
    course, blen, proud = 0.072, 0.215, 0.012
    rows = int((z1 - z0) / course)
    faces = [Wall((x - sx / 2, y - sy / 2), (x + sx / 2, y - sy / 2)), Wall((x + sx / 2, y - sy / 2), (x + sx / 2, y + sy / 2)),
             Wall((x + sx / 2, y + sy / 2), (x - sx / 2, y + sy / 2)), Wall((x - sx / 2, y + sy / 2), (x - sx / 2, y - sy / 2))]
    for r in range(rows):
        z = z0 + (r + 0.5) * course
        for wl in faces:
            s = -((r % 2) * blen / 2)
            while s < wl.length:
                lo, hi = max(0.0, s), min(wl.length, s + blen - 0.012)
                if hi - lo > 0.03:
                    wl.box(b, (lo + hi) / 2, proud / 2 - 0.02, z, (hi - lo, 0.04 + proud, course - 0.012),
                           "BrickHandmade", skip=("-y",))
                s += blen
    for k, grow in enumerate((0.05, 0.10)):
        zz = z1 + 0.036 + k * course
        b.box((x, y, zz), (sx + 2 * grow, sy + 2 * grow, course - 0.01), "BrickHandmade")
    b.box((x, y, z1 + 2 * course + 0.005), (sx - 0.18, sy - 0.18, 0.02), "DarkVoid")


def shingles(b: Builder, origin, e_dir, u_dir, e_span, u_len, exposure=0.15, mat="ShingleCedar"):
    """Hand-split shingles on a roof plane. origin = a point on the eave line; e_dir runs along
    the eave, u_dir up the slope; e_span(u) -> (left, right) limits at slope distance u.
    Each shingle is its top face and its butt, with its own width, length and grain."""
    e_dir, u_dir = Vector(e_dir).normalized(), Vector(u_dir).normalized()
    n = e_dir.cross(u_dir)
    if n.z < 0:
        n = -n
    o = Vector(origin)
    rows = max(1, math.ceil(u_len / exposure))
    for r in range(rows):
        u0, u1 = r * exposure, min(u_len, (r + 1) * exposure + 0.035)
        left, right = e_span((u0 + min(u_len, (r + 1) * exposure)) / 2)
        if right - left < 0.03:
            continue
        e = left - b.rng.uniform(0, 0.12)
        while e < right:
            wd = b.rng.uniform(0.10, 0.22)
            lo, hi = max(left, e), min(right, e + wd - 0.006)
            e += wd
            if hi - lo < 0.02:
                continue
            butt = 0.014 + b.rng.uniform(0, 0.012)
            drop = b.rng.uniform(0, 0.012)  # ragged butt line
            off = b.offset()
            p = lambda ee, uu, hh: o + e_dir * ee + u_dir * uu + n * hh  # noqa: E731
            top = [p(lo, u0 - drop, butt), p(hi, u0 - drop, butt), p(hi, u1, 0.003), p(lo, u1, 0.003)]
            b.face(top, mat, n, [b._uv(mat, uu, ee, off) for ee, uu in ((lo, u0), (hi, u0), (hi, u1), (lo, u1))])
            front = [p(lo, u0 - drop, 0.0), p(hi, u0 - drop, 0.0), p(hi, u0 - drop, butt), p(lo, u0 - drop, butt)]
            b.face(front, mat, -u_dir, [b._uv(mat, hh, ee, off) for ee, hh in ((lo, 0), (hi, 0), (hi, butt), (lo, butt))])


# ----------------------------------------------------------------------------- roofs


def gable_roof(b: Builder, w, y_front, y_back, y_ridge, z_ridge, pitch, overhang=0.25, rake=0.2, trim="PaintedTrim"):
    """Ridge along X at (y_ridge, z_ridge); each slope runs down at `pitch` to its wall plus
    the overhang (a longer back slope gives a saltbox). Closed board deck, shingles, rake
    boards, fascia and a ridge board."""
    t = math.tan(pitch)
    rw = w + 2 * rake
    for sign, y_wall in ((-1, y_front), (1, y_back)):
        run = abs(y_wall - y_ridge) + overhang
        slope = run / math.cos(pitch)
        tangent = Vector((0, sign * math.cos(pitch), -math.sin(pitch)))  # down-slope
        normal = Vector((0, sign * math.sin(pitch), math.cos(pitch)))
        ridge_pt = Vector((0, y_ridge, z_ridge))
        b.box(ridge_pt + tangent * slope / 2 - normal * 0.03, (rw, slope, 0.05), "OakWeathered",
              ((1, 0, 0), tangent, normal), grain=0)
        eave = ridge_pt + tangent * slope
        shingles(b, eave - Vector((rw / 2, 0, 0)) + normal * 0.0, (1, 0, 0), -tangent, lambda u: (0.0, rw), slope)
        b.beam(eave - Vector((rw / 2, 0, 0)) - normal * 0.07, eave + Vector((rw / 2, 0, 0)) - normal * 0.07, 0.03, 0.14, trim)
        for sx in (-1, 1):
            x = sx * (rw / 2 - 0.015)
            b.beam(ridge_pt + Vector((x, 0, 0)) - normal * 0.09, eave + Vector((x, 0, 0)) - normal * 0.09, 0.03, 0.17, trim)
        b.hull_box(ridge_pt + tangent * slope / 2 - normal * 0.04, (rw, slope, 0.12), ((1, 0, 0), tangent, normal))
    b.beam((-rw / 2, y_ridge, z_ridge + 0.03), (rw / 2, y_ridge, z_ridge + 0.03), 0.11, 0.05, "OakWeathered")
    return z_ridge - (abs(y_back - y_ridge) + overhang) * t


def hip_roof(b: Builder, w, d, z_eave, pitch, overhang=0.3, trim="PaintedTrim"):
    """Hip roof over a w x d plan (pyramid when w == d): closed deck, shingled slopes, hip
    and ridge boards, fascia."""
    t = math.tan(pitch)
    hw, hd = w / 2 + overhang, d / 2 + overhang
    z0 = z_eave - overhang * t
    rise = min(hw, hd) * t
    zr = z0 + rise
    rl = max(0.0, hw - hd)  # half ridge length (ridge along X when w > d)
    rdy = max(0.0, hd - hw)
    corners = [(-hw, -hd, z0), (hw, -hd, z0), (hw, hd, z0), (-hw, hd, z0)]
    ridge = [(-rl, -rdy, zr), (rl, rdy, zr)] if (rl or rdy) else [(0, 0, zr)]
    r0, r1 = ridge[0], ridge[-1]
    verts = corners + list(dict.fromkeys(ridge))
    ia, ib = 4, 4 + (len(verts) - 5)
    if rl:
        faces = [(0, 1, 2, 3), (0, 1, ib, ia), (2, 3, ia, ib), (1, 2, ib), (3, 0, ia)]
    elif rdy:
        faces = [(0, 1, 2, 3), (0, 1, ia), (2, 3, ib), (1, 2, ib, ia), (3, 0, ia, ib)]
    else:
        faces = [(0, 1, 2, 3), (0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
    lowered = [(x, y, z - 0.02) for x, y, z in verts]
    b.convex(lowered, faces, "OakWeathered")
    b.hull_convex(lowered, faces)
    slope_fb = (hd - rdy) / math.cos(pitch)
    slope_lr = (hw - rl) / math.cos(pitch)
    # front / back
    for sign in (-1, 1):
        up = Vector((0, -sign * math.cos(pitch), math.sin(pitch)))
        org = Vector((-hw, sign * hd, z0))
        full = 2 * hw
        top_w = 2 * rl
        shingles(b, org, (1, 0, 0), up, lambda u, f=full, tw=top_w, sl=slope_fb: (
            (f - tw) / 2 * u / sl, f - (f - tw) / 2 * u / sl), slope_fb)
    for sign in (-1, 1):
        up = Vector((-sign * math.cos(pitch), 0, math.sin(pitch)))
        org = Vector((sign * hw, -hd, z0))
        full = 2 * hd
        top_w = 2 * rdy
        shingles(b, org, (0, 1, 0), up, lambda u, f=full, tw=top_w, sl=slope_lr: (
            (f - tw) / 2 * u / sl, f - (f - tw) / 2 * u / sl), slope_lr)
    for cx, cy, cz in corners:
        end = r0 if (Vector(r0) - Vector((cx, cy, cz))).length < (Vector(r1) - Vector((cx, cy, cz))).length else r1
        b.beam((cx, cy, cz + 0.03), (end[0], end[1], end[2] + 0.03), 0.12, 0.04, "OakWeathered")
    if rl or rdy:
        b.beam((r0[0], r0[1], zr + 0.035), (r1[0], r1[1], zr + 0.035), 0.12, 0.05, "OakWeathered")
    for a, c in zip(corners, corners[1:] + corners[:1], strict=False):
        b.beam((a[0], a[1], z0 - 0.08), (c[0], c[1], z0 - 0.08), 0.035, 0.16, trim)
    # soffit box closing the overhang down to the frieze
    b.box((0, 0, z0 - 0.1), (2 * hw - 0.06, 2 * hd - 0.06, 0.12), trim, skip=("+z",))
    return zr


# ----------------------------------------------------------------------------- buildings


def house(b: Builder, *, w, d_main, lean=0.0, wall_h, pitch_deg, found_h=0.35, siding="ClapboardWeathered",
          trim="PaintedTrim", bays=5, storeys=2, roof="gable", chimneys=((0.0, 1.25, 1.25),),
          win=(0.80, 1.40), door_w=1.05, door_head="flat", end_windows=1, side_door=False,
          vertical=False, win_cols=4, win_rows=6, attic_window=True):
    """Timber-framed New England house, front = -Y. d_main is the depth under the main roof;
    `lean` adds a rear lean-to under the continued back slope (saltbox)."""
    pitch = math.radians(pitch_deg)
    d = d_main + lean
    yf, yb = -d / 2, d / 2
    y_ridge = yf + d_main / 2
    z0 = found_h
    eave = z0 + wall_h
    t = math.tan(pitch)
    foundation(b, w, d, found_h)
    side = clapboards if not vertical else vertical_boards
    ww, wh = win
    storey_h = wall_h / storeys
    levels = [z0 + storey_h * k + min(storey_h * 0.52, 1.0 + wh / 2) for k in range(storeys)]

    if roof == "hip":
        z_ridge = None
        back_eave = eave
        body = [(-w / 2 + BODY_INSET, yf + BODY_INSET, z0 - 0.05), (w / 2 - BODY_INSET, yb - BODY_INSET, eave)]
        (x0, y0, za), (x1, y1, zb) = body
        b.box(((x0 + x1) / 2, (y0 + y1) / 2, (za + zb) / 2), (x1 - x0, y1 - y0, zb - za), "DarkVoid", skip=("-z",))
        b.hull_box((0, 0, eave / 2), (w + 0.1, d + 0.1, eave))
    else:
        z_ridge = eave + (d_main / 2) * t
        back_eave = z_ridge - (yb - y_ridge) * t
        prof = [(yf + BODY_INSET, z0 - 0.05), (yb - BODY_INSET, z0 - 0.05), (yb - BODY_INSET, back_eave - 0.02),
                (y_ridge, z_ridge - 0.06), (yf + BODY_INSET, eave - 0.02)]
        xs = (-w / 2 + BODY_INSET, w / 2 - BODY_INSET)
        verts = [(x, y, z) for x in xs for y, z in prof]
        n = len(prof)
        faces = [tuple(range(n)), tuple(range(n, 2 * n))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
        b.convex(verts, faces, "DarkVoid")
        hv = [(sx * (w / 2 + 0.05), y, z) for sx in (-1, 1) for y, z in
              [(yf - 0.05, 0), (yb + 0.05, 0), (yb + 0.05, back_eave), (y_ridge, z_ridge), (yf - 0.05, eave)]]
        b.hull_convex(hv, faces)

    # --- front
    front = Wall((-w / 2, yf), (w / 2, yf))
    holes = []
    centre = bays // 2
    for k, z in enumerate(levels):
        for i in range(bays):
            s = w * (i + 0.5) / bays
            if k == 0 and i == centre:
                continue
            holes.append(window(b, front, s, z, ww, wh, win_cols, win_rows, trim))
    holes.append(door(b, front, w / 2, z0, door_w, 2.05, trim, leaves=2 if door_w > 1.3 else 1, head=door_head))
    side(b, front, z0, eave, siding, holes)
    b.stone((0, yf - 0.55, 0.02), (0.75, 0.42, z0 * 0.55 + 0.06), jitter=0.04, yaw=0.0)  # door stone

    # --- back
    back = Wall((w / 2, yb), (-w / 2, yb))
    holes = []
    back_levels = [z for z in levels if z + wh / 2 + 0.3 < back_eave]
    for z in back_levels:
        for i in range(bays):
            if i % 2 == 1 and bays > 3:
                continue
            holes.append(window(b, back, w * (i + 0.5) / bays, z, ww, wh, win_cols, win_rows, trim))
    if lean or side_door:
        holes.append(door(b, back, w * 0.5 if bays <= 3 else w * 1.5 / bays, z0, 0.9, 1.95, trim))
    side(b, back, z0, back_eave, siding, holes)

    # --- ends
    for sx in (1, -1):
        wl = Wall((w / 2, yf), (w / 2, yb)) if sx > 0 else Wall((-w / 2, yb), (-w / 2, yf))
        to_s = (lambda y: y - yf) if sx > 0 else (lambda y: yb - y)
        if roof == "hip":
            top = None
        else:
            top = sorted([(to_s(yf), eave), (to_s(y_ridge), z_ridge), (to_s(yb), back_eave)])
        holes = []
        ys = [y_ridge] if end_windows == 1 else [y_ridge - d_main * 0.24, y_ridge + d_main * 0.24]
        end_door = side_door and sx > 0 and roof == "hip"
        for z in levels:
            for y in ys:
                if end_door and z == levels[0] and y == max(ys):
                    continue  # the side door takes this bay
                holes.append(window(b, wl, to_s(y), z, ww, wh, win_cols, win_rows, trim))
            if lean > 1.6 and z == levels[0]:
                holes.append(window(b, wl, to_s(yb - lean / 2), z, ww * 0.85, wh * 0.8, 3, 4, trim))
        if roof != "hip" and attic_window and z_ridge - eave > 2.2:
            holes.append(window(b, wl, to_s(y_ridge), eave + (z_ridge - eave) * 0.36, ww * 0.8, wh * 0.62, 3, 4, trim))
        if end_door:
            holes.append(door(b, wl, to_s(max(ys)), z0, 0.95, 1.98, trim))
        side(b, wl, z0, eave, siding, holes, top)

    # --- trim: corner boards, water table, frieze
    corners = [(-w / 2, yf), (w / 2, yf), (w / 2, yb), (-w / 2, yb)]
    corner_boards(b, corners[:2], z0, eave, trim)
    corner_boards(b, corners[2:], z0, back_eave, trim)
    for wl, zt in ((front, eave), (back, back_eave)):
        wl.box(b, wl.length / 2, 0.005, z0 + 0.06, (wl.length + 0.06, 0.05, 0.12), trim)
        wl.box(b, wl.length / 2, 0.01, zt - 0.09, (wl.length + 0.06, 0.06, 0.18), trim)
    for wl in (Wall((w / 2, yf), (w / 2, yb)), Wall((-w / 2, yb), (-w / 2, yf))):
        wl.box(b, wl.length / 2, 0.005, z0 + 0.06, (wl.length + 0.06, 0.05, 0.12), trim)
        if roof == "hip":
            wl.box(b, wl.length / 2, 0.01, eave - 0.09, (wl.length + 0.06, 0.06, 0.18), trim)

    # --- roof and chimneys
    if roof == "hip":
        top_z = hip_roof(b, w, d, eave, pitch, trim=trim)
        roof_z = lambda x, y: min(top_z, eave + min(w / 2 - abs(x), d / 2 - abs(y)) * t)  # noqa: E731
    else:
        gable_roof(b, w, yf, yb, y_ridge, z_ridge + 0.03, pitch, trim=trim)
        top_z = z_ridge
        roof_z = lambda x, y: z_ridge - abs(y - y_ridge) * t  # noqa: E731
    for cx, sx_, sy_, *rest in chimneys:
        cy = (rest[0] if rest else 0.0) + (y_ridge if roof != "hip" else 0.0)
        base = roof_z(cx, cy) - sy_ * 0.5 * t - 0.25
        stack_top = top_z + 0.95
        chimney(b, cx, cy, base, stack_top, sx_, sy_)
        b.hull_box((cx, cy, (base + stack_top + 0.2) / 2), (sx_ + 0.2, sy_ + 0.2, stack_top + 0.2 - base))
    return {"eave": eave, "ridge": top_z}


def meetinghouse(b: Builder):
    """Lexington's second meetinghouse (raised 1714, taken down 1794). The town voted a house
    50 ft long, 40 ft wide and 28 ft between joints (15.2 x 12.2 x 8.5 m), with two tiers of
    galleries, hence three tiers of windows. It had NO steeple or tower: the bell hung in a
    separate belfry on the Common (see belfry()). Reading, not record: unpainted weathered
    clapboards (white meetinghouses are a post-Revolution fashion), main door in the long
    south side with doors in both ends, and the window count."""
    w, d, wall_h, found = 15.24, 12.19, 8.53, 0.30
    pitch = math.radians(36)
    siding, trim = "ClapboardWeathered", "ClapboardWeathered"
    z0, eave = found, found + wall_h
    yf, yb = -d / 2, d / 2
    z_ridge = eave + d / 2 * math.tan(pitch)
    foundation(b, w, d, found)
    prof = [(yf + BODY_INSET, z0 - 0.05), (yb - BODY_INSET, z0 - 0.05), (yb - BODY_INSET, eave - 0.02), (0, z_ridge - 0.06),
            (yf + BODY_INSET, eave - 0.02)]
    xs = (-w / 2 + BODY_INSET, w / 2 - BODY_INSET)
    verts = [(x, y, z) for x in xs for y, z in prof]
    faces = [tuple(range(5)), tuple(range(5, 10))] + [(i, (i + 1) % 5, 5 + (i + 1) % 5, 5 + i) for i in range(5)]
    b.convex(verts, faces, "DarkVoid")
    b.hull_convex([(sx * (w / 2 + 0.05), y, z) for sx in (-1, 1) for y, z in
                   [(yf - 0.05, 0), (yb + 0.05, 0), (yb + 0.05, eave), (0, z_ridge), (yf - 0.05, eave)]], faces)
    ww, wh = 0.92, 1.55
    tiers = [z0 + 1.75, z0 + 4.35, z0 + 6.95]
    front = Wall((-w / 2, yf), (w / 2, yf))
    back = Wall((w / 2, yb), (-w / 2, yb))
    for wl, has_door in ((front, True), (back, False)):
        holes = []
        for k, z in enumerate(tiers):
            for i in range(5):
                if has_door and k == 0 and i == 2:
                    continue
                if not has_door and i == 2 and k < 2:
                    # the tall pulpit window sits between the tiers on the north wall
                    if k == 0:
                        holes.append(window(b, wl, w / 2, z0 + 3.4, ww * 1.1, wh * 1.5, 4, 8, trim))
                    continue
                holes.append(window(b, wl, w * (i + 0.5) / 5, z, ww, wh, 4, 6, trim))
        if has_door:
            holes.append(door(b, wl, w / 2, z0, 1.7, 2.45, trim, leaves=2, head="pediment"))
        clapboards(b, wl, z0, eave, siding, holes)
        wl.box(b, wl.length / 2, 0.005, z0 + 0.07, (wl.length + 0.06, 0.05, 0.14), trim)
        wl.box(b, wl.length / 2, 0.012, eave - 0.11, (wl.length + 0.06, 0.06, 0.22), trim)
    for sx in (1, -1):
        wl = Wall((w / 2, yf), (w / 2, yb)) if sx > 0 else Wall((-w / 2, yb), (-w / 2, yf))
        holes = []
        for k, z in enumerate(tiers):
            for i in range(3):
                if k == 0 and i == 1:
                    continue
                holes.append(window(b, wl, d * (i + 0.5) / 3, z, ww, wh, 4, 6, trim))
        holes.append(window(b, wl, d / 2, eave + (z_ridge - eave) * 0.38, ww * 0.85, wh * 0.7, 3, 4, trim))
        holes.append(door(b, wl, d / 2, z0, 1.15, 2.2, trim, leaves=1, head="flat"))
        clapboards(b, wl, z0, eave, siding, holes, [(0, eave), (d / 2, z_ridge), (d, eave)])
        wl.box(b, wl.length / 2, 0.005, z0 + 0.07, (wl.length + 0.06, 0.05, 0.14), trim)
        b.stone(wl.p(d / 2, 0.5, 0.0), (0.7, 0.4, z0 * 0.55 + 0.05), jitter=0.04, yaw=math.pi / 2)
    b.stone((0, yf - 0.6, 0.0), (1.1, 0.5, z0 * 0.55 + 0.05), jitter=0.04, yaw=0.0)
    corner_boards(b, [(-w / 2, yf), (w / 2, yf), (w / 2, yb), (-w / 2, yb)], z0, eave, trim, 0.16)
    gable_roof(b, w, yf, yb, 0.0, z_ridge + 0.03, pitch, overhang=0.3, trim=trim)


def belfry(b: Builder):
    """The Old Belfry: built 1762 on the hill by the parsonage road, moved onto the Common in
    1768 beside the meetinghouse, where it rang the alarm on 19 April 1775 (stood there to
    1797). A small free-standing wooden tower. Dimensions here follow the 1910 replica as a
    reading (about 12 ft square, 27 ft to the peak): louvred bell stage, pyramid roof, vane."""
    w, wall_h, found = 3.66, 5.3, 0.25
    z0, eave = found, found + wall_h
    foundation(b, w, w, found)
    b.box((0, 0, (z0 + eave) / 2), (w - 2 * BODY_INSET, w - 2 * BODY_INSET, eave - z0 + 0.1), "DarkVoid", skip=("-z",))
    b.hull_box((0, 0, eave / 2), (w + 0.1, w + 0.1, eave))
    trim = "ClapboardWeathered"
    pts = [(-w / 2, -w / 2), (w / 2, -w / 2), (w / 2, w / 2), (-w / 2, w / 2)]
    for i in range(4):
        wl = Wall(pts[i], pts[(i + 1) % 4])
        holes = []
        lz, lw, lh = eave - 1.15, 1.25, 1.3
        # louvred opening of the bell stage
        hole = (w / 2 - lw / 2 - 0.1, w / 2 + lw / 2 + 0.1, lz - lh / 2 - 0.08, lz + lh / 2 + 0.1)
        holes.append(hole)
        for sg in (-1, 1):
            wl.box(b, w / 2 + sg * (lw / 2 + 0.05), -0.01, lz, (0.1, 0.12, lh + 0.02), trim, grain=2)
            wl.box(b, w / 2, -0.01, lz + sg * (lh / 2 + 0.05) + (0.01 if sg > 0 else -0.02), (lw + 0.2, 0.13, 0.1), trim)
        slats = 9
        for k in range(slats):
            zc = lz - lh / 2 + lh * (k + 0.5) / slats
            c = wl.p(w / 2, -0.025, zc)
            tilt = math.radians(38)
            out = wl.n * math.cos(tilt) - UP * math.sin(tilt)
            b.box(c, (lw, 0.15, 0.018), "OakWeathered", (wl.d, out, wl.d.cross(out)), grain=0)
        if i == 0:
            holes.append(door(b, wl, w / 2, z0, 0.9, 1.9, trim))
            b.stone(wl.p(w / 2, 0.42, 0.0), (0.55, 0.32, z0 * 0.55 + 0.05), jitter=0.04, yaw=0.0)
        clapboards(b, wl, z0, eave, "ClapboardWeathered", holes)
        wl.box(b, wl.length / 2, 0.01, eave - 0.08, (wl.length + 0.06, 0.06, 0.16), trim)
    corner_boards(b, pts, z0, eave, trim, 0.12)
    top = hip_roof(b, w, w, eave, math.radians(52), overhang=0.28, trim=trim)
    b.beam((0, 0, top - 0.1), (0, 0, top + 0.9), 0.05, 0.05, "Iron")
    b.box((0.14, 0, top + 0.72), (0.34, 0.012, 0.14), "Iron")


def barn(b: Builder):
    """English (three-bay, side-entry) barn, the standard New England barn of the 1700s:
    about 30 x 40 ft, unpainted vertical boards, wagon doors in the middle of the long side."""
    w, d, wall_h, found = 12.2, 9.1, 4.1, 0.2
    pitch = math.radians(40)
    z0, eave = found, found + wall_h
    yf, yb = -d / 2, d / 2
    z_ridge = eave + d / 2 * math.tan(pitch)
    foundation(b, w, d, found)
    prof = [(yf + 0.04, z0 - 0.05), (yb - 0.04, z0 - 0.05), (yb - 0.04, eave), (0, z_ridge - 0.05), (yf + 0.04, eave)]
    verts = [(x, y, z) for x in (-w / 2 + 0.04, w / 2 - 0.04) for y, z in prof]
    faces = [tuple(range(5)), tuple(range(5, 10))] + [(i, (i + 1) % 5, 5 + (i + 1) % 5, 5 + i) for i in range(5)]
    b.convex(verts, faces, "DarkVoid")
    b.hull_convex([(sx * (w / 2 + 0.05), y, z) for sx in (-1, 1) for y, z in
                   [(yf - 0.05, 0), (yb + 0.05, 0), (yb + 0.05, eave), (0, z_ridge), (yf - 0.05, eave)]], faces)
    mat = "ClapboardWeathered"
    front = Wall((-w / 2, yf), (w / 2, yf))
    dw, dh = 3.3, 3.3
    hole = (w / 2 - dw / 2, w / 2 + dw / 2, z0 - 0.05, z0 + dh)
    vertical_boards(b, front, z0, eave, mat, [hole])
    for leaf in (-1, 1):  # wagon doors: boards, ledges, braces and strap hinges
        c = w / 2 + leaf * dw / 4
        n = 6
        for i in range(n):
            front.box(b, c - dw / 4 + dw / 2 * (i + 0.5) / n, 0.0, z0 + dh / 2 + 0.03, (dw / 2 / n - 0.008, 0.035, dh - 0.08), mat, grain=2)
        for zz in (0.45, dh - 0.45):
            front.box(b, c, 0.03, z0 + zz, (dw / 2 - 0.1, 0.03, 0.16), "OakWeathered")
            front.box(b, c + leaf * dw * 0.09, 0.05, z0 + zz, (dw / 2 * 0.62, 0.012, 0.045), "Iron")
        b.beam(front.p(c - leaf * (dw / 4 - 0.1), 0.03, z0 + 0.5), front.p(c + leaf * (dw / 4 - 0.1), 0.03, z0 + dh - 0.5), 0.14, 0.03, "OakWeathered")
    front.box(b, w / 2, 0.02, z0 + dh + 0.09, (dw + 0.3, 0.06, 0.18), "OakWeathered")
    back = Wall((w / 2, yb), (-w / 2, yb))
    vertical_boards(b, back, z0, eave, mat)
    for sx in (1, -1):
        wl = Wall((w / 2, yf), (w / 2, yb)) if sx > 0 else Wall((-w / 2, yb), (-w / 2, yf))
        sq = (d / 2 - 0.3, d / 2 + 0.3, eave + 1.2, eave + 1.8)
        holes = [sq]
        if sx > 0:
            holes.append((1.2, 2.1, z0 - 0.05, z0 + 1.9))
            n = 4
            for i in range(n):
                wl.box(b, 1.2 + 0.9 * (i + 0.5) / n, 0.0, z0 + 0.96, (0.9 / n - 0.008, 0.035, 1.86), mat, grain=2)
            for zz in (0.35, 1.55):
                wl.box(b, 1.65, 0.03, z0 + zz, (0.8, 0.025, 0.12), "OakWeathered")
        wl.box(b, d / 2, -0.03, eave + 1.5, (0.6, 0.02, 0.6), "DarkVoid")
        vertical_boards(b, wl, z0, eave, mat, holes, [(0, eave), (d / 2, z_ridge), (d, eave)])
    for x, y in [(-w / 2, yf), (w / 2, yf), (w / 2, yb), (-w / 2, yb)]:
        b.box((x, y, (z0 + eave) / 2), (0.2, 0.2, eave - z0), "OakWeathered", grain=2)
    gable_roof(b, w, yf, yb, 0.0, z_ridge + 0.03, pitch, overhang=0.3, trim="OakWeathered")
    # earthen/stone ramp to the wagon doors
    for k in range(3):
        b.stone((b.rng.uniform(-0.9, 0.9), yf - 0.45 - k * 0.2, 0.0), (0.7, 0.35, 0.13 - k * 0.03), jitter=0.06)


def shed(b: Builder):
    """Lean-to wagon shed / stable of the kind drawn beside Buckman Tavern in Doolittle's 1775
    view: open-fronted, single-pitch roof, vertical boards."""
    w, d, hf, hb = 7.3, 4.3, 3.0, 2.1
    mat = "ClapboardWeathered"
    yf, yb = -d / 2, d / 2
    pitch = math.atan2(hf - hb, d)
    posts = [-w / 2 + 0.1, -w / 6, w / 6, w / 2 - 0.1]
    for x in posts:
        b.box((x, yf + 0.1, hf / 2), (0.18, 0.18, hf), "OakWeathered", grain=2)
        b.hull_box((x, yf + 0.1, hf / 2), (0.2, 0.2, hf))
    b.beam((-w / 2, yf + 0.1, hf - 0.1), (w / 2, yf + 0.1, hf - 0.1), 0.18, 0.2, "OakWeathered")
    back = Wall((w / 2, yb), (-w / 2, yb))
    vertical_boards(b, back, 0.05, hb, mat)
    b.box((0, yb - 0.03, hb / 2), (w, 0.03, hb), "DarkVoid")
    b.hull_box((0, yb, hb / 2), (w, 0.12, hb))
    for sx in (1, -1):
        wl = Wall((w / 2, yf), (w / 2, yb)) if sx > 0 else Wall((-w / 2, yb), (-w / 2, yf))
        top = [(0, hf), (d, hb)] if sx > 0 else [(0, hb), (d, hf)]
        vertical_boards(b, wl, 0.05, hf, mat, top=top)
        verts = [(sx * (w / 2 - 0.03), yf, 0), (sx * (w / 2 - 0.03), yb, 0), (sx * (w / 2 - 0.03), yb, hb), (sx * (w / 2 - 0.03), yf, hf),
                 (sx * (w / 2 - 0.006), yf, 0), (sx * (w / 2 - 0.006), yb, 0), (sx * (w / 2 - 0.006), yb, hb), (sx * (w / 2 - 0.006), yf, hf)]
        fc = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        b.convex(verts, fc, "DarkVoid")
        b.hull_box((sx * w / 2, 0, hb / 2), (0.12, d, hb))
    run = d + 0.5
    slope = run / math.cos(pitch)
    tangent = Vector((0, math.cos(pitch), -math.sin(pitch)))
    normal = Vector((0, math.sin(pitch), math.cos(pitch)))
    high = Vector((0, yf - 0.25, hf + 0.08 + 0.25 * math.tan(pitch)))
    b.box(high + tangent * slope / 2 - normal * 0.03, (w + 0.4, slope, 0.05), "OakWeathered", ((1, 0, 0), tangent, normal), grain=0)
    b.hull_box(high + tangent * slope / 2 - normal * 0.03, (w + 0.4, slope, 0.1), ((1, 0, 0), tangent, normal))
    shingles(b, high + tangent * slope - Vector(((w + 0.4) / 2, 0, 0)), (1, 0, 0), -tangent, lambda u: (0.0, w + 0.4), slope)
    # manger rail and a hay rack along the back
    b.beam((-w / 2 + 0.2, yb - 0.5, 0.95), (w / 2 - 0.2, yb - 0.5, 0.95), 0.09, 0.09, "OakWeathered")


# ----------------------------------------------------------------------------- fences, walls, props


def stone_wall(b: Builder, length=3.0, height=1.05, width=0.8):
    """New England tossed fieldstone wall, 3 m, about 1.05 m high: large footing stones, two
    battered faces of mixed stones laid as they came off the field, small stones wedged in the
    heart, a ragged top and a few tumbled at the foot. Ends overlap the next piece."""
    r = b.rng
    z = 0.0
    course = 0
    while z < height - 0.1:
        h = r.uniform(0.24, 0.36) if course == 0 else r.uniform(0.13, 0.27)
        wd = width * (1.0 - 0.38 * z / height)
        for sgn in (-1, 1):
            s = -length / 2 - r.uniform(0.0, 0.25)
            while s < length / 2:
                big = r.random() < (0.5 if course == 0 else 0.18)
                ln = r.uniform(0.45, 0.8) if big else r.uniform(0.16, 0.45)
                dp = r.uniform(0.22, 0.36)
                hh = h * (r.uniform(1.0, 1.5) if big else r.uniform(0.6, 1.1))
                b.stone((s + ln / 2, sgn * (wd / 2 - dp / 2) + r.uniform(-0.05, 0.05), z + hh / 2 - 0.04 + r.uniform(-0.02, 0.03)),
                        (ln / 2 + 0.03, dp / 2 + 0.05, hh / 2 + 0.04), yaw=r.uniform(-0.45, 0.45), jitter=0.2, tilt=0.22)
                s += ln * r.uniform(0.85, 1.0)
        s = -length / 2
        while s < length / 2:  # hearting
            b.stone((s, r.uniform(-0.06, 0.06), z + h * 0.5), (r.uniform(0.09, 0.16), r.uniform(0.09, 0.14), h * 0.45),
                    jitter=0.2, tilt=0.3)
            s += r.uniform(0.25, 0.45)
        z += h * r.uniform(0.85, 1.0)
        course += 1
    s = -length / 2
    while s < length / 2:  # ragged top
        ln = r.uniform(0.25, 0.6)
        if r.random() < 0.7:
            b.stone((s + ln / 2, r.uniform(-0.1, 0.1), z + r.uniform(0.0, 0.05)),
                    (ln / 2 + 0.02, r.uniform(0.14, 0.24), r.uniform(0.06, 0.13)), yaw=r.uniform(-0.6, 0.6), jitter=0.2, tilt=0.25)
        s += ln
    for _ in range(7):  # tumbled stones at the foot
        b.stone((r.uniform(-length / 2, length / 2), r.choice((-1, 1)) * r.uniform(width / 2 + 0.05, width / 2 + 0.4), 0.03),
                (r.uniform(0.1, 0.24), r.uniform(0.08, 0.18), r.uniform(0.06, 0.12)), jitter=0.2, tilt=0.3)
    b.hull_box((0, 0, (height + 0.05) / 2), (length, width * 0.8, height + 0.05))


def post_rail_fence(b: Builder, length=3.0, rails=3, post_end=False):
    """Post-and-rail fence panel: one mortised post at the -X end and three riven rails 3 m
    long whose tapered ends would pass into the next post. `post_end` adds the closing post."""
    r = b.rng
    xs = [-length / 2] + ([length / 2] if post_end else [])
    for x in xs:
        h = 1.3 + r.uniform(-0.04, 0.05)
        b.box((x, 0, h / 2 - 0.1), (0.16, 0.13, h + 0.2), "OakWeathered", grain=2)
        b.hull_box((x, 0, h / 2), (0.18, 0.15, h))
    for k in range(rails):
        z = 0.32 + k * 0.36 + r.uniform(-0.02, 0.02)
        dz = r.uniform(-0.03, 0.03)
        a = Vector((-length / 2 - 0.04, r.uniform(-0.012, 0.012), z))
        c = Vector((length / 2 + 0.04, r.uniform(-0.012, 0.012), z + dz))
        # a riven rail: roughly triangular/irregular section, built as two crossing beams
        b.beam(a, c, r.uniform(0.07, 0.09), r.uniform(0.09, 0.12), "OakWeathered")
        b.beam(a + Vector((0.2, 0, 0.012)), c - Vector((0.2, 0, -0.012)), r.uniform(0.09, 0.11), r.uniform(0.05, 0.07), "OakWeathered")
    b.hull_box((0, 0, 0.62), (length, 0.1, 1.05))


def worm_fence(b: Builder, panels=2, rail_len=3.2, rails=5):
    """Virginia / worm (zig-zag) rail fence: stacked split rails, no posts. Two panels form
    one repeating unit along X."""
    r = b.rng
    ang = math.radians(22)
    step = rail_len * math.cos(ang) - 0.25
    off = rail_len * math.sin(ang) / 2
    for p in range(panels):
        x0 = -step * panels / 2 + p * step
        sgn = 1 if p % 2 == 0 else -1
        a0 = Vector((x0 - 0.12, -sgn * off, 0))
        c0 = Vector((x0 + step + 0.12, sgn * off, 0))
        for k in range(rails):
            z = 0.1 + k * 0.2 + (0.1 if p % 2 else 0.0)
            j = Vector((0, 0, r.uniform(-0.015, 0.015)))
            b.beam(a0 + UP * z + j, c0 + UP * z - j, r.uniform(0.08, 0.11), r.uniform(0.09, 0.12), "OakWeathered")
        mid = (a0 + c0) / 2
        d = (c0 - a0).normalized()
        b.hull_box(mid + UP * 0.6, ((c0 - a0).length, 0.14, 1.15), (d, UP.cross(d), UP))


def well(b: Builder):
    """Dug well with a dry-laid stone curb and a well sweep (counterweighted pole on a forked
    post), the usual way to raise water on a New England farm."""
    r = b.rng
    for ring in range(3):
        n = 9
        for i in range(n):
            a = 2 * math.pi * (i + 0.5 * (ring % 2)) / n
            b.stone((0.78 * math.cos(a), 0.78 * math.sin(a), 0.12 + ring * 0.24), (0.27, 0.17, 0.14),
                    yaw=a + math.pi / 2, jitter=0.1)
    b.box((0, 0, 0.3), (1.1, 1.1, 0.02), "DarkVoid")
    b.hull_box((0, 0, 0.4), (1.9, 1.9, 0.8))
    post = Vector((2.3, 0.2, 0))
    b.box(post + UP * 1.6, (0.2, 0.2, 3.4), "OakWeathered", grain=2)
    b.hull_box(post + UP * 1.6, (0.22, 0.22, 3.2))
    a = Vector((5.0, 0.45, 0.9))
    c = Vector((-0.05, 0.02, 4.9))
    b.beam(a, c, 0.13, 0.13, "OakWeathered")
    b.stone(a + UP * -0.05, (0.3, 0.2, 0.18), jitter=0.1)
    b.beam(c, Vector((0, 0, 1.1)), 0.035, 0.035, "OakWeathered")
    b.box((0, 0, 0.95), (0.3, 0.3, 0.34), "PlankDoor", grain=2)
    b.box((0, 0, 1.13), (0.32, 0.32, 0.02), "Iron")
    _ = r


def hitching_rail(b: Builder):
    """Hitching rail for a tavern or meetinghouse yard: two posts and a rail, 3 m."""
    for x in (-1.4, 1.4):
        b.box((x, 0, 0.5), (0.15, 0.15, 1.3), "OakWeathered", grain=2)
        b.hull_box((x, 0, 0.55), (0.17, 0.17, 1.1))
    b.beam((-1.6, 0, 1.02), (1.6, 0, 1.02), 0.11, 0.11, "OakWeathered")
    b.hull_box((0, 0, 1.02), (3.2, 0.12, 0.12))
    for x in (-0.8, 0.5):
        b.box((x, 0.062, 0.98), (0.012, 0.012, 0.09), "Iron")


def woodpile(b: Builder):
    """Split cordwood stacked between stakes, about 2.4 m long and 1.1 m high."""
    r = b.rng
    z = 0.06
    row = 0
    while z < 1.05:
        x = -1.15 + (row % 2) * 0.05
        while x < 1.15:
            wd = r.uniform(0.1, 0.18)
            hh = r.uniform(0.09, 0.15)
            ln = r.uniform(0.55, 0.66)
            c = Vector((x + wd / 2, r.uniform(-0.03, 0.03), z + hh / 2))
            tilt = r.uniform(-0.12, 0.12)
            bx = Vector((math.cos(tilt), 0, math.sin(tilt)))
            b.box(c, (wd - 0.01, ln, hh), "OakWeathered", (bx, (0, 1, 0), bx.cross(Vector((0, 1, 0))) * -1), grain=1)
            x += wd
        z += 0.115
        row += 1
    for x in (-1.24, 1.24):
        b.box((x, 0, 0.6), (0.08, 0.08, 1.35), "OakWeathered", grain=2)
    b.hull_box((0, 0, 0.55), (2.5, 0.68, 1.1))


def tavern_sign(b: Builder):
    """Tavern signpost: a tall post with a bracket and a hanging painted board (left plain;
    Buckman's 1775 sign is not recorded)."""
    b.box((0, 0, 1.9), (0.2, 0.2, 4.2), "OakWeathered", grain=2)
    b.hull_box((0, 0, 2.0), (0.22, 0.22, 4.0))
    b.beam((0, 0, 3.75), (1.25, 0, 3.75), 0.09, 0.09, "OakWeathered")
    b.beam((0, 0, 3.05), (0.75, 0, 3.72), 0.06, 0.06, "OakWeathered")
    b.box((0.72, 0, 3.12), (0.8, 0.035, 1.0), "ClapboardOchre", grain=2)
    for sg in (-1, 1):
        b.box((0.72 + sg * 0.4, 0, 3.12), (0.05, 0.05, 1.06), "PaintedTrim", grain=2)
        b.box((0.72, 0, 3.12 + sg * 0.52), (0.86, 0.05, 0.05), "PaintedTrim")
        b.box((0.72 + sg * 0.28, 0, 3.67), (0.012, 0.012, 0.1), "Iron")


def north_bridge(b: Builder):
    """Concord's North Bridge as it stood on 19 April 1775 (built 1760, taken down 1793):
    a plain timber bridge on pile bents with a plank deck and post-and-rail sides, as in
    Amos Doolittle's 1775 plate of the engagement. Sized to the game's crossing: the deck is
    12 m long (X) and 4.2 m wide with its top 0.285 m above the ground at the origin, over a
    9 m channel; gravel-and-plank approaches run out 1.3 m at each end. The real bridge was
    longer (the river is some 30 m wide there) and slightly humped; that is GAME INVENTION
    scale, not record. Visual only: the game supplies the deck's collision."""
    r = b.rng
    L, W, top = 12.0, 4.2, 0.285
    n = int(L / 0.26)
    for i in range(n):  # transverse deck planks, each a little proud, long or skewed
        x = -L / 2 + L * (i + 0.5) / n
        over = r.uniform(0.0, 0.07)
        b.box((x, r.uniform(-0.02, 0.02), top - 0.03 + r.uniform(-0.004, 0.004)),
              (L / n - 0.008, W + 0.1 + over, 0.06), "OakWeathered", grain=1)
    for y in (-W / 2 + 0.25, -W / 6, W / 6, W / 2 - 0.25):  # stringers
        b.beam((-L / 2, y, top - 0.17), (L / 2, y, top - 0.17), 0.2, 0.22, "OakWeathered")
    for x in (-3.4, 0.0, 3.4):  # pile bents: four driven piles, a cap and sway braces
        for y in (-W / 2 + 0.2, -W / 6, W / 6, W / 2 - 0.2):
            lean = 0.06 * (1 if y > 0 else -1) * (abs(y) > 1)
            b.beam((x, y + lean, -0.9), (x, y, top - 0.3), 0.22, 0.22, "OakWeathered")
        b.beam((x, -W / 2 - 0.15, top - 0.36), (x, W / 2 + 0.15, top - 0.36), 0.22, 0.2, "OakWeathered")
        b.beam((x + 0.13, -W / 2 + 0.2, -0.55), (x + 0.13, W / 2 - 0.2, top - 0.5), 0.05, 0.16, "OakWeathered")
    for sy in (-1, 1):  # railings: posts tenoned into the deck edge, two rails
        y = sy * (W / 2 + 0.02)
        xs = [-L / 2 + 0.25 + k * (L - 0.5) / 6 for k in range(7)]
        for x in xs:
            h = 1.12 + r.uniform(-0.02, 0.03)
            b.box((x, y, top + h / 2 - 0.2), (0.14, 0.14, h + 0.4), "OakWeathered", grain=2)
        for z in (top + 0.5, top + 1.0):
            for xa, xb in zip(xs, xs[1:], strict=False):
                b.beam((xa - 0.06, y + r.uniform(-0.01, 0.01), z + r.uniform(-0.015, 0.015)),
                       (xb + 0.06, y + r.uniform(-0.01, 0.01), z + r.uniform(-0.015, 0.015)), 0.09, 0.11, "OakWeathered")
    for sx in (-1, 1):  # approaches: abutment stones, a gravel ramp and three ramp planks
        x0 = sx * L / 2
        for y in (-1.6, -0.55, 0.55, 1.6):
            b.stone((x0 + sx * 0.2, y + r.uniform(-0.1, 0.1), 0.02), (0.45, 0.5, 0.2), jitter=0.12, tilt=0.1)
        for sy in (-1, 1):
            b.stone((x0 + sx * 0.5, sy * (W / 2 + 0.35), 0.05), (0.5, 0.35, 0.3), jitter=0.15, tilt=0.15)
        ramp = [(x0 - sx * 0.05, -W / 2, top - 0.005), (x0 - sx * 0.05, W / 2, top - 0.005),
                (x0 + sx * 1.3, W / 2 + 0.3, -0.03), (x0 + sx * 1.3, -W / 2 - 0.3, -0.03),
                (x0 - sx * 0.05, -W / 2, -0.05), (x0 - sx * 0.05, W / 2, -0.05)]
        b.convex(ramp, [(0, 1, 2, 3), (0, 3, 4), (1, 5, 2), (0, 4, 5, 1), (4, 3, 2, 5)], "RoadDirt")


def north_bridge_1775(b: Builder):
    """Concord's North Bridge at full length, for a level with a real river (L_Concord).

    Record: the town rebuilt the bridge in 1760 and that bridge stood on 19 April 1775; it
    was a plain timber bridge on pile bents with post-and-rail sides (Doolittle's 1775 plate).
    The causeway it met was one rod (16.5 ft) wide (NPS, "The Groton Road").
    Reading: no measured drawing of the 1760 bridge survives. Length (33.4 m) and the six
    bents of three piles are those of the 1956 replica on the same abutments (109 ft 8 in);
    the width is the causeway's rod; the 0.9 m rise is the gentle hump of the plate.

    X runs along the span, origin at mid-span on the centreline, z = 0 at the deck top where
    it meets the causeway. Collision: eight deck hulls following the hump and one abutment hull
    at each end. The railings carry none (shot must pass); the level keeps men on the deck."""
    r = b.rng
    L, W, rise = 33.4, 4.9, 0.9
    oak = "OakWeathered"

    def top(x):
        return rise * (1.0 - (2.0 * x / L) ** 2)

    def frame(x):
        t = Vector((1.0, 0.0, -8.0 * rise * x / (L * L))).normalized()
        return t, Vector((0, 1, 0)), Vector((-t.z, 0.0, t.x))

    n = int(L / 0.28)
    for i in range(n):  # transverse deck planks, each a little proud, long or skewed
        x = -L / 2 + L * (i + 0.5) / n
        t, y, up = frame(x)
        b.box((x, r.uniform(-0.02, 0.02), top(x) - 0.03 + r.uniform(-0.004, 0.004)),
              (L / n - 0.008, W + 0.1 + r.uniform(0.0, 0.08), 0.06), oak, (t, y, up), grain=1)
    seg = 14
    xs = [-L / 2 + L * k / seg for k in range(seg + 1)]
    for y in (-W / 2 + 0.28, 0.0, W / 2 - 0.28):  # three stringers over the three piles
        for xa, xb in zip(xs, xs[1:], strict=False):
            b.beam((xa - 0.02, y, top(xa) - 0.2), (xb + 0.02, y, top(xb) - 0.2), 0.24, 0.26, oak)
    for k in range(6):  # six bents of three driven piles, a cap and a sway brace
        x = -L / 2 + L * (k + 1) / 7
        z = top(x)
        for y in (-W / 2 + 0.28, 0.0, W / 2 - 0.28):
            lean = 0.1 * (1 if y > 0 else -1 if y < 0 else 0)
            b.beam((x + r.uniform(-0.03, 0.03), y + lean, -3.6), (x, y, z - 0.33), 0.3, 0.3, oak)
        b.beam((x, -W / 2 - 0.2, z - 0.44), (x, W / 2 + 0.2, z - 0.44), 0.28, 0.24, oak)
        b.beam((x + 0.17, -W / 2 + 0.3, -1.3), (x + 0.17, W / 2 - 0.3, z - 0.7), 0.06, 0.2, oak)
        b.beam((x - 0.17, W / 2 - 0.3, -1.3), (x - 0.17, -W / 2 + 0.3, z - 0.7), 0.06, 0.2, oak)
    posts = 15
    px = [-L / 2 + 0.3 + k * (L - 0.6) / (posts - 1) for k in range(posts)]
    for sy in (-1, 1):  # railings: posts tenoned into the deck edge, two rails
        y = sy * (W / 2 + 0.03)
        for x in px:
            h = 1.15 + r.uniform(-0.02, 0.03)
            b.box((x, y, top(x) + h / 2 - 0.22), (0.15, 0.15, h + 0.44), oak, grain=2)
        for dz in (0.52, 1.02):
            for xa, xb in zip(px, px[1:], strict=False):
                b.beam((xa - 0.07, y + r.uniform(-0.01, 0.01), top(xa) + dz + r.uniform(-0.015, 0.015)),
                       (xb + 0.07, y + r.uniform(-0.01, 0.01), top(xb) + dz + r.uniform(-0.015, 0.015)), 0.1, 0.12, oak)
    for sx in (-1, 1):  # dry-laid fieldstone abutments under each end, and the gravel that tops them
        x0 = sx * L / 2
        for row in range(4):
            for col in range(6):
                b.stone((x0 + sx * (0.25 + r.uniform(-0.08, 0.08)), -W / 2 - 0.35 + (W + 0.7) * (col + 0.5) / 6, -0.3 - row * 0.52),
                        (0.5, 0.52, 0.28), jitter=0.12, tilt=0.08)
        slab = [(x0 - sx * 0.1, -W / 2 - 0.4, -0.03), (x0 - sx * 0.1, W / 2 + 0.4, -0.03),
                (x0 + sx * 2.4, W / 2 + 0.4, -0.03), (x0 + sx * 2.4, -W / 2 - 0.4, -0.03),
                (x0 - sx * 0.1, -W / 2 - 0.4, -0.5), (x0 - sx * 0.1, W / 2 + 0.4, -0.5),
                (x0 + sx * 2.4, W / 2 + 0.4, -0.5), (x0 + sx * 2.4, -W / 2 - 0.4, -0.5)]
        b.convex(slab, [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (0, 3, 7, 4)], "RoadDirt")
        b.hull_box((x0 + sx * 1.15, 0, -1.2), (2.5, W + 0.8, 2.34))
    hs = 8
    hx = [-L / 2 + L * k / hs for k in range(hs + 1)]
    for xa, xb in zip(hx, hx[1:], strict=False):
        xm = (xa + xb) / 2
        t = Vector((xb - xa, 0.0, top(xb) - top(xa)))
        length = t.length
        t.normalize()
        up = Vector((-t.z, 0.0, t.x))
        mid = Vector((xm, 0.0, (top(xa) + top(xb)) / 2))
        # The chord sags below the planks at mid-segment by under 2 cm; the hull is lifted 1 cm.
        b.hull_box(mid - up * 0.19, (length + 0.02, W + 0.1, 0.4), (t, Vector((0, 1, 0)), up))
    # No hulls on the railings: a solid hull there stopped every ball fired over or between the
    # rails. The level closes the sides to men only (build_levels.py, place_river).


RECIPES = {
    # Lexington Common, 19 April 1775
    "Meetinghouse1714": meetinghouse,
    "Belfry": belfry,
    # Buckman Tavern (c. 1710): two storeys, five bays, hip roof, two interior chimneys. The
    # dormers of the standing building are left out (date uncertain); colour is a reading.
    "BuckmanTavern": lambda b: house(b, w=13.4, d_main=9.6, wall_h=5.5, pitch_deg=33, found_h=0.4, siding="ClapboardOchre",
                                     roof="hip", chimneys=((-2.7, 1.15, 1.15), (2.7, 1.15, 1.15)), door_head="pediment",
                                     end_windows=2, side_door=True, win=(0.84, 1.45)),
    # Generic houses of the kind that faced the Common (Harrington, Munroe): not portraits.
    "HouseGable": lambda b: house(b, w=11.6, d_main=8.2, wall_h=5.2, pitch_deg=38, siding="ClapboardRed",
                                  chimneys=((0.0, 1.5, 1.35),), end_windows=2, door_head="pediment"),
    "HouseSaltbox": lambda b: house(b, w=11.0, d_main=6.4, lean=3.0, wall_h=5.0, pitch_deg=37, siding="ClapboardWeathered",
                                    trim="ClapboardWeathered", chimneys=((0.0, 1.6, 1.4),), end_windows=1),
    "HouseCape": lambda b: house(b, w=10.2, d_main=7.4, wall_h=2.5, pitch_deg=42, storeys=1, siding="ClapboardWeathered",
                                 trim="PaintedTrim", chimneys=((0.0, 1.5, 1.3),), end_windows=2, found_h=0.3,
                                 win=(0.78, 1.2), win_rows=4),
    "Barn": barn,
    "WagonShed": shed,
    "StoneWall_A": stone_wall,
    "StoneWall_B": stone_wall,
    "StoneWall_C": stone_wall,
    "PostRailFence": post_rail_fence,
    "PostRailFenceEnd": lambda b: post_rail_fence(b, post_end=True),
    "WormFence": worm_fence,
    "Well": well,
    "HitchingRail": hitching_rail,
    "Woodpile": woodpile,
    "TavernSign": tavern_sign,
    "NorthBridge": north_bridge,
    "NorthBridge1775": north_bridge_1775,
    # The Common's schoolhouse. Record: the town built one on the Common in 1715 and one stood
    # there in 1775. Reading: its size (28 x 20 ft, the 1715 vote as I recall it - not checked
    # against the town records), single storey, three bays, unpainted.
    "Schoolhouse": lambda b: house(b, w=8.5, d_main=6.1, wall_h=2.6, pitch_deg=40, storeys=1, bays=3,
                                   siding="ClapboardWeathered", trim="ClapboardWeathered", chimneys=((0.0, 1.1, 1.0),),
                                   end_windows=1, found_h=0.25, win=(0.78, 1.2), win_rows=4, attic_window=False),
    # The Old Manse, Concord (built 1770 for the Rev. William Emerson, 100 m from the North
    # Bridge). Record: two and a half storeys, five bays, two chimneys, weathered clapboards.
    # NOT modelled: its gambrel roof (this kit has gable and hip roofs only) - a stand-in.
    "OldManse": lambda b: house(b, w=13.7, d_main=9.4, wall_h=5.4, pitch_deg=40, siding="ClapboardWeathered",
                                trim="ClapboardWeathered", chimneys=((-3.0, 1.2, 1.2), (3.0, 1.2, 1.2)),
                                door_head="pediment", end_windows=2),
}


# ----------------------------------------------------------------------------- blender scene


def make_material(name):
    tile, _grain, rgb, rough = MATERIALS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bs = m.node_tree.nodes.get("Principled BSDF")
    bs.inputs["Base Color"].default_value = (*rgb, 1)
    bs.inputs["Roughness"].default_value = rough
    m.diffuse_color = (*rgb, 1)
    if name == "Iron":
        bs.inputs["Metallic"].default_value = 0.8
    if name in UNTEXTURED:
        return m
    tex = OUT / "textures"
    files = [tex / f"{name}_{s}.png" for s in ("BaseColor", "Roughness", "Normal")]
    missing = [f.name for f in files if not f.exists()]
    if missing:
        raise FileNotFoundError("missing textures (run build_battlefield_textures.py): " + ", ".join(missing))
    for i, f in enumerate(files):
        node = m.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = bpy.data.images.load(str(f), check_existing=True)
        node.image.colorspace_settings.name = "sRGB" if i == 0 else "Non-Color"
        node.location = (-600, 300 - 280 * i)
        if i < 2:
            m.node_tree.links.new(node.outputs["Color"], bs.inputs[("Base Color", "Roughness")[i]])
        else:
            nm = m.node_tree.nodes.new("ShaderNodeNormalMap")
            nm.location = (-300, -300)
            m.node_tree.links.new(node.outputs["Color"], nm.inputs["Color"])
            m.node_tree.links.new(nm.outputs["Normal"], bs.inputs["Normal"])
    return m


def to_object(name, verts, faces, mats, mi=None, uvs=None, smooth=None, coll=None):
    me = bpy.data.meshes.new(name + "_Mesh")
    me.from_pydata(verts, [], faces)
    for m in mats:
        me.materials.append(m)
    if uvs is not None:
        layer = me.uv_layers.new(name="UV0_Surface")
        for poly, idx, co, sm in zip(me.polygons, mi, uvs, smooth, strict=False):
            poly.material_index = idx
            poly.use_smooth = sm
            for li, uv in zip(poly.loop_indices, co, strict=False):
                layer.data[li].uv = uv
    me.update()
    ob = bpy.data.objects.new(name, me)
    (coll or bpy.context.scene.collection).objects.link(ob)
    return ob


def select(objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def studio(coll, size):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 40
    sc.cycles.use_denoising = True
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for dev in prefs.devices:
            dev.use = True
        sc.cycles.device = "GPU"
    except Exception as exc:  # noqa: BLE001
        print(f"KIT: Cycles on CPU ({exc})")
    sc.render.resolution_x, sc.render.resolution_y = 1200, 900
    sc.render.image_settings.file_format = "PNG"
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.exposure = -1.1
    sc.world = bpy.data.worlds.new("Preview_World")
    sc.world.use_nodes = True
    nt = sc.world.node_tree
    sky = nt.nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(24)
    sky.sun_rotation = math.radians(-125)
    nt.links.new(sky.outputs["Color"], nt.nodes["Background"].inputs["Color"])
    nt.nodes["Background"].inputs["Strength"].default_value = 0.18
    gm = bpy.data.materials.new("PREVIEW_Ground")
    gm.use_nodes = True
    gb = gm.node_tree.nodes.get("Principled BSDF")
    gb.inputs["Base Color"].default_value = (0.13, 0.15, 0.08, 1)
    gb.inputs["Roughness"].default_value = 0.95
    g = to_object("PREVIEW_Ground", [(-200, -200, 0), (200, -200, 0), (200, 200, 0), (-200, 200, 0)], [(0, 1, 2, 3)], [gm], coll=coll)
    cams = {}
    sx, sy, sz = size
    span = max(sx, sy, sz)
    target = Vector((0, 0, sz * 0.45))
    for nm, pos, scale in (("front", Vector((0, -span * 3, sz * 0.5)), max(sx, sz) * 1.25),
                           ("side", Vector((span * 3, 0, sz * 0.5)), max(sy, sz) * 1.25),
                           ("iso", Vector((span * 1.7, -span * 2.4, span * 1.3 + sz * 0.3)), max(sx, sy, sz) * 1.55)):
        data = bpy.data.cameras.new("Camera_" + nm)
        data.type = "ORTHO"
        data.ortho_scale = max(scale, 1.5)
        data.clip_end = 2000
        ob = bpy.data.objects.new("Camera_" + nm, data)
        coll.objects.link(ob)
        ob.location = pos
        ob.rotation_euler = (target - pos).to_track_quat("-Z", "Y").to_euler()
        cams[nm] = ob
    return cams, g


def build_asset(name: str, index: int, render: bool) -> dict:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 1.0
    b = Builder(name, SEED + index * 101)
    RECIPES[name](b)
    asset = bpy.data.collections.new("01_Asset")
    hull_c = bpy.data.collections.new("02_UE5_Collision")
    prev = bpy.data.collections.new("90_Preview_Only")
    for c in (asset, hull_c, prev):
        sc.collection.children.link(c)
    mats = [make_material(m) for m in b.slots]
    mesh_name = "SM_" + name
    ob = to_object(mesh_name, b.v, b.f, mats, b.mi, b.uv, b.smooth, asset)
    ob["units"] = "meters"
    ob["front_axis"] = "-Y"
    ob["asset_role"] = "battlefield_kit"
    hulls = []
    for i, (vs, fs) in enumerate(b.hulls):
        h = to_object(f"UCX_{mesh_name}_{i:02d}", vs, fs, [], coll=hull_c)
        h.display_type = "WIRE"
        h.hide_render = True
        hulls.append(h)
    bpy.context.view_layer.update()
    me = ob.data
    me.calc_loop_triangles()
    tris = len(me.loop_triangles)
    degenerate = sum(t.area < 1e-10 for t in me.loop_triangles)
    lo = [min(v[i] for v in b.v) for i in range(3)]
    hi = [max(v[i] for v in b.v) for i in range(3)]
    info = {"mesh": mesh_name, "triangles": tris, "degenerate_triangles": degenerate, "min_m": lo, "max_m": hi,
            "dimensions_m": [top - low for low, top in zip(lo, hi, strict=False)], "hulls": len(hulls), "slots": list(b.slots)}
    (OUT / "ue5").mkdir(parents=True, exist_ok=True)
    (OUT / "previews").mkdir(exist_ok=True)
    select([ob])
    bpy.ops.export_scene.gltf(filepath=str(OUT / f"{name}.glb"), export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=True, export_texcoords=True, export_normals=True, export_tangents=False,
                              export_materials="EXPORT", export_extras=True,
                              export_image_format="JPEG", export_jpeg_quality=88)  # 19 GLBs of PNG sets ran to 400 MB
    select([ob] + hulls)
    bpy.ops.export_scene.fbx(filepath=str(OUT / "ue5" / f"{mesh_name}.fbx"), use_selection=True, object_types={"MESH"},
                             global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
                             axis_forward="-Y", axis_up="Z", use_space_transform=True, bake_space_transform=False,
                             use_mesh_modifiers=True, mesh_smooth_type="FACE", use_triangles=True,
                             add_leaf_bones=False, bake_anim=False, path_mode="AUTO", embed_textures=False)
    cams, _ground = studio(prev, info["dimensions_m"])
    sun = bpy.data.lights.new("Sun", "SUN")
    sun.energy = 1.4
    sun.angle = 0.05
    so = bpy.data.objects.new("Sun", sun)
    prev.objects.link(so)
    so.rotation_euler = (math.radians(62), 0, math.radians(-38))
    for im in bpy.data.images:
        if im.source == "FILE" and not im.packed_file:
            im.pack()
    sc.camera = cams["iso"]
    hull_c.hide_viewport = True
    sc["kit_asset"] = name
    sc["asset_notes"] = "Play America 250 battlefield kit; preview ground, cameras and lights are not exported."
    bpy.context.preferences.filepaths.save_version = 0  # no .blend1 copies beside the pack
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / f"{name}.blend"), compress=True)
    if render:
        for nm, cam in cams.items():
            sc.camera = cam
            sc.render.filepath = str(OUT / "previews" / f"{name}_{nm}.png")
            bpy.ops.render.render(write_still=True)
    print("KIT_ASSET " + json.dumps(info), flush=True)
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--no-render", action="store_true")
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    a = ap.parse_args(argv)
    names = [n for n in a.only.split(",") if n] or list(RECIPES)
    OUT.mkdir(parents=True, exist_ok=True)
    manifest_path = OUT / "kit_manifest.txt"  # JSON text; .txt because the repo ignores *.json
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    for name in names:
        manifest[name] = build_asset(name, list(RECIPES).index(name), not a.no_render)
        manifest_path.write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")
    print(f"KIT_DONE {len(names)} assets, blender {bpy.app.version_string}", flush=True)


if __name__ == "__main__":
    main()
