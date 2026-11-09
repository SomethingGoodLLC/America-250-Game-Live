#!/usr/bin/env bash
# Envoy voice, end to end WITH SOUND: main:app (real ElevenLabs, MockLocal LLM) + L_DiplomacyRoom in a real
# window (-game -windowed, Metal, audio device on). The room sends one scripted line, plays the envoy's WAV
# through USoundWaveProcedural, and exits 0 only after "EnvoyVoice finished" with the line audible.
#   scripts/run_voice_test.sh                 # turn your volume up: the envoy speaks through the speakers
#   LLM=claude scripts/run_voice_test.sh      # real Claude reply instead of MockLocal
#   PA250_TTS=say scripts/run_voice_test.sh   # macOS say backup instead of ElevenLabs
#   EXTRA_ARGS="-PA250VoiceTestVideo=/abs/clip.mp4" scripts/run_voice_test.sh   # test the MP4 (talking portrait) path
# Logs: unreal/PlayAmerica250/Saved/Logs/voicetest_game.log, voicetest_service.log
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
GAME="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
SVC="$ROOT/services/negotiation"
PORT="${PA250_PORT:-8792}"
LOGDIR="$ROOT/unreal/PlayAmerica250/Saved/Logs"
GLOG="$LOGDIR/voicetest_game.log"
SLOG="$LOGDIR/voicetest_service.log"
TIMEOUT="${TIMEOUT:-240}"
mkdir -p "$LOGDIR"

if pgrep -fl "UnrealEditor.* -game" >/dev/null; then echo "Another Unreal game instance is running; close it first."; exit 2; fi
if pgrep -fl "UnrealBuildTool.dll" >/dev/null; then echo "A UnrealBuildTool build is running; wait for it."; exit 2; fi
if curl -fs -m 1 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then echo "port $PORT is busy; set PA250_PORT"; exit 2; fi

SVC_PID=""
cleanup() { [ -n "$SVC_PID" ] && { kill "$SVC_PID" 2>/dev/null; wait "$SVC_PID" 2>/dev/null; }; }
trap cleanup EXIT

echo "== starting main:app on :$PORT (TTS=${PA250_TTS:-auto}, LLM=${LLM:-mock}, voice only)"
(cd "$SVC" && PA250_LLM="${LLM:-mock}" PA250_TTS="${PA250_TTS:-auto}" PA250_AVATAR=none \
  exec .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port "$PORT" >"$SLOG" 2>&1) &
SVC_PID=$!
for _ in $(seq 1 60); do curl -fs -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 && break; sleep 0.5; done
curl -fs -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 || { echo "service did not start; see $SLOG"; exit 2; }

echo "== launching L_DiplomacyRoom with sound (-PA250VoiceTest)"
rm -f "$ROOT/unreal/PlayAmerica250/Saved/PA250/voicetest_result.json"
# UnfocusedVolumeMultiplier defaults to 0: if Terminal keeps focus the game would be muted. Keep it audible.
"$GAME" "$PROJECT" /Game/PA250/Maps/L_DiplomacyRoom -game -windowed -ResX=1280 -ResY=720 -nosplash \
  -PA250VoiceTest "-PA250Service=127.0.0.1:$PORT" "-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0" \
  ${EXTRA_ARGS:-} -log "-abslog=$GLOG" >/dev/null 2>&1 &
GAME_PID=$!
waited=0
while kill -0 "$GAME_PID" 2>/dev/null; do
  sleep 1; waited=$((waited + 1))
  if [ "$waited" -ge "$TIMEOUT" ]; then echo "TIMEOUT after ${TIMEOUT}s"; kill -9 "$GAME_PID" 2>/dev/null; break; fi
done
wait "$GAME_PID" 2>/dev/null; RC=$?

echo "== service"
grep -E '"(TTS done|ElevenLabs TTS failed|say TTS failed|TTS produced no audio|Speech job [a-z ]+)"' "$SLOG" | cut -c1-400
echo "== game (exit $RC, ${waited}s)"
grep -E "EnvoyVoice|EnvoyVideo|PA250 (PASS|FAIL)|PA250 voicetest" "$GLOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | cut -c1-300
if [ "$RC" -eq 0 ] && grep -qE "Envoy(Voice|Video) finished" "$GLOG" && grep -q "EnvoyVoice test PASSED" "$GLOG"; then
  echo "== VOICE TEST PASSED"; exit 0
fi
echo "== VOICE TEST FAILED"; exit 1
