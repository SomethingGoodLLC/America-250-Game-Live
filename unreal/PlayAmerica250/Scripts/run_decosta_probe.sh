#!/usr/bin/env bash
# Scripted Mac probe for the de Costa campaign overview (C++ timer; no Python delay).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
UE_APP="${UE_EDITOR_APP:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app}"
PROJ="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/decosta_probe.log"
mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"
open -na "$UE_APP" --args "$PROJ" /Game/PA250/Maps/L_Campaign -game -windowed -ResX=1280 -ResY=900 \
  -AbsLog="$LOG" \
  -PA250DecostaProbe -PA250Quit
echo "launched; AbsLog=$LOG (expects DECOSTA_PROBE lines after ~12s)"
