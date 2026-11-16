"""Import the animation clips dropped in the drop folder and register them (M4, 2026-11-15).

Drop a file named  <clip_name>@<rig>.fbx  in  unreal/PlayAmerica250/SourceArt/anim_drop  (git
ignores that folder; PA250_ANIM_DROP names another) and run, with the editor closed:

  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=<this file> -unattended -nullrhi
  PA250_ANIM_VERIFY_ONLY=1 ...   checks what is on disk in a fresh process and changes nothing

  <clip_name>  a name in the "clips" block of unreal/PA250Sim/data/anim_clips.json (for example
               reload_stand_ram_charge). A name that is not there is refused, unless
               PA250_ANIM_NEW_GROUP=<group> is set: then it is added to the registry in that group.
  <rig>        a name in "source_rigs" of the same file: soldier (the game's own skeleton, no
               retarget) or a rig with a mesh and a retargeter to the soldier on disk (uefn).
  .local.fbx   in place of .fbx: the clip must stay on this machine (MotionBricks evaluation
               output, for one). It goes to /Game/PA250/Anim/Local, which git ignores.

What it makes: /Game/PA250/Anim/Clips/A_<clip_name> (or .../Local/A_<clip_name>), an animation on
the soldier's skeleton, and the clip's "asset" and "source" in anim_clips.json. The battle then
plays the clip where it showed the placeholder (Battle/PA250AnimClips).

It owns what it makes: each asset carries the tag PA250.AnimDrop (the source file and its SHA-1).
A dropped file whose SHA-1 is the asset's is left alone; a changed file replaces the asset; an
asset at that path without the tag is somebody else's and is refused.
Ends with "PA250 anim drop DONE" or "PA250 anim drop VERIFIED", or raises.
"""
import hashlib
import json
import os
import re

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
DROP = os.environ.get('PA250_ANIM_DROP') or os.path.join(PROJECT, 'SourceArt', 'anim_drop')
REGISTRY = os.environ.get('PA250_ANIM_REGISTRY') or os.path.normpath(os.path.join(PROJECT, '..', 'PA250Sim', 'data', 'anim_clips.json'))
OUT = os.environ.get('PA250_ANIM_OUT') or '/Game/PA250/Anim/Clips'
OUT_LOCAL = os.environ.get('PA250_ANIM_OUT_LOCAL') or '/Game/PA250/Anim/Local'
STAGING = '/Game/PA250/Anim/_DropStaging'
VERIFY_ONLY = os.environ.get('PA250_ANIM_VERIFY_ONLY') == '1'
NEW_GROUP = os.environ.get('PA250_ANIM_NEW_GROUP')
SOLDIER = '/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER'
TAG = 'PA250.AnimDrop'
FILE_NAME = re.compile(r'^([a-z0-9_]+)@([a-z0-9_]+)(\.local)?\.fbx$')
GROUPS = ('loading_drill', 'gun_crew', 'rowing', 'rout', 'surrender', 'crawl', 'drummer', 'ensign', 'locomotion', 'traversal')

eal = unreal.EditorAssetLibrary
problems = []


def problem(text):
    problems.append(text)
    unreal.log_error('PA250 anim drop: ' + text)


def sha1_of(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def on_soldier(asset, soldier_skeleton):
    return isinstance(asset, unreal.AnimSequence) and asset.get_editor_property('skeleton') == soldier_skeleton


def import_fbx(path, folder, name, skeleton):
    """An animation-only legacy FBX import onto `skeleton`. Returns the AnimSequence or None."""
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
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.save = True
    task.replace_existing = False
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.load_asset('%s/%s' % (folder, name)) if eal.does_asset_exist('%s/%s' % (folder, name)) else None
    return asset if isinstance(asset, unreal.AnimSequence) else None


def retarget(staged_path, folder, rig, soldier):
    """The staged clip (on the rig's skeleton) onto the soldier, into `folder` under the same name."""
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = [eal.find_asset_data(staged_path)]
    inputs.source_mesh = unreal.load_asset(rig['mesh'])
    inputs.target_mesh = soldier
    inputs.ik_retarget_asset = unreal.load_asset(rig['retargeter'])
    inputs.use_source_path = False
    inputs.target_path = folder
    inputs.include_referenced_assets = False
    inputs.overwrite_existing_files = False
    made = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    for data in made:
        eal.save_asset(str(data.package_name), only_if_is_dirty=False)
    return len(made)


def import_one(file_name, doc, soldier, soldier_skeleton):
    """Returns 'imported', 'current' or None (a problem was logged)."""
    match = FILE_NAME.match(file_name)
    if not match:
        problem('%s: the name must be <clip_name>@<rig>.fbx (lower case, digits, underscores), or ...@<rig>.local.fbx' % file_name)
        return None
    clip, rig_name, local = match.group(1), match.group(2), bool(match.group(3))
    clips, rigs = doc['clips'], doc['source_rigs']
    if clip not in clips:
        if NEW_GROUP not in GROUPS:
            problem('%s: "%s" is not a clip name in anim_clips.json (add it there, or set PA250_ANIM_NEW_GROUP to one of: %s)' % (file_name, clip, ', '.join(GROUPS)))
            return None
        clips[clip] = {'group': NEW_GROUP, 'asset': None, 'placeholder': 'none', 'shows': 'nothing is acted (a clip dropped without a registry entry)',
                       'loop': False, 'wired': False}
    rig = rigs.get(rig_name)
    if not rig:
        problem('%s: "%s" is not a rig in anim_clips.json source_rigs (%s)' % (file_name, rig_name, ', '.join(sorted(rigs))))
        return None
    if rig_name != 'soldier':
        if not eal.does_asset_exist(rig['mesh']) or not rig.get('retargeter') or not eal.does_asset_exist(rig['retargeter']):
            problem('%s: rig "%s" needs its mesh (%s) and its retargeter (%s) on disk' % (file_name, rig_name, rig['mesh'], rig.get('retargeter')))
            return None
    path = os.path.join(DROP, file_name)
    digest = sha1_of(path)
    folder = OUT_LOCAL if local else OUT
    name = 'A_' + clip
    package = '%s/%s' % (folder, name)
    if eal.does_asset_exist(package):
        tag = eal.get_metadata_tag(unreal.load_asset(package), TAG)
        if not tag:
            problem('%s: %s exists and was not made by this importer; it is not replaced' % (file_name, package))
            return None
        if tag.endswith('|' + digest):
            if clips[clip].get('asset') != package:  # on disk but not in the registry: put it back
                clips[clip]['asset'] = package
                clips[clip]['source'] = {'file': file_name, 'rig': rig_name, 'sha1': digest, 'local': local}
            unreal.log('PA250 anim drop: UP TO DATE %s (%s)' % (clip, package))
            return 'current'
        if not eal.delete_asset(package):
            problem('%s: the older %s could not be removed' % (file_name, package))
            return None
    if rig_name == 'soldier':
        asset = import_fbx(path, folder, name, soldier_skeleton)
    else:
        staged_folder = '%s/%s' % (STAGING, rig_name)
        staged = '%s/%s' % (staged_folder, name)
        if eal.does_asset_exist(staged):
            eal.delete_asset(staged)
        source = import_fbx(path, staged_folder, name, unreal.load_asset(rig['mesh']).get_editor_property('skeleton'))
        if not source:
            problem('%s: the FBX did not import as an animation on the %s skeleton' % (file_name, rig_name))
            return None
        made = retarget(staged, folder, rig, soldier)
        eal.delete_asset(staged)
        asset = unreal.load_asset(package) if made and eal.does_asset_exist(package) else None
    if not on_soldier(asset, soldier_skeleton) or asset.get_play_length() <= 0.0:
        problem('%s: no animation on the soldier skeleton came out of it' % file_name)
        return None
    eal.set_metadata_tag(asset, TAG, '%s|%s' % (file_name, digest))
    eal.save_loaded_asset(asset, only_if_is_dirty=False)
    clips[clip]['asset'] = package
    clips[clip]['source'] = {'file': file_name, 'rig': rig_name, 'sha1': digest, 'local': local}
    unreal.log('PA250 anim drop: IMPORTED %s <- %s (%s), %.2f s, %s' % (clip, file_name, rig_name, asset.get_play_length(), package))
    return 'imported'


def verify(doc, soldier_skeleton):
    """Every registry clip with an asset: it is on disk, on the soldier, and is the file it says it came from."""
    verified = absent_local = 0
    for clip, entry in sorted(doc['clips'].items()):
        package = entry.get('asset')
        if not package:
            continue
        source = entry.get('source') or {}
        if not eal.does_asset_exist(package):
            if source.get('local'):
                absent_local += 1
                unreal.log('PA250 anim drop: LOCAL %s is not on this machine (%s); its placeholder shows' % (clip, package))
            else:
                problem('%s: %s is in the registry but not on disk' % (clip, package))
            continue
        asset = unreal.load_asset(package)
        tag = eal.get_metadata_tag(asset, TAG) or ''
        if not on_soldier(asset, soldier_skeleton):
            problem('%s: %s is not an animation on the soldier skeleton' % (clip, package))
        elif asset.get_play_length() <= 0.0:
            problem('%s: %s has no length' % (clip, package))
        elif not tag.endswith('|' + source.get('sha1', '?')):
            problem('%s: %s is not the file the registry names (%s)' % (clip, package, source.get('file')))
        else:
            verified += 1
            unreal.log('PA250 anim drop: VERIFIED %s, %.2f s, %d bone tracks, from %s (%s)' % (
                clip, asset.get_play_length(), len(unreal.AnimationLibrary.get_animation_track_names(asset)), source.get('file'), source.get('rig')))
    return verified, absent_local


def main():
    if not os.path.exists(REGISTRY):
        problem('no registry at %s' % REGISTRY)
        return
    doc = json.load(open(REGISTRY, encoding='utf-8'))
    if 'clips' not in doc or 'source_rigs' not in doc:
        problem('%s has no "clips" or no "source_rigs" block' % REGISTRY)
        return
    soldier = unreal.load_asset(SOLDIER)
    soldier_skeleton = soldier.get_editor_property('skeleton') if soldier else None
    if not soldier_skeleton:
        problem('the soldier mesh %s does not load' % SOLDIER)
        return
    imported = current = 0
    if not VERIFY_ONLY:
        # The legacy FBX importer, for this process only (project settings untouched): it honours FbxImportUI.
        unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
        files = sorted(f for f in os.listdir(DROP) if f.lower().endswith('.fbx')) if os.path.isdir(DROP) else []
        if not os.path.isdir(DROP):
            unreal.log('PA250 anim drop: no drop folder at %s (nothing to import)' % DROP)
        before = json.dumps(doc, sort_keys=True)
        for file_name in files:
            result = import_one(file_name, doc, soldier, soldier_skeleton)
            imported += result == 'imported'
            current += result == 'current'
        if eal.does_directory_exist(STAGING):
            eal.delete_directory(STAGING)  # the retarget's staging folder: nothing is kept there
        if json.dumps(doc, sort_keys=True) != before:
            with open(REGISTRY, 'w', encoding='utf-8', newline='\n') as f:
                json.dump(doc, f, indent=2)
                f.write('\n')
    verified, absent_local = verify(doc, soldier_skeleton)
    unreal.log('PA250 anim drop %s: %d imported, %d up to date, %d verified, %d local clips not on this machine, %d problems' % (
        'VERIFIED' if VERIFY_ONLY else 'DONE', imported, current, verified, absent_local, len(problems)))


main()
if problems:
    raise RuntimeError('PA250 anim drop: %d problems' % len(problems))
