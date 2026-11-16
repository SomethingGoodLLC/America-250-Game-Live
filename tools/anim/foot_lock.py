"""Pin planted feet in a Kimodo clip (toes pivoting about the heel, slow slides in long stands).

Kimodo's own post-processing holds the ankles; the toes still swing a few centimetres about a planted
heel and a foot that stays down for seconds drifts. For every contact phase of every foot joint (ankle,
toe base, toe end; Kimodo's foot_contacts columns) this solves the leg (thigh, shin, foot rotations) so
the joint keeps the horizontal place it had in the middle of that phase. Hips, spine and arms are not
touched, so hand targets and the root path stay as generated.

Used by gen_clips.py before a clip is saved; also runs over clips already on disk:
  python foot_lock.py [--out <AnimationPass>] [--only KEY ...]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np

DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
LEG = ("LeftLeg", "LeftShin", "LeftFoot", "RightLeg", "RightShin", "RightFoot")
MIN_PHASE = 3


def phases(on: np.ndarray):
    t, n = 0, len(on)
    while t < n:
        if not on[t]:
            t += 1
            continue
        e = t
        while e + 1 < n and on[e + 1]:
            e += 1
        if e - t + 1 >= MIN_PHASE:
            yield t, e
        t = e + 1


def lock_feet(out: dict, skel77, torch, device, steps: int = 400) -> dict:
    """Return a copy of a Kimodo output dict with planted feet pinned, plus out['_foot_lock'] stats."""
    from kimodo.geometry import axis_angle_to_matrix

    local0 = torch.as_tensor(out["local_rot_mats"], device=device, dtype=torch.float32)
    root = torch.as_tensor(out["root_positions"], device=device, dtype=torch.float32)
    contacts = np.asarray(out["foot_contacts"]) > 0.5
    T = local0.shape[0]
    foot_names = list(skel77.foot_joint_names)[: contacts.shape[1]]
    with torch.no_grad():
        _, pos0, _ = skel77.fk(local0, root)
    target = torch.zeros(T, len(foot_names), 2, device=device)
    mask = torch.zeros(T, len(foot_names), device=device)
    n_phases = 0
    for col, name in enumerate(foot_names):
        j = skel77.bone_index[name]
        for t, e in phases(contacts[:, col]):
            target[t : e + 1, col] = pos0[t : e + 1, j][:, [0, 2]].median(dim=0).values
            mask[t : e + 1, col] = 1.0
            n_phases += 1
    stats = {"phases": n_phases}
    if n_phases == 0:
        res = dict(out)
        res["_foot_lock"] = stats
        return res
    fj = [skel77.bone_index[n] for n in foot_names]
    lj = [skel77.bone_index[n] for n in LEG]
    gen = torch.Generator().manual_seed(0)
    delta = (1e-3 * torch.randn(T, len(lj), 3, generator=gen)).to(device).requires_grad_(True)
    opt = torch.optim.Adam([delta], lr=0.01)

    def run():
        local = local0.clone()
        local[:, lj] = local0[:, lj] @ axis_angle_to_matrix(delta)
        grot, pos, _ = skel77.fk(local, root)
        return local, grot, pos

    def err(pos):
        return ((pos[:, fj][:, :, [0, 2]] - target) ** 2).sum(-1) * mask

    with torch.no_grad():
        stats["before_cm"] = float(err(pos0).max().sqrt()) * 100
    for _ in range(steps):
        opt.zero_grad()
        _, _, pos = run()
        loss = 1e4 * err(pos).sum() / mask.sum() + 1.0 * (delta ** 2).mean()
        if T > 1:
            loss = loss + 20.0 * ((delta[1:] - delta[:-1]) ** 2).mean()
        loss.backward()
        opt.step()
    with torch.no_grad():
        local, grot, pos = run()
        stats["after_cm"] = float(err(pos).max().sqrt()) * 100
        stats["max_leg_change_deg"] = float(delta.norm(dim=-1).max()) * 57.2958
    res = dict(out)
    res["local_rot_mats"] = local.cpu().numpy().astype(out["local_rot_mats"].dtype)
    res["global_rot_mats"] = grot.cpu().numpy().astype(out["global_rot_mats"].dtype)
    new_pos = pos.cpu().numpy().astype(out["posed_joints"].dtype)
    if "posed_joints_norootpos" in out:  # keep the 30-joint copy in step: shift it by what its joints moved
        names77 = list(skel77.bone_index.keys())
        from kimodo.skeleton import SOMASkeleton30
        idx30 = [names77.index(n) for n in SOMASkeleton30().bone_index.keys()]
        moved = new_pos[:, idx30] - np.asarray(out["posed_joints"])[:, idx30]
        res["posed_joints_norootpos"] = (np.asarray(out["posed_joints_norootpos"]) + moved).astype(out["posed_joints_norootpos"].dtype)
    res["posed_joints"] = new_pos
    res["_foot_lock"] = stats
    return res


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--only", action="append", default=[])
    a = ap.parse_args()
    import torch
    from kimodo.exports.bvh import save_motion_bvh
    from kimodo.exports.motion_io import save_kimodo_npz
    from kimodo.skeleton import SOMASkeleton77, global_rots_to_local_rots

    device = "cuda:0" if torch.cuda.is_available() else "cpu"
    skel = SOMASkeleton77().to(device)
    out_root = Path(a.out)
    mpath = out_root / "manifest.json"
    manifest = json.loads(mpath.read_text(encoding="utf-8"))
    for key in sorted(manifest["clips"]):
        m = manifest["clips"][key]
        if (a.only and key not in a.only) or m.get("foot_lock"):
            continue
        npz, bvh = out_root / "native" / m["files"]["npz"], out_root / "bvh" / m["files"]["bvh"]
        out = {k: v for k, v in np.load(npz).items()}
        res = lock_feet(out, skel, torch, device)
        stats = res.pop("_foot_lock")
        save_kimodo_npz(str(npz), res)
        rot = torch.from_numpy(res["global_rot_mats"]).to(device)
        pos = torch.from_numpy(res["posed_joints"]).to(device)
        save_motion_bvh(str(bvh), global_rots_to_local_rots(rot, skel), pos[:, skel.root_idx, :],
                        skeleton=skel, fps=float(m["fps"]), standard_tpose=True)
        m["foot_lock"] = stats
        m["files"]["npz_sha256"], m["files"]["bvh_sha256"] = sha256(npz), sha256(bvh)
        mpath.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
        print("%-34s phases %2d  worst %.1f -> %.1f cm  leg change %.1f deg" % (
            key, stats["phases"], stats.get("before_cm", 0), stats.get("after_cm", 0), stats.get("max_leg_change_deg", 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
