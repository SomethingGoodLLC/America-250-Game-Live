"""BVH -> FBX for Unreal, with headless Blender (the official bpy wheel; Kimodo's guide has no FBX path).

  Tooling/bpy-venv/Scripts/python.exe tools/anim/bvh_to_fbx.py [--out <AnimationPass>] [--only KEY ...] [--force]

Writes <out>/fbx/SK_SOMA.fbx (the SOMA skeleton in its standard T-pose with a proxy mesh of this pass's own:
a small box on every joint, so Unreal has a skeletal mesh to hang the skeleton and the IK rig on; NVIDIA's
SOMA body mesh is not used) and <out>/fbx/<clip>.fbx for every BVH in <out>/bvh (armature and baked keys only).

Units: Kimodo's BVH is in centimetres, Y up. The scene's unit scale is set to 0.01 so the FBX is written in
centimetres at scale 1 and Unreal imports it 1:1 with no scaled root. The armature object is named
"Armature" in every file, which the FBX importer drops, so the top bone is the BVH's "Root" each time.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy

DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
REST_BVH = "kimodo/kimodo/assets/skeletons/somaskel77/somaskel77_standard_tpose.bvh"


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s = bpy.context.scene
    s.unit_settings.system = "METRIC"
    s.unit_settings.scale_length = 0.01
    s.render.fps = 30
    s.render.fps_base = 1.0


def import_bvh(path: Path):
    bpy.ops.import_anim.bvh(filepath=str(path), global_scale=1.0, frame_start=0, use_fps_scale=False,
                            update_scene_fps=False, update_scene_duration=True, rotate_mode="NATIVE",
                            axis_forward="-Z", axis_up="Y")
    arm = bpy.context.object
    arm.name = "Armature"
    arm.data.name = "Armature"
    return arm


def export(path: Path, objects, anim: bool):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=str(path), use_selection=True, object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True, global_scale=1.0, apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Z", axis_up="Y", add_leaf_bones=False, use_armature_deform_only=False,
        primary_bone_axis="Y", secondary_bone_axis="X", mesh_smooth_type="FACE",
        bake_anim=anim, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True, bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0)


def build_skeleton_mesh(out_root: Path, dst: Path):
    reset()
    arm = import_bvh(out_root / REST_BVH)
    arm.animation_data_clear()
    for pb in arm.pose.bones:  # rest pose only
        pb.location = (0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
    verts, faces, groups = [], [], []
    h = 1.5  # half size of the box on each joint, cm
    for b in arm.data.bones:
        c = arm.matrix_world @ b.head_local
        base = len(verts)
        for dx in (-h, h):
            for dy in (-h, h):
                for dz in (-h, h):
                    verts.append((c.x + dx, c.y + dy, c.z + dz))
        for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            faces.append(tuple(base + i for i in f))
        groups.append((b.name, list(range(base, base + 8))))
    mesh = bpy.data.meshes.new("SK_SOMA")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("SK_SOMA", mesh)
    bpy.context.scene.collection.objects.link(obj)
    for name, idx in groups:
        obj.vertex_groups.new(name=name).add(idx, 1.0, "REPLACE")
    obj.parent = arm
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    export(dst, [arm, obj], anim=False)
    return len(arm.data.bones), len(verts)


def with_root_motion(src: Path, npz: Path, tmp: Path) -> Path:
    """Copy of a BVH whose Root bone carries the travel over the ground (Kimodo leaves it all on Hips).

    Root takes Kimodo's smoothed root path (smooth_root_pos, X and Z, metres -> cm) and stays on the ground;
    Hips keeps what is left, so the pose in the world is unchanged. Unreal reads root motion from Root.
    """
    import numpy as np

    smooth = np.load(npz)["smooth_root_pos"] * 100.0
    head, motion = src.read_text(encoding="utf-8").split("MOTION")
    lines = motion.strip().splitlines()
    out = lines[:2]
    frames = lines[2:]
    if len(frames) != len(smooth):
        raise RuntimeError("%s: %d BVH frames against %d root samples" % (src.name, len(frames), len(smooth)))
    for line, r in zip(frames, smooth):
        v = [float(x) for x in line.split()]
        v[0], v[1], v[2] = r[0], 0.0, r[2]   # Root: X Y Z position
        v[6] -= r[0]                         # Hips position is relative to Root
        v[8] -= r[2]
        out.append(" ".join("%.6f" % x for x in v))
    tmp.write_text(head + "MOTION\n" + "\n".join(out) + "\n", encoding="utf-8")
    return tmp


def convert_clip(src: Path, dst: Path, root_npz: Path | None = None) -> int:
    reset()
    if root_npz is not None:
        src = with_root_motion(src, root_npz, dst.with_suffix(".rootmotion.bvh"))
    arm = import_bvh(src)
    act = arm.animation_data.action
    act.name = dst.stem
    start, end = (int(round(v)) for v in act.frame_range)
    s = bpy.context.scene
    s.frame_start, s.frame_end = start, end
    export(dst, [arm], anim=True)
    if root_npz is not None:
        src.unlink()
    return end - start + 1


def main() -> int:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    out_root = Path(a.out)
    fbx = out_root / "fbx"
    fbx.mkdir(exist_ok=True)
    sk = fbx / "SK_SOMA.fbx"
    if a.force or not sk.exists():
        bones, verts = build_skeleton_mesh(out_root, sk)
        print("SK_SOMA.fbx: %d bones, %d proxy vertices" % (bones, verts))
    import json
    manifest = json.loads((out_root / "manifest.json").read_text(encoding="utf-8"))["clips"]
    done = 0
    for src in sorted((out_root / "bvh").glob("*.bvh")):
        if a.only and src.stem not in a.only:
            continue
        dst = fbx / (src.stem + ".fbx")
        if dst.exists() and not a.force and dst.stat().st_mtime >= src.stat().st_mtime:
            continue
        rm = bool(manifest.get(src.stem, {}).get("root_motion"))
        frames = convert_clip(src, dst, out_root / "native" / (src.stem + ".npz") if rm else None)
        done += 1
        print("%s.fbx  %d frames%s" % (src.stem, frames, "  root motion on Root" if rm else ""))
    print("converted %d clips" % done)
    return 0


if __name__ == "__main__":
    sys.exit(main())
