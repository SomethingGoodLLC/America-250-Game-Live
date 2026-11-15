"""Read-only: writes every actor of the named levels to Saved/PA250/level_dumps/<level>.json.

Used before a battle level is regenerated (Scripts/build_levels.py deletes the level and builds it
again), to prove nothing placed by hand would be lost: dump the saved level, build the same level
under a scratch name (PA250_LEVEL_SUFFIX=_regen), dump that, and compare the two with
tools/ue/compare_level_dumps.py. Saves nothing and changes no asset.

  PA250_DUMP_LEVELS=/Game/PA250/Maps/L_Lexington,/Game/PA250/Maps/L_Concord \\
  UnrealEditor-Cmd <uproject> -run=pythonscript -script=Scripts/dump_level_actors.py -unattended -nullrhi -nosplash
"""
import json
import os

import unreal

levels = [p.strip() for p in os.environ.get("PA250_DUMP_LEVELS", "").split(",") if p.strip()]
out_dir = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "PA250", "level_dumps")
os.makedirs(out_dir, exist_ok=True)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def r(v, n=1):
    return round(float(v), n)


for path in levels:
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_error("PA250 dump: level %s does not exist" % path)
        continue
    if not les.load_level(path):
        unreal.log_error("PA250 dump: could not load %s" % path)
        continue
    rows = []
    for a in actors_sub.get_all_level_actors():
        loc, rot, scale = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
        row = {
            "label": a.get_actor_label(),
            "class": a.get_class().get_name(),
            "tags": sorted(str(t) for t in a.tags),
            "loc": [r(loc.x), r(loc.y), r(loc.z)],
            "rot": [r(rot.pitch), r(rot.yaw), r(rot.roll)],
            "scale": [r(scale.x, 3), r(scale.y, 3), r(scale.z, 3)],
        }
        meshes, instances = [], 0
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            m = c.get_editor_property("static_mesh")
            if m:
                meshes.append(m.get_path_name())
            if isinstance(c, unreal.InstancedStaticMeshComponent):
                instances += c.get_instance_count()
        if meshes:
            row["meshes"] = sorted(set(meshes))
        if instances:
            row["instances"] = instances
        rows.append(row)
    rows.sort(key=lambda x: (x["class"], x["label"], x["loc"]))
    name = path.rsplit("/", 1)[-1]
    with open(os.path.join(out_dir, name + ".json"), "w") as f:
        json.dump({"level": path, "actors": rows}, f, indent=1)
    unreal.log("PA250 dump: %s -> %d actors" % (path, len(rows)))
