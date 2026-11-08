#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
GEN="$PWD/TestHost/Plugins/InteractiveMap/Source/ThirdParty/MapGeneratorLibrary/MapGenerator"
bash "$GEN/build-mac.sh"
xcrun clang++ -std=c++20 -arch arm64 -mmacosx-version-min=14.0 -I "$GEN/source" tests/generator_smoke.cpp "$GEN/lib/Mac/arm64/libMapGenerator.a" -o tests/generator_smoke
cd tests
./generator_smoke
