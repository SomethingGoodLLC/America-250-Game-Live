// PA250Cover.h — small cover / visibility queries (no EQS). Traces only, so AI cannot see or hide
// behind what the world's collision does not provide.
#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

namespace PA250Cover
{
// Offsets from the capsule centre (~90 cm above the ground for a 180 cm soldier).
constexpr float ChestOffsetCm = 0.f;    // ~0.9 m: a waist/chest-high fence or wall stops this ray
constexpr float FiringOffsetCm = 55.f;  // ~1.45 m: the musket at the shoulder clears that fence
constexpr float EnemyEyeCm = 70.f;

/** Where a man looks from: 150 cm above his feet, standing or ducked. A man behind a wall or a
 *  bank looks over it; measured from his (lowered) capsule centre a ducked man looked out from
 *  110 cm, under a 120 cm breastwork, saw nobody, never rose to fire and was bayoneted where he
 *  knelt (works matchup, 2026-11-14). */
inline FVector EyePoint(const AActor* Man)
{
	return Man ? Man->GetActorLocation() + FVector(0.f, 0.f, 150.f - Man->GetSimpleCollisionHalfHeight()) : FVector::ZeroVector;
}

/** Line of sight from one eye point to another, ignoring the listed actors. */
inline bool HasLineOfSight(const UWorld* World, const FVector& From, const FVector& To, const TArray<const AActor*>& Ignore)
{
	if (!World) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250CoverLOS), false);
	for (const AActor* A : Ignore) Params.AddIgnoredActor(A);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

/** Is a soldier standing at Point (capsule centre) covered from EnemyPos while still able to fire?
 *  Covered = the enemy's ray to his chest is stopped close in front of him (fence, wall, tree);
 *  can fire = his shoulder-height ray to the enemy is clear. */
inline bool IsFiringPosition(const UWorld* World, const FVector& Point, const FVector& EnemyPos, const TArray<const AActor*>& Ignore,
	float MaxObstacleDistCm = 150.f)
{
	if (!World) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Cover), false);
	for (const AActor* A : Ignore) Params.AddIgnoredActor(A);
	FHitResult Hit;
	const FVector Chest = Point + FVector(0.f, 0.f, ChestOffsetCm);
	const FVector Eye = EnemyPos + FVector(0.f, 0.f, EnemyEyeCm);
	if (!World->LineTraceSingleByChannel(Hit, Eye, Chest, ECC_Visibility, Params)) return false; // chest in the open
	if (FVector::Dist(Hit.ImpactPoint, Chest) > MaxObstacleDistCm) return false;               // blocked far away, not cover
	FHitResult Hit2;
	return !World->LineTraceSingleByChannel(Hit2, Point + FVector(0.f, 0.f, FiringOffsetCm), Eye, ECC_Visibility, Params);
}

/** Is a man whose feet are at Feet, fronting Facing, close behind something he can fire over? The
 *  ray at his chest is stopped within SnapCm; the ray from his shoulder is clear. Heights are taken
 *  from the ground, so it reads the same standing or knelt. */
inline bool IsAtCover(const UWorld* World, const FVector& Feet, const FVector& Facing, const TArray<const AActor*>& Ignore, float SnapCm)
{
	if (!World) return false;
	const FVector Dir = Facing.GetSafeNormal2D();
	if (Dir.IsNearlyZero()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250AtCover), false);
	for (const AActor* A : Ignore) Params.AddIgnoredActor(A);
	const FVector Chest = Feet + FVector(0.f, 0.f, 90.f + ChestOffsetCm);
	const FVector Shoulder = Feet + FVector(0.f, 0.f, 90.f + FiringOffsetCm);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Chest, Chest + Dir * FMath::Max(10.f, SnapCm), ECC_Visibility, Params)) return false;
	FHitResult Hit2;
	return !World->LineTraceSingleByChannel(Hit2, Shoulder, Shoulder + Dir * FMath::Max(10.f, SnapCm) * 2.f, ECC_Visibility, Params);
}

/** Pure: the fire-over stance. Behind cover a man is down while he loads and up while he presents;
 *  he does not kneel on the run. */
inline bool FireOverDown(bool bAtCover, bool bLoading, bool bPresenting, bool bRunning)
{
	return bAtCover && bLoading && !bPresenting && !bRunning;
}

/** Look around Desired (deterministic ring: centre, 8 x 0.5 R, 8 x R) for the closest firing
 *  position against EnemyPos. Points must lie on the navmesh when one exists. */
inline bool FindFiringPosition(const UWorld* World, const FVector& Desired, float SearchRadiusCm, const FVector& EnemyPos,
	const TArray<const AActor*>& Ignore, FVector& OutPos)
{
	if (!World) return false;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World));
	TArray<FVector, TInlineAllocator<17>> Candidates;
	Candidates.Add(Desired);
	for (const float R : {0.5f * SearchRadiusCm, SearchRadiusCm})
	{
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = k * PI / 4.f;
			Candidates.Add(Desired + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.f));
		}
	}
	float Best = TNumericLimits<float>::Max();
	bool bFound = false;
	for (FVector C : Candidates)
	{
		if (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
		{
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(C, Projected, FVector(100.f, 100.f, 300.f))) continue;
			C = Projected.Location + FVector(0.f, 0.f, 90.f); // capsule centre above the nav point
		}
		const float D = FVector::DistSquared2D(C, Desired);
		if (D < Best && IsFiringPosition(World, C, EnemyPos, Ignore))
		{
			Best = D;
			OutPos = C;
			bFound = true;
		}
	}
	return bFound;
}
}
