// PA250BattleModes.h — the three kinds of fight (BattleSetup.mode, chosen by the sim's
// choose_battle_mode): conquest (the open fight for the posts), assault (the attacker carries the
// defender's posts in order, against the clock) and landing (the attacker comes ashore in boats and
// must hold the landing post before any other opens). Pure rules, checked without a world.
// GAMEPLAY SETTINGS: battle_rules.json "modes".
#pragma once

#include "CoreMinimal.h"

namespace PA250Modes
{
inline bool IsAssault(FName Mode) { return Mode == TEXT("assault"); }
inline bool IsLanding(FName Mode) { return Mode == TEXT("landing"); }
/** Assault and landing start with every post the defender's and order them from the attacker's start. */
inline bool IsOrdered(FName Mode) { return IsAssault(Mode) || IsLanding(Mode); }

/** The words for the battle's banner and the pre-battle card. */
inline const TCHAR* ModeWords(FName Mode)
{
	if (IsAssault(Mode)) return TEXT("Assault on the works");
	if (IsLanding(Mode)) return TEXT("Landing from the boats");
	return TEXT("Fight for the posts");
}
inline const TCHAR* ModeRule(FName Mode)
{
	if (IsAssault(Mode)) return TEXT("The posts must be carried in order, and the clock is the defender's.");
	if (IsLanding(Mode)) return TEXT("The first wave comes in by boat; nothing inland can be taken until the landing place is held.");
	return TEXT("Whoever holds more posts wears the other side down.");
}

/** Post indices in the order the attacker reaches them: nearest his start first (index breaks ties). */
inline TArray<int32> AttackOrder(const TArray<FVector>& PostLocations, const FVector& AttackerStart)
{
	TArray<int32> Order;
	for (int32 i = 0; i < PostLocations.Num(); ++i) Order.Add(i);
	Order.StableSort([&](int32 A, int32 B) {
		return FVector::DistSquared2D(PostLocations[A], AttackerStart) < FVector::DistSquared2D(PostLocations[B], AttackerStart);
	});
	return Order;
}

/** Assault: the one post open to contest is the first in the order the attacker does not hold.
 *  INDEX_NONE when he holds them all. */
inline int32 OpenPost(const TArray<int32>& Order, const TArray<FName>& Holders, FName Attacker)
{
	for (const int32 I : Order)
	{
		if (Holders.IsValidIndex(I) && Holders[I] != Attacker) return I;
	}
	return INDEX_NONE;
}

/** Which posts are closed to contest right now.
 *  Assault: every post but the open one (a post carried stays the attacker's; the posts behind the
 *  open one wait their turn). Landing: every post but the landing post until the attacker has held it
 *  (bLanded); none after. Conquest: none. */
inline TArray<bool> LockedPosts(FName Mode, const TArray<int32>& Order, const TArray<FName>& Holders, FName Attacker, bool bLanded)
{
	TArray<bool> Locked;
	Locked.Init(false, Holders.Num());
	if (IsAssault(Mode))
	{
		const int32 Open = OpenPost(Order, Holders, Attacker);
		for (int32 i = 0; i < Locked.Num(); ++i) Locked[i] = i != Open;
	}
	else if (IsLanding(Mode) && !bLanded && Order.Num() > 0)
	{
		for (int32 i = 0; i < Locked.Num(); ++i) Locked[i] = i != Order[0];
	}
	return Locked;
}

/** Posts the attacker has carried (assault). */
inline int32 PostsCarried(const TArray<FName>& Holders, FName Attacker)
{
	int32 N = 0;
	for (const FName& H : Holders) N += H == Attacker ? 1 : 0;
	return N;
}

/** Who wins an assault when the clock runs out: the defender, who has held. */
inline FName AssaultWinnerAtTimeLimit(FName Defender) { return Defender; }

/** Where boat Index of Count starts and lands: abreast, Spacing apart, RowOut off the landing place
 *  along Seaward (unit, pointing from the land to the sea). */
inline void BoatCourse(int32 Index, int32 Count, const FVector& LandingPlace, const FVector& Seaward, float SpacingCm, float RowOutCm, FVector& OutStart, FVector& OutBeach)
{
	const FVector Along(-Seaward.Y, Seaward.X, 0.f);
	const float Lateral = (Index - 0.5f * (Count - 1)) * SpacingCm;
	OutBeach = LandingPlace + Along * Lateral;
	OutStart = OutBeach + Seaward * RowOutCm;
}
}
