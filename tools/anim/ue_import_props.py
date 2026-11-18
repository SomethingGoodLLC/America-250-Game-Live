"""Import the gun-tool props (tools/anim/make_props.py) and report their sizes and the musket's.

  UnrealEditor-Cmd <project> -run=pythonscript -script=tools/anim/ue_import_props.py -unattended -nullrhi -nosplash
Props go to /Game/PA250/Props/GunTools/SM_* with the materials the FBX carries; a prop already there is kept.
"""
import os

import unreal

PASS = os.environ.get("PA250_ANIM_PASS", r"C:\Users\user\Documents\ChatGPT\Play America 250\AnimationPass")
DST = "/Game/PA250/Props/GunTools"
MUSKET = "/Game/HS_WeaponPack_01/Meshes/MusketRifle/Meshes/SM_HS_WP01_MusketRifle_X_0"
NAMES = ("SM_SpongeRammer", "SM_Linstock", "SM_Handspike", "SM_Cartridge", "SM_RoundShot", "SM_Spyglass")
eal = unreal.EditorAssetLibrary

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
tasks = []
for n in NAMES:
    if eal.does_asset_exist("%s/%s" % (DST, n)):
        continue
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.import_animations = False
    ui.import_materials = True
    ui.import_textures = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    sd = ui.static_mesh_import_data
    sd.set_editor_property("combine_meshes", True)
    sd.set_editor_property("auto_generate_collision", False)
    sd.set_editor_property("convert_scene", True)
    sd.set_editor_property("convert_scene_unit", True)
    t = unreal.AssetImportTask()
    t.filename = os.path.join(PASS, "props", n + ".fbx")
    t.destination_path = DST
    t.destination_name = n
    t.automated = True
    t.save = True
    t.replace_existing = False
    t.options = ui
    tasks.append(t)
if tasks:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    eal.save_directory(DST, only_if_is_dirty=False, recursive=True)
for path in ["%s/%s" % (DST, n) for n in NAMES] + [MUSKET]:
    m = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if not isinstance(m, unreal.StaticMesh):
        unreal.log_error("PA250 props: missing " + path)
        continue
    b = m.get_bounding_box()
    unreal.log("PA250 props: %s min (%.1f, %.1f, %.1f) max (%.1f, %.1f, %.1f) cm, %d materials" % (
        path.rsplit("/", 1)[-1], b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z, len(m.static_materials)))
