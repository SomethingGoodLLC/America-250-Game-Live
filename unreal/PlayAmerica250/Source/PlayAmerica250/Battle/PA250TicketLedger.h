// PA250TicketLedger.h — Conquest ticket rules as plain C++ (no UObject), shared by
// APA250BattleGameMode and the PA250.Battle.Conquest tests so the tests run the shipped rules.
#pragma once

#include "CoreMinimal.h"

struct FPA250TicketLedger
{
	/** Remaining tickets per side (soldiers: campaign units x SoldiersPerUnit). */
	TMap<FName, int32> Pool;
	/** Deaths per side per class; the source of BattleResult.losses. Bleed is not a death. */
	TMap<FName, TMap<FName, int32>> Deaths;
	float BleedAccum = 0.f;
	int32 BleedTicks = 0;

	int32 Remaining(FName Faction) const { return Pool.FindRef(Faction); }

	/** -1 ticket on death (M6: overrides the B1 spend-on-spawn rule). */
	void SpendDeath(FName Faction, FName UnitClass)
	{
		int32& T = Pool.FindOrAdd(Faction);
		if (T > 0) --T;
		Deaths.FindOrAdd(Faction).FindOrAdd(UnitClass)++;
	}

	/** Pure: tickets the side with fewer posts loses per bleed interval. Legacy (TotalPosts <= 0): 1.
	 *  Scaled (Battlefront-style): 1 per post of difference, +FullControlBonus when the other side
	 *  holds every post - so dominating the map drains the enemy fast. */
	static int32 BleedAmount(int32 PostsA, int32 PostsB, int32 TotalPosts, int32 FullControlBonus = 1)
	{
		const int32 Diff = FMath::Abs(PostsA - PostsB);
		if (Diff == 0) return 0;
		if (TotalPosts <= 0) return 1;
		const bool bFullControl = FMath::Max(PostsA, PostsB) >= TotalPosts;
		return Diff + (bFullControl ? FMath::Max(0, FullControlBonus) : 0);
	}

	/** Side holding fewer posts bleeds every Interval seconds; equal holdings reset the clock. */
	void Bleed(float DeltaSeconds, float IntervalSeconds, FName SideA, int32 PostsA, FName SideB, int32 PostsB,
		int32 TotalPosts = 0, int32 FullControlBonus = 1)
	{
		if (PostsA == PostsB)
		{
			BleedAccum = 0.f;
			return;
		}
		BleedAccum += DeltaSeconds;
		const float Interval = FMath::Max(1.f, IntervalSeconds);
		const FName Loser = PostsA < PostsB ? SideA : SideB;
		const int32 Amount = BleedAmount(PostsA, PostsB, TotalPosts, FullControlBonus);
		while (BleedAccum >= Interval)
		{
			BleedAccum -= Interval;
			int32& T = Pool.FindOrAdd(Loser);
			const int32 Take = FMath::Min(T, Amount);
			T -= Take;
			BleedTicks += Take;
		}
	}

	/** Deaths converted back to campaign units, rounded and capped at the class's starting units. */
	static int32 LossesInUnits(int32 DeathCount, int32 SoldiersPerUnit, int32 InitialUnits)
	{
		return FMath::Clamp(FMath::RoundToInt((float)DeathCount / (float)FMath::Max(1, SoldiersPerUnit)), 0, FMath::Max(0, InitialUnits));
	}
};
