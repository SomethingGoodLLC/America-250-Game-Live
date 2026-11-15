"""Stage the Game Animation Sample packages the animation pipeline retargets FROM.

Plain Python (no Unreal): copies an allow-list of .uasset files from the sample project's Content
into this project's Content at the SAME package paths, so their references to one another still
resolve. The staging folders (Content/Characters/UEFN_Mannequin, Content/Characters/UE4_Mannequin)
are git-ignored: only what Scripts/build_locomotion.py makes from them is kept.

  python tools/ue/stage_gas.py --gas "<GameAnimationSample>/Content" --manifest <gas_manifest.json>

The manifest comes from tools/ue/gas_manifest.py, run once inside the sample project:
  PA250_OUT=<gas_manifest.json> UnrealEditor-Cmd <GameAnimationSample.uproject> -run=pythonscript
      -script=tools/ue/gas_manifest.py -unattended -nullrhi
Nothing in the sample project is changed. Existing staged files are left alone.
"""
import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / 'unreal' / 'PlayAmerica250' / 'Content'
DB = '/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/'

# gait -> kind -> sample database (the Sparse set: fewer clips than Dense, every start, stop and pivot).
LOCOMOTION = {
    'idle': {'idles': DB + 'Dense/PSD_Dense_Stand_Idles', 'turn_in_place': DB + 'Dense/PSD_Dense_Stand_TurnInPlace'},
    'walk': {k: DB + 'Sparse/PSD_Sparse_Stand_Walk_' + v for k, v in
             {'loops': 'Loops', 'pivots': 'Pivots', 'starts': 'Starts', 'stops': 'Stops'}.items()},
    'run': {k: DB + 'Sparse/PSD_Sparse_Stand_Run_' + v for k, v in
            {'loops': 'Loops', 'pivots': 'Pivots', 'starts': 'Starts', 'stops': 'Stops'}.items()},
    'sprint': {k: DB + 'Sparse/PSD_Sparse_Stand_Sprint_' + v for k, v in
               {'loops': 'Loops', 'pivots': 'Pivots', 'starts': 'Starts', 'stops': 'Stops'}.items()},
}
RIGS = [
    '/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin',
    '/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin',
    '/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin',
    '/Game/Characters/UE4_Mannequin/Meshes/SK_UE4_Mannequin',
    '/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin',
    '/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget',
    '/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin',
]
# Traversal (M2): the Neutral hurdle, vault and mantle montages and the sequences under them.
TRAVERSAL_FOLDERS = ('/Traversal/Hurdle/', '/Traversal/Vault/', '/Traversal/Mantle/')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gas', required=True, help="the sample project's Content folder")
    ap.add_argument('--manifest', required=True)
    ap.add_argument('--traversal', action='store_true', help='also stage the Neutral traversal montages')
    ap.add_argument('--out', default=str(ROOT / 'unreal' / 'PlayAmerica250' / 'Saved' / 'PA250' / 'gas_staged.json'))
    args = ap.parse_args()
    gas = Path(args.gas)
    manifest = json.loads(Path(args.manifest).read_text(encoding='utf-8'))
    packages = list(RIGS)
    plan = {'locomotion': {}, 'traversal': []}
    for gait, kinds in LOCOMOTION.items():
        plan['locomotion'][gait] = {}
        for kind, db in kinds.items():
            entry = manifest['databases'].get(db)
            if entry is None:
                raise SystemExit('the manifest has no database %s' % db)
            schemas = [p for p, k in entry['other'] if k == 'PoseSearchSchema']
            packages += [db] + schemas + entry['sequences']
            plan['locomotion'][gait][kind] = {'database': db, 'schemas': schemas, 'sequences': entry['sequences']}
    if args.traversal:
        for package, info in manifest['traversal'].items():
            name = package.rsplit('/', 1)[-1]
            if not any(f in package for f in TRAVERSAL_FOLDERS) or '/Catch/' in package:
                continue
            if info['class'] == 'AnimMontage' and name.startswith('AM_M_Neutral_'):
                seqs = [p for p, k in info['deps'] if k == 'AnimSequence']
                packages += [package] + seqs
                plan['traversal'].append({'montage': package, 'sequences': seqs})
    copied = skipped = 0
    size = 0
    for package in sorted(set(packages)):
        rel = package[len('/Game/'):] + '.uasset'
        src, dst = gas / rel, CONTENT / rel
        if not src.is_file():
            raise SystemExit('missing in the sample: %s' % src)
        size += src.stat().st_size
        if dst.exists():
            skipped += 1
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied += 1
    plan['packages'] = sorted(set(packages))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(plan, indent=1), encoding='utf-8')
    print('staged %d packages (%d copied, %d already there), %.1f MB -> %s' % (
        len(plan['packages']), copied, skipped, size / 1e6, out))


if __name__ == '__main__':
    main()
