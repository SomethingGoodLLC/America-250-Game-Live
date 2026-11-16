"""Import the pass's FBX clips onto the SOMA skeleton and retarget them to the soldier.

  UnrealEditor-Cmd <project> -run=pythonscript -script=tools/anim/ue_import_retarget.py -unattended -nullrhi -nosplash
Environment:
  PA250_ANIM_PASS   the AnimationPass folder (manifest.json, fbx/)
  PA250_ONLY        comma-separated clip keys (default: everything in the manifest)

Source clips (SOMA skeleton) go to /Game/PA250/Anim/Rig/SOMA/Clips/<key>; they are intermediates.
Retargeted clips go to /Game/PA250/Anim/Generated/{Gun,Volley,React}/<key> through RTG_SOMA_to_Soldier,
with root motion enabled on the clips the manifest marks `root_motion`. A clip already in place is kept.
"""
import json
import os

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
ONLY = [k for k in os.environ.get("PA250_ONLY", "").split(",") if k]
RIG = "/Game/PA250/Anim/Rig"
SRC_DIR = RIG + "/SOMA/Clips"
OUT = "/Game/PA250/Anim/Generated"
SOLDIER = "/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER"
eal = unreal.EditorAssetLibrary


def fail(text):
    unreal.log_error("PA250 retarget: " + text)
    raise RuntimeError(text)


manifest = json.load(open(os.path.join(PASS, "manifest.json"), encoding="utf-8"))
clips = {}
for key, m in manifest["clips"].items():
    clips[key] = {"family": m["family"], "root_motion": bool(m.get("root_motion"))}
for key, t in manifest.get("transitions", {}).items():
    clips[key] = {"family": manifest["clips"][t["from"]]["family"], "root_motion": False}
keys = sorted(k for k in clips if not ONLY or k in ONLY)

soma = unreal.load_asset(RIG + "/SOMA/SK_SOMA")
soldier = unreal.load_asset(SOLDIER)
rtg = unreal.load_asset(RIG + "/RTG_SOMA_to_Soldier")
if not soma or not soldier or not rtg:
    fail("SK_SOMA, the soldier or RTG_SOMA_to_Soldier is missing: run ue_build_soma_rig.py first")

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
tasks = []
for key in keys:
    fbx = os.path.join(PASS, "fbx", key + ".fbx")
    if eal.does_asset_exist("%s/%s" % (SRC_DIR, key)):
        continue
    if not os.path.exists(fbx):
        fail("no FBX for %s: run bvh_to_fbx.py" % key)
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_as_skeletal = True
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.skeleton = soma.skeleton
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ad = ui.anim_sequence_import_data
    ad.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    ad.set_editor_property("import_bone_tracks", True)
    ad.set_editor_property("use_default_sample_rate", False)
    ad.set_editor_property("custom_sample_rate", 30)
    ad.set_editor_property("convert_scene", True)
    ad.set_editor_property("convert_scene_unit", True)
    ad.set_editor_property("remove_redundant_keys", False)
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = SRC_DIR
    task.destination_name = key
    task.automated = True
    task.save = True
    task.replace_existing = False
    task.options = ui
    tasks.append(task)
if tasks:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
missing = [k for k in keys if not eal.does_asset_exist("%s/%s" % (SRC_DIR, k))]
if missing:
    fail("%d clips did not import, first: %s" % (len(missing), missing[0]))

made = 0
for family in ("Gun", "Volley", "React"):
    folder = "%s/%s" % (OUT, family)
    todo = [k for k in keys if clips[k]["family"] == family and not eal.does_asset_exist("%s/%s" % (folder, k))]
    if not todo:
        continue
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = [eal.find_asset_data("%s/%s" % (SRC_DIR, k)) for k in todo]
    inputs.source_mesh = soma
    inputs.target_mesh = soldier
    inputs.ik_retarget_asset = rtg
    inputs.use_source_path = False
    inputs.target_path = folder
    inputs.include_referenced_assets = False
    inputs.overwrite_existing_files = False
    out = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    for data in out:
        seq = unreal.load_asset(str(data.package_name))
        key = str(data.asset_name)
        if isinstance(seq, unreal.AnimSequence) and key in clips:
            seq.set_editor_property("enable_root_motion", clips[key]["root_motion"])
            seq.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
        eal.save_asset(str(data.package_name), only_if_is_dirty=False)
    made += len(out)

lost = [k for k in keys if not eal.does_asset_exist("%s/%s/%s" % (OUT, clips[k]["family"], k))]
unreal.log("PA250 retarget: %d clips asked, %d imported now, %d retargeted now, %d missing%s" % (
    len(keys), len(tasks), made, len(lost), (" (first: %s)" % lost[0]) if lost else ""))
if lost:
    fail("%d clips have no retargeted AnimSequence" % len(lost))
