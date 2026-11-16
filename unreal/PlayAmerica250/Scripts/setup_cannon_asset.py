"""Prepare the purchased cannon mesh for the field gun (APA250FieldGun).

The Fab pack "CANNON - GAME READY" (PTM-MODELS) at /Game/ThirdParty/PTM_Cannon is ONE static mesh
(barrel, carriage and wheels in one section), 333 cm long, 124 cm high, with no sockets and no
collision. This script adds, on the asset itself:
  sockets   Muzzle, Vent, Crew_1 .. Crew_4, Trail, TrailL, Linstock (mesh space; muzzle at +X)
  collision "use complex collision as simple", so men, balls and sight lines are stopped by the piece
and reports its size beside a 180 cm soldier. It is idempotent and touches nothing else.

Run (editor closed):
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=Scripts/setup_cannon_asset.py -unattended -nullrhi
  PA250_CANNON_VERIFY_ONLY=1 ...   reports and changes nothing
"""
import os

import unreal

MESH = "/Game/ThirdParty/PTM_Cannon/Mesh"
VERIFY_ONLY = os.environ.get("PA250_CANNON_VERIFY_ONLY") == "1"
# Mesh-space places, read off the mesh's own vertices (the narrow, high end of the barrel is at +X).
# Role map (Artillery pass): Crew_1 sponger/rammer (muzzle R), Crew_2 loader (muzzle L),
# Vent ventsman, Linstock firer, TrailL handspike, Trail captain; Crew_3/4 stand-by.
SOCKETS = {
    "Muzzle": (156.0, 0.0, 113.0),
    "Vent": (10.0, 0.0, 116.0),
    "Crew_1": (40.0, 115.0, 0.0),
    "Crew_2": (40.0, -115.0, 0.0),
    "Crew_3": (-90.0, 75.0, 0.0),
    "Crew_4": (-90.0, -75.0, 0.0),
    "Trail": (-140.0, 0.0, 40.0),
    "TrailL": (-120.0, 90.0, 20.0),
    "Linstock": (20.0, 160.0, 40.0),
}

mesh = unreal.load_asset(MESH)
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError("PA250 cannon: %s is not a StaticMesh (the Fab pack is missing?)" % MESH)
b = mesh.get_bounds()
length = 2.0 * max(b.box_extent.x, b.box_extent.y)
height = 2.0 * b.box_extent.z
unreal.log("PA250 cannon: one mesh, %d section(s), %d triangles; %.0f cm long, %.0f cm wide, %.0f cm high" % (
    mesh.get_num_sections(0), mesh.get_num_triangles(0), length, 2.0 * min(b.box_extent.x, b.box_extent.y), height))
unreal.log("PA250 cannon: beside a 180 cm soldier the wheels stand to %.0f %% of his height (a 6-pounder's wheels are about chest high: 65-75 %%)" % (
    100.0 * height / 180.0))
changed = False
errors = []
for name, (x, y, z) in SOCKETS.items():
    sock = mesh.find_socket(name)
    want = unreal.Vector(x, y, z)
    if sock is None:
        if VERIFY_ONLY:
            errors.append("socket %s is missing" % name)
            continue
        sock = unreal.StaticMeshSocket(mesh)
        sock.set_editor_property("socket_name", name)
        sock.set_editor_property("relative_location", want)
        mesh.add_socket(sock)
        changed = True
        unreal.log("PA250 cannon: added socket %s at (%.0f, %.0f, %.0f)" % (name, x, y, z))
    else:
        at = sock.get_editor_property("relative_location")
        if abs(at.x - x) + abs(at.y - y) + abs(at.z - z) > 0.5:
            if VERIFY_ONLY:
                errors.append("socket %s is misplaced" % name)
            else:
                sock.set_editor_property("relative_location", want)
                changed = True
        unreal.log("PA250 cannon: socket %s at (%.0f, %.0f, %.0f)" % (name, at.x, at.y, at.z))
body = mesh.get_editor_property("body_setup")
flag = body.get_editor_property("collision_trace_flag")
if flag != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
    if VERIFY_ONLY:
        errors.append("collision is %s, not complex-as-simple" % flag)
    else:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        changed = True
        unreal.log("PA250 cannon: collision set to use the mesh itself (complex as simple)")
else:
    unreal.log("PA250 cannon: collision uses the mesh itself (complex as simple)")
if changed:
    mesh.modify()
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
        raise RuntimeError("PA250 cannon: failed to save mesh")
    unreal.log("PA250 cannon: asset saved")
if errors:
    raise RuntimeError("PA250 cannon: verification failed: " + "; ".join(errors))
for name in SOCKETS:
    if mesh.find_socket(name) is None:
        raise RuntimeError("PA250 cannon: missing socket after setup: " + name)
unreal.log("PA250 cannon: all %d sockets asserted" % len(SOCKETS))
unreal.log("PA250 cannon: setup done (%s)" % ("verified only" if VERIFY_ONLY else ("changed" if changed else "already set up")))
