"""Read-only, run inside the Game Animation Sample project (-run=pythonscript), for tools/ue/stage_gas.py: what do the
locomotion databases, the traversal montages and the retarget rigs depend on? Writes one JSON file
(path in PA250_OUT) and changes nothing in the project."""
import json
import os

import unreal

OUT = os.environ['PA250_OUT']
MM = '/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData'
TRAV = '/Game/Characters/UEFN_Mannequin/Animations/Traversal'

reg = unreal.AssetRegistryHelpers.get_asset_registry()
reg.search_all_assets(True)


def assets_under(path, class_name=None):
    out = []
    for d in reg.get_assets_by_path(path, recursive=True):
        kind = str(d.asset_class_path.asset_name)
        if class_name and kind != class_name:
            continue
        out.append((str(d.package_name), kind))
    return sorted(out)


def deps(package, hard_only=False):
    opt = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=not hard_only, include_hard_package_references=True,
        include_searchable_names=False, include_soft_management_references=False,
        include_hard_management_references=False)
    got = reg.get_dependencies(package, opt) or []
    return sorted(str(p) for p in got if str(p).startswith('/Game/'))


def size_of(package):
    rel = package[len('/Game/'):]
    for ext in ('.uasset', '.umap'):
        p = os.path.join(unreal.Paths.project_content_dir(), rel + ext)
        if os.path.exists(p):
            return os.path.getsize(p)
    return 0


def klass(package):
    data = reg.get_assets_by_package_name(package)
    return str(data[0].asset_class_path.asset_name) if data else '?'


report = {'engine': unreal.SystemLibrary.get_engine_version(), 'databases': {}, 'schemas': {}, 'traversal': {},
          'rigs': {}, 'sequence_info': {}}

for package, kind in assets_under(MM):
    if kind == 'PoseSearchDatabase':
        d = deps(package)
        report['databases'][package] = {
            'sequences': [p for p in d if klass(p) in ('AnimSequence', 'BlendSpace', 'AnimComposite', 'AnimMontage')],
            'other': [(p, klass(p)) for p in d if klass(p) not in ('AnimSequence', 'BlendSpace', 'AnimComposite', 'AnimMontage')],
        }
    elif kind in ('PoseSearchSchema', 'PoseSearchNormalizationSet'):
        report['schemas'][package] = {'class': kind, 'deps': [(p, klass(p)) for p in deps(package)]}

for package, kind in assets_under(TRAV):
    report['traversal'][package] = {'class': kind, 'size': size_of(package),
                                    'deps': [(p, klass(p)) for p in deps(package)]}

for path in ('/Game/Characters/UEFN_Mannequin/Rigs', '/Game/Characters/UEFN_Mannequin/Meshes',
             '/Game/Characters/UE4_Mannequin/Rigs', '/Game/Characters/UE4_Mannequin/Meshes',
             '/Game/Characters/UE5_Mannequins/Rigs', '/Game/Characters/UE5_Mannequins/Meshes'):
    for package, kind in assets_under(path):
        report['rigs'][package] = {'class': kind, 'size': size_of(package),
                                   'hard_deps': [(p, klass(p)) for p in deps(package, hard_only=True)]}

# Per sequence: size, skeleton, hard dependencies that are not the skeleton (notify classes, curves...)
seen = set()
for db in report['databases'].values():
    seen.update(db['sequences'])
for package, info in report['traversal'].items():
    if info['class'] in ('AnimSequence', 'AnimMontage'):
        seen.add(package)
for package in sorted(seen):
    report['sequence_info'][package] = {'class': klass(package), 'size': size_of(package),
                                        'hard_deps': [(p, klass(p)) for p in deps(package, hard_only=True)]}

# Reference skeletons of the rigs the sample retargets between: names and reference pose.
report['skeletons'] = {}
for path in ('/Game/Characters/UEFN_Mannequin/Meshes', '/Game/Characters/UE4_Mannequin/Meshes'):
    for d in reg.get_assets_by_path(path, recursive=True):
        if str(d.asset_class_path.asset_name) != 'Skeleton':
            continue
        try:
            pose = unreal.AnimPoseExtensions.get_reference_pose(d.get_asset())
            bones = []
            for n in unreal.AnimPoseExtensions.get_bone_names(pose):
                t = unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD)
                bones.append({'name': str(n),
                              'pos': [round(t.translation.x, 2), round(t.translation.y, 2), round(t.translation.z, 2)]})
            report['skeletons'][str(d.package_name)] = bones
        except Exception as exc:
            report['skeletons'][str(d.package_name)] = 'failed: %s' % exc

with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(report, f, indent=1)
unreal.log('PA250 GAS deps: %d databases, %d sequences, %d traversal assets -> %s' % (
    len(report['databases']), len(report['sequence_info']), len(report['traversal']), OUT))
