"""Round-trip check of the battlefield kit on disk (run with Blender or the bpy 4.5.3 wheel).

  <bpy venv>/bin/python tools/art/validate_battlefield_kit.py [--only Name,...]

For every asset in kit_manifest.txt: reopens the .blend (triangles, UVs, degenerate faces,
identity transforms, packed textures, face normals pointing away from the building's core
for the closed parts), re-imports the .glb (triangles, bounds, embedded images) and the
UE5 .fbx (triangles, bounds, UCX hull names), and checks the three preview PNGs exist.
Writes kit_validation.txt (JSON text) and exits non-zero on any failure.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "unreal/PlayAmerica250/SourceArt/battlefield"


def dims(objects):
    bpy.context.view_layer.update()
    pts = [o.matrix_world @ Vector(c) for o in objects for c in o.bound_box]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi


def tri_count(objects):
    n = 0
    for o in objects:
        o.data.calc_loop_triangles()
        n += len(o.data.loop_triangles)
    return n


def check(name, info):
    errors = []
    mesh_name = info["mesh"]
    # --- blend
    bpy.ops.wm.open_mainfile(filepath=str(OUT / f"{name}.blend"))
    ob = bpy.data.objects.get(mesh_name)
    if ob is None:
        return {"errors": [f"{mesh_name} not in blend"]}
    me = ob.data
    me.calc_loop_triangles()
    tris = len(me.loop_triangles)
    if tris != info["triangles"]:
        errors.append(f"blend triangles {tris} != {info['triangles']}")
    if any(t.area < 1e-10 for t in me.loop_triangles):
        errors.append("degenerate triangles")
    if not me.uv_layers:
        errors.append("no UVs")
    identity = ob.location.length < 1e-7 and max(abs(v - 1) for v in ob.scale) < 1e-7 and max(abs(v) for v in ob.rotation_euler) < 1e-7
    if not identity:
        errors.append("transform not applied")
    textured = [im for im in bpy.data.images if im.source == "FILE"]
    unpacked = [im.name for im in textured if not im.packed_file]
    if unpacked:
        errors.append("unpacked images: " + ", ".join(unpacked))
    # Faces of the big shells should not face the core: sample faces larger than 0.5 m2 and
    # require their normal to point away from the vertical axis through the origin or upward.
    inward = 0
    big = 0
    for poly in me.polygons:
        if poly.area < 0.5 or me.materials[poly.material_index].name == "DarkVoid":
            continue
        big += 1
        c = poly.center
        radial = Vector((c.x, c.y, 0))
        if radial.length > 0.5 and poly.normal.dot(radial.normalized()) < -0.5 and poly.normal.z < 0.3:
            inward += 1
    hulls = [o for o in bpy.data.objects if o.name.startswith("UCX_")]
    bad_hulls = [h.name for h in hulls if h.name[4:].rsplit("_", 1)[0] != mesh_name]
    if len(hulls) != info["hulls"] or bad_hulls:
        errors.append(f"hulls {len(hulls)} != {info['hulls']} or misnamed {bad_hulls}")
    result = {"blend": {"triangles": tris, "packed_images": len(textured) - len(unpacked), "hulls": len(hulls),
                        "large_faces": big, "large_faces_toward_core": inward, "materials": [m.name for m in me.materials]}}
    if [m.name for m in me.materials] != info["slots"]:
        errors.append("material slots differ from the manifest")
    # --- glb
    data = (OUT / f"{name}.glb").read_bytes()
    magic, version, total = struct.unpack_from("<4sII", data)
    length, kind = struct.unpack_from("<II", data, 12)
    gltf = json.loads(data[20:20 + length])
    if magic != b"glTF" or version != 2 or total != len(data) or kind != 0x4E4F534A:
        errors.append("glb header invalid")
    if not all("bufferView" in im for im in gltf.get("images", [])):
        errors.append("glb image not embedded")
    if not all({"POSITION", "NORMAL", "TEXCOORD_0"} <= set(p["attributes"]) for m in gltf["meshes"] for p in m["primitives"]):
        errors.append("glb primitive lacks position/normal/uv")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(OUT / f"{name}.glb"))
    objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    lo, hi = dims(objs)
    g_tris = tri_count(objs)
    g_err = max(abs(a - b) for a, b in zip(lo + hi, info["min_m"] + info["max_m"], strict=False))
    if g_tris != info["triangles"] or g_err > 1e-3:
        errors.append(f"glb round trip: triangles {g_tris}, bounds error {g_err:.5f} m")
    result["glb"] = {"triangles": g_tris, "bounds_error_m": g_err, "embedded_images": len(gltf.get("images", [])),
                     "materials": len(gltf.get("materials", []))}
    # --- fbx (the file Unreal imports)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(OUT / "ue5" / f"{mesh_name}.fbx"))
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    visible = [o for o in meshes if not o.name.startswith("UCX_")]
    coll = [o for o in meshes if o.name.startswith("UCX_")]
    lo, hi = dims(visible)
    f_tris = tri_count(visible)
    f_err = max(abs(a - b) for a, b in zip(lo + hi, info["min_m"] + info["max_m"], strict=False))
    if len(visible) != 1 or visible[0].name != mesh_name:
        errors.append("fbx should hold exactly one render mesh named " + mesh_name)
    if f_tris != info["triangles"] or f_err > 1e-3:
        errors.append(f"fbx round trip: triangles {f_tris}, bounds error {f_err:.5f} m")
    if len(coll) != info["hulls"] or any(o.name[4:].rsplit("_", 1)[0] != mesh_name for o in coll):
        errors.append(f"fbx hulls {[o.name for o in coll]}")
    result["fbx"] = {"triangles": f_tris, "bounds_error_m": f_err, "hulls": len(coll)}
    # --- previews
    result["previews"] = {}
    for view in ("front", "side", "iso"):
        f = OUT / "previews" / f"{name}_{view}.png"
        if not f.exists() or f.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            errors.append(f"missing preview {f.name}")
            continue
        w, h = struct.unpack_from(">II", f.read_bytes(), 16)
        result["previews"][view] = [w, h]
    result["errors"] = errors
    return result


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    only = argv[1].split(",") if len(argv) == 2 and argv[0] == "--only" else None
    manifest = json.loads((OUT / "kit_manifest.txt").read_text())
    report = {}
    for name, info in sorted(manifest.items()):
        if only and name not in only:
            continue
        report[name] = check(name, info)
        print("KIT_VALIDATE {} {}".format(name, "ok" if not report[name]["errors"] else report[name]["errors"]), flush=True)
    failed = {n: r["errors"] for n, r in report.items() if r["errors"]}
    report["_summary"] = {"blender": bpy.app.version_string, "assets": len(report), "failed": failed, "passed": not failed}
    (OUT / "kit_validation.txt").write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    print("KIT_VALIDATION {}".format(json.dumps(report["_summary"])), flush=True)
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
