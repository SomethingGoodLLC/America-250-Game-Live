"""Import the battle stories' narration WAVs into /Game/PA250/Audio/Story (re-import replaces in place).

  SourceAudio/story/story_<key>.wav -> /Game/PA250/Audio/Story/story_<key>

Render the WAVs first (ElevenLabs, only if a key is configured):
  services/negotiation/.venv/bin/python tools/audio/gen_story_narration.py
Run:
  UnrealEditor-Cmd PlayAmerica250.uproject -run=pythonscript -script=Scripts/import_story_audio.py -unattended -nullrhi

Only this folder is touched: no other sound in the project is re-imported or re-saved.
"""
import os

import unreal

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SourceAudio", "story")
DEST = "/Game/PA250/Audio/Story"

tasks = []
if os.path.isdir(SRC):
    for name in sorted(os.listdir(SRC)):
        if not name.lower().endswith(".wav") or not name.startswith("story_"):
            continue
        t = unreal.AssetImportTask()
        t.filename = os.path.join(SRC, name)
        t.destination_path = DEST
        t.destination_name = os.path.splitext(name)[0]
        t.replace_existing = True
        t.automated = True
        t.save = True
        tasks.append(t)
if tasks:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = sum(1 for t in tasks if t.imported_object_paths)
unreal.log("PA250 story narration: imported %d/%d into %s" % (ok, len(tasks), DEST))
