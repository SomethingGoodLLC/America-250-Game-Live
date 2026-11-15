#!/usr/bin/env bash
# Sourced by the scripts in this folder: where Unreal and Python are on this machine.
# Two hosts: the Mac the game is developed on, and Windows under Git Bash (added 2026-11-15).
# UE_ROOT overrides the engine folder; PA250_PYTHON overrides the Python used for summaries.
#
# Sets: PA250_HOST (mac | win64), PA250_UE_CMD (commandlets, headless -game), PA250_UE_GAME (a real
# window), PA250_PY, PA250_SHOT_DIR (the folder name Unreal saves screenshots under).
# Functions: pa250_native <path> (a path the engine can read: C:/... on Windows),
#            pa250_busy (prints what is running and returns 0 when a game or a build is in the way).
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    PA250_HOST=win64
    PA250_UE_ROOT="${UE_ROOT:-/c/Program Files/Epic Games/UE_5.8}"
    PA250_UE_CMD="$PA250_UE_ROOT/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    PA250_UE_GAME="$PA250_UE_ROOT/Engine/Binaries/Win64/UnrealEditor.exe"
    PA250_PY="${PA250_PYTHON:-$PA250_UE_ROOT/Engine/Binaries/ThirdParty/Python3/Win64/python.exe}"
    PA250_SHOT_DIR=WindowsEditor
    # Git Bash rewrites arguments that look like POSIX paths ("/Game/PA250/Maps/L_Lexington" would
    # become a path under the Git folder). Turn that off and hand the engine native paths ourselves.
    export MSYS_NO_PATHCONV=1
    export MSYS2_ARG_CONV_EXCL='*'
    pa250_native() { cygpath -m "$1"; }
    pa250_busy() {
      local games builds
      games=$(powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.Name -like 'UnrealEditor*' -and \$_.CommandLine -match ' -game' } | ForEach-Object { \$_.ProcessId.ToString() + ' ' + \$_.Name }" 2>/dev/null | tr -d '\r')
      builds=$(powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.Name -notlike 'powershell*' -and \$_.CommandLine -match 'UnrealBuildTool' } | ForEach-Object { \$_.ProcessId.ToString() + ' ' + \$_.Name }" 2>/dev/null | tr -d '\r')
      if [ -n "$games" ]; then echo "Another Unreal game instance is running (close it first):"; echo "$games" | cut -c1-160; return 0; fi
      if [ -n "$builds" ]; then echo "A UnrealBuildTool build is running; wait for it (the game would load half-linked modules):"; echo "$builds" | cut -c1-160; return 0; fi
      return 1
    }
    ;;
  *)
    PA250_HOST=mac
    PA250_UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
    PA250_UE_CMD="$PA250_UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd"
    PA250_UE_GAME="$PA250_UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
    PA250_PY="${PA250_PYTHON:-python3}"
    PA250_SHOT_DIR=MacEditor
    pa250_native() { printf '%s' "$1"; }
    pa250_busy() {
      local games builds
      games=$(pgrep -fl "UnrealEditor.* -game" | grep -v "$$" || true)
      builds=$(pgrep -fl "UnrealBuildTool.dll" || true)
      if [ -n "$games" ]; then echo "Another Unreal game instance is running (close it first; it crashes new instances at startup):"; echo "$games" | cut -c1-160; return 0; fi
      if [ -n "$builds" ]; then echo "A UnrealBuildTool build is running; wait for it (the game would load half-linked modules):"; echo "$builds" | cut -c1-160; return 0; fi
      return 1
    }
    ;;
esac
