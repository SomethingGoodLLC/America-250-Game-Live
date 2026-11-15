#!/usr/bin/env bash
# Run one campaign-map probe in a real window and keep its screenshots.
#   scripts/run_campaign_probe.sh <ProbeFlag> <shot-name> [<shot-name> ...] [-- extra args]
# e.g. scripts/run_campaign_probe.sh PA250MarchProbe march_route march_halted
# The probe (PA250CampaignGameMode.cpp) takes its own "Shot showui" stills, in order, and quits; the
# stills are copied to docs/cards/<shot-name>.png and the probe's log lines are printed.
# RESX / RESY set the window (default 1280x720); OUT_DIR sends the stills somewhere other than docs/cards.
set -u
FLAG="${1:?probe flag}"; shift
NAMES=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do NAMES+=("$1"); shift; done
[ $# -gt 0 ] && shift
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
GAME="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
SHOTS="$ROOT/unreal/PlayAmerica250/Saved/Screenshots/MacEditor"
LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/probe_${FLAG}.log"
TIMEOUT="${TIMEOUT:-150}"
RESX="${RESX:-1280}"; RESY="${RESY:-720}"
OUT_DIR="${OUT_DIR:-$ROOT/docs/cards}"
if pgrep -fl "UnrealEditor.* -game" >/dev/null; then echo "another Unreal game instance is running"; exit 2; fi
if pgrep -fl "UnrealBuildTool.dll" >/dev/null; then echo "a build is running"; exit 2; fi
mkdir -p "$SHOTS" "$OUT_DIR"
STAMP="$SHOTS/.probe_stamp_$$"; touch "$STAMP"
"$GAME" "$PROJECT" /Game/PA250/Maps/L_Campaign -game -windowed "-ResX=$RESX" "-ResY=$RESY" -nosound -nosplash "-$FLAG" "$@" -log "-abslog=$LOG" >/dev/null 2>&1 &
PID=$!
waited=0
while kill -0 "$PID" 2>/dev/null; do
  sleep 2; waited=$((waited + 2))
  if [ "$waited" -ge "$TIMEOUT" ]; then echo "timeout after ${TIMEOUT}s: killed"; kill -9 "$PID" 2>/dev/null; break; fi
done
i=0
for f in $(find "$SHOTS" -name '*.png' -newer "$STAMP" | sort); do
  if [ "$i" -lt "${#NAMES[@]}" ]; then cp "$f" "$OUT_DIR/${NAMES[$i]}.png"; echo "shot: $OUT_DIR/${NAMES[$i]}.png"; fi
  i=$((i + 1))
done
rm -f "$STAMP"
echo "stills taken: $i (wanted ${#NAMES[@]})"
grep -E "_PROBE:|PA250 march route|PA250 decision shown|PA250 story|Failed to compile Material|Assertion failed|Fatal error" "$LOG" | sed -E 's/^\[[^]]*\]\[[ 0-9]*\]//' | cut -c1-400
[ "$i" -ge "${#NAMES[@]}" ] || exit 1
