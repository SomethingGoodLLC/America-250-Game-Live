// PA250BattleRng.h — the battle's one outcome stream.
// Every roll that decides who is hit, who misfires, who answers a guard or where a man stands comes
// from here, never from FMath::FRand / VRandCone / RandRange (the engine's unseeded global). The game
// mode reseeds it from BattleSetup.seed when the battle starts; outside a battle (commandlet tests,
// the menu) it holds a fixed seed. Rolls left on the global RNG are cosmetic only and are listed in
// PROGRESS.md under "RNG left cosmetic". Game thread only.
#pragma once

#include "CoreMinimal.h"

namespace PA250BattleRng
{
/** The seed the stream holds when no battle has set one. */
inline constexpr int32 FallbackSeed = 0x1775;

/** The battle's outcome stream. */
PLAYAMERICA250_API FRandomStream& Get();

/** Seeds the stream from BattleSetup.seed (called once the setup is known). */
PLAYAMERICA250_API void Reseed(uint64 BattleSeed);

/** A stream of its own for one actor's rolls (seeded from the battle seed and Salt), for rolls that
 *  must not depend on the order other actors drew from Get(). */
PLAYAMERICA250_API FRandomStream Derived(uint32 Salt);
}
