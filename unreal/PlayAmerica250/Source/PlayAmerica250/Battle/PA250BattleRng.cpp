#include "PA250BattleRng.h"

namespace
{
FRandomStream GStream(PA250BattleRng::FallbackSeed);
uint32 GBattleSeedHash = static_cast<uint32>(PA250BattleRng::FallbackSeed);
}

namespace PA250BattleRng
{
FRandomStream& Get()
{
	return GStream;
}

void Reseed(uint64 BattleSeed)
{
	GBattleSeedHash = GetTypeHash(BattleSeed);
	GStream.Initialize(static_cast<int32>(GBattleSeedHash));
	UE_LOG(LogTemp, Display, TEXT("PA250 rng: battle stream seeded from BattleSetup.seed %llu"), BattleSeed);
}

FRandomStream Derived(uint32 Salt)
{
	return FRandomStream(static_cast<int32>(HashCombine(GBattleSeedHash, Salt)));
}
}
