"""Import the SOMA skeleton and build IK_SOMA and RTG_SOMA_to_Soldier (new packages only).

  UnrealEditor-Cmd <project> -run=pythonscript -script=tools/anim/ue_build_soma_rig.py -unattended -nullrhi -nosplash
Environment:
  PA250_ANIM_PASS     the AnimationPass folder (fbx/SK_SOMA.fbx is read from it)
  PA250_SOMA_DUMP=1   only import the mesh and dump the soldier rig's chains and both skeletons to
                      <AnimationPass>/logs/rig_dump.json (no rig is made)

/Game/PA250/Anim/Rig/SOMA/SK_SOMA (+ skeleton, physics asset) is this pass's proxy mesh on Kimodo's SOMA
skeleton. IK_SOMA carries the same retarget chain names as the soldier's IK_Soldier so the retargeter
maps them one to one. Existing packages are never overwritten: the script stops if one is in the way.
"""
import json
import os

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
RIG = "/Game/PA250/Anim/Rig"
SOMA_DIR = RIG + "/SOMA"
SOLDIER = "/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER"
IK_SOLDIER = RIG + "/IK_Soldier"
IK_SOMA = RIG + "/IK_SOMA"
RTG = RIG + "/RTG_SOMA_to_Soldier"
DUMP_ONLY = os.environ.get("PA250_SOMA_DUMP") == "1"
eal = unreal.EditorAssetLibrary

# Soldier chain name -> (SOMA start bone, SOMA end bone). Filled against rig_dump.json; a soldier chain with
# no entry here stops the script, so a renamed chain cannot go unmapped unnoticed.
CHAINS = {
    "root": ("Root", "Root"),
    "Spine": ("Spine1", "Chest"),
    "Neck": ("Neck1", "Neck2"),
    "head": ("Head", "Head"),
    "LeftLeg": ("LeftLeg", "LeftFoot"), "RightLeg": ("RightLeg", "RightFoot"),
    "LeftToe": ("LeftToeBase", "LeftToeBase"), "RightToe": ("RightToeBase", "RightToeBase"),
    "LeftClavicle": ("LeftShoulder", "LeftShoulder"), "RightClavicle": ("RightShoulder", "RightShoulder"),
    "LeftArm": ("LeftArm", "LeftHand"), "RightArm": ("RightArm", "RightHand"),
}
for _side in ("Left", "Right"):
    CHAINS[_side + "Thumb"] = (_side + "HandThumb1", _side + "HandThumb3")
    for _f in ("Index", "Middle", "Ring", "Pinky"):  # SOMA's finger 1 is the metacarpal; the soldier starts at the knuckle
        CHAINS[_side + _f] = ("%sHand%s2" % (_side, _f), "%sHand%s4" % (_side, _f))


def fail(text):
    unreal.log_error("PA250 soma rig: " + text)
    raise RuntimeError(text)


def import_mesh():
    path = SOMA_DIR + "/SK_SOMA"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeletal_mesh_import_data.set_editor_property("convert_scene", True)
    ui.skeletal_mesh_import_data.set_editor_property("convert_scene_unit", True)
    ui.skeletal_mesh_import_data.set_editor_property("use_t0_as_ref_pose", False)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(PASS, "fbx", "SK_SOMA.fbx")
    task.destination_path = SOMA_DIR
    task.destination_name = "SK_SOMA"
    task.automated = True
    task.save = True
    task.replace_existing = False
    task.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    eal.save_directory(SOMA_DIR, only_if_is_dirty=False, recursive=True)
    mesh = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if not isinstance(mesh, unreal.SkeletalMesh):
        fail("SK_SOMA.fbx did not import as a skeletal mesh")
    return mesh


def bones_of(mesh):
    """Bone name, parent name and reference-pose component-space position for every bone."""
    skel = mesh.skeleton
    ref = skel.get_reference_pose()
    names = ref.get_bone_names()
    out = []
    comp = {}
    for n in names:
        local = ref.get_ref_bone_pose(n, unreal.AnimPoseSpaces.WORLD)
        comp[str(n)] = local
        out.append({"name": str(n), "pos": [round(local.translation.x, 2), round(local.translation.y, 2), round(local.translation.z, 2)]})
    return out


def dump(soma, soldier):
    ik = unreal.load_asset(IK_SOLDIER)
    c = unreal.IKRigController.get_controller(ik)
    chains = []
    for ch in c.get_retarget_chains():
        n = ch.chain_name
        chains.append({"name": str(n), "start": str(c.get_retarget_chain_start_bone(n)),
                       "end": str(c.get_retarget_chain_end_bone(n)), "goal": str(c.get_retarget_chain_goal(n))})
    data = {"soldier_chains": chains, "soldier_retarget_root": str(c.get_retarget_root()),
            "soldier_bones": bones_of(soldier), "soma_bones": bones_of(soma)}
    path = os.path.join(PASS, "logs", "rig_dump.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    unreal.log("PA250 soma rig: %d soldier chains, %d soldier bones, %d SOMA bones; wrote %s" % (
        len(chains), len(data["soldier_bones"]), len(data["soma_bones"]), path))


def configure_ops(rc):
    """Name the pelvis and root bones on the ops that need them (the default ops leave them unset, and
    a retarget then leaves the soldier's pelvis at the origin)."""
    for i in range(rc.get_num_retarget_ops()):
        name = str(rc.get_op_name(i))
        c = rc.get_op_controller(i)
        if name == "Pelvis Motion":
            c.set_source_pelvis_bone("Hips")
            c.set_target_pelvis_bone("pelvis")
            unreal.log("PA250 soma rig: pelvis motion %s -> %s" % (c.get_source_pelvis_bone(), c.get_target_pelvis_bone()))
        elif name == "Root Motion":
            c.set_source_root_bone("Root")
            c.set_target_root_bone("root")
            c.set_target_pelvis_bone("pelvis")
            unreal.log("PA250 soma rig: root motion %s -> %s" % (c.get_source_root_bone(), c.get_target_root_bone()))


def build(soma, soldier):
    for path in (IK_SOMA, RTG):
        if eal.does_asset_exist(path):
            fail("%s already exists; this script only makes new packages" % path)
    if not CHAINS:
        fail("CHAINS is empty: run with PA250_SOMA_DUMP=1 and fill it from rig_dump.json")
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    src = unreal.IKRigController.get_controller(unreal.load_asset(IK_SOLDIER))
    ik = tools.create_asset("IK_SOMA", RIG, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(ik)
    if not c.set_skeletal_mesh(soma):
        fail("IK_SOMA does not take the SOMA mesh")
    c.set_retarget_root("Hips")
    for ch in src.get_retarget_chains():
        name = str(ch.chain_name)
        if name not in CHAINS:
            fail("soldier chain %s has no SOMA bones in CHAINS" % name)
        start, end = CHAINS[name]
        c.add_retarget_chain(name, start, end, "")
    eal.save_loaded_asset(ik)

    rtg = tools.create_asset("RTG_SOMA_to_Soldier", RIG, unreal.IKRetargeter, unreal.IKRetargetFactory())
    rc = unreal.IKRetargeterController.get_controller(rtg)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(IK_SOLDIER))
    rc.add_default_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, ik)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(IK_SOLDIER))
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, soma)
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, soldier)
    configure_ops(rc)
    rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    eal.save_loaded_asset(rtg)
    unreal.log("PA250 soma rig: made %s (%d chains) and %s" % (IK_SOMA, len(c.get_retarget_chains()), RTG))


soma_mesh = import_mesh()
soldier_mesh = unreal.load_asset(SOLDIER)
if not isinstance(soldier_mesh, unreal.SkeletalMesh):
    fail("no soldier mesh at " + SOLDIER)
if DUMP_ONLY:
    dump(soma_mesh, soldier_mesh)
elif os.environ.get("PA250_SOMA_FIX_OPS") == "1":  # repair a retargeter this script made before configure_ops existed
    _rtg = unreal.load_asset(RTG)
    configure_ops(unreal.IKRetargeterController.get_controller(_rtg))
    eal.save_loaded_asset(_rtg, only_if_is_dirty=False)
else:
    build(soma_mesh, soldier_mesh)
