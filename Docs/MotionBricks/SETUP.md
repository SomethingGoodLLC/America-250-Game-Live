# Run the MotionBricks evaluation

The Windows evaluation is built with **Unreal 5.8.4** (CL 58902052). It uses Quinn from the supplied game archive. Open the separate test host or launch the test map directly.

From the workspace folder:

```powershell
& '.\America-250-Game-Live\tools\motionbricks\launch_test.ps1'
```

In the demo, **WASD** moves and the **left/right arrows** turn. Walk into the outlined approach region and press **E** while facing the wall. **R** returns to the marked approach, **B** toggles the landing obstruction, and **V** toggles the guides. When testing the rejection cases, read the message on screen or look for `MotionBricks:` in the Unreal log.

To spot-check the course, press **E** from the spawn, then use **R** and turn 90° before retrying. Both attempts should be rejected. Reset, toggle the blocker with **B**, and confirm **E** is rejected again. Remove the blocker, press **E** twice, and hold **W** after landing to verify walking. Repeat with `-FPS 30` and `-FPS 60` if you want to compare motion at the two caps.

## Rebuild

Prerequisites on Windows: Git LFS, uv, Unreal 5.8.4, MSVC Build Tools 2022 17.14, Windows SDK 10.0.22621, .NET Framework 4.8 SDK and targeting pack, and the Visual C++ x64 runtime required by UE 5.8.4. The editor requests runtime 14.50.35719 or later. GPU generation requires an NVIDIA CUDA driver. On this machine the L4 uses driver 596.86. [Microsoft runtime downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/).

Place the game tree and NVIDIA's MotionBricks checkout in sibling directories. Install or locate uv first. The VM has it at `Tooling/uv/uv.exe`.

```powershell
$workspace = (Get-Location).Path
$tools = Join-Path $workspace 'America-250-Game-Live/tools/motionbricks'
$uv = Join-Path $workspace 'Tooling/uv/uv.exe'
$ue = 'C:/Program Files/Epic Games/UE_5.8'
$mb = Join-Path $workspace 'motionbricks'

# Skip clone if the sibling checkout is already present.
git clone https://github.com/NVlabs/motionbricks.git $mb
Push-Location $mb
git switch --detach 54fc563aeaaf819a753d72cc9fdc6713989cdd43
git lfs pull
& $uv sync --locked
& $uv run --locked python docs/examples/generate_once.py
# Optional independent GUI preview.
# & $uv run --locked python -m app.scene_preview --scene assets/scenes/soma_gym.scene.json
Pop-Location

$eval = Join-Path $workspace 'MotionBricksEvaluation'
$export = Join-Path $eval 'GeneratedVault'
& "$mb/.venv/Scripts/python.exe" "$tools/generate_traversal.py" --motionbricks $mb --output $export
if ($LASTEXITCODE) { throw 'MotionBricks exporter failed' }

# Use a fresh directory so the importer never replaces existing assets.
$project = & "$tools/prepare_test_host.ps1" -Destination "$eval/FreshTestHost"
& "$ue/Engine/Build/BatchFiles/Build.bat" MBTestHostEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE -NoUBTMakefiles
if ($LASTEXITCODE) { throw 'Unreal build failed' }

$env:PA250_MB_MOTION = Join-Path $export 'generated_traversal.json'
$script = "$tools/build_test_map.py".Replace('\','/')
& "$ue/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$script" -unattended -nullrhi -nosplash
# Look for MB_MAP_CREATED and the three packages in Content/Developers/MotionBricksEvaluation.
& "$tools/launch_test.ps1" -Project $project -Verify -FPS 30
& "$tools/launch_test.ps1" -Project $project -Verify -FPS 60
& "$tools/launch_test.ps1" -Project $project
```

The first TensorRT launch builds a GPU-specific plan. Keep the evaluation JSON, generated Unreal animation and map, model downloads, TensorRT plans, and capture files local. The built test host is disposable. To bring the code and docs back to Mac, apply the local task commits or the exported Git patches to the original checkout. The plugin defaults to disabled in the main game. Enable it explicitly on any editor project used to evaluate it. Offline Unreal playback uses engine APIs only. Generation still uses the Windows NVIDIA workspace, and macOS Metal playback requires a fresh test.
