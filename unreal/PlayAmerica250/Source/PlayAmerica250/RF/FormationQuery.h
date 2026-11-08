// FormationQuery.h — plain helpers for line detection and volley windows (no UObject).
#pragma once

#include "CoreMinimal.h"

struct FFormationMember
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	int32 Index = INDEX_NONE;
};

struct FFormationTuningPlain
{
	float LineRadius = 450.f;
	float LineFacingDegrees = 35.f;
	int32 LineMinSize = 3;
	float LineAccuracyBonus = 0.2f;
};

struct FVolleyTuningPlain
{
	float VolleyWindowSeconds = 0.85f;
	int32 VolleyMinShooters = 3;
};

struct FVolleyShot
{
	float Time = 0.f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	bool bHit = false;
};

struct FVolleyResult
{
	bool bValid = false;
	int32 Shooters = 0;
	int32 Hits = 0;
};

inline int32 CountLinemates(const TArray<FFormationMember>& Members, int32 SelfIndex, const FFormationTuningPlain& T)
{
	if (!Members.IsValidIndex(SelfIndex)) return 1;
	const FFormationMember& Self = Members[SelfIndex];
	const float CosMin = FMath::Cos(FMath::DegreesToRadians(T.LineFacingDegrees));
	int32 Count = 1;
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		if (i == SelfIndex) continue;
		const FFormationMember& O = Members[i];
		if (FVector::Dist2D(Self.Location, O.Location) > T.LineRadius) continue;
		if (FVector::DotProduct(Self.Forward.GetSafeNormal2D(), O.Forward.GetSafeNormal2D()) < CosMin) continue;
		++Count;
	}
	return Count;
}

inline float LineSpreadScale(int32 LineSize, const FFormationTuningPlain& T)
{
	if (LineSize < T.LineMinSize) return 1.f;
	return FMath::Max(0.05f, 1.f - T.LineAccuracyBonus);
}

inline FVolleyResult EvaluateVolley(const TArray<FVolleyShot>& Shots, float Now, const FVolleyTuningPlain& T)
{
	FVolleyResult R;
	TArray<int32> InWindow;
	for (int32 i = 0; i < Shots.Num(); ++i)
	{
		if (Now - Shots[i].Time <= T.VolleyWindowSeconds) InWindow.Add(i);
	}
	R.Shooters = InWindow.Num();
	if (R.Shooters < T.VolleyMinShooters) return R;
	// Shared cone: majority forward of the windowed shots.
	FVector AvgDir = FVector::ZeroVector;
	for (int32 i : InWindow) AvgDir += Shots[i].Direction.GetSafeNormal();
	AvgDir = AvgDir.GetSafeNormal();
	if (AvgDir.IsNearlyZero()) return R;
	R.bValid = true;
	for (int32 i : InWindow)
	{
		if (Shots[i].bHit) ++R.Hits;
	}
	return R;
}
