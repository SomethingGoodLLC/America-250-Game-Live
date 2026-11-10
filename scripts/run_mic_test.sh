#!/usr/bin/env bash
# Push-to-talk, end to end WITH SOUND: main:app (local whisper STT, ElevenLabs voice) + L_DiplomacyRoom.
# The room sends a mic clip to /v1/stt, sends the text as the player's line, and passes when the envoy's
# reply has been spoken ("EnvoyVoice finished").
#   scripts/run_mic_test.sh                     # injected clip: macOS `say` speaking a line (no mic needed)
#   LIVE=1 scripts/run_mic_test.sh              # live microphone: records 3 s, so speak right after "recording from"
# Logs: unreal/PlayAmerica250/Saved/Logs/mictest_game.log, mictest_service.log
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
GAME="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
SVC="$ROOT/services/negotiation"
PORT="${PA250_PORT:-8793}"
LOGDIR="$ROOT/unreal/PlayAmerica250/Saved/Logs"
GLOG="$LOGDIR/mictest_game.log"; SLOG="$LOGDIR/mictest_service.log"
mkdir -p "$LOGDIR"
if pgrep -fl "UnrealEditor.* -game" >/dev/null; then echo "Another Unreal game instance is running; close it first."; exit 2; fi
SVC_PID=""; cleanup() { [ -n "$SVC_PID" ] && { kill "$SVC_PID" 2>/dev/null; wait "$SVC_PID" 2>/dev/null; }; }; trap cleanup EXIT
(cd "$SVC" && PA250_LLM="${LLM:-mock}" exec .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port "$PORT" >"$SLOG" 2>&1) &
SVC_PID=$!
for _ in $(seq 1 60); do curl -fs -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 && break; sleep 0.5; done
ARGS=(-PA250MicTest)
if [ "${LIVE:-0}" != "1" ]; then
  CLIP="$LOGDIR/mictest_clip.wav"
  say -o "$LOGDIR/mictest_clip.aiff" "General Gage, I propose a ceasefire." && afconvert -f WAVE -d LEI16@48000 "$LOGDIR/mictest_clip.aiff" "$CLIP"
  ARGS=("-PA250MicTestWav=$CLIP")
fi
rm -f "$ROOT/unreal/PlayAmerica250/Saved/PA250/mictest_result.json"
"$GAME" "$PROJECT" /Game/PA250/Maps/L_DiplomacyRoom -game -windowed -ResX=1280 -ResY=720 -nosplash \
  "${ARGS[@]}" "-PA250Service=127.0.0.1:$PORT" -log "-abslog=$GLOG" >/dev/null 2>&1 &
RESULT="$ROOT/unreal/PlayAmerica250/Saved/PA250/mictest_result.json"
waited=0  # the .app relaunches itself, so wait for the result file rather than a PID
until [ -f "$RESULT" ] || grep -q "Critical error" "$GLOG" 2>/dev/null; do
  sleep 1; waited=$((waited + 1)); [ "$waited" -ge "${TIMEOUT:-240}" ] && { echo "TIMEOUT after ${waited}s"; break; }
done
sleep 2; pkill -f "PA250MicTest" 2>/dev/null
echo "== service"; grep -E '"(STT model loaded|Player speech transcribed|STT got silence|TTS done|Speech job finished|Avatar video)"' "$SLOG" | cut -c1-300
echo "== game"; grep -E "PA250 mic|EnvoyVoice (started|finished|failed|test)|PA250 mictest" "$GLOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | cut -c1-260
grep -q '"passed": true' "$ROOT/unreal/PlayAmerica250/Saved/PA250/mictest_result.json" 2>/dev/null && { echo "== MIC TEST PASSED"; exit 0; }
echo "== MIC TEST FAILED"; exit 1
