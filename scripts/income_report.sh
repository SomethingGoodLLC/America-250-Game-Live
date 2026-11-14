#!/usr/bin/env bash
# Monthly net income of the opening holdings left alone (sim only): the treasury on the 1st of each
# month for a year, under a truce, with no purchases and no building.
#   scripts/income_report.sh [months]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIM="$ROOT/unreal/PA250Sim"
cmake -S "$SIM" -B "$SIM/build" >/dev/null && cmake --build "$SIM/build" --target pa250sim_tool >/dev/null || { echo "build failed"; exit 2; }
exec "$SIM/build/pa250sim_tool" income-report --months "${1:-12}"
