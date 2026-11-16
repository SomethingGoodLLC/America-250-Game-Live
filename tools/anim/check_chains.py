"""Measure the end-to-start pose gap of every chain, directly and through its transition clip.

Chains: every `chain_to` pair in clip_specs.yaml, and each ragdoll rest class (the last frame of a
knockdown: F -> on the back, B -> face down, L and R -> on the side) into each matching AM_GetUp_*.
A join is within threshold when the root is within 5 cm and every joint within 10 degrees.
For same-place chains (the drill, the volley) the root is compared where it stands; for ragdoll -> get-up
the two clips are first laid over each other (root moved to the same spot, best turn about the vertical).

A chain passes when every variant pairing joins directly, or when a transition clip named
AM_<X>_to_<Y> exists in manifest.json["transitions"] and joins both of its ends for that pairing.

  python check_chains.py [--out <AnimationPass>] [--specs clip_specs.yaml] [--report check_chains_report.json]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import yaml

HERE = Path(__file__).resolve().parent
DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
ROOT_LIMIT_M, JOINT_LIMIT_DEG = 0.05, 10.0
RAGDOLL = {  # knockdown clip -> get-up clip its rest pose leads into
    "AM_React_BlastKnockdown_F": "AM_GetUp_Back",
    "AM_React_BlastKnockdown_B": "AM_GetUp_Front",
    "AM_React_BlastKnockdown_L": "AM_GetUp_Side",
    "AM_React_BlastKnockdown_R": "AM_GetUp_Side",
}


def angle_deg(Ra: np.ndarray, Rb: np.ndarray) -> np.ndarray:
    """Rotation angle between matching 3x3 matrices, in degrees."""
    tr = np.einsum("...ij,...ij->...", Ra, Rb)
    return np.degrees(np.arccos(np.clip((tr - 1.0) / 2.0, -1.0, 1.0)))


def yaw(deg: float) -> np.ndarray:
    c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg))
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def gap(end: dict, start: dict, align: bool) -> dict:
    """Pose gap between a clip's last frame and another clip's first frame."""
    la, lb = end["local_rot_mats"][-1], start["local_rot_mats"][0]
    pa, pb = end["root_positions"][-1], start["root_positions"][0]
    joints = angle_deg(la[1:], lb[1:])
    if align:
        root_m = abs(float(pa[1] - pb[1]))
        root_rot = min(float(angle_deg(yaw(d) @ la[0], lb[0])) for d in range(0, 360, 1))
    else:
        root_m = float(np.linalg.norm(pa - pb))
        root_rot = float(angle_deg(la[0], lb[0]))
    worst = max(float(joints.max()), root_rot)
    return {"root_cm": round(root_m * 100, 2), "worst_joint_deg": round(worst, 2),
            "ok": root_m <= ROOT_LIMIT_M and worst <= JOINT_LIMIT_DEG}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--specs", default=str(HERE / "clip_specs.yaml"))
    ap.add_argument("--report", default=None)
    a = ap.parse_args()
    out_root = Path(a.out)
    manifest = json.loads((out_root / "manifest.json").read_text(encoding="utf-8"))
    specs = {s["name"]: s for s in yaml.safe_load(Path(a.specs).read_text(encoding="utf-8"))}
    transitions = manifest.get("transitions", {})
    cache: dict[str, dict] = {}

    def load(folder: str, key: str) -> dict:
        if key not in cache:
            d = np.load(out_root / folder / (key + ".npz"))
            cache[key] = {k: d[k] for k in ("local_rot_mats", "root_positions")}
        return cache[key]

    pairs = [(n, s["chain_to"], False) for n, s in specs.items() if s.get("chain_to") in specs]
    pairs += [(x, y, True) for x, y in RAGDOLL.items() if x in specs and y in specs]
    rows, all_ok = [], True
    print("%-30s %-28s %5s %9s %9s %7s  %s" % ("from", "to", "pairs", "root cm", "joint deg", "direct", "result"))
    for x, y, align in pairs:
        worst_root = worst_joint = 0.0
        direct_ok = via_ok = True
        n = 0
        for vx in range(int(specs[x].get("variants", 1))):
            for vy in range(int(specs[y].get("variants", 1))):
                kx, ky = "%s_v%d" % (x, vx), "%s_v%d" % (y, vy)
                g = gap(load("native", kx), load("native", ky), align)
                n += 1
                worst_root, worst_joint = max(worst_root, g["root_cm"]), max(worst_joint, g["worst_joint_deg"])
                if g["ok"]:
                    continue
                direct_ok = False
                tkey = next((k for k, t in transitions.items() if t["from"] == kx and t["to"] == ky), None)
                if tkey is None:
                    via_ok = False
                    continue
                t = load("transitions", tkey)
                if not (gap(load("native", kx), t, align)["ok"] and gap(t, load("native", ky), align)["ok"]):
                    via_ok = False
        ok = direct_ok or via_ok
        all_ok &= ok
        result = "PASS direct" if direct_ok else ("PASS via transition" if via_ok else "FAIL: needs transition")
        rows.append({"from": x, "to": y, "pairings": n, "worst_root_cm": worst_root, "worst_joint_deg": worst_joint,
                     "direct": direct_ok, "ok": ok, "ragdoll": align})
        print("%-30s %-28s %5d %9.2f %9.2f %7s  %s" % (x, y, n, worst_root, worst_joint, "yes" if direct_ok else "no", result))
    print("\n%d chains, %d pass" % (len(rows), sum(r["ok"] for r in rows)))
    if a.report:
        Path(a.report).write_text(json.dumps(rows, indent=2), encoding="utf-8")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
