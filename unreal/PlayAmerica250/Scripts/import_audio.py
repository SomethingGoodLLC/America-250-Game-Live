"""Import SourceAudio WAVs into /Game/PA250/Audio (re-import replaces in place).

  sfx/*.wav              -> /Game/PA250/Audio/SFX/<name>
  voice/<side>/*.wav     -> /Game/PA250/Audio/Voice/<side>/VO_<key>
  music/*.wav            -> /Game/PA250/Audio/Music/<name> (looping)

Generate the WAVs first:
  services/negotiation/.venv/bin/python tools/audio/synth_sfx.py        (original synthesized SFX)
  services/negotiation/.venv/bin/python tools/audio/gen_voice_lines.py  (ElevenLabs shouted lines)
  services/negotiation/.venv/bin/python tools/audio/gen_music.py       (ElevenLabs music + battle stinger)
Run:
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=Scripts/import_audio.py -unattended -nullrhi
"""
import os

import unreal

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SourceAudio")


def import_dir(src_dir, dest_path, prefix=""):
    tasks = []
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith(".wav"):
            continue
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src_dir, name)
        t.destination_path = dest_path
        t.destination_name = prefix + os.path.splitext(name)[0]
        t.replace_existing = True
        t.automated = True
        t.save = True
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    ok = sum(1 for t in tasks if t.imported_object_paths)
    unreal.log("PA250: imported %d/%d into %s" % (ok, len(tasks), dest_path))


import_dir(os.path.join(ROOT, "sfx"), "/Game/PA250/Audio/SFX")
music = os.path.join(ROOT, "music")
if os.path.isdir(music):
    import_dir(music, "/Game/PA250/Audio/Music")
    for name in sorted(os.listdir(music)):
        if name.lower().endswith(".wav"):
            asset = unreal.load_asset("/Game/PA250/Audio/Music/" + os.path.splitext(name)[0])
            if asset:
                asset.set_editor_property("looping", True)
                unreal.EditorAssetLibrary.save_loaded_asset(asset)
voice = os.path.join(ROOT, "voice")
for side in sorted(os.listdir(voice)):
    if os.path.isdir(os.path.join(voice, side)):
        import_dir(os.path.join(voice, side), "/Game/PA250/Audio/Voice/" + side, "VO_")
