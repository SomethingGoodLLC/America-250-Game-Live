"""Checks for faults a reviewer has already found by eye, so they cannot come back unnoticed.

check_clips.py says a clip is well formed and does roughly what its name says. It cannot say a clip looks right:
the first Make Ready passed it with the men hunched and their hands over their faces (user review 2026-11-16).
These are the faults seen so far, measured on the generated motion (SOMA space, before the retarget):

  hands_at_face   a hand in front of the face (within 12 cm sideways of the head, from 5 cm below the head joint to
                  20 above it, 5 to 28 cm ahead of it) for more than a quarter of the clip, in a clip not meant to
                  (clip_specs.yaml `intent: [hands_at_face]` marks the ones that are: spyglass, shielding the face, ...)
  wide_stance     ankles more than 48 cm apart sideways (shoulder joints are 33 cm apart) for most of a clip in which
                  he stands throughout, not marked `intent: [wide_stance]`
  barely_moves    a clip that is not an idle or a hold (`intent: [hold]`) in which no hand, foot, head or hip travels 15 cm
  hunched         the head more than 22 cm ahead of the hips for most of a clip in which he stands throughout, not
                  marked `intent: [stoop]`

A flag is a reason to look at the clip, not a verdict. Prop faults (a staff through the barrel, a musket through the
head) are checked on the finished soldier clips in Unreal: ue_finish_clips.py.

  python check_quality.py [--out <AnimationPass>] [--specs clip_specs.yaml] [--report quality_report.json]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
import yaml

HERE = Path(__file__).resolve().parent
DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
REST_BVH = "kimodo/kimodo/assets/skeletons/somaskel77/somaskel77_standard_tpose.bvh"
FACE_SHARE = 0.25
STANCE_CM, STANCE_SHARE = 48.0, 0.6
MOVE_CM = 15.0
HUNCH_CM, HUNCH_SHARE = 22.0, 0.6
STANDING_HIP_M = 0.8


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--specs", default=str(HERE / "clip_specs.yaml"))
    ap.add_argument("--report", default=None)
    a = ap.parse_args()
    out_root = Path(a.out)
    names = re.findall(r"(?:ROOT|JOINT)\s+(\S+)", (out_root / REST_BVH).read_text().split("MOTION")[0])
    names = names[1:] if names[0] == "Root" else names
    ix = {n: i for i, n in enumerate(names)}
    specs = yaml.safe_load(Path(a.specs).read_text(encoding="utf-8"))
    rows = []
    for spec in specs:
        intent = set(spec.get("intent") or [])
        for v in range(int(spec.get("variants", 1))):
            key = "%s_v%d" % (spec["name"], v)
            path = out_root / "native" / (key + ".npz")
            if not path.exists():
                continue
            pj = np.load(path)["posed_joints"].astype(float)
            head, hips = pj[:, ix["Head"]], pj[:, ix["Hips"]]
            standing = hips[:, 1] > STANDING_HIP_M
            grips = [0.5 * (pj[:, ix[s + "Hand"]] + pj[:, ix[s + "HandMiddleEnd"]]) for s in ("Left", "Right")]
            right = pj[:, ix["RightLeg"]] - pj[:, ix["LeftLeg"]]
            right[:, 1] = 0.0
            right /= np.maximum(np.linalg.norm(right, axis=1, keepdims=True), 1e-6)
            ahead = np.cross(right, np.array([0.0, 1.0, 0.0]))  # the way the hips face, on the ground
            at_face = np.zeros(len(pj), dtype=bool)
            for g in grips:
                d = (g - head) * 100.0
                side, up, front = (d * right).sum(axis=1), d[:, 1], (d * ahead).sum(axis=1)
                at_face |= (np.abs(side) < 12.0) & (up > -5.0) & (up < 20.0) & (front > 5.0) & (front < 28.0)
            face_share = float(at_face.mean())
            stands_throughout = float(standing.mean()) >= 0.95
            apart = np.abs(((pj[:, ix["RightFoot"]] - pj[:, ix["LeftFoot"]]) * right).sum(axis=1)) * 100.0
            stance_share = float((apart > STANCE_CM).mean()) if stands_throughout else 0.0
            travel = max(float(np.linalg.norm(pj[:, ix[n]] - pj[0, ix[n]], axis=1).max()) * 100.0
                         for n in ("LeftHand", "RightHand", "LeftFoot", "RightFoot", "Head", "Hips"))
            fwd = np.cross(right, np.array([0.0, 1.0, 0.0]))  # the way the hips face, on the ground
            lean = ((head - hips) * fwd).sum(axis=1) * 100.0
            lean = np.abs(lean)
            hunch_share = float((lean > HUNCH_CM).mean()) if stands_throughout else 0.0
            flags = []
            if face_share > FACE_SHARE and "hands_at_face" not in intent:
                flags.append("hands_at_face %.0f%% of frames" % (face_share * 100))
            if stance_share > STANCE_SHARE and "wide_stance" not in intent:
                flags.append("wide_stance %.0f cm median" % float(np.median(apart[standing])))
            if travel < MOVE_CM and not spec.get("loop") and "hold" not in intent:
                flags.append("barely_moves %.0f cm" % travel)
            if hunch_share > HUNCH_SHARE and "stoop" not in intent:
                flags.append("hunched %.0f cm median" % float(np.median(lean[standing])))
            rows.append({"clip": key, "hands_at_face_share": round(face_share, 2),
                         "stance_cm_median": round(float(np.median(apart[standing])), 1) if standing.any() else None,
                         "most_travel_cm": round(travel, 1),
                         "lean_cm_median": round(float(np.median(lean[standing])), 1) if standing.any() else None,
                         "flags": flags})
    print("%-34s %6s %7s %7s %6s  %s" % ("clip", "face%", "stance", "travel", "lean", "flags"))
    for r in rows:
        print("%-34s %6.0f %7s %7.0f %6s  %s" % (
            r["clip"], r["hands_at_face_share"] * 100, r["stance_cm_median"], r["most_travel_cm"], r["lean_cm_median"],
            "; ".join(r["flags"]) or "-"))
    flagged = [r for r in rows if r["flags"]]
    print("\n%d variants looked at, %d flagged" % (len(rows), len(flagged)))
    if a.report:
        Path(a.report).write_text(json.dumps(rows, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
