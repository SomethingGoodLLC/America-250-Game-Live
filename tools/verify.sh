#!/usr/bin/env bash
# PA250 verification harness. Subcommands: sim | py | mapgen | ue-build | ue-script | ue-test | ue-shot | all
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
UPROJECT="$ROOT/unreal/PlayAmerica250/PlayAmerica250.uproject"
BUILD_SH="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd"

die() { echo "verify.sh: $*" >&2; exit 1; }

cmd_sim() {
  cmake -S "$ROOT/unreal/PA250Sim" -B "$ROOT/unreal/PA250Sim/build"
  cmake --build "$ROOT/unreal/PA250Sim/build"
  ctest --test-dir "$ROOT/unreal/PA250Sim/build" --output-on-failure
}

cmd_py() {
  local venv="$ROOT/services/negotiation/.venv/bin/python"
  [[ -x "$venv" ]] || die "negotiation venv missing at $venv"
  (cd "$ROOT/services/negotiation" && "$venv" -m pytest tests/diplomacy -q --no-cov)
}

cmd_mapgen() {
  local py="$ROOT/tools/mapgen/.venv/bin/python"
  local script="$ROOT/tools/mapgen/build_map.py"
  [[ -x "$py" ]] || die "mapgen venv missing (run M4 setup)"
  [[ -f "$script" ]] || die "missing $script"
  "$py" "$script" --check
}

cmd_ue_build() {
  [[ -x "$BUILD_SH" ]] || die "Build.sh not found: $BUILD_SH"
  "$BUILD_SH" PlayAmerica250Editor Mac Development -Project="$UPROJECT"
}

cmd_ue_script() {
  local script="${1:-}"
  [[ -n "$script" ]] || die "usage: verify.sh ue-script <file.py>"
  if [[ "$script" != /* ]]; then
    if [[ -f "$ROOT/$script" ]]; then
      script="$ROOT/$script"
    elif [[ -f "$script" ]]; then
      script="$(cd "$(dirname "$script")" && pwd)/$(basename "$script")"
    fi
  fi
  [[ -f "$script" ]] || die "script not found: $1"
  [[ -x "$EDITOR_CMD" ]] || die "UnrealEditor-Cmd not found: $EDITOR_CMD"
  "$EDITOR_CMD" "$UPROJECT" -run=pythonscript -script="$script" -unattended -nullrhi -nosplash -stdout
}

cmd_ue_test() {
  local filter="${1:-}"
  [[ -n "$filter" ]] || die "usage: verify.sh ue-test <filter>"
  [[ -x "$EDITOR_CMD" ]] || die "UnrealEditor-Cmd not found: $EDITOR_CMD"
  # Prefer commandlets on this Mac (Automation often SIGBUS in FMacCursor).
  local cmdlet=""
  case "$filter" in
    PA250.Campaign.Clock) cmdlet=PA250Clock ;;
    PA250.Map.Picking) cmdlet=PA250MapPicking ;;
    PA250.Map.SingleLayer) cmdlet=PA250MapSingleLayer ;;
    PA250.Map.Framing) cmdlet=PA250MapFraming ;;
    PA250.Battle|PA250.Battle.*) cmdlet=PA250Battle ;; # all PA250.Battle.* cases (PA250BattleCases.cpp)
    PA250.Sim.Core) cmdlet="" ;; # automation / existing
  esac
  if [[ -n "$cmdlet" ]]; then
    "$EDITOR_CMD" "$UPROJECT" -run="$cmdlet" -unattended -nullrhi -nosplash -stdout
    return $?
  fi
  local log
  log="$(mktemp -t pa250_uetest.XXXXXX)"
  set +e
  "$EDITOR_CMD" "$UPROJECT" \
    -ExecCmds="Automation RunTests ${filter};Quit" \
    -unattended -nullrhi -nosplash -stdout 2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  if grep -Eiq 'Result=\{Fail|TEST FAILED|Automation Test Failed|Test Completed\. Result=\{Fail' "$log"; then
    rm -f "$log"
    die "ue-test reported failures for filter: $filter"
  fi
  if ! grep -Eq 'TEST COMPLETE\. EXIT CODE: 0|Result=\{Success\}' "$log"; then
    rm -f "$log"
    die "ue-test did not report success for filter: $filter"
  fi
  rm -f "$log"
  [[ $rc -eq 0 ]] || true  # RequestExit may ignore codes on Mac
}

cmd_ue_shot() {
  # Stub Binaries/Mac/UnrealEditor re-launches the .app and returns immediately;
  # use the real binary so we wait for -PA250Shot to HighResShot + Quit.
  local editor_app="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
  [[ -x "$editor_app" ]] || die "UnrealEditor not found: $editor_app"
  local shot_root="$ROOT/unreal/PlayAmerica250/Saved/Screenshots"
  mkdir -p "$shot_root/Mac" "$shot_root/MacEditor"
  local marker
  marker="$(mktemp -t pa250_ueshot.XXXXXX)"
  # Editor -game writes HighResShot under MacEditor/, not Mac/.
  "$editor_app" "$UPROJECT" \
    /Game/PA250/Maps/L_Campaign \
    -game -windowed -ResX=1920 -ResY=1080 -PA250Shot \
    -nosplash 2>&1 || true
  local after
  after="$(find "$shot_root" -type f \( -name '*.png' -o -name '*.jpg' \) -newer "$marker" 2>/dev/null | head -1 || true)"
  rm -f "$marker"
  [[ -n "$after" && -f "$after" ]] || die "ue-shot: no screenshot produced (is -PA250Shot wired?)"
  echo "SCREENSHOT=$after"
}

cmd_all() {
  local failed=0
  for c in sim py mapgen ue-build; do
    echo "==== verify.sh $c ===="
    if ! "cmd_${c//-/_}"; then
      echo "FAIL: $c"
      failed=1
    else
      echo "PASS: $c"
    fi
  done
  # ue-script / ue-test need args; smoke with known-good placeholders when present
  if [[ -f "$ROOT/tools/ue/build_map_assets.py" ]]; then
    echo "==== verify.sh ue-script tools/ue/build_map_assets.py ===="
    if ! cmd_ue_script "$ROOT/tools/ue/build_map_assets.py"; then
      echo "FAIL: ue-script"
      failed=1
    else
      echo "PASS: ue-script"
    fi
  else
    echo "SKIP: ue-script (tools/ue/build_map_assets.py not yet present)"
  fi
  echo "==== verify.sh ue-test PA250.Sim.Core ===="
  if ! cmd_ue_test "PA250.Sim.Core"; then
    echo "FAIL: ue-test"
    failed=1
  else
    echo "PASS: ue-test"
  fi
  [[ $failed -eq 0 ]] || die "one or more subcommands failed"
  echo "ALL PASSED (except ue-shot, excluded from all)"
}

usage() {
  cat <<EOF
Usage: tools/verify.sh <subcommand> [args]
  sim
  py
  mapgen
  ue-build
  ue-script <file.py>
  ue-test <filter>
  ue-shot
  all
EOF
}

main() {
  local sub="${1:-}"
  shift || true
  case "$sub" in
    sim) cmd_sim "$@" ;;
    py) cmd_py "$@" ;;
    mapgen) cmd_mapgen "$@" ;;
    ue-build) cmd_ue_build "$@" ;;
    ue-script) cmd_ue_script "$@" ;;
    ue-test) cmd_ue_test "$@" ;;
    ue-shot) cmd_ue_shot "$@" ;;
    all) cmd_all "$@" ;;
    -h|--help|help|"") usage; [[ -n "$sub" ]] || exit 1 ;;
    *) die "unknown subcommand: $sub" ;;
  esac
}

main "$@"
