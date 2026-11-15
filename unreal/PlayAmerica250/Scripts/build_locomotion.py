"""Retarget the staged Game Animation Sample locomotion to the period soldier and build the
motion-matching databases (M1 of the movement task, 2026-11-15).

Before this: tools/ue/gas_manifest.py (once, in the sample project) and tools/ue/stage_gas.py.
Run (editor closed):
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=Scripts/build_locomotion.py -unattended -nullrhi
  PA250_LOCO_VERIFY_ONLY=1 ...   checks what is on disk in a fresh process and changes nothing

What it makes, all under /Game/PA250/Anim (and nothing anywhere else):
  Rig/IK_Soldier, Rig/RTG_UEFN_to_Soldier   the soldier's IK rig (the sample's UE4-mannequin rig with
                                            the soldier's mesh) and the sample's retargeter aimed at it
  Loco/<clip>                               every clip of the staged databases, on the soldier's skeleton
  MM/PSS_*, MM/PSD_*                        the schemas and databases, on the soldier's skeleton
and the "locomotion" block of unreal/PA250Sim/data/anim_clips.json (asset paths the battle loads).
It never overwrites: an asset that exists is kept and reported. To rebuild, delete Content/PA250/Anim.
The pose-search database is filled by UPA250AnimBuildLibrary (C++), which Python cannot do.
"""
import json
import os

import unreal

OUT = '/Game/PA250/Anim'
SOLDIER = '/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER'
UEFN_MESH = '/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin'
IK_UE4 = '/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget'
RTG_UE4 = '/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin'
VERIFY_ONLY = os.environ.get('PA250_LOCO_VERIFY_ONLY') == '1'
SAVED = os.path.join(unreal.Paths.project_saved_dir(), 'PA250')
REGISTRY = os.path.normpath(os.path.join(unreal.Paths.project_dir(), '..', 'PA250Sim', 'data', 'anim_clips.json'))

eal = unreal.EditorAssetLibrary
lib = unreal.PA250AnimBuildLibrary
problems = []


def problem(text):
    problems.append(text)
    unreal.log_error('PA250 locomotion: ' + text)


def name_of(package):
    return package.rsplit('/', 1)[-1]


def ensure_rig(soldier, uefn):
    ik_path, rtg_path = OUT + '/Rig/IK_Soldier', OUT + '/Rig/RTG_UEFN_to_Soldier'
    ik = unreal.load_asset(ik_path) if eal.does_asset_exist(ik_path) else None
    if not ik and not VERIFY_ONLY:
        ik = eal.duplicate_asset(IK_UE4, ik_path)
        c = unreal.IKRigController.get_controller(ik)
        if not c.is_skeletal_mesh_compatible(soldier) or not c.set_skeletal_mesh(soldier):
            problem('the UE4 mannequin rig does not fit the soldier mesh')
        eal.save_loaded_asset(ik)
    rtg = unreal.load_asset(rtg_path) if eal.does_asset_exist(rtg_path) else None
    if not rtg and ik and not VERIFY_ONLY:
        rtg = eal.duplicate_asset(RTG_UE4, rtg_path)
        rc = unreal.IKRetargeterController.get_controller(rtg)
        rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik)
        rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, ik)
        rc.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, uefn)
        rc.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, soldier)
        rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
        eal.save_loaded_asset(rtg)
    if not ik or not rtg:
        problem('no soldier rig or retargeter')
    return ik, rtg


def retarget(sources, folder, rtg, soldier, uefn):
    """Clips of `sources` (package paths) that are not yet in `folder`; returns how many were made."""
    todo = [p for p in sources if not eal.does_asset_exist('%s/%s' % (folder, name_of(p)))]
    if not todo or VERIFY_ONLY:
        return 0
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = [eal.find_asset_data(p) for p in todo]
    inputs.source_mesh = uefn
    inputs.target_mesh = soldier
    inputs.ik_retarget_asset = rtg
    inputs.use_source_path = False
    inputs.target_path = folder
    inputs.include_referenced_assets = False
    inputs.overwrite_existing_files = False
    made = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    for data in made:
        eal.save_asset(str(data.package_name), only_if_is_dirty=False)
    return len(made)


def main():
    plan_path = os.path.join(SAVED, 'gas_staged.json')
    if not os.path.exists(plan_path):
        problem('no %s: run tools/ue/stage_gas.py first' % plan_path)
        return
    plan = json.load(open(plan_path, encoding='utf-8'))
    soldier = unreal.load_asset(SOLDIER)
    uefn = unreal.load_asset(UEFN_MESH)
    if not soldier or not uefn:
        problem('the soldier mesh or the staged UEFN mesh does not load')
        return
    skeleton = soldier.get_editor_property('skeleton')
    ik, rtg = ensure_rig(soldier, uefn)
    if not ik or not rtg:
        return

    sequences = sorted({s for kinds in plan['locomotion'].values() for k in kinds.values() for s in k['sequences']})
    made = retarget(sequences, OUT + '/Loco', rtg, soldier, uefn)
    missing = [name_of(s) for s in sequences if not eal.does_asset_exist('%s/Loco/%s' % (OUT, name_of(s)))]
    if missing:
        problem('%d clips were not retargeted: %s' % (len(missing), ', '.join(missing[:6])))

    # Schemas: one soldier copy of each schema the staged databases use.
    schemas = {}
    for kinds in plan['locomotion'].values():
        for k in kinds.values():
            for src in k['schemas']:
                dst = '%s/MM/%s' % (OUT, name_of(src))
                if src in schemas:
                    continue
                asset = unreal.load_asset(dst) if eal.does_asset_exist(dst) else None
                if not asset and not VERIFY_ONLY:
                    asset = eal.duplicate_asset(src, dst)
                    if not lib.set_schema_skeleton(asset, skeleton):
                        problem('schema %s could not be pointed at the soldier' % dst)
                    eal.save_loaded_asset(asset)
                if not asset:
                    problem('no schema %s' % dst)
                schemas[src] = asset

    registry = {'retargeted_from': 'Game Animation Sample (Epic Games), UEFN mannequin, through RTG_UEFN_to_Soldier',
                'rig': OUT + '/Rig/IK_Soldier', 'retargeter': OUT + '/Rig/RTG_UEFN_to_Soldier',
                'clip_folder': OUT + '/Loco', 'databases': {}}
    clips_in_databases = 0
    for gait, kinds in plan['locomotion'].items():
        registry['databases'][gait] = {}
        for kind, k in kinds.items():
            dst = '%s/MM/%s' % (OUT, name_of(k['database']).replace('PSD_Sparse_Stand_', 'PSD_Soldier_').replace('PSD_Dense_Stand_', 'PSD_Soldier_'))
            db = unreal.load_asset(dst) if eal.does_asset_exist(dst) else None
            if not db and not VERIFY_ONLY:
                source = unreal.load_asset(k['database'])
                schema = schemas.get(k['schemas'][0]) if k['schemas'] else None
                db, dropped = lib.build_database(source, schema, OUT + '/Loco', dst)
                if dropped:
                    problem('%s: clips dropped: %s' % (dst, ', '.join(dropped)))
                if db:
                    eal.save_loaded_asset(db)
            if not db:
                problem('no database %s' % dst)
                continue
            clips = [c for c in lib.database_clips(db)]
            bad = [c for c in clips if not c.startswith(OUT + '/Loco/')]
            if bad or len(clips) != len(k['sequences']):
                problem('%s holds %d clips (%d expected), %d not in %s/Loco' % (dst, len(clips), len(k['sequences']), len(bad), OUT))
            schema_skeleton = db.get_editor_property('schema')
            clips_in_databases += len(clips)
            registry['databases'][gait][kind] = dst

    # Every retargeted clip sits on the soldier's skeleton.
    wrong = 0
    for s in sequences:
        data = eal.find_asset_data('%s/Loco/%s' % (OUT, name_of(s)))
        tag = str(data.get_tag_value('Skeleton') or '')
        if 'SK_AMERICAN_SOLDIER_Skeleton' not in tag:
            wrong += 1
    if wrong:
        problem('%d retargeted clips are not on the soldier skeleton' % wrong)

    if not VERIFY_ONLY and not problems:
        doc = json.load(open(REGISTRY, encoding='utf-8')) if os.path.exists(REGISTRY) else {}
        doc['locomotion'] = registry
        with open(REGISTRY, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(doc, f, indent=2)
            f.write('\n')
    report = {'verify_only': VERIFY_ONLY, 'retargeted_now': made, 'clips': len(sequences), 'clips_in_databases': clips_in_databases,
              'databases': sum(len(v) for v in registry['databases'].values()), 'schemas': len(schemas), 'problems': problems}
    os.makedirs(SAVED, exist_ok=True)
    with open(os.path.join(SAVED, 'locomotion_build.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    unreal.log('PA250 locomotion %s: %d clips (%d retargeted now), %d databases holding %d clips, %d schemas, %d problems' % (
        'VERIFIED' if VERIFY_ONLY else 'BUILT', len(sequences), made, report['databases'], clips_in_databases, len(schemas), len(problems)))


main()
if problems:
    raise RuntimeError('PA250 locomotion: %d problems' % len(problems))
