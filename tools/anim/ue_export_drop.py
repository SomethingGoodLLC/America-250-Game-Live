"""Export finished clips (soldier skeleton, hand fix-up baked) as FBX for the game's drop folder.

  UnrealEditor-Cmd <scratch project> -run=pythonscript -script=tools/anim/ue_export_drop.py -unattended -nullrhi -nosplash
Environment: PA250_ANIM_PASS; PA250_DROP_DIR (default: the repository's unreal/PlayAmerica250/SourceArt/anim_drop)

Two sets:
  <AnimationPass>/drop_soldier/<key lower case>@soldier.fbx   every clip and transition (139)
  <PA250_DROP_DIR>/<registry name>@soldier.fbx                 the clips that answer a name already in
                                                               anim_clips.json "clips" (MAPPED below)
The game's importer (Scripts/import_anim_drop.py) takes the second set as it stands: rig `soldier`, no retarget.
An FBX carries bone tracks only: the notifies, root-motion flags and loop flags live on the AnimSequences in
/Game/PA250/Anim/Generated and are listed in its README_clips.md and in manifest.json.
"""
import json
import os

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DROP = os.environ.get("PA250_DROP_DIR") or os.path.join(REPO, "unreal", "PlayAmerica250", "SourceArt", "anim_drop")
GEN = "/Game/PA250/Anim/Generated"
# registry name (anim_clips.json) -> clip of this pass. Only names whose `shows` text this clip actually answers.
MAPPED = {
    "reload_stand_prime_pan": "AM_Reload_PrimePan_v0",
    "reload_stand_ram_charge": "AM_Reload_RamCharge_v0",
    "reload_stand_shoulder_aim": "AM_Reload_Shoulder_v0",
    "gun_crew_ram_home": "AM_Gun_Ram_v0",
    "crawl_wounded": "AM_Wound_Crawl_v0",
}

manifest = json.load(open(os.path.join(PASS, "manifest.json"), encoding="utf-8"))
family = {k: c["family"] for k, c in manifest["clips"].items()}
for k, t in manifest.get("transitions", {}).items():
    family[k] = manifest["clips"][t["from"]]["family"]


def export(key, path):
    seq = unreal.load_asset("%s/%s/%s" % (GEN, family[key], key))
    if not isinstance(seq, unreal.AnimSequence):
        raise RuntimeError("no clip " + key)
    opt = unreal.FbxExportOption()
    opt.set_editor_property("ascii", False)
    opt.set_editor_property("export_preview_mesh", False)
    opt.set_editor_property("bake_camera_and_light_animation", unreal.MovieSceneBakeType.BAKE_NONE) if hasattr(
        unreal, "MovieSceneBakeType") and hasattr(opt, "bake_camera_and_light_animation") else None
    task = unreal.AssetExportTask()
    task.object = seq
    task.filename = path
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.options = opt
    task.exporter = unreal.AnimSequenceExporterFBX()
    return unreal.Exporter.run_asset_export_task(task) and os.path.exists(path)


all_dir = os.path.join(PASS, "drop_soldier")
os.makedirs(all_dir, exist_ok=True)
os.makedirs(DROP, exist_ok=True)
done = failed = 0
for key in sorted(family):
    if export(key, os.path.join(all_dir, key.lower() + "@soldier.fbx")):
        done += 1
    else:
        failed += 1
mapped = 0
for name, key in MAPPED.items():
    if export(key, os.path.join(DROP, name + "@soldier.fbx")):
        mapped += 1
    else:
        failed += 1
unreal.log("PA250 export drop: %d clips to %s, %d registry clips to %s, %d failed" % (done, all_dir, mapped, DROP, failed))
