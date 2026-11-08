"""Import the extracted PTM cannon, never replacing existing assets."""
import os
from pathlib import Path

import unreal

source = Path(os.environ.get('PA250_CANNON_SOURCE', '/private/tmp/pa250-cannon/Cannon'))
destination = '/Game/ThirdParty/PTM_Cannon'
content = Path(unreal.Paths.project_content_dir()) / 'ThirdParty/PTM_Cannon'
# Protect the entire destination on reruns, including generated material names.
if content.exists() and any(content.rglob('*.uasset')):
    unreal.log('CANNON_SKIPPED destination already contains assets; verify existing content')
else:
    mesh = source / 'Mesh.FBX'
    if not mesh.is_file():
        raise RuntimeError('Cannon source missing: {}'.format(mesh))
    options = unreal.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', True)
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_as_skeletal', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property('import_materials', True)
    options.set_editor_property('import_textures', True)
    options.set_editor_property('import_animations', False)
    options.static_mesh_import_data.set_editor_property('build_nanite', False)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(mesh))
    task.set_editor_property('destination_path', destination)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', False)
    task.set_editor_property('replace_existing_settings', False)
    task.set_editor_property('save', True)
    task.set_editor_property('options', options)
    # UE 5.8 converts FbxImportUI to Interchange settings, including build_nanite.
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not task.get_editor_property('imported_object_paths'):
        raise RuntimeError('No cannon assets imported')
    texture_tasks = []
    for texture in sorted(source.glob('*.tga')):
        if unreal.EditorAssetLibrary.does_asset_exist(destination + '/' + texture.stem):
            continue
        item = unreal.AssetImportTask()
        item.filename = str(texture)
        item.destination_path = destination
        item.automated = True
        item.replace_existing = False
        item.save = True
        texture_tasks.append(item)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(texture_tasks)
    unreal.log('CANNON_IMPORTED {}'.format(list(task.imported_object_paths)))
