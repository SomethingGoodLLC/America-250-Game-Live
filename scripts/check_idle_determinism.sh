#!/usr/bin/env bash
# An idle player across the whole Boston 1775 window, twice from the same seed (sim only: battles
# are settled by the sim's resolver). Passes when the British win both runs and the two final saves
# are byte for byte the same.
#   scripts/check_idle_determinism.sh [seed]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIM="$ROOT/unreal/PA250Sim"
OUT="${TMPDIR:-/tmp}/pa250_idle_$$"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT
cmake -S "$SIM" -B "$SIM/build" >/dev/null && cmake --build "$SIM/build" --target pa250sim_tool >/dev/null || { echo "build failed"; exit 2; }
ARGS=()
[ $# -ge 1 ] && ARGS=(--seed "$1")
STATUS=0
for run in 1 2; do
  # ${ARGS[@]+...}: macOS ships bash 3.2, where an empty array is "unbound" under set -u.
  "$SIM/build/pa250sim_tool" idle-run --out "$OUT/run$run.json" ${ARGS[@]+"${ARGS[@]}"} >"$OUT/run$run.txt" || STATUS=1
done
cat "$OUT/run1.txt"
grep -q "british win" "$OUT/run1.txt" && grep -q "british win" "$OUT/run2.txt" || { echo "FAIL: the British did not win both runs"; STATUS=1; }
if cmp -s "$OUT/run1.json" "$OUT/run2.json"; then
  echo "PASS  the two final saves are identical ($(wc -c <"$OUT/run1.json" | tr -d ' ') bytes)"
else
  echo "FAIL  the two final saves differ"; STATUS=1
fi
[ "$STATUS" -eq 0 ] && echo "== IDLE DETERMINISM PASSED" || echo "== IDLE DETERMINISM FAILED"
exit "$STATUS"
