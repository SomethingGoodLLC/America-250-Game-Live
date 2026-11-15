#!/usr/bin/env bash
# Capture battle-level preview stills: scripts/capture_preview.sh <label> [lexington|concord|harbor]
# (label: flags | walls | smoke | any). Stills land in docs/preview/<level>_<label>_*.png.
#
# On this Mac a second Unreal *game* instance (or a busy, swap-starved machine) makes the engine die at
# startup with SIGBUS in FMacCursor -> NSImage -> ImageIO, before any project code runs. So: refuse to
# start while another game instance or a UnrealBuildTool build is running (say which), and retry once
# if that specific crash still happens. RESX / RESY set the window (default 1280x720).
set -u
LABEL="${1:-smoke}"
LEVEL="${2:-lexington}"
case "$LEVEL" in concord) MAP=L_Concord ;; lexington) MAP=L_Lexington ;; harbor) MAP=L_Harbor ;; *) echo "unknown level $LEVEL"; exit 2 ;; esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/_ue_env.sh"  # PA250_UE_GAME, pa250_native, pa250_busy (Mac and Windows)
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
GAME="$PA250_UE_GAME"
TIMEOUT="${TIMEOUT:-180}"
RESX="${RESX:-1280}"; RESY="${RESY:-720}"

if pa250_busy; then exit 2; fi

for attempt in 1 2; do
  LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/preview_${LEVEL}_${LABEL}_${attempt}.log"
  rm -f "$ROOT"/docs/preview/"$LEVEL"_"$LABEL"_*.png
  "$GAME" "$(pa250_native "$PROJECT")" "/Game/PA250/Maps/$MAP" -game -windowed "-ResX=$RESX" "-ResY=$RESY" -nosound -nosplash \
    "-PA250Preview=$LABEL" "${@:3}" -log "-abslog=$(pa250_native "$LOG")" >/dev/null 2>&1 &
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
