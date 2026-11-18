#!/usr/bin/env bash
# Run Unreal PA250.Battle tests headless (commandlet). Exit non-zero on failure.
# On this Mac Automation often SIGBUS in FMacCursor, so we use -run=PA250Battle
# via tools/verify.sh (same path as `verify.sh ue-test PA250.Battle`).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/_ue_env.sh"

if pa250_busy; then exit 2; fi

LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/battle_tests.log"
mkdir -p "$(dirname "$LOG")"

set +e
"$ROOT/tools/verify.sh" ue-test PA250.Battle 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e

# Prefer the commandlet summary line (Windows exit codes are noisy).
if grep -E 'PA250 battle commandlet done checks=[0-9]+ fails=0' "$LOG" >/dev/null; then
  echo "battle_tests.sh: PASS ($(grep -E 'PA250 battle commandlet done' "$LOG" | tail -1))"
  exit 0
fi
if grep -E 'PA250 battle commandlet done checks=[0-9]+ fails=[1-9]' "$LOG" >/dev/null; then
  echo "battle_tests.sh: FAIL — see $LOG" >&2
  grep -E 'PA250 battle commandlet done' "$LOG" | tail -1 >&2
  exit 1
fi
echo "battle_tests.sh: no commandlet summary (rc=$RC); see $LOG" >&2
exit 1
