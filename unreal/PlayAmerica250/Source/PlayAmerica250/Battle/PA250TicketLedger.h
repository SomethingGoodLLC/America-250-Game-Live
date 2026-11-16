// PA250TicketLedger.h — Conquest ticket rules as plain C++ (no UObject), shared by
// APA250BattleGameMode and the PA250.Battle tests so the tests run the shipped rules.
// Artillery pass (2026-11): tickets = men. One man leaves → one ticket. CP bleed does not
// spend tickets (it drains Army Resolve instead). KIA and WIA are recorded on each event.
#pragma once

#include "CoreMinimal.h"

struct FPA250TicketLedger
{
	/** Remaining tickets per side (men available: field + reserve). */
	TMap<FName, int32> Pool;
	/** Recorded killed / wounded per side per class. CasualtiesFromLedger sums these. */
	TMap<FName, TMap<FName, int32>> Killed;
	TMap<FName, TMap<FName, int32>> Wounded;
	/** Fallen = killed + wounded (for class-left / reserve math). Kept in sync with Record*. */
	TMap<FName, TMap<FName, int32>> Deaths;
	/** How many Resolve bleed units fired (HUD / traces); no longer ticket spends. */
	int32 BleedTicks = 0;
	float BleedAccum = 0.f;

	/** Tickets held back per side for the human player (0 or 1). */
	TMap<FName, int32> Held;

	int32 Remaining(FName Faction) const { return Pool.FindRef(Faction); }
	int32 HeldFor(FName Faction) const { return FMath::Min(Held.FindRef(Faction), Remaining(Faction)); }
	bool CanReinforce(FName Faction) const { return Remaining(Faction) > HeldFor(Faction); }
	bool DrawHeld(FName Faction)
	{
		if (HeldFor(Faction) <= 0 || Remaining(Faction) > HeldFor(Faction)) return false;
		Held.Remove(Faction);
		return true;
	}

	int32 FallenOf(FName Faction, FName UnitClass) const
	{
		return Deaths.FindRef(Faction).FindRef(UnitClass);
	}

	/** One man = one ticket. Records KIA. */
	bool SpendKilled(FName Faction, FName UnitClass)
	{
		if (!SpendOne(Faction)) return false;
		Killed.FindOrAdd(Faction).FindOrAdd(UnitClass)++;
		Deaths.FindOrAdd(Faction).FindOrAdd(UnitClass)++;
		return true;
	}

	/** One man = one ticket. Records WIA. */
	bool SpendWounded(FName Faction, FName UnitClass)
	{
		if (!SpendOne(Faction)) return false;
		Wounded.FindOrAdd(Faction).FindOrAdd(UnitClass)++;
		Deaths.FindOrAdd(Faction).FindOrAdd(UnitClass)++;
		return true;
	}

	/** POW is recorded separately by the battle; spend its man through the same guard. */
	bool SpendCaptured(FName Faction) { return SpendOne(Faction); }

	/** Legacy name: a death on the field is recorded as killed (surgeon may later convert). */
	void SpendDeath(FName Faction, FName UnitClass, float /*TicketCost*/ = 1.f)
	{
		SpendKilled(Faction, UnitClass);
	}

	/** Surgeon reached him: move one recorded kill to wounded (ticket already spent). */
	void ConvertKillToWound(FName Faction, FName UnitClass)
	{
		int32& K = Killed.FindOrAdd(Faction).FindOrAdd(UnitClass);
		if (K <= 0) return;
		--K;
		Wounded.FindOrAdd(Faction).FindOrAdd(UnitClass)++;
	}

	/** Pure: Resolve drain units per bleed interval (was ticket bleed amount). */
	static int32 BleedAmount(int32 PostsA, int32 PostsB, int32 TotalPosts, int32 FullControlBonus = 1)
	{
		const int32 Diff = FMath::Abs(PostsA - PostsB);
		if (Diff == 0) return 0;
		if (TotalPosts <= 0) return 1;
		const bool bFullControl = FMath::Max(PostsA, PostsB) >= TotalPosts;
		return Diff + (bFullControl ? FMath::Max(0, FullControlBonus) : 0);
	}

	/**
	 * CP pressure no longer spends tickets. Accumulates bleed intervals and returns how many
	 * Resolve-drain units fired this call (for Army Resolve). Equal posts reset the clock.
	 */
	int32 TickBleedClock(float DeltaSeconds, float IntervalSeconds, int32 PostsA, int32 PostsB,
		int32 TotalPosts = 0, int32 FullControlBonus = 1)
	{
		if (PostsA == PostsB)
		{
			BleedAccum = 0.f;
			return 0;
		}
		BleedAccum += DeltaSeconds;
		const float Interval = FMath::Max(1.f, IntervalSeconds);
		const int32 Amount = BleedAmount(PostsA, PostsB, TotalPosts, FullControlBonus);
		int32 Fired = 0;
		while (BleedAccum >= Interval)
		{
			BleedAccum -= Interval;
			Fired += Amount;
			BleedTicks += Amount;
		}
		return Fired;
	}

	/** Deprecated ticket bleed: no longer changes Pool. Prefer TickBleedClock + Army Resolve. */
	void Bleed(float DeltaSeconds, float IntervalSeconds, FName /*SideA*/, int32 PostsA, FName /*SideB*/, int32 PostsB,
		int32 TotalPosts = 0, int32 FullControlBonus = 1)
	{
		TickBleedClock(DeltaSeconds, IntervalSeconds, PostsA, PostsB, TotalPosts, FullControlBonus);
	}

private:
	bool SpendOne(FName Faction)
	{
		int32& T = Pool.FindOrAdd(Faction);
		if (T <= 0) return false;
		// Held reserves a spawn for the human. It cannot protect an actual casualty.
		--T;
		return true;
	}
};
