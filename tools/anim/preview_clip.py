"""Stick-figure contact sheet of a Kimodo clip (for a quick look before anything goes to Unreal).

  python preview_clip.py <clip.npz> <out.png> [--frames 8] [--targets manifest.json:KEY]
Top row: seen from the character's front-right quarter; bottom row: from above. Red dots are hand targets.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

BVH_REST = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass\kimodo\kimodo\assets\skeletons\somaskel77\somaskel77_standard_tpose.bvh")


def parents_from_bvh(path: Path):
    names, parents, stack = [], [], []
    for line in path.read_text().split("MOTION")[0].splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] in ("ROOT", "JOINT"):
            names.append(t[1]); parents.append(stack[-1] if stack else -1); pending = len(names) - 1
        elif t[0] == "End":
            pending = None
        elif t[0] == "{":
            stack.append(pending if pending is not None else -2)
        elif t[0] == "}":
            stack.pop()
    return names, parents


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("npz"); ap.add_argument("out")
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--targets", default=None)
    a = ap.parse_args()
    pj = np.load(a.npz)["posed_joints"]
    names, parents = parents_from_bvh(BVH_REST)
    if len(names) == pj.shape[1] + 1:  # the BVH has an extra Root above Hips
        names, parents = names[1:], [p - 1 for p in parents[1:]]
    keep = [i for i, n in enumerate(names) if not re.search(r"Thumb|Index|Ring|Pinky|Eye|Jaw|Middle[1-3]", n)]
    targets = []
    if a.targets:
        mpath, key = a.targets.rsplit(":", 1)
        for r in json.loads(Path(mpath).read_text())["clips"][key]["constraints"]:
            if r.get("type") == "hands":
                targets += [(r["frame"], np.array(p)) for p in r["targets_kimodo_m"].values()]
    T = len(pj); idx = np.linspace(0, T - 1, a.frames).round().astype(int)
    for f, _ in targets:
        idx[np.argmin(np.abs(idx - f))] = f
    W, H = 260, 300
    img = Image.new("RGB", (W * len(idx), H * 2), "white"); d = ImageDraw.Draw(img)
    c = np.cos(np.radians(35)); s = np.sin(np.radians(35))

    def quarter(p):  # rotate about Y, look along -Z'
        return np.array([p[0] * c - p[2] * s, p[1]])

    def top(p):
        return np.array([p[0], p[2]])

    for col, f in enumerate(idx):
        for row, proj, org, sc in ((0, quarter, (W * 0.5, H * 0.92), 135.0), (1, top, (W * 0.5, H * 0.7), 110.0)):
            ox, oy = col * W + org[0], row * H + org[1]
            ref = pj[f, 0] if row == 0 else pj[0, 0]
            base = np.array([ref[0], 0.0, ref[2]])

            def px(p):
                q = proj(p - base); return (ox - q[0] * sc, oy - q[1] * sc)
            if row == 0:
                d.line([(col * W + 10, oy), (col * W + W - 10, oy)], fill=(190, 190, 190))
            for i in keep:
                if parents[i] >= 0:
                    left = names[i].startswith("Left")
                    d.line([px(pj[f, parents[i]]), px(pj[f, i])], fill=(0, 90, 200) if left else (40, 40, 40), width=3)
            for tf, tp in targets:
                if tf == f:
                    x, y = px(tp); d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(220, 0, 0))
            d.text((col * W + 6, row * H + 4), "f%d %s" % (f, "front-quarter" if row == 0 else "top (fwd up)"), fill=(0, 0, 0))
    img.save(a.out)
    print("wrote", a.out, img.size)


if __name__ == "__main__":
    main()
