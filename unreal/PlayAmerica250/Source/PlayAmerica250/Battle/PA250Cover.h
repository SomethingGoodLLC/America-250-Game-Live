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
