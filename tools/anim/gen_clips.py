"""Generate the animation-pass clips with Kimodo (offline; nothing here ships in the game).

Reads clip_specs.yaml and gun_geometry.json, generates every variant of every requested clip with its own
seed, and writes per variant:
  <out>/native/<name>_v<N>.npz   Kimodo's native output (77-joint SOMA, 30 fps, metres, Y up)
  <out>/bvh/<name>_v<N>.bvh      SOMA BVH with the standard T-pose rest (for the Blender -> FBX step)
plus one entry in <out>/manifest.json (model, version, seed, prompt, constraints, duration, fps, hashes).

Hand targets are either points on the gun (cannon mesh space) or, for clips with no gun, points in the
soldier's own space ({body_cm: [left, forward, up]}, cm, from where he stands at frame 0). Kimodo takes an end-effector constraint as a full pose, not a point. For a constrained
clip the script first generates the clip unconstrained with the same seed, takes that pose at the
constraint frame, bends it (arms, spine, a little root travel) until the hands are on the targets, and
generates again with that pose as left-hand / right-hand constraints. Targets are given in the cannon's
mesh space (cm) and scaled by SOMA height / soldier height before they reach Kimodo.

Run with Kimodo's venv, text-encoder service up (TEXT_ENCODER_MODE=api is the default here):
  python gen_clips.py --only AM_Gun_Sponge
  python gen_clips.py --all
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
ALLOWED_MODELS = ("Kimodo-SOMA-RP-v1", "Kimodo-SOMA-SEED-v1.1")  # NVIDIA Open Model License; SMPL-X is refused
MAX_DURATION = 10.0
IK_JOINTS = ("Spine1", "Spine2", "Chest",
             "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
             "RightShoulder", "RightArm", "RightForeArm", "RightHand")
HAND = {"left_hand": ("LeftHand", "LeftHandMiddleEnd"), "right_hand": ("RightHand", "RightHandMiddleEnd")}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def clip_seed(spec: dict, variant: int) -> int:
    base = spec.get("seed")
    if base is None:
        base = zlib.crc32(spec["name"].encode("utf-8")) % 1_000_000
    return int(base) + variant


# ---------- gun space -> Kimodo space ----------

def gun_point(geo: dict, ref: dict) -> list[float]:
    """Resolve a target reference to a point in cannon mesh space (cm)."""
    if "point_cm" in ref:
        p = [float(x) for x in ref["point_cm"]]
    else:
        kind = ref["point"]
        muzzle = geo["bore_axis"]["origin_cm"]  # the measured centre of the muzzle face, not the socket
        if kind == "bore":  # on the bore axis, forward of the muzzle face
            d = geo["bore_axis"]["direction"]
            fwd = float(ref["forward_of_muzzle_cm"])
            p = [muzzle[i] + d[i] * fwd for i in range(3)]
        elif kind == "muzzle_mouth":
            p = list(muzzle)
        elif kind == "vent":
            p = list(geo.get("vent_measured_cm") or geo["sockets"]["Vent"])  # the metal, not the socket above it
        elif kind in geo["sockets"] and geo["sockets"][kind]:
            p = list(geo["sockets"][kind])
        elif kind in geo["stations"]:
            p = list(geo["stations"][kind]["cm"])
        else:
            raise ValueError("unknown target point %r" % kind)
    off = ref.get("offset_cm", [0.0, 0.0, 0.0])
    return [p[i] + float(off[i]) for i in range(3)]


def to_kimodo(geo: dict, station_cm: list[float], face_cm: list[float], p_cm: list[float]) -> list[float]:
    """Cannon mesh space (UE: X forward, Y right, Z up, cm) -> Kimodo space (X left, Y up, Z forward, m).

    The man stands at the station at frame 0 and faces `face_cm`; that is Kimodo's origin facing +Z.
    """
    fx, fy = face_cm[0] - station_cm[0], face_cm[1] - station_cm[1]
    n = math.hypot(fx, fy)
    if n < 1e-6:
        raise ValueError("station and face point coincide")
    fx, fy = fx / n, fy / n
    dx, dy, dz = (p_cm[i] - station_cm[i] for i in range(3))
    fwd = dx * fx + dy * fy
    right = dx * -fy + dy * fx
    k = float(geo["scale_soma_over_soldier"]) / 100.0
    return [-right * k, dz * k, fwd * k]


# ---------- IK on the SOMA 30-joint skeleton ----------

def solve_hands(skeleton, seed_local, seed_root, targets: dict, torch, anchor=None):
    """Bend the seed pose so each hand's grip point (midway wrist -> middle fingertip) is on its target.

    seed_local [30,3,3], seed_root [3]; targets {"left_hand": [3], ...} in Kimodo space.
    Returns local rots [30,3,3], root [3], residuals {hand: metres}, root shift in metres.
    """
    from kimodo.geometry import axis_angle_to_matrix

    dev = seed_local.device
    idx = [skeleton.bone_index[n] for n in IK_JOINTS]
    # Start a hair off zero: the axis-angle map has no usable gradient at exactly zero rotation.
    gen = torch.Generator().manual_seed(0)
    delta = (1e-3 * torch.randn(len(idx), 3, generator=gen)).to(dev).requires_grad_(True)
    droot = torch.zeros(3, device=dev, requires_grad=True)
    yaw = torch.full((1,), 1e-3, device=dev, requires_grad=True)  # turn of the whole body about the vertical
    weight = torch.tensor([4.0 if n.startswith(("Spine", "Chest")) else 1.0 for n in IK_JOINTS], device=dev)
    tgt = {h: torch.tensor(p, device=dev, dtype=seed_local.dtype) for h, p in targets.items()}
    opt = torch.optim.Adam([delta, droot, yaw], lr=0.03)

    def pose():
        local = seed_local.clone()
        local[idx] = seed_local[idx] @ axis_angle_to_matrix(delta)
        turn = axis_angle_to_matrix(torch.stack([yaw.new_zeros(1), yaw, yaw.new_zeros(1)], dim=-1))[0]
        local[skeleton.root_idx] = turn @ seed_local[skeleton.root_idx]
        root = seed_root + droot
        _, pos, _ = skeleton.fk(local[None], root[None])
        return local, root, pos[0]

    def grips(pos):
        return {h: 0.5 * (pos[skeleton.bone_index[HAND[h][0]]] + pos[skeleton.bone_index[HAND[h][1]]]) for h in tgt}

    for _ in range(800):
        opt.zero_grad()
        _, _, pos = pose()
        g = grips(pos)
        loss = sum(((g[h] - tgt[h]) ** 2).sum() for h in tgt) * 100.0
        loss = loss + 0.02 * (weight[:, None] * delta ** 2).sum()
        # The feet travel with the root: the pose asks the model to have stepped there by this frame.
        loss = loss + 0.01 * (droot[[0, 2]] ** 2).sum() + 5.0 * droot[1] ** 2 + 0.01 * (yaw ** 2).sum()
        if anchor is not None:  # later constraints of a clip: stay where the first one put the body, and lean
            loss = loss + 20.0 * (((seed_root + droot) - anchor)[[0, 2]] ** 2).sum()
        loss.backward()
        opt.step()
    with torch.no_grad():
        local, root, pos = pose()
        g = grips(pos)
        res = {h: float((g[h] - tgt[h]).norm()) for h in tgt}
        return local.detach(), root.detach(), res, float(droot[[0, 2]].norm())



def local30_from_output(skeleton, out: dict, frame: int, torch, device):
    """Frame `frame` of a generated sample as 30-joint local rotations and root position."""
    local77 = torch.as_tensor(out["local_rot_mats"][frame], device=device)
    local30 = skeleton.from_SOMASkeleton77(local77[None])[0]
    root = torch.as_tensor(out["root_positions"][frame], device=device)
    return local30, root


def build_constraints(skeleton, spec: dict, geo: dict, seed_out: dict, n_frames: int, torch, device, chain_npz=None,
                      start_npz=None):
    """Constraint sets for one variant plus the record that goes in the manifest."""
    from kimodo.constraints import EndEffectorConstraintSet, FullBodyConstraintSet

    sets, record = [], []
    anchor = None
    station = geo["stations"][spec["station"]]["cm"] if spec.get("station") else None
    face = gun_point(geo, spec["face"]) if spec.get("face") else None
    for c in spec.get("constraints") or []:
        frame = int(c["frame"])
        if not 0 <= frame < n_frames:
            raise ValueError("%s: constraint frame %d is outside 0..%d" % (spec["name"], frame, n_frames - 1))
        hands = {h: c[h] for h in HAND if h in c}
        from_v0 = False
        targets_cm, targets = {}, {}
        k = float(geo["scale_soma_over_soldier"]) / 100.0
        for h, ref in hands.items():
            if "body_cm" in ref:
                # In the soldier's own space, from where he stands at frame 0: [left, forward, up] in cm.
                x, y, z = (float(v) for v in ref["body_cm"])
                targets[h] = [x * k, z * k, y * k]
            else:
                if station is None or face is None:
                    raise ValueError("%s: gun hand targets need 'station' and 'face'" % spec["name"])
                targets_cm[h] = gun_point(geo, ref)
                targets[h] = to_kimodo(geo, station, face, targets_cm[h])
        if c.get("seed_clip") and chain_npz is not None and (Path(chain_npz).parent / (c["seed_clip"] + ".npz")).exists():
            # Bend the first pose of a named clip (a plain upright stand) instead of whatever the unconstrained pass
            # showed at this frame: solved from that, the shouldered-arms start leaned back with an arm overhead.
            import numpy as np
            local, root = local30_from_output(
                skeleton, np.load(Path(chain_npz).parent / (c["seed_clip"] + ".npz")), 0, torch, device)
        elif c.get("seed") == "chain_end" and chain_npz is not None and Path(chain_npz).exists():
            # Bend the pose this clip must end in (the next clip's first frame) instead of the unconstrained
            # pose at this frame: the hold before the end and the end pose then agree, and the model only holds.
            import numpy as np
            local, root = local30_from_output(skeleton, np.load(chain_npz), 0, torch, device)
        elif frame == 0 and start_npz is not None and Path(start_npz).exists():
            # This variant must start in variant 0's first pose: bend that pose onto the targets and give it as the
            # one full-body constraint at frame 0 (a separate start pose 5 cm off at the hands made every seed miss).
            import numpy as np
            local, root = local30_from_output(skeleton, np.load(start_npz), 0, torch, device)
            from_v0 = True
        else:
            local, root = local30_from_output(skeleton, seed_out, frame, torch, device)
        if anchor is None and any("body_cm" in ref for ref in hands.values()):
            anchor = root  # targets in his own space: he stays where he stands and turns or leans to reach
        # A hold solved from the pose the clip ends in keeps that pose's own place: pulled toward where the clip
        # began, it disagreed with the end pose by the hips' sway and the hands missed by 7 cm.
        hold_at = root if c.get("seed") == "chain_end" else anchor
        local, root, res, shift = solve_hands(skeleton, local, root, targets, torch, anchor=hold_at)
        anchor = root if anchor is None else anchor
        grot, gpos, _ = skeleton.fk(local[None], root[None])
        if from_v0 or (frame == 0 and c.get("full_body")):
            # A hold on the very first frame is given as the whole pose: as a hands-only constraint there the
            # model missed by 5 to 7 cm (kneeling Present), as a full pose by under 3.
            sets.append(FullBodyConstraintSet(skeleton, torch.tensor([frame]), gpos, grot))
        else:
            sets.append(EndEffectorConstraintSet(
                skeleton, torch.tensor([frame]), gpos, grot, None,
                joint_names=[HAND[h][0] for h in hands]))
        record.append({"type": "hands", "frame": frame, "targets_gun_cm": targets_cm, "targets_kimodo_m": targets,
                       "ik_residual_m": res, "ik_root_shift_m": shift, "full_body_from_v0": from_v0})
    if spec.get("loop"):  # first and last frame share the unconstrained first pose
        local, root = local30_from_output(skeleton, seed_out, 0, torch, device)
        grot, gpos, _ = skeleton.fk(local[None].repeat(2, 1, 1, 1), root[None].repeat(2, 1))
        sets.append(FullBodyConstraintSet(skeleton, torch.tensor([0, n_frames - 1]), gpos, grot))
        record.append({"type": "loop", "frames": [0, n_frames - 1]})
    return sets, record


def achieved_errors(model, out: dict, record: list, torch, device) -> None:
    """Write into each hands record how far the generated grip points are from their targets."""
    from kimodo.skeleton import SOMASkeleton30

    skel = model.skeleton
    if isinstance(skel, SOMASkeleton30):
        skel = skel.somaskel77
    for r in record:
        if r.get("type") != "hands":
            continue
        pos = out["posed_joints"][r["frame"]]
        r["achieved_error_m"] = {}
        for h, tgt in r["targets_kimodo_m"].items():
            g = 0.5 * (pos[skel.bone_index[HAND[h][0]]] + pos[skel.bone_index[HAND[h][1]]])
            r["achieved_error_m"][h] = float(((g - tgt) ** 2).sum() ** 0.5)


def end_pose_constraint(skeleton, next_npz: Path, n_frames: int, torch, device):
    """Full-body constraint on the last frame: the first pose of the clip this one chains to."""
    import numpy as np
    from kimodo.constraints import FullBodyConstraintSet

    nxt = np.load(next_npz)
    local, root = local30_from_output(skeleton, nxt, 0, torch, device)
    grot, gpos, _ = skeleton.fk(local[None], root[None])
    return FullBodyConstraintSet(skeleton, torch.tensor([n_frames - 1]), gpos, grot)


# ---------- generation ----------

def cfg_for(spec: dict, constrained: bool, cli_weight) -> dict:
    """Classifier-free guidance as [text weight, constraint weight]; kimodo's own default when unconstrained."""
    w = spec.get("cfg_weight") or cli_weight
    if not constrained or not w:
        return {}
    return {"cfg_type": "separated", "cfg_weight": [float(w[0]), float(w[1])]}


def generate(model, spec: dict, seed: int, n_frames: int, constraint_lst, steps: int, cfg: dict | None = None):
    from kimodo.tools import seed_everything

    seed_everything(seed)
    out = model([spec["prompt"]], [n_frames], constraint_lst=constraint_lst, num_denoising_steps=steps,
                num_samples=1, multi_prompt=True, num_transition_frames=5, post_processing=True, return_numpy=True,
                **(cfg or {}))
    return {k: (v[0] if hasattr(v, "shape") and len(v.shape) > 0 and v.shape[0] == 1 else v) for k, v in out.items()}


def save_variant(model, out: dict, npz: Path, bvh: Path, torch, device):
    from kimodo.exports.bvh import save_motion_bvh
    from kimodo.exports.motion_io import save_kimodo_npz
    from kimodo.skeleton import SOMASkeleton30, global_rots_to_local_rots

    save_kimodo_npz(str(npz), out)
    skel = model.skeleton
    if isinstance(skel, SOMASkeleton30):
        skel = skel.somaskel77.to(device)
    pos = torch.from_numpy(out["posed_joints"]).to(device)
    rot = torch.from_numpy(out["global_rot_mats"]).to(device)
    save_motion_bvh(str(bvh), global_rots_to_local_rots(rot, skel), pos[:, skel.root_idx, :],
                    skeleton=skel, fps=model.fps, standard_tpose=True)


def order_for_chains(specs: list[dict], wanted: list[str]) -> list[dict]:
    """Wanted clips, with a clip generated after the clip it chains to when both share a station."""
    by_name = {s["name"]: s for s in specs}
    done, order = set(), []

    def visit(name, stack=()):
        if name in done or name not in wanted:
            return
        nxt = by_name[name].get("chain_to")
        if nxt and nxt in by_name and nxt not in stack and by_name[nxt].get("station") == by_name[name].get("station"):
            visit(nxt, stack + (name,))
        done.add(name)
        order.append(by_name[name])

    for n in wanted:
        visit(n)
    return order


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--specs", default=str(HERE / "clip_specs.yaml"))
    ap.add_argument("--geometry", default=str(HERE / "gun_geometry.json"))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--only", action="append", default=[], help="clip name (repeatable)")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--model", default=ALLOWED_MODELS[0])
    ap.add_argument("--steps", type=int, default=100)
    ap.add_argument("--from-variant", type=int, default=0, help="keep variants below this number as they are on disk")
    ap.add_argument("--max-hand-error", type=float, default=0.05, help="metres, at constraint frames")
    ap.add_argument("--retries", type=int, default=7, help="new seeds to try when the hands or the expectations miss")
    ap.add_argument("--cfg-weight", type=float, nargs=2, default=[2.0, 4.0], metavar=("TEXT", "CONSTRAINT"),
                    help="guidance weights for constrained clips (a spec's cfg_weight overrides)")
    args = ap.parse_args()

    if args.model not in ALLOWED_MODELS:
        print("refused: model %r is not one of %s (SMPL-X weights are non-commercial)" % (args.model, ALLOWED_MODELS))
        return 2
    if not args.all and not args.only:
        ap.error("give --only NAME or --all")
    os.environ.setdefault("TEXT_ENCODER_MODE", "api")

    import yaml
    specs = yaml.safe_load(Path(args.specs).read_text(encoding="utf-8"))
    names = [s["name"] for s in specs]
    if len(set(names)) != len(names):
        raise SystemExit("duplicate clip names in %s" % args.specs)
    wanted = names if args.all else args.only
    for n in wanted:
        if n not in names:
            raise SystemExit("no clip named %r in %s" % (n, args.specs))
    for s in specs:
        if float(s["duration"]) > MAX_DURATION:
            raise SystemExit("%s: duration %.1f s is over %.0f s" % (s["name"], s["duration"], MAX_DURATION))
    geo = json.loads(Path(args.geometry).read_text(encoding="utf-8"))

    import numpy as np  # noqa: F401
    import torch
    from huggingface_hub import snapshot_download
    from kimodo import load_model
    from kimodo.model.registry import get_model_info

    device = "cuda:0" if torch.cuda.is_available() else "cpu"
    model, resolved = load_model(args.model, device=device, default_family="Kimodo", return_resolved_name=True)
    info = get_model_info(resolved)
    if info is None or info.display_name != args.model or info.skeleton != "SOMA":
        raise SystemExit("model resolved to %r, not the SOMA model asked for" % (info.display_name if info else resolved))
    revision = Path(snapshot_download(repo_id=info.repo_id, local_files_only=True)).name
    skeleton = model.skeleton
    fps = float(model.fps)
    from expectations import failures as expectation_failures
    from foot_lock import lock_feet
    from kimodo.skeleton import SOMASkeleton30
    skel77 = skeleton.somaskel77.to(device) if isinstance(skeleton, SOMASkeleton30) else skeleton

    out_root = Path(args.out)
    (out_root / "native").mkdir(parents=True, exist_ok=True)
    (out_root / "bvh").mkdir(parents=True, exist_ok=True)
    manifest_path = out_root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {"clips": {}}
    by_name = {s["name"]: s for s in specs}
    chain_targets = {s["chain_to"] for s in specs
                     if s.get("chain_to") in by_name and by_name[s["chain_to"]].get("station") == s.get("station")}

    for spec in order_for_chains(specs, wanted):
        n_frames = int(round(float(spec["duration"]) * fps))
        for v in range(int(spec.get("variants", 1))):
            key = "%s_v%d" % (spec["name"], v)
            if v < args.from_variant and (out_root / "native" / (key + ".npz")).exists():
                continue
            base_seed = clip_seed(spec, v)
            t0 = time.time()
            nxt = spec.get("chain_to")
            attempts = []
            for attempt in range(args.retries + 1):
                # A variant whose hands miss their targets is generated again with a new seed, never hand-edited.
                seed = base_seed + 1000 * attempt
                constraint_lst, record = [], []
                if spec.get("constraints") or spec.get("loop"):
                    first = generate(model, spec, seed, n_frames, [], args.steps)
                    chain_npz = out_root / "native" / ("%s_v0.npz" % nxt) if nxt else None
                    start_npz = out_root / "native" / ("%s_v0.npz" % spec["name"]) if (
                        v > 0 and spec["name"] in chain_targets) else None
                    constraint_lst, record = build_constraints(skeleton, spec, geo, first, n_frames, torch, device, chain_npz,
                                                               start_npz)
                if nxt and nxt in by_name and by_name[nxt].get("station") == spec.get("station"):
                    nxt_npz = out_root / "native" / ("%s_v0.npz" % nxt)
                    if nxt_npz.exists():
                        constraint_lst.append(end_pose_constraint(skeleton, nxt_npz, n_frames, torch, device))
                        record.append({"type": "end_pose", "frame": n_frames - 1, "from": "%s_v0 frame 0" % nxt})
                    else:
                        record.append({"type": "end_pose", "skipped": "%s_v0 not generated yet" % nxt})
                own_v0 = out_root / "native" / ("%s_v0.npz" % spec["name"])
                if v > 0 and spec["name"] in chain_targets and own_v0.exists() \
                        and not any(r.get("full_body_from_v0") for r in record):
                    # Every variant of a clip that others chain into starts from variant 0's first pose.
                    c0 = end_pose_constraint(skeleton, own_v0, 1, torch, device)
                    constraint_lst.append(c0)
                    record.append({"type": "start_pose", "frame": 0, "from": "%s_v0 frame 0" % spec["name"]})
                # Index tensors stay on the CPU and pose data on the model's device, as kimodo's own loader leaves them.
                cfg = cfg_for(spec, bool(constraint_lst), args.cfg_weight)
                out = generate(model, spec, seed, n_frames, constraint_lst, args.steps, cfg)
                achieved_errors(model, out, record, torch, device)
                got0 = max([max(r["achieved_error_m"].values()) for r in record if r.get("type") == "hands"] or [0.0])
                if attempt == args.retries or (
                        got0 <= args.max_hand_error and not expectation_failures(out["root_positions"], spec.get("expect"))):
                    out = lock_feet(out, skel77, torch, device)  # planted feet pinned; hips and arms untouched
                    foot_stats = out.pop("_foot_lock")
                got = max([max(r["achieved_error_m"].values()) for r in record if r.get("type") == "hands"] or [0.0])
                misses = expectation_failures(out["root_positions"], spec.get("expect"))
                attempts.append({"seed": seed, "hand_error_m": got, "expect_failures": misses})
                if got <= args.max_hand_error and not misses:
                    break
            npz = out_root / "native" / (key + ".npz")
            bvh = out_root / "bvh" / (key + ".bvh")
            save_variant(model, out, npz, bvh, torch, device)
            manifest["clips"][key] = {
                "name": spec["name"], "variant": v, "family": spec.get("family"),
                "model": info.display_name, "model_repo": info.repo_id, "model_revision": revision,
                "seed": seed, "prompt": spec["prompt"], "diffusion_steps": args.steps,
                "cfg": cfg or "model default",
                "duration_s": n_frames / fps, "frames": n_frames, "fps": fps,
                "station": spec.get("station"), "chain_to": spec.get("chain_to"),
                "root_motion": bool(spec.get("root_motion", False)), "loop": bool(spec.get("loop", False)),
                "scale_soma_over_soldier": geo["scale_soma_over_soldier"],
                "constraints": record, "foot_lock": foot_stats,
                "attempts": attempts, "hand_error_limit_m": args.max_hand_error,
                "hand_error_ok": attempts[-1]["hand_error_m"] <= args.max_hand_error,
                "expect": spec.get("expect"), "expect_ok": not attempts[-1]["expect_failures"],
                "files": {"npz": npz.name, "npz_sha256": sha256(npz), "bvh": bvh.name, "bvh_sha256": sha256(bvh)},
            }
            manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
            worst = max([max(r["ik_residual_m"].values()) for r in record if r.get("type") == "hands"] or [0.0])
            print("%s seed %d  %d frames  %.1f s  ik residual %.3f m  hand error in output %.3f m (try %d)  npz %s" % (
                key, seed, n_frames, time.time() - t0, worst, got, len(attempts), manifest["clips"][key]["files"]["npz_sha256"][:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
