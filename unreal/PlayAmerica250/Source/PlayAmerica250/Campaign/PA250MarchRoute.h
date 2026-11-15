// PA250MarchRoute.h — the planned march as it is drawn on the campaign map: a dashed ink line along
// the road through each town on the route, an arrowhead at its end and an arrival label. Pure layout
// (no UObject), so the commandlet checks it without a window. Presentation only: the route itself is
// the sim's (pa250::march_plan).
#pragma once

#include "CoreMinimal.h"

namespace PA250MarchRoute
{
/** One ink stroke on the map plane (world XY, UU). */
struct FStroke
{
	FVector2D Center = FVector2D::ZeroVector;
	float YawDegrees = 0.f;
	float Length = 0.f;
	/** The stroke lies on the part of the route already marched: it is drawn faded. */
	bool bMarched = false;
};

inline float PathLength(const TArray<FVector2D>& Path)
{
	float Total = 0.f;
	for (int32 i = 0; i + 1 < Path.Num(); ++i)
	{
		Total += FVector2D::Distance(Path[i], Path[i + 1]);
	}
	return Total;
}

/** Point and direction at arc length S along the polyline (clamped to its ends). */
inline void PointAt(const TArray<FVector2D>& Path, float S, FVector2D& OutPoint, FVector2D& OutDir)
{
	OutPoint = Path.Num() > 0 ? Path[0] : FVector2D::ZeroVector;
	OutDir = FVector2D(1.f, 0.f);
	float Left = FMath::Max(0.f, S);
	for (int32 i = 0; i + 1 < Path.Num(); ++i)
	{
		const float Seg = FVector2D::Distance(Path[i], Path[i + 1]);
		if (Seg <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		OutDir = (Path[i + 1] - Path[i]) / Seg;
		if (Left <= Seg || i + 2 == Path.Num())
		{
			OutPoint = Path[i] + OutDir * FMath::Min(Left, Seg);
			return;
		}
		Left -= Seg;
	}
	if (Path.Num() > 0)
	{
		OutPoint = Path.Last();
	}
}

/**
 * Dashes of DashLen separated by GapLen along the whole polyline. A dash is one straight stroke
 * from its start to its end point on the path (so it follows a bend as a chord). Dashes that end
 * within MarchedLen of the start are flagged marched. The last ReserveEnd of the path is left clear
 * for the arrowhead.
 */
inline void LayDashes(const TArray<FVector2D>& Path, float DashLen, float GapLen, float MarchedLen, float ReserveEnd, TArray<FStroke>& Out)
{
	const float Total = PathLength(Path) - FMath::Max(0.f, ReserveEnd);
	if (Path.Num() < 2 || DashLen <= 0.f || Total <= 0.f)
	{
		return;
	}
	const float Step = DashLen + FMath::Max(0.f, GapLen);
	for (float S = 0.f; S < Total; S += Step)
	{
		const float E = FMath::Min(S + DashLen, Total);
		FVector2D A, B, Dir;
		PointAt(Path, S, A, Dir);
		PointAt(Path, E, B, Dir);
		const FVector2D D = B - A;
		const float Len = D.Size();
		if (Len <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		FStroke St;
		St.Center = (A + B) * 0.5f;
		St.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		St.Length = Len;
		St.bMarched = E <= MarchedLen + KINDA_SMALL_NUMBER;
		Out.Add(St);
	}
}

/** Two strokes meeting at the end of the path, each swept back by HalfAngleDegrees: the arrowhead. */
inline void LayArrowhead(const TArray<FVector2D>& Path, float StrokeLen, float HalfAngleDegrees, TArray<FStroke>& Out)
{
	if (Path.Num() < 2 || StrokeLen <= 0.f)
	{
		return;
	}
	FVector2D Tip, Dir;
	PointAt(Path, PathLength(Path), Tip, Dir);
	// Direction of travel a little before the tip, so a last short kink in the road does not turn the head.
	FVector2D Before, Unused;
	PointAt(Path, FMath::Max(0.f, PathLength(Path) - StrokeLen), Before, Unused);
	if (!(Tip - Before).IsNearlyZero())
	{
		Dir = (Tip - Before).GetSafeNormal();
	}
	const float Base = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	for (const float Side : {-1.f, 1.f})
	{
		const float Yaw = Base + 180.f + Side * HalfAngleDegrees;
		const FVector2D Back(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
		FStroke St;
		St.Center = Tip + Back * (StrokeLen * 0.5f);
		St.YawDegrees = Yaw;
		St.Length = StrokeLen;
		Out.Add(St);
	}
}

/** "arrives 22 April, 6 a.m." (the hour as a clock reads it; noon and midnight are named). */
inline FString ArrivalLabel(int32 Day, int32 Month, int32 Hour24)
{
	static const TCHAR* Months[] = {TEXT(""),	 TEXT("January"), TEXT("February"),	 TEXT("March"),	  TEXT("April"),	TEXT("May"),	  TEXT("June"),
									TEXT("July"), TEXT("August"),  TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")};
	const int32 M = FMath::Clamp(Month, 1, 12);
	const int32 H = ((Hour24 % 24) + 24) % 24;
	FString When;
	if (H == 0)
	{
		When = TEXT("midnight");
	}
	else if (H == 12)
	{
		When = TEXT("noon");
	}
	else
	{
		When = FString::Printf(TEXT("%d %s"), H % 12, H < 12 ? TEXT("a.m.") : TEXT("p.m."));
	}
	return FString::Printf(TEXT("arrives %d %s, %s"), Day, Months[M], *When);
}
} // namespace PA250MarchRoute
