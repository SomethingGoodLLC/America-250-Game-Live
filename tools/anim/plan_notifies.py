"""Work out each clip's anim-notify times from its motion and store them in manifest.json.

  Notify_GunStep_Done      last frame of every gun drill step (not the idles or the flinch)
  Notify_Volley_Discharge  the recoil frame of the fire clips: where the right shoulder starts its fastest jerk back
  Notify_Reload_PhaseEnd   last frame of the three reload clips
  Notify_Ragdoll_Handoff   last frame of the knockdowns
  Notify_FootPlant         each frame an ankle comes down (start of a contact phase of LeftFoot / RightFoot)

Run with Kimodo's venv (numpy only): python plan_notifies.py [--out <AnimationPass>]
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import numpy as np

DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
REST_BVH = "kimodo/kimodo/assets/skeletons/somaskel77/somaskel77_standard_tpose.bvh"
GUN_STEPS = ("AM_Gun_Sponge", "AM_Gun_Vent", "AM_Gun_Load", "AM_Gun_Ram", "AM_Gun_Prime", "AM_Gun_Traverse",
             "AM_Gun_RunUp", "AM_Gun_Lay", "AM_Gun_Fire")
FIRE = ("AM_Volley_Fire", "AM_Volley_Kneel_Fire")
RELOAD = ("AM_Reload_PrimePan", "AM_Reload_RamCharge", "AM_Reload_Shoulder")
FPS = 30.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    a = ap.parse_args()
    out_root = Path(a.out)
    names = re.findall(r"(?:ROOT|JOINT)\s+(\S+)", (out_root / REST_BVH).read_text().split("MOTION")[0])
    names = names[1:] if names[0] == "Root" else names
    j_sh = names.index("RightArm")
    mpath = out_root / "manifest.json"
    manifest = json.loads(mpath.read_text(encoding="utf-8"))
    count = {}
    for key, m in manifest["clips"].items():
        d = np.load(out_root / "native" / (key + ".npz"))
        pj, fc = d["posed_joints"], d["foot_contacts"] > 0.5
        last = (len(pj) - 1) / FPS
        notes = []
        if m["name"] in GUN_STEPS:
            notes.append({"name": "Notify_GunStep_Done", "time": last})
        if m["name"] in RELOAD:
            notes.append({"name": "Notify_Reload_PhaseEnd", "time": last})
        if m["name"].startswith("AM_React_BlastKnockdown"):
            notes.append({"name": "Notify_Ragdoll_Handoff", "time": last})
        if m["name"] in FIRE:
            # backward speed of the right shoulder relative to the hips, in the hips' own forward direction at frame 0
            rel = pj[:, j_sh] - pj[:, 0]
            back = -(rel[1:, 2] - rel[:-1, 2]) * FPS
            f = int(np.argmax(back[: int(len(back) * 0.8)]))
            notes.append({"name": "Notify_Volley_Discharge", "time": f / FPS, "frame": f, "back_speed_m_s": round(float(back[f]), 3)})
        for col in (0, 3):  # LeftFoot, RightFoot
            on = fc[:, col]
            for t in range(1, len(on)):
                if on[t] and not on[t - 1]:
                    notes.append({"name": "Notify_FootPlant", "time": t / FPS, "foot": "left" if col == 0 else "right"})
        m["notifies"] = sorted(notes, key=lambda n: n["time"])
        for n in notes:
            count[n["name"]] = count.get(n["name"], 0) + 1
    mpath.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    print("notifies planned:", json.dumps(count, sort_keys=True))


if __name__ == "__main__":
    main()
