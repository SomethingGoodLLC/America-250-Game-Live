"""Generate and validate an evaluation-only MotionBricks vault, without a viewer.

Run with the sibling MotionBricks environment. No model or sampled motion is
vendored into the game. Output stays outside game Content until explicit import.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import numpy as np
from scipy.spatial.transform import Rotation


def global_poses(local, parents):
    result = np.array(local, dtype=np.float64, copy=True)
    for j, parent in enumerate(parents):
        if parent < 0:
            continue
        rotation = Rotation.from_quat(result[..., parent, 3:])
        result[..., j, :3] = result[..., parent, :3] + rotation.apply(local[..., j, :3])
        result[..., j, 3:] = (rotation * Rotation.from_quat(local[..., j, 3:])).as_quat()
    return result


def to_unreal(global_pose):
    """Runtime RH Z-up/+Y-forward metres -> UE LH Z-up/+X-forward cm.

    A reflection is a basis change, not a quaternion multiplication. Conjugate
    rotation matrices by the same orthogonal swap used for positions.
    """
    basis = np.array([[0., 1., 0.], [1., 0., 0.], [0., 0., 1.]])
    shape = global_pose.shape
    flat = np.asarray(global_pose).reshape(-1, 7)
    result = flat.copy()
    result[:, :3] = flat[:, :3] @ basis.T * 100.
    result[:, 3:] = Rotation.from_matrix(basis @ Rotation.from_quat(flat[:, 3:]).as_matrix() @ basis.T).as_quat()
    return result.reshape(shape)


def validate(poses):
    if poses.ndim != 3 or poses.shape[-1] != 7 or len(poses) < 4:
        raise ValueError(f"Invalid pose array: {poses.shape}")
    if not np.isfinite(poses).all():
        raise ValueError("Generated motion contains NaN/Inf")
    error = float(np.abs(np.linalg.norm(poses[..., 3:], axis=-1) - 1).max())
    if error > 0.005:
        raise ValueError(f"Unnormalized generated quaternion: {error}")
    return error


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--motionbricks", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--primitive", choices=("jump_0_5m", "jump_0_75m"), default="jump_0_75m")
    parser.add_argument("--sequence", type=int, default=0)
    parser.add_argument("--benchmark", type=int, default=30)
    args = parser.parse_args()
    if args.benchmark < 0:
        parser.error("--benchmark cannot be negative")
    root = args.motionbricks.resolve()
    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(root))
    sys.path.insert(0, str(root / "docs/examples"))
    os.chdir(root)
    import warp as wp
    from _rig import load_rig
    from app.runtime.common.clip_registry import ClipRegistry
    from app.runtime.common.io.smart_primitive_document import SmartPrimitiveAssetPool
    from motionbricks.model.contract import ModelRig
    from motionbricks.model.config import MotionBricksInferenceConfig

    wp.init()
    wp.set_device("cuda:0")
    device = wp.get_device()
    free_before = device.free_memory
    started = time.perf_counter()
    rig = load_rig("soma")
    setup_seconds = time.perf_counter() - started
    parents = rig.skeleton.parent_indices.numpy().tolist()
    names = rig.skeleton.joint_names
    registry = ClipRegistry(ModelRig.SOMA, skeleton=rig.skeleton)
    pool = SmartPrimitiveAssetPool(registry, rig.reference_pose, rig.model_to_runtime)
    primitive_path = root / f"assets/smart_primitives/soma/{args.primitive}.smart_primitive.json"
    primitive = pool.instantiate(primitive_path)
    count = primitive.sequence_length(args.sequence)
    if count < 2:
        raise ValueError("Traversal requires an entry pose window and at least one generated segment")
    targets = wp.empty((count * 4, len(names)), dtype=wp.transform)
    stats = []
    benchmark = []
    motions = []
    contacts = []
    with rig.engine as engine, wp.ScopedStream(engine.stream):
        params = primitive.get_world_target_poses_for_sequence(args.sequence, targets, stream=engine.stream)
        context = engine.create_input(rig.skeleton)
        target = engine.create_input(rig.skeleton)
        output = engine.create_output(rig.skeleton)
        # Entry window supplies only context; every exported pose is generated.
        context_poses = wp.clone(targets[:4])
        for i in range(1, count):
            settings = params[i]
            context.update_from_poses(context_poses, stream=engine.stream)
            target.update_from_poses(targets[i*4:i*4+settings.num_poses_to_use], stream=engine.stream)
            config = MotionBricksInferenceConfig(allowed_token_counts=settings.allowed_token_counts or tuple(range(8, 15)))
            begin = time.perf_counter()
            engine.generate_animation(context, target, output, config=config)
            wall_ms = (time.perf_counter() - begin) * 1000
            used = output.num_frames
            if i < count - 1:
                used -= settings.transition_early_by_frames
            if used < 4:
                raise ValueError("Too few frames to chain")
            poses = output.poses[:used].numpy()
            validate(poses)
            stats.append(dict(target=primitive.target_names_for_sequence(args.sequence)[i],
                              frames=used, wall_ms=wall_ms, gpu_ms=dict(engine.timer.milliseconds)))
            # Adjacent generations share their endpoint at t=0. Omit that duplicate.
            motions.append(poses if not motions else poses[1:])
            contact = output.foot_contacts[:used].numpy()
            contacts.append(contact if not contacts else contact[1:])
            context_poses = wp.clone(output.poses[used-4:used])
        # Fixed input after warm-up: do not confound performance with evolving input.
        for i in range(args.benchmark + 3):
            begin = time.perf_counter()
            engine.generate_animation(context, target, output, config=config)
            elapsed = (time.perf_counter() - begin) * 1000
            if i >= 3:
                benchmark.append(dict(wall_ms=elapsed, **engine.timer.milliseconds))
        rest = rig.reference_pose.numpy().copy()
        rest[0] = np.array(list(wp.mul(rig.model_to_runtime, wp.transform(rest[0, :3], rest[0, 3:]))))
        free_loaded = device.free_memory
    poses = np.concatenate(motions)
    error = validate(poses)
    world = to_unreal(global_poses(poses, parents))
    rest_world = to_unreal(global_poses(rest, parents))
    revision = subprocess.check_output(["git", "-c", f"safe.directory={root.as_posix()}", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    foot_contacts = np.concatenate(contacts)
    if not np.isfinite(foot_contacts).all():
        raise ValueError("Nonfinite generated contact values")
    data = dict(schema="pa250.motionbricks.global.v1", evaluation_only=True,
                fps=30, units="centimeters", coordinates="Unreal LH Z-up +X-forward",
                motionbricks_commit=revision, primitive=args.primitive,
                sequence=primitive.sequence_name(args.sequence), joint_names=names,
                parents=parents, rest_global=rest_world.tolist(),
                frames_global=world.tolist(), foot_contacts=foot_contacts.tolist())
    destination = output_dir / "generated_traversal.json"
    destination.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    np.savez_compressed(output_dir / "generated_soma.npz", poses=poses, parents=parents)
    summary = {key: dict(mean=float(np.mean([r[key] for r in benchmark])),
                         p50=float(np.median([r[key] for r in benchmark])),
                         p95=float(np.percentile([r[key] for r in benchmark],95)))
               for key in benchmark[0]} if benchmark else {}
    metrics = dict(revision=revision, model="SOMA FP32, TF32 permitted", setup_seconds=setup_seconds,
                   frames=len(poses), duration_seconds=(len(poses)-1)/30,
                   finite=True, max_quaternion_norm_error=error, segments=stats,
                   warm_samples=len(benchmark), benchmark_ms=summary,
                   gpu_device_used_mib_before=(device.total_memory-free_before)/2**20,
                   gpu_device_used_mib_with_engine=(device.total_memory-free_loaded)/2**20,
                   root_start_cm=world[0,0,:3].tolist(), root_end_cm=world[-1,0,:3].tolist(),
                   root_height_range_cm=[float(world[:,0,2].min()),float(world[:,0,2].max())],
                   sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
    (output_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    (output_dir / "EVALUATION_ONLY.txt").write_text(
        "Generated using Bones-derived MotionBricks sample target poses. Internal NVIDIA technology evaluation only. "
        "Do not ship or redistribute. See motionbricks/assets/motions/LICENSE.txt.\n", encoding="utf-8")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
