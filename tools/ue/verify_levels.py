"""Load existing levels without saving; report and compare log errors externally."""
import json
import re
import shlex
from pathlib import Path

import unreal

results = {}
subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
for name in ['L_Campaign', 'L_Lexington', 'L_DiplomacyRoom']:
    path = '/Game/PA250/Maps/' + name
    loaded = subsystem.load_level(path)
    results[path] = bool(loaded)
    if not loaded:
        unreal.log_error('FAILED level ' + path)
    else:
        unreal.log('LEVEL_OK ' + path)
def error_lines(text):
    return {re.sub(r"^.*?(Log[A-Za-z0-9_]+:)", r"\1", line).strip()
            for line in text.splitlines() if 'Error:' in line or 'Failed to load' in line}

saved = Path(unreal.Paths.project_saved_dir())
baseline = set()
for log in (saved / 'FabImport/BaselineLogs').glob('*'):
    if log.is_file():
        baseline.update(error_lines(log.read_text(errors='replace')))
log_path = next((arg.split('=', 1)[1] for arg in shlex.split(unreal.SystemLibrary.get_command_line())
                 if arg.lower().startswith('-abslog=')), None)
if not log_path:
    raise RuntimeError('verify_levels.py requires -abslog=<absolute log path>')
found = error_lines(Path(log_path).read_text(errors='replace'))
new = sorted(found - baseline)
report = {'levels': results, 'error_lines': sorted(found), 'new_error_lines': new}
(saved / 'LevelImportReport.json').write_text(json.dumps(report, indent=2))
if new:
    raise RuntimeError('New level load errors: ' + repr(new))
if not all(results.values()):
    raise RuntimeError('Level load regression')
unreal.log('LEVEL_VERIFY failures=0')
