"""Make AM_X_to_Y transition clips with the MotionBricks SOMA in-betweening engine.

Conditioned only on this pass's own Kimodo clips: the context is the last four frames of clip X, the
target the first four frames of clip Y. Nothing from MotionBricks' bundled sample motions or smart
primitives is read. Run with the MotionBricks venv (numpy, scipy, warp):

  motionbricks/.venv/Scripts/python.exe tools/anim/make_transitions.py --motionbricks <checkout> [--out <AnimationPass>]

Which pairings need one comes from the same thresholds as check_chains.py (root 5 cm, joints 10 degrees).
For ragdoll -> get-up the get-up's first poses are first laid over the knockdown's last pose (root moved
to the same spot, best turn about the vertical); the clip is in the knockdown's space.

Rig conversion. Both rigs have the same 77 joints in the same order and the same world axes (Y up).
Kimodo's joint frames are world-aligned in the T-pose; MotionBricks' point along the bones. With C_j the
MotionBricks global rotation of joint j in its reference pose (a T-pose within 4 cm of Kimodo's):
  G_mb_j = G_kimodo_j @ C_j     and back     G_kimodo_j = G_mb_j @ C_j^T
The engine takes the root in its runtime space: root_runtime = model_to_runtime * root_model.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation as Rot

DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
ROOT_LIMIT_M, JOINT_LIMIT_DEG = 0.05, 10.0
RAGDOLL = {
    "AM_React_BlastKnockdown_F": "AM_GetUp_Back",
    "AM_React_BlastKnockdown_B": "AM_GetUp_Front",
    "AM_React_BlastKnockdown_L": "AM_GetUp_Side",
    "AM_React_BlastKnockdown_R": "AM_GetUp_Side",
}


def angle_deg(Ra, Rb):
    tr = np.einsum("...ij,...ij->...", Ra, Rb)
    return np.degrees(np.arccos(np.clip((tr - 1.0) / 2.0, -1.0, 1.0)))


def yaw(deg):
    c, s = np.cos(np.radians(deg)), np.sin(np.radians(deg))
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def best_yaw(root_a, root_b) -> float:
    """Turn about the vertical that brings root rotation b closest to a."""
    return float(min(range(0, 360), key=lambda d: float(angle_deg(root_a, yaw(d) @ root_b))))


def gap(la, pa, lb, pb, align: bool):
    joints = float(angle_deg(la[1:], lb[1:]).max())
    if align:
        root_m = abs(float(pa[1] - pb[1]))
        root_rot = min(float(angle_deg(yaw(d) @ la[0], lb[0])) for d in range(0, 360))
    else:
        root_m = float(np.linalg.norm(pa - pb))
        root_rot = float(angle_deg(la[0], lb[0]))
    worst = max(joints, root_rot)
    return {"root_cm": round(root_m * 100, 2), "worst_joint_deg": round(worst, 2),
            "ok": root_m <= ROOT_LIMIT_M and worst <= JOINT_LIMIT_DEG}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--motionbricks", type=Path, required=True)
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--only", action="append", default=[], help="FROMKEY:TOKEY, e.g. AM_GetUp_..._v0:..._v1")
    a = ap.parse_args()
    out_root = Path(a.out)
    root = a.motionbricks.resolve()
    sys.path.insert(0, str(root))
    sys.path.insert(0, str(root / "docs/examples"))
    os.chdir(root)
    import warp as wp
    from _rig import load_rig
    from motionbricks.model.config import MotionBricksInferenceConfig

    wp.init()
    wp.set_device("cuda:0")
    rig = load_rig("soma")
    names = rig.skeleton.joint_names
    parents = rig.skeleton.parent_indices.numpy().tolist()
    rest = rig.reference_pose.numpy().astype(np.float64)
    J = len(names)
    M = np.array(list(rig.model_to_runtime), dtype=np.float64)
    M_rot, M_pos = Rot.from_quat(M[3:]), M[:3]
    rest_t = rest[:, :3]
    C = np.zeros((J, 3, 3))  # MotionBricks global joint rotations in its reference pose
    for j in range(J):
        r = Rot.from_quat(rest[j, 3:]).as_matrix()
        C[j] = r if parents[j] < 0 else C[parents[j]] @ r
    revision = subprocess.check_output(["git", "-c", "safe.directory=%s" % root.as_posix(), "rev-parse", "HEAD"],
                                       cwd=root, text=True).strip()

    tpose = np.load(out_root / "logs" / "kimodo_tpose.npz")
    if list(tpose["names"]) != list(names) or list(tpose["parents"]) != parents:
        raise SystemExit("Kimodo's and MotionBricks' SOMA joint lists differ; no joint map is written for that case")
    k_off = tpose["pos"] - np.where(np.array(parents)[:, None] >= 0, tpose["pos"][np.maximum(parents, 0)], tpose["pos"])

    def to_mb(gk, root_pos):
        """Kimodo global rotations [F,J,3,3] + root positions [F,3] -> MotionBricks local transforms [F,J,7]."""
        F = gk.shape[0]
        gm = gk @ C[None]
        out = np.zeros((F, J, 7))
        for j in range(J):
            if parents[j] < 0:
                out[:, j, :3] = M_rot.apply(root_pos) + M_pos
                out[:, j, 3:] = (M_rot * Rot.from_matrix(gm[:, j])).as_quat()
            else:
                out[:, j, :3] = rest_t[j]
                out[:, j, 3:] = Rot.from_matrix(np.swapaxes(gm[:, parents[j]], -1, -2) @ gm[:, j]).as_quat()
        return out.astype(np.float32)

    def from_mb(poses):
        """MotionBricks local transforms [F,J,7] -> Kimodo local rots, global rots, joint positions, root positions."""
        F = poses.shape[0]
        gm = np.zeros((F, J, 3, 3)); root_pos = None
        for j in range(J):
            r = Rot.from_quat(poses[:, j, 3:])
            if parents[j] < 0:
                gm[:, j] = (M_rot.inv() * r).as_matrix()
                root_pos = M_rot.inv().apply(poses[:, j, :3] - M_pos)
            else:
                gm[:, j] = gm[:, parents[j]] @ r.as_matrix()
        gk = gm @ np.swapaxes(C, -1, -2)[None]
        local = np.zeros_like(gk); pos = np.zeros((F, J, 3))
        for j in range(J):
            if parents[j] < 0:
                local[:, j] = gk[:, j]; pos[:, j] = root_pos
            else:
                local[:, j] = np.swapaxes(gk[:, parents[j]], -1, -2) @ gk[:, j]
                pos[:, j] = pos[:, parents[j]] + np.einsum("fij,j->fi", gk[:, parents[j]], k_off[j])
        return local, gk, pos, root_pos

    mpath = out_root / "manifest.json"
    manifest = json.loads(mpath.read_text(encoding="utf-8"))
    clips = manifest["clips"]
    by_name: dict[str, list[str]] = {}
    for key, m in sorted(clips.items()):
        by_name.setdefault(m["name"], []).append(key)
    pairs = [(n, m["chain_to"], False) for n, m in {m["name"]: m for m in clips.values()}.items() if m.get("chain_to") in by_name]
    pairs += [(x, y, True) for x, y in RAGDOLL.items() if x in by_name and y in by_name]
    cache = {}

    def load(key):
        if key not in cache:
            d = np.load(out_root / "native" / (key + ".npz"))
            cache[key] = {k: d[k].astype(np.float64) for k in ("local_rot_mats", "global_rot_mats", "root_positions")}
        return cache[key]

    (out_root / "transitions").mkdir(exist_ok=True)
    transitions = manifest.setdefault("transitions", {})
    made = failed = 0
    with rig.engine as engine, wp.ScopedStream(engine.stream):
        context = engine.create_input(rig.skeleton)
        target = engine.create_input(rig.skeleton)
        output = engine.create_output(rig.skeleton)
        for x, y, align in pairs:
            for kx in by_name[x]:
                for ky in by_name[y]:
                    if a.only and ("%s:%s" % (kx, ky)) not in a.only:
                        continue
                    X, Y = load(kx), load(ky)
                    direct = gap(X["local_rot_mats"][-1], X["root_positions"][-1], Y["local_rot_mats"][0], Y["root_positions"][0], align)
                    if direct["ok"]:
                        continue
                    gy, py, turn = Y["global_rot_mats"][:4], Y["root_positions"][:4].copy(), 0.0
                    if align:
                        turn = best_yaw(X["global_rot_mats"][-1, 0], gy[0, 0])
                        Ry = yaw(turn)
                        gy = Ry[None, None] @ gy
                        anchor = py[0].copy()
                        py = (py - anchor) @ Ry.T
                        py[:, [0, 2]] += X["root_positions"][-1][[0, 2]]
                        py[:, 1] += anchor[1]
                    ctx = to_mb(X["global_rot_mats"][-4:], X["root_positions"][-4:])
                    tgt = to_mb(gy, py)
                    context.update_from_poses(wp.array(ctx, dtype=wp.transform), stream=engine.stream)
                    target.update_from_poses(wp.array(tgt, dtype=wp.transform), stream=engine.stream)
                    config = MotionBricksInferenceConfig()
                    engine.generate_animation(context, target, output, config=config)
                    n = int(output.num_frames)
                    poses = output.poses[:n].numpy().astype(np.float64)
                    contacts = output.foot_contacts[:n].numpy()
                    if not np.isfinite(poses).all():
                        print("FAIL non-finite output for %s -> %s" % (kx, ky)); failed += 1; continue
                    local, gk, pos, rpos = from_mb(poses)
                    ylocal0 = Y["local_rot_mats"][0].copy()
                    ylocal0[0] = gy[0, 0]
                    g_in = gap(X["local_rot_mats"][-1], X["root_positions"][-1], local[0], rpos[0], False)
                    g_out = gap(local[-1], rpos[-1], ylocal0, py[0], False)
                    name = "%s_to_%s_v%s_v%s" % (x, y[3:], kx.rsplit("_v", 1)[1], ky.rsplit("_v", 1)[1])
                    npz = out_root / "transitions" / (name + ".npz")
                    np.savez(npz, local_rot_mats=local.astype(np.float32), global_rot_mats=gk.astype(np.float32),
                             posed_joints=pos.astype(np.float32), root_positions=rpos.astype(np.float32),
                             foot_contacts=contacts.astype(np.float32))
                    transitions[name] = {
                        "from": kx, "to": ky, "frames": n, "fps": 30.0, "duration_s": n / 30.0,
                        "tool": "MotionBricks SOMA in-betweening engine", "tool_revision": revision,
                        "conditioned_on": "last 4 frames of %s, first 4 frames of %s" % (kx, ky),
                        "config": {"allowed_token_counts": list(config.allowed_token_counts),
                                   "correct_generated_diff": bool(config.correct_generated_diff)},
                        "ragdoll_alignment": {"turn_deg": turn} if align else None,
                        "direct_gap": direct, "gap_in": g_in, "gap_out": g_out,
                        "ok": bool(g_in["ok"] and g_out["ok"]),
                        "files": {"npz": npz.name, "npz_sha256": hashlib.sha256(npz.read_bytes()).hexdigest()},
                    }
                    made += 1
                    failed += 0 if transitions[name]["ok"] else 1
                    print("%-52s %3d frames  direct %5.1f cm %6.1f deg | in %4.1f cm %5.1f deg | out %4.1f cm %5.1f deg  %s" % (
                        name, n, direct["root_cm"], direct["worst_joint_deg"], g_in["root_cm"], g_in["worst_joint_deg"],
                        g_out["root_cm"], g_out["worst_joint_deg"], "OK" if transitions[name]["ok"] else "ENDS OFF"))
    mpath.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    print("\n%d transitions written, %d with an end outside the threshold" % (made, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
