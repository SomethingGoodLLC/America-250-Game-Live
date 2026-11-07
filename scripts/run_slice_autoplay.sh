#!/usr/bin/env bash
# Headless vertical-slice autoplay: main:app (MockLocal) + the game with -PA250Autoplay, then assertions.
#   scripts/run_slice_autoplay.sh            # one run
#   RUNS=2 scripts/run_slice_autoplay.sh     # two runs; compares pre-battle state hashes between them
# Exit status is non-zero on any failure. Never calls a cloud LLM, TTS or D-ID.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
SAVED="$ROOT/unreal/PlayAmerica250/Saved/PA250"
SVC="$ROOT/services/negotiation"
PORT="${PA250_PORT:-8791}"
RUNS="${RUNS:-1}"
TIMEOUT="${AUTOPLAY_TIMEOUT:-420}"
LOGDIR="$ROOT/unreal/PlayAmerica250/Saved/logs"
mkdir -p "$LOGDIR" "$SAVED"

SVC_PID=""
cleanup() {
  if [ -n "$SVC_PID" ]; then kill "$SVC_PID" 2>/dev/null; wait "$SVC_PID" 2>/dev/null; fi
}
trap cleanup EXIT

# A previous run's service may still be shutting down: wait for the port to be free.
for _ in $(seq 1 20); do
  curl -fs -m 1 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 || break
  sleep 0.5
done
if curl -fs -m 1 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then
  echo "port $PORT is already serving something else; set PA250_PORT"; exit 2
fi

echo "== starting main:app (MockLocal) on :$PORT"
(cd "$SVC" && PA250_LLM=mock PA250_TTS="${PA250_TTS:-say}" PA250_DID=0 MUSETALK_DISABLE=1 \
  exec .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port "$PORT" >"$LOGDIR/autoplay_service.log" 2>&1) &
SVC_PID=$!
for _ in $(seq 1 40); do
  curl -fs -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 && break
  sleep 0.5
done
curl -fs -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 || { echo "service did not start; see $LOGDIR/autoplay_service.log"; exit 2; }

STATUS=0
PREV=""
for run in $(seq 1 "$RUNS"); do
  echo "== autoplay run $run/$RUNS"
  rm -f "$SAVED"/autoplay_trace.json "$SAVED"/battle_result.json "$SAVED"/campaign.json "$SAVED"/autoplay_result.json
  "$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PROJECT" /Game/PA250/Maps/L_Campaign \
    -game -nullrhi -unattended -nosplash -nosound -PA250Autoplay "-PA250Service=127.0.0.1:$PORT" \
    -log -stdout -FullStdOutLogOutput >"$LOGDIR/autoplay_run$run.log" 2>&1 &
  GAME_PID=$!
  waited=0
  while kill -0 "$GAME_PID" 2>/dev/null; do
    sleep 2; waited=$((waited + 2))
    if [ "$waited" -ge "$TIMEOUT" ]; then echo "TIMEOUT after ${TIMEOUT}s"; kill -9 "$GAME_PID" 2>/dev/null; STATUS=1; break; fi
  done
  wait "$GAME_PID" 2>/dev/null; GAME_RC=$?
  echo "game exit code: $GAME_RC (${waited}s)"
  [ "$GAME_RC" -ne 0 ] && STATUS=1
  if [ ! -f "$SAVED/autoplay_trace.json" ]; then echo "no autoplay_trace.json produced"; STATUS=1; continue; fi
  cp "$SAVED/autoplay_trace.json" "$SAVED/run${run}_trace.json"
  ARGS=("$SAVED/autoplay_trace.json" --battle "$SAVED/battle_result.json")
  [ -n "$PREV" ] && ARGS+=(--compare "$PREV")
  "$SVC/.venv/bin/python" "$ROOT/scripts/assert_autoplay_trace.py" "${ARGS[@]}" || STATUS=1
  PREV="$SAVED/run${run}_trace.json"
done

[ "$STATUS" -eq 0 ] && echo "== SLICE AUTOPLAY PASSED ($RUNS run(s))" || echo "== SLICE AUTOPLAY FAILED"
exit "$STATUS"
