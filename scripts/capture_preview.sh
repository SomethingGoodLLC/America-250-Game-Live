#!/usr/bin/env bash
# Capture battle-level preview stills: scripts/capture_preview.sh <label> [lexington|concord]
# (label: flags | walls | smoke | any). Stills land in docs/preview/<level>_<label>_*.png.
#
# On this Mac a second Unreal *game* instance (or a busy, swap-starved machine) makes the engine die at
# startup with SIGBUS in FMacCursor -> NSImage -> ImageIO, before any project code runs. So: refuse to
# start while another game instance or a UnrealBuildTool build is running (say which), and retry once
# if that specific crash still happens.
set -u
LABEL="${1:-smoke}"
LEVEL="${2:-lexington}"
case "$LEVEL" in concord) MAP=L_Concord ;; lexington) MAP=L_Lexington ;; *) echo "unknown level $LEVEL"; exit 2 ;; esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
GAME="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
TIMEOUT="${TIMEOUT:-180}"

busy() {
  local games builds
  games=$(pgrep -fl "UnrealEditor.* -game" | grep -v "$$" || true)
  builds=$(pgrep -fl "UnrealBuildTool.dll" || true)
  if [ -n "$games" ]; then echo "Another Unreal game instance is running (close it first; it crashes new instances at startup):"; echo "$games" | cut -c1-160; return 0; fi
  if [ -n "$builds" ]; then echo "A UnrealBuildTool build is running; wait for it (the game would load half-linked modules):"; echo "$builds" | cut -c1-160; return 0; fi
  return 1
}

if busy; then exit 2; fi

for attempt in 1 2; do
  LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/preview_${LEVEL}_${LABEL}_${attempt}.log"
  rm -f "$ROOT"/docs/preview/"$LEVEL"_"$LABEL"_*.png
  "$GAME" "$PROJECT" "/Game/PA250/Maps/$MAP" -game -windowed -ResX=1280 -ResY=720 -nosound -nosplash \
    "-PA250Preview=$LABEL" -log "-abslog=$LOG" >/dev/null 2>&1 &
  PID=$!
  waited=0
  while kill -0 "$PID" 2>/dev/null; do
    sleep 2; waited=$((waited + 2))
    if [ "$waited" -ge "$TIMEOUT" ]; then echo "timeout after ${TIMEOUT}s"; kill -9 "$PID" 2>/dev/null; break; fi
  done
  wait "$PID" 2>/dev/null
  if grep -q "FMacCursor" "$LOG" 2>/dev/null && grep -q "SIGBUS" "$LOG"; then
    echo "attempt $attempt: engine startup crash (SIGBUS in FMacCursor/ImageIO); see $LOG"
    [ "$attempt" -eq 1 ] && { sleep 10; continue; }
    echo "Still crashing. Check for other Unreal processes and memory pressure (sysctl vm.swapusage)."
    exit 1
  fi
  grep -h "PA250 preview wrote\|PA250 preview FAILED" "$LOG" | sed 's/^.*Display: //;s/^.*Error: //'
  ls "$ROOT"/docs/preview/"$LEVEL"_"$LABEL"_*.png >/dev/null 2>&1 && exit 0
  echo "no stills written; see $LOG"
  exit 1
done
