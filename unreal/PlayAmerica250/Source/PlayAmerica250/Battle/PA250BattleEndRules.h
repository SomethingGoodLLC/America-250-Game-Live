// PA250BattleEndRules.h — how a battle reads the campaign, as plain C++ (no UObject): the two
// endings added beside tickets and the clock, and the side's ammunition pool. Shared by
// APA250BattleGameMode and the PA250Battle commandlet cases, so the tests run the shipped rules.
// GAMEPLAY SETTINGS: every number comes from unreal/PA250Sim/data/battle_rules.json (pa250::BattleRules).
#pragma once

#include "CoreMinimal.h"
#include "pa250/types.hpp"

namespace PA250EndRules
{
/** Squads that must be withdrawing or routing for the side to be breaking (the fraction, rounded up). */
inline int32 BrokenSquadsNeeded(int32 Squads, float Fraction)
{
	return Squads <= 0 ? 0 : FMath::Max(1, FMath::CeilToInt(Squads * FMath::Clamp(Fraction, 0.f, 1.f) - KINDA_SMALL_NUMBER));
}

/** army_broke, this instant: enough of the side's squads are withdrawing or routing. Never in the
 *  opening minutes, and never for a side that has not fielded MinSquads squads at once (PeakSquads):
 *  one or two squads falling back is a skirmish, not an army breaking. */
inline bool ArmyIsBreaking(int32 Squads, int32 Broken, int32 PeakSquads, float ElapsedSeconds, const pa250::BattleRules& R)
{
	if (ElapsedSeconds < R.army_broke_min_elapsed_seconds) return false;
	if (PeakSquads < R.army_broke_min_squads || Squads <= 0) return false;
	return Broken >= BrokenSquadsNeeded(Squads, R.army_broke_fraction);
}

/** posts_swept, this instant: the side holds every post, and the enemy has held at least one of
 *  them in this fight. Without that second half a defender wins by raising a flag on the one
 *  neutral post before the attacker has reached the field (seen in the first 2:1 autoplay run: 12
 *  defenders "swept" 28 attackers in 80 seconds), and a garrison that starts with every post wins
 *  by standing still. A sweep is posts taken from, or taken back from, the enemy. */
inline bool PostsAreSwept(int32 Owned, int32 Total, bool bEnemyEverHeldAPost, float ElapsedSeconds, const pa250::BattleRules& R)
{
	return Total > 0 && Owned >= Total && bEnemyEverHeldAPost && ElapsedSeconds >= R.posts_swept_min_elapsed_seconds;
}

/** In plain words, for the banner and the after-battle card. */
inline const TCHAR* EndReasonWords(const FString& Reason)
{
	if (Reason == TEXT("tickets_exhausted")) return TEXT("the last men were down");
	if (Reason == TEXT("time_limit")) return TEXT("the day ended with more men standing");
	if (Reason == TEXT("force_preserved")) return TEXT("the column kept more of its force");
	if (Reason == TEXT("routed")) return TEXT("the last handful quit the field");
	if (Reason == TEXT("army_broke")) return TEXT("the army broke and fell back");
	if (Reason == TEXT("posts_swept")) return TEXT("every post was taken and held");
	return TEXT("the fight ended");
}
}

/** A side's ammunition on the field, in rounds. Every man draws his opening box from it; a man
 *  standing in a post his side holds refills from what is left. A little is kept back for the human
 *  player (the same idea as FPA250TicketLedger::Held), so his empty box is never a dead end. */
struct FPA250AmmoPool
{
	TMap<FName, int32> Pool;
	TMap<FName, int32> Start;
	/** Rounds only the human player may draw. */
	TMap<FName, int32> Held;
	TMap<FName, int32> Refills;
	TMap<FName, int32> RefillRounds;

	void Init(FName Faction, int32 Rounds)
	{
		Pool.Add(Faction, FMath::Max(0, Rounds));
		Start.Add(Faction, FMath::Max(0, Rounds));
	}
	int32 Remaining(FName Faction) const { return Pool.FindRef(Faction); }
	int32 HeldFor(FName Faction) const { return FMath::Min(Held.FindRef(Faction), Remaining(Faction)); }
	/** What this man may still draw: everything for the human, the unheld part for a bot. */
	int32 Available(FName Faction, bool bHuman) const
	{
		return bHuman ? Remaining(Faction) : FMath::Max(0, Remaining(Faction) - HeldFor(Faction));
	}
	/** Take up to Want rounds. Returns what was granted. */
	int32 Draw(FName Faction, int32 Want, bool bHuman)
	{
		const int32 Granted = FMath::Clamp(Want, 0, Available(Faction, bHuman));
		if (Granted <= 0) return 0;
		Pool.FindOrAdd(Faction) -= Granted;
		if (bHuman)
		{
			// The human drawing on the held rounds uses them up: they are ordinary rounds afterwards.
			int32& H = Held.FindOrAdd(Faction);
			H = FMath::Min(H, Remaining(Faction));
		}
		return Granted;
	}
	/** A refill at a held post (counted for the battle trace). */
	int32 Refill(FName Faction, int32 Want, bool bHuman)
	{
		const int32 Granted = Draw(Faction, Want, bHuman);
		if (Granted > 0)
		{
			Refills.FindOrAdd(Faction)++;
			RefillRounds.FindOrAdd(Faction) += Granted;
		}
		return Granted;
	}
	int32 Spent(FName Faction) const { return Start.FindRef(Faction) - Remaining(Faction); }

	/** Rounds a side brings: its magazine (cartridges x rounds each), and never less than one opening
	 *  box for every man it has. */
	static int32 OpeningPool(int32 CartridgeBudget, int32 RoundsPerCartridge, int32 Men, int32 RoundsPerMan)
	{
		return FMath::Max(FMath::Max(0, CartridgeBudget) * FMath::Max(1, RoundsPerCartridge), FMath::Max(0, Men) * FMath::Max(0, RoundsPerMan));
	}
};
