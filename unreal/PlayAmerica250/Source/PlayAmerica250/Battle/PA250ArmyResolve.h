// PA250ArmyResolve.h — per-side Army Resolve (100→0). CP bleed, casualties, and shocks drain it;
// majority posts recover it. At 0 the side breaks (end_reason army_broke). Plain C++, no UObject.
#pragma once

#include "CoreMinimal.h"
#include "PA250TicketLedger.h"

struct FPA250ArmyResolveTuning
{
	float Start = 100.f;
	/** Resolve lost per BleedAmount unit each bleed interval. */
	float DrainPerBleedUnit = 2.f;
	/** Resolve lost when casualties / opening_strength rises (scale × fraction × 100). */
	float CasualtyScale = 40.f;
	/** Resolve lost when an officer or the colours fall. */
	float OfficerShock = 8.f;
	float ColoursShock = 10.f;
	float CannonShock = 3.f;
	/** Resolve recovered per second while holding a strict majority of posts. */
	float MajorityRecoverPerSecond = 0.35f;
};

struct FPA250ArmyResolve
{
	TMap<FName, float> Value;
	TMap<FName, int32> OpeningStrength;
	TMap<FName, int32> CasualtiesSeen;
	FPA250ArmyResolveTuning Tuning;

	void Init(FName A, FName B, int32 OpenA, int32 OpenB, const FPA250ArmyResolveTuning& InTuning)
	{
		Tuning = InTuning;
		Value.Reset();
		OpeningStrength.Reset();
		CasualtiesSeen.Reset();
		Value.Add(A, Tuning.Start);
		Value.Add(B, Tuning.Start);
		OpeningStrength.Add(A, FMath::Max(1, OpenA));
		OpeningStrength.Add(B, FMath::Max(1, OpenB));
	}

	float Get(FName Faction) const { return Value.FindRef(Faction); }
	bool IsBroken(FName Faction) const { return Get(Faction) <= 0.f; }

	void Clamp(FName Faction)
	{
		float& V = Value.FindOrAdd(Faction);
		V = FMath::Clamp(V, 0.f, Tuning.Start);
	}

	void ApplyBleedUnits(FName Loser, int32 Units)
	{
		if (Units <= 0) return;
		Value.FindOrAdd(Loser) -= float(Units) * Tuning.DrainPerBleedUnit;
		Clamp(Loser);
	}

	/** Call when a man is recorded KIA/WIA/POW. Drains by share of opening strength. */
	void OnManDown(FName Faction)
	{
		int32& Seen = CasualtiesSeen.FindOrAdd(Faction);
		++Seen;
		const int32 Open = FMath::Max(1, OpeningStrength.FindRef(Faction));
		float& V = Value.FindOrAdd(Faction);
		V -= Tuning.CasualtyScale / float(Open);
		Clamp(Faction);
	}

	void Shock(FName Faction, float Amount)
	{
		Value.FindOrAdd(Faction) -= Amount;
		Clamp(Faction);
	}

	void TickRecover(float DeltaSeconds, FName SideA, int32 PostsA, FName SideB, int32 PostsB, int32 TotalPosts)
	{
		if (PostsA == PostsB || FMath::Max(PostsA, PostsB) * 2 <= TotalPosts) return;
		const FName Leader = PostsA > PostsB ? SideA : SideB;
		Value.FindOrAdd(Leader) += Tuning.MajorityRecoverPerSecond * DeltaSeconds;
		Clamp(Leader);
	}

	/**
	 * Drive from CP state: drain the side with fewer posts by BleedAmount units, recover majority.
	 * Returns the Resolve loser if newly broken this tick (NAME_None otherwise).
	 */
	FName TickPosts(float DeltaSeconds, float BleedInterval, FPA250TicketLedger& Ledger,
		FName SideA, int32 PostsA, FName SideB, int32 PostsB, int32 TotalPosts)
	{
		// A casualty or shock can break either side between bleed intervals.
		if (IsBroken(SideA)) return SideA;
		if (IsBroken(SideB)) return SideB;
		const int32 Fired = Ledger.TickBleedClock(DeltaSeconds, BleedInterval, PostsA, PostsB, TotalPosts);
		if (Fired > 0 && PostsA != PostsB)
		{
			const FName Loser = PostsA < PostsB ? SideA : SideB;
			const bool bWasUp = !IsBroken(Loser);
			ApplyBleedUnits(Loser, Fired);
			if (bWasUp && IsBroken(Loser)) return Loser;
		}
		TickRecover(DeltaSeconds, SideA, PostsA, SideB, PostsB, TotalPosts);
		return NAME_None;
	}
};
