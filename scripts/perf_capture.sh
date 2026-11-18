#!/usr/bin/env bash
# Fixed L_Lexington CSV profiler capture for Battle Feel M0/M7 baselines.
# Same resolution, seed/scenario, camera, and frame count every run.
# Editor -game numbers only (not a packaged build).
#
# Env overrides (keep M0 and M7 identical unless deliberately changing the scenario):
#   RESX RESY CAPTURE_FRAMES WARMUP_FRAMES BOTS TIMEOUT OUT_DIR
#   PREWARM=1  launch the same command once untimed first, so shader / PSO compilation and the DDC are
#              warm for the timed run. Compare two runs only when both used the same PREWARM (a cold
#              run after a build is slower for reasons that have nothing to do with the code).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/_ue_env.sh"

if pa250_busy; then exit 2; fi

PROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
MAP="/Game/PA250/Maps/L_Lexington"
RESX="${RESX:-1710}"
RESY="${RESY:-1112}"
# Total CSV frames from boot; first WARMUP_FRAMES are discarded in the summary.
CAPTURE_FRAMES="${CAPTURE_FRAMES:-900}"
WARMUP_FRAMES="${WARMUP_FRAMES:-300}"
BOTS="${BOTS:-20}"
TIMEOUT="${TIMEOUT:-360}"
OUT_DIR="${OUT_DIR:-$ROOT/unreal/PlayAmerica250/Saved/PA250/perf}"
LABEL="${1:-baseline}"
mkdir -p "$OUT_DIR"

LOG="$ROOT/unreal/PlayAmerica250/Saved/Logs/perf_capture_${LABEL}.log"
# Editor -game writes CSV under Engine Saved/Profiling/CSV (FPaths::ProfilingDir), not the project.
CSV_DIRS=(
  "$HOME/Library/Application Support/Epic/UnrealEngine/5.8/Saved/Profiling/CSV"
  "$ROOT/unreal/PlayAmerica250/Saved/Profiling/CSV"
  "$ROOT/unreal/PlayAmerica250/Saved/Profiling"
)
for d in "${CSV_DIRS[@]}"; do mkdir -p "$d"; done
BEFORE=$(ls -1t "${CSV_DIRS[0]}"/*.csv 2>/dev/null | head -1 || true)

# Uncap: r.VSync 0, t.MaxFPS 0. Fixed scenario. Do NOT use -PA250Preview=* — that RequestExit
# after a screenshot and aborts CSV before ExitAfterCsvProfiling.
# Seed comes from lexington_common BattleSetup; bots fixed.
EXEC="r.VSync 0,t.MaxFPS 0"
CMD=(
  "$PA250_UE_GAME"
  "$(pa250_native "$PROJECT")"
  "$MAP"
  -game -windowed
  "-ResX=$RESX" "-ResY=$RESY"
  -nosound -nosplash -unattended
  -PA250Scenario=lexington_common
  "-PA250Bots=$BOTS"
  "-csvCaptureFrames=$CAPTURE_FRAMES"
  -ExitAfterCsvProfiling
  -csvCompression=0
  "-ExecCmds=$EXEC"
  -log
  "-abslog=$(pa250_native "$LOG")"
)

echo "perf_capture.sh: running (${#CMD[@]} args)"
printf ' %q' "${CMD[@]}"; echo
echo "perf_capture.sh: RES=${RESX}x${RESY} bots=$BOTS capture_frames=$CAPTURE_FRAMES warmup_discard=$WARMUP_FRAMES"

run_capture() {
  "${CMD[@]}" >/dev/null 2>&1 &
  PID=$!
  waited=0
  while kill -0 "$PID" 2>/dev/null; do
    sleep 3; waited=$((waited + 3))
    if [ "$waited" -ge "$TIMEOUT" ]; then
      echo "perf_capture.sh: TIMEOUT after ${TIMEOUT}s" >&2
      kill -9 "$PID" 2>/dev/null || true
      break
    fi
  done
  wait "$PID" 2>/dev/null || true
}
if [ "${PREWARM:-0}" = "1" ]; then
  echo "perf_capture.sh: PREWARM=1 - one untimed launch first"
  run_capture
  BEFORE=$(ls -1t "${CSV_DIRS[0]}"/*.csv 2>/dev/null | head -1 || true)
fi
echo "perf_capture.sh: timed run (PREWARM=${PREWARM:-0})"
run_capture

# Prefer a new non-empty CSV under Engine/project Profiling dirs (skip 0-byte aborted captures).
# Separate globs: a missing *.csv.gz must not make ls fail the *.csv listing.
CSV=""
for d in "${CSV_DIRS[@]}"; do
  while IFS= read -r cand; do
    [[ -f "$cand" ]] || continue
    [[ -n "$BEFORE" && "$cand" == "$BEFORE" ]] && continue
    sz=$(wc -c < "$cand" | tr -d ' ')
    [[ "${sz:-0}" -lt 1000 ]] && continue
    CSV="$cand"
    break 2
  done < <(ls -1t "$d"/*.csv 2>/dev/null; ls -1t "$d"/*.csv.gz 2>/dev/null)
done
if [[ -z "$CSV" || ! -f "$CSV" ]]; then
  echo "perf_capture.sh: no CSV found; see $LOG" >&2
  grep -E 'Csv|csv|CSV|RequestExit|ExitAfter' "$LOG" 2>/dev/null | tail -30 || true
  exit 1
fi

DEST="$OUT_DIR/lexington_${LABEL}.csv"
cp "$CSV" "$DEST"
echo "perf_capture.sh: wrote $DEST"

"$PA250_PY" - "$DEST" "$WARMUP_FRAMES" "$OUT_DIR/lexington_${LABEL}_summary.json" <<'PY'
import csv, json, math, os, sys
csv.field_size_limit(sys.maxsize)
path, warm_s, out_json = sys.argv[1], int(sys.argv[2]), sys.argv[3]
# Unreal CSV: first rows are metadata; look for a frame-time column.
rows = []
with open(path, newline="", encoding="utf-8", errors="replace") as f:
    # Skip comment / metadata lines that are not CSV tables
    text = f.read().splitlines()
header_i = None
for i, line in enumerate(text):
    if "frametime" in line.lower() or "FrameTime" in line or line.startswith("Frame,"):
        header_i = i
        break
if header_i is None:
    # Fallback: first line with commas and a numeric second column later
    for i, line in enumerate(text):
        if line.count(",") >= 2 and not line.startswith("//"):
            header_i = i
            break
if header_i is None:
    print("perf_capture.sh: could not find CSV header", file=sys.stderr)
    sys.exit(1)
reader = csv.DictReader(text[header_i:])
# Normalize keys
keys = reader.fieldnames or []
ft_key = None
for k in keys:
    lk = (k or "").lower()
    if lk in ("frametime", "frame_time", "gt/s", "game thread time") or "frametime" in lk.replace(" ", ""):
        ft_key = k
        break
if ft_key is None:
    for k in keys:
        if "time" in (k or "").lower() and "frame" in (k or "").lower():
            ft_key = k
            break
times = []
for row in reader:
    if not ft_key:
        break
    raw = (row.get(ft_key) or "").strip()
    if not raw:
        continue
    try:
        v = float(raw)
    except ValueError:
        continue
    # Unreal often stores ms already; if values look like seconds (< 1), convert
    times.append(v)
if not times:
    # Try positional: column index 1 after Frame#
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        lines = [ln for ln in f.read().splitlines() if ln and not ln.startswith("//")]
    for ln in lines[header_i + 1 :]:
        parts = ln.split(",")
        if len(parts) < 2:
            continue
        try:
            times.append(float(parts[1]))
        except ValueError:
            continue
if len(times) <= warm_s:
    print(f"perf_capture.sh: only {len(times)} frames, need > {warm_s} warmup", file=sys.stderr)
    sys.exit(1)
sample = times[warm_s:]
# Detect unit: if median > 5 assume milliseconds
med = sorted(sample)[len(sample) // 2]
unit = "ms"
vals = sample
if med < 1.0:
    vals = [v * 1000.0 for v in sample]
    unit = "ms (from seconds)"
vals_sorted = sorted(vals)
n = len(vals_sorted)
avg = sum(vals_sorted) / n
p95 = vals_sorted[min(n - 1, int(math.ceil(0.95 * n) - 1))]
summary = {
    "csv": path,
    "frames_total": len(times),
    "frames_used": n,
    "warmup_discarded": warm_s,
    "avg_frame_ms": round(avg, 3),
    "p95_frame_ms": round(p95, 3),
    "unit_note": unit,
    "prewarm": os.environ.get("PREWARM", "0") == "1",
}
with open(out_json, "w", encoding="utf-8") as f:
    json.dump(summary, f, indent=2)
print(f"avg_frame_ms={summary['avg_frame_ms']} p95_frame_ms={summary['p95_frame_ms']} frames_used={n} (discarded {warm_s})")
print(f"summary={out_json}")
PY

echo "perf_capture.sh: DONE label=$LABEL"
