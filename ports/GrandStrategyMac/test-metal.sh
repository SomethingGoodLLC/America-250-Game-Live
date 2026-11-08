#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
ENGINE="${UE_ENGINE:-/Users/Shared/Epic Games/UE_5.8}"
"$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/TestHost/TestHost.uproject" -unattended -nosplash -RenderOffscreen -AllowCommandletRendering -ExecCmds="Automation RunTests GrandStrategy.MacPort" -TestExit="Automation Test Queue Empty" -ReportExportPath="$PWD/TestHost/Saved/Automation" -abslog="$PWD/logs/metal-tests.log" -stdout
