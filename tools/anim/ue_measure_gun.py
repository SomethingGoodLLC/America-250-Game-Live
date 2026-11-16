"""Measure the field gun and the soldier for the animation pass (read-only on assets).

Writes gun_geometry.json: the cannon's bounds and sockets, the drill stations approved by the user on
2026-11-15 (all provisional), each station's offset from Muzzle and Vent, the bore axis and staff hand
targets, the soldier's standing height and the SOMA/soldier scale ratio.

Run (editor Python, no rendering needed):
  UnrealEditor-Cmd <project.uproject> -run=pythonscript -script=<this file> -unattended -nullrhi -nosplash
Environment:
  PA250_GUN_GEOMETRY_OUT   output path (default: gun_geometry.json beside this script)
  PA250_SOMA_HEIGHT_M      SOMA standard body height in metres (default 1.758, kimodo skin_standard.npz)
"""
import json
import os

import unreal

MESH = "/Game/ThirdParty/PTM_Cannon/Mesh"
SOLDIER = "/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER"
OUT = os.environ.get("PA250_GUN_GEOMETRY_OUT") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "gun_geometry.json")
SOMA_HEIGHT_M = float(os.environ.get("PA250_SOMA_HEIGHT_M", "1.758"))
SOCKETS = ["Muzzle", "Vent", "Crew_1", "Crew_2", "Crew_3", "Crew_4", "Trail", "TrailL", "Linstock"]


def v3(v):
    return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]


def sub(a, b):
    return [round(a[i] - b[i], 2) for i in range(3)]


mesh = unreal.load_asset(MESH)
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError("PA250 gun geometry: %s is not a StaticMesh" % MESH)
box = mesh.get_bounding_box()
bmin, bmax = v3(box.min), v3(box.max)

sockets = {}
for name in SOCKETS:
    s = mesh.find_socket(name)
    sockets[name] = v3(s.get_editor_property("relative_location")) if s else None
if not sockets["Muzzle"] or not sockets["Vent"]:
    raise RuntimeError("PA250 gun geometry: Muzzle or Vent socket is missing; run Scripts/setup_cannon_asset.py first")
muzzle, vent = sockets["Muzzle"], sockets["Vent"]

soldier = unreal.load_asset(SOLDIER)
if not isinstance(soldier, unreal.SkeletalMesh):
    raise RuntimeError("PA250 gun geometry: %s is not a SkeletalMesh" % SOLDIER)
sb = soldier.get_bounds()
soldier_min_z = sb.origin.z - sb.box_extent.z
soldier_max_z = sb.origin.z + sb.box_extent.z
soldier_height_cm = round(soldier_max_z - soldier_min_z, 2)
ratio = round(SOMA_HEIGHT_M * 100.0 / soldier_height_cm, 4)

# Drill stations (user, 2026-11-15), mesh space, cm, ground level. Trail stations come from the bounds.
trail_x = round(bmin[0] - 40.0, 2)
stations = {
    "station_MuzzleR": {"role": "sponger (sponge, ram)", "cm": [176.0, 70.0, 0.0]},
    "station_MuzzleL": {"role": "loader", "cm": [176.0, -70.0, 0.0]},
    "station_Vent": {"role": "ventsman (vent, prime)", "cm": [10.0, 50.0, 0.0]},
    "Linstock": {"role": "firer", "cm": [10.0, -90.0, 0.0]},
    "Trail": {"role": "captain (lay)", "cm": [trail_x, 0.0, 0.0], "derived": "x = min bound - 40"},
    "TrailL": {"role": "handspike man", "cm": [round(bmin[0] + 20.0, 2), 60.0, 0.0],
               "derived": "x = min bound + 20, y = 60: beside the trail end, a bar's length from it (my placement)"},
    "Crew_3": {"role": "stand-by", "cm": sockets["Crew_3"]},
    "Crew_4": {"role": "stand-by", "cm": sockets["Crew_4"]},
}
for st in stations.values():
    st["provisional"] = True
    if st["cm"] is not None:
        st["offset_from_muzzle_cm"] = sub(st["cm"], muzzle)
        st["offset_from_vent_cm"] = sub(st["cm"], vent)

bore_z = muzzle[2]
out = {
    "units": "cm, cannon mesh space: +X is the muzzle end, Z up, ground at z = 0",
    "mesh": MESH,
    "bounds": {"min": bmin, "max": bmax, "size": sub(bmax, bmin)},
    "sockets": sockets,
    "sockets_missing": [n for n in SOCKETS if sockets[n] is None],
    "socket_offsets": {n: {"from_muzzle_cm": sub(p, muzzle), "from_vent_cm": sub(p, vent), "height_cm": p[2]}
                       for n, p in sockets.items() if p},
    "stations": stations,
    "bore_axis": {"origin_cm": muzzle, "direction": [1.0, 0.0, 0.0], "height_cm": bore_z,
                  "note": "the one-piece mesh stays level (actor pitch 0), so the bore is the +X line through Muzzle"},
    "hand_targets": {
        "sponge_hold_forward_of_muzzle_cm": [100.0, 140.0],
        "ram_stroke_forward_of_muzzle_cm": [140.0, 70.0, 140.0],
        "muzzle_mouth_cm": muzzle,
        "vent_cm": vent,
        "note": "staff hands lie on the bore axis extended forward of Muzzle at bore height",
    },
    "soldier": {"mesh": SOLDIER, "standing_height_cm": soldier_height_cm,
                "bounds_z_cm": [round(soldier_min_z, 2), round(soldier_max_z, 2)],
                "note": "reference-pose mesh bounds, hat included if the mesh has one"},
    "soma": {"standing_height_cm": round(SOMA_HEIGHT_M * 100.0, 2), "source": "kimodo somaskel77 skin_standard.npz"},
    "scale_soma_over_soldier": ratio,
    "scale_note": "multiply every constraint target by this ratio before it goes to Kimodo",
}
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=2)
unreal.log("PA250 gun geometry: bounds min %s max %s; muzzle %s vent %s; soldier %.1f cm; SOMA/soldier %.4f; wrote %s"
           % (bmin, bmax, muzzle, vent, soldier_height_cm, ratio, OUT))
