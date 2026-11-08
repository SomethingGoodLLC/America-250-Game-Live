#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
ENGINE="${UE_ENGINE:-/Users/Shared/Epic Games/UE_5.8}"
"$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/TestHost/TestHost.uproject" -unattended -nosplash -nullrhi -ExecCmds="Automation RunTests GrandStrategy.MacPort.ProvinceLookup" -TestExit="Automation Test Queue Empty" -ReportExportPath="$PWD/TestHost/Saved/AutomationNullRHI" -abslog="$PWD/logs/load-tests.log" -stdout
