#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
ENGINE="${UE_ENGINE:-/Users/Shared/Epic Games/UE_5.8}"
"$ENGINE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/TestHost/TestHost.uproject" /Game/Colonies1775/L_Colonies1775 -game -windowed -ResX=1280 -ResY=900 -NoSound -log -abslog="$PWD/logs/colonies-play.log"
