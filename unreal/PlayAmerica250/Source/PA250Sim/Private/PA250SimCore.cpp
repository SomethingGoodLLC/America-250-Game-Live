// Thin translation unit: compiles the canonical PA250Sim core from unreal/PA250Sim.
// Do not copy those sources into this module.
// Unreal builds with -fvisibility=hidden; the sim core is plain C++ with no export macros, so
// export everything defined in this translation unit.
#pragma GCC visibility push(default)
#include "src/turn.cpp"
#include "src/battle.cpp"
#include "src/diplomacy.cpp"
#include "src/serialize.cpp"
#include "src/controller.cpp"
#pragma GCC visibility pop
