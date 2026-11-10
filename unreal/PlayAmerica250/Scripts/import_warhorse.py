"""Import WarHorse source art. Run using UnrealEditor-Cmd -run=pythonscript.

Creates /Game/PA250/Characters/WarHorse; existing assets are preserved on reruns.
The mesh faces +Y: apply -90 degrees yaw when adding it to a pawn.
"""
import json
from pathlib import Path
import unreal

SRC = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'SourceArt/warhorse'
SRC = SRC.resolve()
DEST = '/Game/PA250/Characters/WarHorse'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def import_asset(source, folder, name, options=None):
    path = folder + '/' + name
    asset = LIB.load_asset(path) if LIB.does_asset_exist(path) else None
    if asset:
        asset.get_editor_property('asset_import_data').scripted_add_filename(str(source), 0, '')
        LIB.save_loaded_asset(asset)
        return asset
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
        raise RuntimeError('Import failed: {} ({})'.format(source, task.imported_object_paths))
    return asset


def main():
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_mesh = True
    options.import_animations = False
    options.import_materials = False
    options.import_textures = False
    options.skeletal_mesh_import_data.import_uniform_scale = 1.0
    mesh = import_asset(SRC / 'SK_WarHorse.fbx', DEST, 'SK_WarHorse', options)
    skeleton = mesh.get_editor_property('skeleton')
    if skeleton is None:
        raise RuntimeError('Horse skeleton missing')

    materials = {}
    for part in ('Body', 'Tack'):
        textures = {}
        for suffix in ('BC', 'ORM'):
            name = 'T_WarHorse_{}_{}'.format(part, suffix)
            texture = import_asset(SRC / 'Textures' / (name + '.png'), DEST + '/Textures', name)
            texture.srgb = suffix == 'BC'
            if suffix == 'ORM':
                texture.compression_settings = unreal.TextureCompressionSettings.TC_MASKS
            LIB.save_loaded_asset(texture)
            textures[suffix] = texture
        name = 'M_WarHorse_' + part
        path = DEST + '/Materials/' + name
        if LIB.does_asset_exist(path):
            material = LIB.load_asset(path)
        else:
            material = TOOLS.create_asset(name, DEST + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
            mel = unreal.MaterialEditingLibrary
            for i, suffix in enumerate(('BC', 'ORM')):
                sample = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, i * 300)
                sample.texture = textures[suffix]
                sample.sampler_type = (unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if suffix == 'BC'
                                       else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
                if suffix == 'BC':
                    assert mel.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
                else:
                    assert mel.connect_material_property(sample, 'G', unreal.MaterialProperty.MP_ROUGHNESS)
                    assert mel.connect_material_property(sample, 'B', unreal.MaterialProperty.MP_METALLIC)
            mel.recompile_material(material)
            LIB.save_loaded_asset(material)
        materials[part.lower()] = material
    slots = mesh.get_editor_property('materials')
    if len(slots) != 2:
        raise RuntimeError('Expected two material slots, got {}'.format(len(slots)))
    for slot in slots:
        slot_name = str(slot.get_editor_property('material_slot_name')).lower()
        part = next((key for key in materials if key in slot_name), None)
        if part is None:
            raise RuntimeError('Unknown material slot: ' + slot_name)
        slot.set_editor_property('material_interface', materials[part])
    mesh.set_editor_property('materials', slots)
    LIB.save_loaded_asset(mesh)

    clips = json.loads((SRC / 'WarHorse_clips.json').read_text())
    report = {'mesh': mesh.get_path_name(), 'skeleton': skeleton.get_path_name(), 'clips': {}}
    for clip, info in clips.items():
        name = 'A_WarHorse_' + clip
        options = unreal.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
        options.import_mesh = False
        options.import_animations = True
        options.import_materials = False
        options.import_textures = False
        options.skeleton = skeleton
        options.anim_sequence_import_data.import_uniform_scale = 1.0
        options.anim_sequence_import_data.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        anim = import_asset(SRC / 'Animations' / (name + '.fbx'), DEST + '/Animations', name, options)
        if anim.get_editor_property('skeleton') != skeleton:
            raise RuntimeError('Wrong skeleton: ' + name)
        anim.set_editor_property('enable_root_motion', False)
        LIB.set_metadata_tag(anim, 'WarHorse.Loop', str(info['loop']).lower())
        LIB.set_metadata_tag(anim, 'WarHorse.SpeedCmS', str(info['speed_cm_s']))
        LIB.save_loaded_asset(anim)
        if abs(anim.get_play_length() - info['frames'] / info['fps']) > 0.04:
            raise RuntimeError('Unexpected clip duration: ' + name)
        report['clips'][clip] = dict(info, asset=anim.get_path_name(), duration=anim.get_play_length())
    LIB.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    out = Path(unreal.Paths.project_saved_dir()) / 'WarHorseImportReport.json'
    out.write_text(json.dumps(report, indent=2))
    unreal.log('WARHORSE_IMPORT_OK ' + json.dumps(report))


main()
