"""Read-only Fab asset loading verification; run with -run=pythonscript."""
import collections
import json
import os
from pathlib import Path

import unreal

ROOTS = os.environ.get('PA250_ASSET_ROOTS', '/Game/HS_WeaponPack_01,/Game/american_soldier,/Game/KiteDemo,/Game/ThirdParty/PTM_Cannon').split(',')
LOAD_CLASSES = {'StaticMesh', 'SkeletalMesh', 'Skeleton', 'AnimSequence', 'Material', 'MaterialInstanceConstant'}
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
report = {'engine': unreal.SystemLibrary.get_engine_version(), 'packs': {}, 'failed': []}
for root in ROOTS:
    assets = registry.get_assets_by_path(root, recursive=True)
    counts = collections.Counter()
    skeletons = []
    meshes = []
    static_meshes = []
    for data in assets:
        kind = str(data.asset_class_path.asset_name)
        counts[kind] += 1
        if kind not in LOAD_CLASSES:
            continue
        path = str(data.package_name) + '.' + str(data.asset_name)
        try:
            obj = data.get_asset()
            if obj is None:
                raise RuntimeError('get_asset returned None')
            if kind == 'StaticMesh' and root == '/Game/ThirdParty/PTM_Cannon':
                nanite = bool(obj.get_editor_property('nanite_settings').enabled)
                static_meshes.append({'mesh': path, 'nanite_enabled': nanite})
                if nanite:
                    raise RuntimeError('Cannon Nanite must be disabled')
            if kind == 'Skeleton':
                skeletons.append(path)
            if kind == 'SkeletalMesh':
                skeleton = obj.get_editor_property('skeleton')
                meshes.append({'mesh': path, 'skeleton': skeleton.get_path_name() if skeleton else None})
        except Exception as exc:
            report['failed'].append({'asset': path, 'error': str(exc)})
            unreal.log_error('FAILED {}: {}'.format(path, exc))
    report['packs'][root] = {'counts': dict(counts), 'skeletons': skeletons, 'skeletal_meshes': meshes, 'static_meshes': static_meshes}
    unreal.log('FAB_COUNTS {} {}'.format(root, json.dumps(dict(counts), sort_keys=True)))
    if not assets:
        report['failed'].append({'asset': root, 'error': 'No assets registered'})
        unreal.log_error('FAILED {}: No assets registered'.format(root))
output = Path(unreal.Paths.project_saved_dir()) / 'AssetImportReport.json'
output.write_text(json.dumps(report, indent=2))
unreal.log('FAB_VERIFY failures={} report={}'.format(len(report['failed']), output))
if report['failed']:
    raise RuntimeError('Fab asset verification failed')
