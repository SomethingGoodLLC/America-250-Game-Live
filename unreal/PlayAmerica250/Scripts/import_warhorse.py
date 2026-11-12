"""Import WarHorse source art. Run using UnrealEditor-Cmd -run=pythonscript.

Creates /Game/PA250/Characters/WarHorse. On reruns the mesh, skeleton and clips are
kept (the Rider/ clips and APA250MountedSoldier depend on them). Texture settings,
material graphs and mesh slot assignments are corrected in place and always saved.
The mesh faces +Y: apply -90 degrees yaw when adding it to a pawn.

PA250_WARHORSE_VERIFY_ONLY=1 checks the saved assets (run it in a fresh process) and
changes nothing. Each run ends with WARHORSE_IMPORT_OK / WARHORSE_VERIFY_OK, or raises.
"""

import json
import os
from pathlib import Path

import unreal

SRC = (
    Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    / "SourceArt/warhorse"
)
SRC = SRC.resolve()
DEST = "/Game/PA250/Characters/WarHorse"
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
PARTS = ("Body", "Tack")
CLIPS_JSON = SRC / "WarHorse_clips.json"
# ORM: R unused AO (white), G roughness, B metallic (README_UE_Import.md).
WIRING = (
    ("BC", "RGB", "MP_BASE_COLOR"),
    ("ORM", "G", "MP_ROUGHNESS"),
    ("ORM", "B", "MP_METALLIC"),
)


def save(asset):
    # Always write: setting properties from Python does not reliably mark a package
    # dirty, and the default only_if_is_dirty=True then silently skips the save.
    if not LIB.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + asset.get_path_name())


def import_asset(source, folder, name, options=None):
    path = folder + "/" + name
    if LIB.does_asset_exist(path):
        return LIB.load_asset(path)
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.save = True
    task.replace_existing = False
    if options:
        task.options = options
    TOOLS.import_asset_tasks([task])
    asset = LIB.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Import failed: {source} ({task.imported_object_paths})")
    return asset


def fix_texture(texture, suffix):
    texture.set_editor_property("srgb", suffix == "BC")
    texture.set_editor_property(
        "compression_settings",
        unreal.TextureCompressionSettings.TC_DEFAULT
        if suffix == "BC"
        else unreal.TextureCompressionSettings.TC_MASKS,
    )
    save(texture)


def sampler_for(suffix):
    return (
        unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
        if suffix == "BC"
        else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    )


def wiring_problems(material, part):
    problems = []
    if not material.get_editor_property("used_with_skeletal_mesh"):
        problems.append(material.get_name() + " not flagged used_with_skeletal_mesh")
    for suffix, _pin, prop in WIRING:
        node = MEL.get_material_property_input_node(
            material, getattr(unreal.MaterialProperty, prop)
        )
        tex = node.get_editor_property("texture") if node else None
        if (
            tex is None
            or tex.get_name() != f"T_WarHorse_{part}_{suffix}"
            or node.get_editor_property("sampler_type") != sampler_for(suffix)
        ):
            problems.append(
                f"{material.get_name()} {prop} -> {tex.get_name() if tex else None}"
            )
    return problems


def build_material(part, textures):
    name = "M_WarHorse_" + part
    path = DEST + "/Materials/" + name
    if LIB.does_asset_exist(path):
        material = LIB.load_asset(path)
        # Skinned mesh: without this flag the game silently draws the default material.
        material.set_editor_property("used_with_skeletal_mesh", True)
        if not wiring_problems(material, part):
            # Already right: recompile for the corrected texture settings. Never rebuild
            # a graph the mounted soldier's class default may hold through SK_WarHorse
            # (UE 5.8 !IsRooted assert).
            MEL.recompile_material(material)
            save(material)
            return material
    else:
        material = TOOLS.create_asset(
            name, DEST + "/Materials", unreal.Material, unreal.MaterialFactoryNew()
        )
        material.set_editor_property("used_with_skeletal_mesh", True)
    # Two samples, BC -> base colour, ORM.G/B -> roughness/metallic.
    MEL.delete_all_material_expressions(material)
    samples = {}
    for i, suffix in enumerate(("BC", "ORM")):
        sample = MEL.create_material_expression(
            material, unreal.MaterialExpressionTextureSample, -400, i * 300
        )
        sample.set_editor_property("texture", textures[suffix])
        sample.set_editor_property("sampler_type", sampler_for(suffix))
        samples[suffix] = sample
    for suffix, pin, prop in WIRING:
        if not MEL.connect_material_property(
            samples[suffix], pin, getattr(unreal.MaterialProperty, prop)
        ):
            raise RuntimeError(f"Could not wire {name}.{pin} -> {prop}")
    MEL.recompile_material(material)
    save(material)
    return material


def assign_slots(mesh, materials):
    slots = mesh.get_editor_property("materials")
    if len(slots) != 2:
        raise RuntimeError(f"Expected two material slots, got {len(slots)}")
    new_slots = []
    for slot in slots:
        slot_name = str(slot.get_editor_property("material_slot_name")).lower()
        part = next((key for key in materials if key in slot_name), None)
        if part is None:
            raise RuntimeError("Unknown material slot: " + slot_name)
        slot.set_editor_property("material_interface", materials[part])
        new_slots.append(slot)
    mesh.set_editor_property("materials", new_slots)
    save(mesh)


def verify():
    """Raise on any wrong setting; log the measured values."""
    problems = []
    mesh = LIB.load_asset(DEST + "/SK_WarHorse")
    skeleton = mesh.get_editor_property("skeleton") if mesh else None
    if mesh is None or skeleton is None:
        raise RuntimeError("SK_WarHorse or its skeleton is missing")
    ref = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    bones = [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(ref)]
    if len(bones) != 39 or "rider_seat" not in bones:
        problems.append(
            "skeleton: {} bones, rider_seat={}".format(
                len(bones), "rider_seat" in bones
            )
        )
    seat = unreal.AnimPoseExtensions.get_bone_pose(
        ref, "rider_seat", unreal.AnimPoseSpaces.WORLD
    ).translation
    report = {
        "mesh": mesh.get_path_name(),
        "skeleton": skeleton.get_path_name(),
        "bones": len(bones),
        "rider_seat_z_cm": round(seat.z, 1),
        "slots": {},
        "textures": {},
        "clips": {},
    }
    for slot in mesh.get_editor_property("materials"):
        name = str(slot.get_editor_property("material_slot_name"))
        mat = slot.get_editor_property("material_interface")
        got = mat.get_path_name() if mat else None
        report["slots"][name] = got
        want = f"{DEST}/Materials/{name}.{name}"
        if got != want:
            problems.append(f"slot {name} -> {got} (want {want})")
    for part in PARTS:
        for suffix in ("BC", "ORM"):
            tex = LIB.load_asset(f"{DEST}/Textures/T_WarHorse_{part}_{suffix}")
            srgb = tex.get_editor_property("srgb")
            comp = tex.get_editor_property("compression_settings")
            report["textures"][tex.get_name()] = [
                tex.blueprint_get_size_x(),
                srgb,
                str(comp),
            ]
            want_comp = (
                unreal.TextureCompressionSettings.TC_DEFAULT
                if suffix == "BC"
                else unreal.TextureCompressionSettings.TC_MASKS
            )
            if srgb != (suffix == "BC") or comp != want_comp:
                problems.append(f"{tex.get_name()} srgb={srgb} comp={comp}")
        problems += wiring_problems(
            LIB.load_asset(f"{DEST}/Materials/M_WarHorse_{part}"), part
        )
    clips = json.loads(CLIPS_JSON.read_text())
    for clip, info in clips.items():
        anim = LIB.load_asset(f"{DEST}/Animations/A_WarHorse_{clip}")
        if anim is None:
            problems.append("missing clip " + clip)
            continue
        dur = anim.get_play_length()
        report["clips"][clip] = round(dur, 3)
        if anim.get_editor_property("skeleton") != skeleton:
            problems.append("wrong skeleton: " + clip)
        if abs(dur - info["frames"] / info["fps"]) > 0.04:
            problems.append(
                "clip {} lasts {:.3f}s, want {:.3f}s".format(
                    clip, dur, info["frames"] / info["fps"]
                )
            )
    if problems:
        raise RuntimeError("WARHORSE_VERIFY_FAIL " + "; ".join(problems))
    return report


def main():
    if os.environ.get("PA250_WARHORSE_VERIFY_ONLY") == "1":
        unreal.log("WARHORSE_VERIFY_OK " + json.dumps(verify()))
        return
    # Legacy FBX importer, process-local only (project settings untouched):
    # it honours FbxImportUI.
    unreal.SystemLibrary.execute_console_command(
        None, "Interchange.FeatureFlags.Import.FBX 0"
    )
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_mesh = True
    options.import_animations = False
    options.import_materials = False
    options.import_textures = False
    options.skeletal_mesh_import_data.import_uniform_scale = 1.0
    mesh = import_asset(SRC / "SK_WarHorse.fbx", DEST, "SK_WarHorse", options)
    skeleton = mesh.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("Horse skeleton missing")

    materials = {}
    for part in PARTS:
        textures = {}
        for suffix in ("BC", "ORM"):
            name = f"T_WarHorse_{part}_{suffix}"
            texture = import_asset(
                SRC / "Textures" / (name + ".png"), DEST + "/Textures", name
            )
            fix_texture(texture, suffix)
            textures[suffix] = texture
        materials[part.lower()] = build_material(part, textures)
    assign_slots(mesh, materials)

    clips = json.loads(CLIPS_JSON.read_text())
    for clip, info in clips.items():
        name = "A_WarHorse_" + clip
        options = unreal.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
        options.import_mesh = False
        options.import_animations = True
        options.import_materials = False
        options.import_textures = False
        options.skeleton = skeleton
        options.anim_sequence_import_data.import_uniform_scale = 1.0
        options.anim_sequence_import_data.set_editor_property(
            "animation_length",
            unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
        )
        anim = import_asset(
            SRC / "Animations" / (name + ".fbx"), DEST + "/Animations", name, options
        )
        if anim.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Wrong skeleton: " + name)
        anim.set_editor_property("enable_root_motion", False)
        LIB.set_metadata_tag(anim, "WarHorse.Loop", str(info["loop"]).lower())
        LIB.set_metadata_tag(anim, "WarHorse.SpeedCmS", str(info["speed_cm_s"]))
        save(anim)

    report = verify()
    out = Path(unreal.Paths.project_saved_dir()) / "WarHorseImportReport.json"
    out.write_text(json.dumps(report, indent=2))
    unreal.log("WARHORSE_IMPORT_OK " + json.dumps(report))


main()
