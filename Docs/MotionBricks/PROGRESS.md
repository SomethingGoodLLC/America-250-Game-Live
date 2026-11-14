# MotionBricks evaluation progress

## 2026-11-14 — baseline and environment setup (in progress)

- User request: isolated soldier traversal prototype; no startup-map or combat changes; local milestone commits; no pushes or machine restarts.
- User explicitly corrected the engine choice to **installed Unreal 5.8.4** after inspection found no 5.6.8. Project already has EngineAssociation 5.8. Engine changelist 58902052.
- Original source: `C:/Users/user/Downloads/America-250-Game-Live-master`. This is an archive extraction, **not a Git checkout**. No origin or original commit is available. Original files remain untouched.
- Working copy: `C:/Users/user/Documents/ChatGPT/Play America 250/America-250-Game-Live`.
- MotionBricks sibling: `../motionbricks`. Tools/installers: `../Tooling`.
- Read README.md, CLAUDE.md, GIT_GUIDELINES.md, .gitattributes and project descriptor. No AGENTS.md found in project or inspected ancestors. User-requested MotionBricks docs/commits override the older documentation-only-at-root/no-commits conventions.
- Archive has 1,878 files (403,548,133 bytes), 1,072 uassets, 8 maps, 9 FBX files. Many assets are LFS pointer text, so the archive is not a complete playable baseline. Quinn mesh is an actual binary. Referenced `/Game/american_soldier` content is absent. Licensed soldier recovery remains unresolved; mannequin is the candidate placeholder.
- GPU: NVIDIA L4, 23,034 MiB reported framebuffer, driver 596.86, CUDA driver capability 13.2. Initial GPU usage 612 MiB. Disk initially about 108 GB free. RAM probe through CIM was denied; not yet measured.
- Git 2.56.0, Git LFS 3.8.1. No Python/uv or Visual Studio toolchain on PATH at inspection. Downloaded portable uv 0.13.8 and official Visual Studio Build Tools installer.
- Unreal Windows_SDK.json prefers MSVC 14.44.35211+ (VS2022) or 14.50.35723+ (VS2026), with Windows SDK 10.0.22621.0 preferred. Installing VS2022 C++ workload and SDK without restart.
- MotionBricks clone succeeded with elevated network access; `git lfs pull` succeeded. Pinned `uv sync --locked` is running. Headless generation and Scene Preview requests are queued behind environment setup.
- Baseline UBT build and unattended editor launch requested. No build/gameplay success claimed.

## Exact commands executed so far (PowerShell)

```powershell
Get-Content -LiteralPath 'C:\Users\user\.codex\attachments\1a501ea3-f83a-4ab1-a2c7-150f9fcb9f49\Pasted text.txt'
git -C 'C:\Users\user\Downloads\America-250-Game-Live-master' status --short --branch
git -C 'C:\Users\user\Downloads\America-250-Game-Live-master' remote -v
git --version
git lfs version
nvidia-smi
Get-CimInstance Win32_OperatingSystem
Get-PSDrive -PSProvider FileSystem
Copy-Item -LiteralPath 'C:\Users\user\Downloads\America-250-Game-Live-master' -Destination 'C:\Users\user\Documents\ChatGPT\Play America 250\America-250-Game-Live' -Recurse
# From workspace root (first sandbox attempt failed network; elevated retry succeeded):
git clone https://github.com/NVlabs/motionbricks.git motionbricks
# From working game copy:
git init -b feature/motionbricks-traversal
# From motionbricks:
git lfs pull
& '..\Tooling\uv\uv.exe' sync --locked
& '..\Tooling\uv\uv.exe' run --locked python docs/examples/generate_once.py
& '..\Tooling\uv\uv.exe' run --locked python -m app.scene_preview --scene assets/scenes/soma_gym.scene.json
# From workspace root:
Invoke-WebRequest -Uri 'https://github.com/astral-sh/uv/releases/latest/download/uv-x86_64-pc-windows-msvc.zip' -OutFile '.\Tooling\uv.zip'
Expand-Archive -LiteralPath '.\Tooling\uv.zip' -DestinationPath '.\Tooling\uv' -Force
Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile '.\Tooling\vs_BuildTools.exe'
$installer = Start-Process -FilePath 'C:\Users\user\Documents\ChatGPT\Play America 250\Tooling\vs_BuildTools.exe' -ArgumentList '--quiet','--wait','--norestart','--nocache','--installPath','C:\BuildTools','--add','Microsoft.VisualStudio.Workload.VCTools','--add','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','--add','Microsoft.VisualStudio.Component.Windows11SDK.22621','--includeRecommended' -WindowStyle Hidden -PassThru
$installer.WaitForExit()
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' PlayAmerica250Editor Win64 Development '-Project=C:\Users\user\Documents\ChatGPT\Play America 250\America-250-Game-Live\unreal\PlayAmerica250\PlayAmerica250.uproject' -WaitMutex -NoHotReloadFromIDE
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'C:\Users\user\Documents\ChatGPT\Play America 250\America-250-Game-Live\unreal\PlayAmerica250\PlayAmerica250.uproject' -unattended -nosplash -log
```

## Resume priorities

1. Inspect installer/sync/build outcomes; record MotionBricks revision and licenses.
2. Measure and validate generated skeletal output; run SOMA preview.
3. Export a generated transition, then isolated Unreal import/test implementation.
4. Record actual tests and limitations in SETUP.md, ARCHITECTURE.md, TEST_RESULTS.md.

The new Git repository deliberately commits only task-owned additions. Imported archive contents are initially untracked, not represented as an invented upstream commit. Apply task commits onto the original repository on Mac; this repository is not a replacement for its history.
