"""Gun-tool props for the crew, made with Blender (bpy): actual geometry, fixed sizes, no downloads.

  Tooling/bpy-venv/Scripts/python.exe tools/anim/make_props.py [--out <AnimationPass>/props]

Each prop is one mesh with flat Principled materials, long axis +X, origin at its middle (the cartridge:
at its middle), metres in Blender and centimetres in the FBX. Sizes are for a 6-pounder (bore 9.3 cm,
documented); the exact lengths and the shapes are this pass's own (GAME INVENTION where not noted).
  SM_SpongeRammer  2.40 m ash staff; +X end a fleece sponge head (9 x 22 cm), -X end a wooden rammer head (8.6 x 10 cm)
  SM_Linstock      1.50 m staff with a forked iron head at +X holding the slow match
  SM_Handspike     1.70 m lever, 6.5 cm at the +X (working) end tapering to 4 cm at the grip
  SM_Cartridge     flannel powder bag, 8.6 cm across and 18 cm long
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy

DEFAULT_OUT = Path(r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass\props")
WOOD = (0.36, 0.24, 0.13, 1.0)
DARK_WOOD = (0.22, 0.14, 0.08, 1.0)
FLEECE = (0.82, 0.78, 0.68, 1.0)
IRON = (0.08, 0.08, 0.09, 1.0)
FLANNEL = (0.74, 0.70, 0.62, 1.0)
MATCH = (0.55, 0.47, 0.33, 1.0)


def material(name, rgba, rough=0.8, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = rgba
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    m.diffuse_color = rgba
    return m


def cyl(x0, x1, r0, r1, mat, verts=20):
    """A (tapered) cylinder along +X from x0 to x1."""
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r0, radius2=r1, depth=abs(x1 - x0),
                                    location=((x0 + x1) / 2.0, 0, 0), rotation=(0, math.radians(90), 0))
    o = bpy.context.object
    o.data.materials.append(mat)
    return o


def box(center, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=center)
    o = bpy.context.object
    o.scale = size
    o.data.materials.append(mat)
    return o


def finish(name, parts, out: Path):
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    o = bpy.context.object
    o.name = name
    o.data.name = name
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.shade_smooth()
    bpy.ops.export_scene.fbx(filepath=str(out / (name + ".fbx")), use_selection=True, object_types={"MESH"},
                             apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS", axis_forward="-Y", axis_up="Z",
                             mesh_smooth_type="FACE", bake_anim=False)
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
    dims = tuple(round(d, 3) for d in o.dimensions)
    print("%s: %d triangles, %s m" % (name, tris, dims))


def main() -> int:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    a = ap.parse_args(argv)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    def fresh():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        return {"wood": material("M_Tool_Wood", WOOD), "dark": material("M_Tool_DarkWood", DARK_WOOD),
                "fleece": material("M_Tool_Fleece", FLEECE, 1.0), "iron": material("M_Tool_Iron", IRON, 0.5, 1.0),
                "flannel": material("M_Tool_Flannel", FLANNEL, 1.0), "match": material("M_Tool_Match", MATCH, 1.0)}

    m = fresh()
    finish("SM_SpongeRammer", [
        cyl(-1.10, 0.98, 0.02, 0.02, m["wood"]),
        cyl(0.98, 1.20, 0.045, 0.045, m["fleece"], 24), cyl(1.20, 1.205, 0.045, 0.03, m["fleece"], 24),
        cyl(-1.20, -1.10, 0.043, 0.043, m["dark"], 24), cyl(-1.10, -1.07, 0.043, 0.02, m["dark"], 24),
    ], out)

    m = fresh()
    finish("SM_Linstock", [
        cyl(-0.75, 0.62, 0.016, 0.016, m["wood"]), cyl(-0.75, -0.80, 0.016, 0.004, m["iron"]),
        cyl(0.62, 0.66, 0.02, 0.02, m["iron"]),
        box((0.71, 0.0, 0.03), (0.10, 0.012, 0.012), m["iron"]), box((0.71, 0.0, -0.03), (0.10, 0.012, 0.012), m["iron"]),
        cyl(0.60, 0.76, 0.006, 0.006, m["match"], 8),
    ], out)

    m = fresh()
    finish("SM_Handspike", [cyl(-0.85, 0.35, 0.02, 0.026, m["wood"]), cyl(0.35, 0.85, 0.026, 0.0325, m["wood"])], out)

    m = fresh()
    finish("SM_Cartridge", [cyl(-0.09, 0.07, 0.043, 0.043, m["flannel"], 24), cyl(0.07, 0.09, 0.043, 0.012, m["flannel"], 24)], out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
