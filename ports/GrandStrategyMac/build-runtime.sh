#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
ENGINE="${UE_ENGINE:-/Users/Shared/Epic Games/UE_5.8}"
bash "$ENGINE/Engine/Build/BatchFiles/Mac/Build.sh" PortHost Mac Development -Project="$PWD/TestHost/TestHost.uproject" -architecture=arm64 -NoHotReload -MaxParallelActions=2 -WaitMutex "$@"
