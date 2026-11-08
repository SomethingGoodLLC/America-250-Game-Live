#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
ENGINE="${UE_ENGINE:-/Users/Shared/Epic Games/UE_5.8}"
bash "$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PWD/TestHost/TestHost.uproject" -noP4 -platform=Mac -architecture=arm64 -clientconfig=Development -target=PortHost -skipbuild -cook -map=/InteractiveMap/GSMap/Levels/WorldMapFlatCombined -stage -pak -package -unattended -utf8output
