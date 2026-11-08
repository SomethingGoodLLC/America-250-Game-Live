#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
APP="$PWD/TestHost/Saved/StagedBuilds/Mac/PortHost.app/Contents/MacOS/PortHost"
if [[ ! -x "$APP" ]]; then
    echo "Locate the staged .app in TestHost/Saved/StagedBuilds/Mac before running." >&2
    exit 1
fi
"$APP" /InteractiveMap/GSMap/Levels/WorldMapFlatCombined -unattended -nosplash -RenderOffscreen -NoSound -benchmark -seconds=30 -stdout -abslog="$PWD/logs/packaged-runtime.log"
