"""Check every generated clip in manifest.json against the pass's limits.

Per clip variant: the clip's `expect` block (travel, direction, lying or standing at its ends), no NaN/inf, 30 fps, duration within 0.1 s of its spec, foot skate under 3 cm in every
contact phase, and for constrained clips hand-to-target error under 5 cm at each constraint frame
(recomputed from the NPZ, not read back from the manifest).

  python check_clips.py [--out <AnimationPass>] [--specs clip_specs.yaml] [--report check_clips_report.json]
Exit code 0 only if every clip passes and every spec variant is present.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
import yaml

from expectations import failures as expectation_failures

HERE = Path(__file__).resolve().parent
DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
REST_BVH = "kimodo/kimodo/assets/skeletons/somaskel77/somaskel77_standard_tpose.bvh"
FOOT_JOINTS = ["LeftFoot", "LeftToeBase", "LeftToeEnd", "RightFoot", "RightToeBase", "RightToeEnd"]  # contact columns
HAND = {"left_hand": ("LeftHand", "LeftHandMiddleEnd"), "right_hand": ("RightHand", "RightHandMiddleEnd")}
SKATE_LIMIT_M, HAND_LIMIT_M, DURATION_TOL_S, FPS = 0.03, 0.05, 0.1, 30.0
MIN_PHASE_FRAMES = 3  # a one- or two-frame touch is not a plant


def joint_names(out_root: Path) -> list[str]:
    text = (out_root / REST_BVH).read_text().split("MOTION")[0]
    names = re.findall(r"(?:ROOT|JOINT)\s+(\S+)", text)
    return names[1:] if names[0] == "Root" else names


def bvh_fps(path: Path) -> float:
    for line in path.open(encoding="utf-8"):
        if line.startswith("Frame Time:"):
            return 1.0 / float(line.split(":")[1])
    return float("nan")


def worst_skate(pj: np.ndarray, contacts: np.ndarray, idx: dict) -> tuple[float, int]:
    """Largest horizontal travel of a foot joint inside one of its contact phases, and the phase count."""
    worst, phases = 0.0, 0
    for col, name in enumerate(FOOT_JOINTS[: contacts.shape[1]]):
        on = contacts[:, col] > 0.5
        t = 0
        while t < len(on):
            if not on[t]:
                t += 1
                continue
            e = t
            while e + 1 < len(on) and on[e + 1]:
                e += 1
            if e - t + 1 >= MIN_PHASE_FRAMES:
                p = pj[t : e + 1, idx[name]][:, [0, 2]]
                worst = max(worst, float(np.linalg.norm(p - p[0], axis=1).max()))
                phases += 1
            t = e + 1
    return worst, phases


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--specs", default=str(HERE / "clip_specs.yaml"))
    ap.add_argument("--report", default=None)
    a = ap.parse_args()
    out_root = Path(a.out)
    manifest = json.loads((out_root / "manifest.json").read_text(encoding="utf-8"))["clips"]
    specs = yaml.safe_load(Path(a.specs).read_text(encoding="utf-8"))
    names = joint_names(out_root)
    idx = {n: i for i, n in enumerate(names)}
    rows, missing = [], []
    for spec in specs:
        for v in range(int(spec.get("variants", 1))):
            key = "%s_v%d" % (spec["name"], v)
            m = manifest.get(key)
            if m is None or not (out_root / "native" / (key + ".npz")).exists():
                missing.append(key)
                continue
            d = np.load(out_root / "native" / (key + ".npz"))
            pj = d["posed_joints"]
            finite = all(np.isfinite(d[k]).all() for k in d.files)
            fps = bvh_fps(out_root / "bvh" / (key + ".bvh"))
            duration = len(pj) / FPS
            skate, phases = worst_skate(pj, d["foot_contacts"], idx)
            hand = 0.0
            for r in m["constraints"]:
                if r.get("type") != "hands":
                    continue
                for h, tgt in r["targets_kimodo_m"].items():
                    g = 0.5 * (pj[r["frame"], idx[HAND[h][0]]] + pj[r["frame"], idx[HAND[h][1]]])
                    hand = max(hand, float(np.linalg.norm(g - np.array(tgt))))
            fails = []
            if not finite:
                fails.append("NaN")
            if abs(fps - FPS) > 1e-3 or abs(m["fps"] - FPS) > 1e-3:
                fails.append("fps %.3f" % fps)
            if abs(duration - float(spec["duration"])) > DURATION_TOL_S:
                fails.append("duration %.2f vs %.2f" % (duration, spec["duration"]))
            if skate > SKATE_LIMIT_M:
                fails.append("skate %.1f cm" % (skate * 100))
            if hand > HAND_LIMIT_M:
                fails.append("hand %.1f cm" % (hand * 100))
            fails += expectation_failures(d["root_positions"], spec.get("expect"))
            rows.append({"clip": key, "seed": m["seed"], "tries": len(m.get("attempts", [1])), "finite": finite,
                         "fps": round(fps, 3), "duration_s": round(duration, 3), "skate_cm": round(skate * 100, 2),
                         "contact_phases": phases, "hand_cm": round(hand * 100, 2), "fails": fails})
    print("%-34s %7s %5s %6s %8s %7s  %s" % ("clip", "seed", "dur", "fps", "skate cm", "hand cm", "result"))
    for r in rows:
        print("%-34s %7d %5.2f %6.2f %8.2f %7.2f  %s" % (
            r["clip"], r["seed"], r["duration_s"], r["fps"], r["skate_cm"], r["hand_cm"],
            "PASS" if not r["fails"] else "FAIL: " + ", ".join(r["fails"])))
    failed = [r for r in rows if r["fails"]]
    print("\n%d variants checked, %d pass, %d fail, %d missing" % (len(rows), len(rows) - len(failed), len(failed), len(missing)))
    if missing:
        print("missing:", ", ".join(missing))
    if a.report:
        Path(a.report).write_text(json.dumps({"rows": rows, "missing": missing}, indent=2), encoding="utf-8")
    return 0 if not failed and not missing else 1


if __name__ == "__main__":
    sys.exit(main())
