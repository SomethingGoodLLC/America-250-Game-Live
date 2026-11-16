// PA250Blast.h — shared blast path for shell, premature muzzle, and ammo-chest detonation.
// Outcome rings and cover demotion are pure; ApplyBlast drives WoundOut / knockdown / react.
// GAMEPLAY SETTINGS: battle_rules.json "blast".
#pragma once

#include "CoreMinimal.h"
#include "pa250/types.hpp"

class ARFCharacter;
class APA250BattleGameMode;
class UWorld;
class AActor;

enum class EPA250BlastSize : uint8
{
	Shell,
	Premature,
	Chest
};

enum class EPA250BlastRing : uint8
{
	Killed,
	Wounded,
	Knockdown,
	Stagger,
	Flinch,
	None
};

enum class EPA250BlastDir : uint8
{
	Front,
	Back,
	Left,
	Right
};

struct FPA250BlastResult
{
	int32 Hit = 0;
	int32 Killed = 0;
	int32 Wounded = 0;
	int32 Knocked = 0;
	int32 Staggered = 0;
	int32 Flinched = 0;
	int32 FriendlyHit = 0;
	int32 FriendlyKilled = 0;
	int32 FriendlyWounded = 0;
};

namespace PA250Blast
{
/** Outer radius (cm) for a blast size. */
inline float OuterRadiusCm(EPA250BlastSize Size, const pa250::BattleRules& R)
{
	switch (Size)
	{
	case EPA250BlastSize::Premature: return R.blast_premature_radius_cm;
	case EPA250BlastSize::Chest: return R.blast_chest_radius_cm;
	default: return R.blast_shell_radius_cm;
	}
}

inline float ImpulseCm(EPA250BlastSize Size, const pa250::BattleRules& R)
{
	switch (Size)
	{
	case EPA250BlastSize::Premature: return R.blast_premature_impulse_cm;
	case EPA250BlastSize::Chest: return R.blast_chest_impulse_cm;
	default: return R.blast_shell_impulse_cm;
	}
}

/** Pure: ring from distance / outer radius. Cover demotes one ring (killed→wounded→…). */
inline EPA250BlastRing ClassifyRing(float DistCm, float OuterCm, bool bCovered, const pa250::BattleRules& R)
{
	if (OuterCm <= 1.f || DistCm > OuterCm) return EPA250BlastRing::None;
	const float Frac = DistCm / OuterCm;
	EPA250BlastRing Ring = EPA250BlastRing::None;
	if (Frac <= R.blast_ring_killed_frac) Ring = EPA250BlastRing::Killed;
	else if (Frac <= R.blast_ring_wounded_frac) Ring = EPA250BlastRing::Wounded;
	else if (Frac <= R.blast_ring_knockdown_frac) Ring = EPA250BlastRing::Knockdown;
	else if (Frac <= R.blast_ring_stagger_frac) Ring = EPA250BlastRing::Stagger;
	else if (Frac <= R.blast_ring_flinch_frac) Ring = EPA250BlastRing::Flinch;
	if (!bCovered || Ring == EPA250BlastRing::None) return Ring;
	switch (Ring)
	{
	case EPA250BlastRing::Killed: return EPA250BlastRing::Wounded;
	case EPA250BlastRing::Wounded: return EPA250BlastRing::Knockdown;
	case EPA250BlastRing::Knockdown: return EPA250BlastRing::Stagger;
	case EPA250BlastRing::Stagger: return EPA250BlastRing::Flinch;
	default: return EPA250BlastRing::None;
	}
}

/** Pure: blast relative to facing → F/B/L/R. */
inline EPA250BlastDir ReactionDir(const FVector& Facing2D, const FVector& FromPawnToBlast2D)
{
	const FVector F = Facing2D.GetSafeNormal2D();
	const FVector T = FromPawnToBlast2D.GetSafeNormal2D();
	if (F.IsNearlyZero() || T.IsNearlyZero()) return EPA250BlastDir::Front;
	const float Dot = FVector::DotProduct(F, T);
	const float Cross = F.X * T.Y - F.Y * T.X;
	if (FMath::Abs(Dot) >= FMath::Abs(Cross)) return Dot >= 0.f ? EPA250BlastDir::Front : EPA250BlastDir::Back;
	return Cross >= 0.f ? EPA250BlastDir::Left : EPA250BlastDir::Right;
}

/** World apply: hits both sides. Friendly casualties skip personal_kills and opposing Resolve credit. */
PLAYAMERICA250_API FPA250BlastResult ApplyBlast(
	UWorld* World,
	const FVector& Origin,
	EPA250BlastSize Size,
	FName SourceFaction,
	ARFCharacter* Instigator,
	AActor* Causer,
	uint32 Seed = 0,
	const TSet<ARFCharacter*>* SkipMen = nullptr);

/** Works batter radius uses the same outer radius. */
PLAYAMERICA250_API void BatterWorksInRadius(UWorld* World, const FVector& Origin, float RadiusCm, float Damage, FName Faction, APA250BattleGameMode* GM);

/** VFX / audio placeholders (smoke bank + optional Niagara). */
PLAYAMERICA250_API void SpawnBlastFX(UWorld* World, const FVector& Origin, EPA250BlastSize Size);
}
