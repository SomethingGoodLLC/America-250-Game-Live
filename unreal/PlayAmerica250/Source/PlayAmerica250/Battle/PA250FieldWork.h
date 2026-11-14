// PA250FieldWork.h — earthworks on the battlefield: a breastwork (one bank a man fires over) or a
// redoubt (three banks about a post, open to the rear). The defender's fortification_level stands
// some of them at the start; sappers build and repair them; round shot batters them down.
// GAMEPLAY SETTINGS: every size and strength is battle_rules.json "works".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250FieldWork.generated.h"

class UStaticMeshComponent;

UENUM()
enum class EPA250WorkKind : uint8
{
	Breastwork,
	Redoubt
};

/** Pure rules of a work (checked without a world). */
namespace PA250Works
{
/** How much of its full height a work stands to: what is built of it, less what has been battered away. */
inline float StandingShare(float Built, float Health, float MaxHealth)
{
	return FMath::Clamp(Built, 0.f, 1.f) * (MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f);
}
/** A work stops a ball (and counts as cover) once it stands to CoverShare of its height. */
/** Marked places in the order they are stood: the ones lying most toward the enemy from the post
 *  first (index breaks ties, so the order is the same every time). */
inline TArray<int32> EnemySideFirst(const TArray<FVector>& Places, const FVector& Post, const FVector& TowardEnemy)
{
	TArray<int32> Order;
	TArray<int32> Toward; // hundredths, so near-equal places keep their marked order
	for (int32 i = 0; i < Places.Num(); ++i)
	{
		Order.Add(i);
		Toward.Add(FMath::RoundToInt(100.f * FVector::DotProduct((Places[i] - Post).GetSafeNormal2D(), TowardEnemy.GetSafeNormal2D())));
	}
	Order.StableSort([&Toward](int32 A, int32 B) { return Toward[A] > Toward[B]; });
	return Order;
}
inline bool GivesCover(float Built, float Health, float MaxHealth, float CoverShare)
{
	return StandingShare(Built, Health, MaxHealth) >= CoverShare;
}
/** Works standing at the start for a fortification level (the last entry serves any higher level). */
inline int32 CountForLevel(const TArray<int32>& ByLevel, int32 Level)
{
	if (ByLevel.Num() == 0 || Level <= 0) return 0;
	return FMath::Max(0, ByLevel[FMath::Min(Level, ByLevel.Num() - 1)]);
}
/** Places for breastworks about a post when the level has none marked: an arc of Count places
 *  RingCm out from the post, centred on the way the enemy comes. Out: where each stands and which
 *  way it fronts. The middle of the arc first, then outward to either side. */
PLAYAMERICA250_API void RingSlots(const FVector& Post, const FVector& TowardEnemy, float RingCm, int32 Count, float SpacingCm,
	TArray<FVector>& OutPlaces, TArray<FVector>& OutFronts);
}

UCLASS()
class PLAYAMERICA250_API APA250FieldWork : public AActor
{
	GENERATED_BODY()

public:
	APA250FieldWork();

	/** Stand the work up: its kind, who it belongs to, how much of it is built (0 = a marked place only). */
	void Configure(EPA250WorkKind InKind, FName InFaction, float InBuilt);
	/** A sapper's labour: Share of the whole work. Mends damage first, then raises what is unbuilt. */
	void AddWork(float Share, FName ByFaction);
	/** Round shot and the like. Returns true when this blow brought it below cover. */
	bool Batter(float Damage);

	bool GivesCover() const;
	bool NeedsWork() const { return Built < 1.f || Health < MaxHealth; }
	float GetStandingShare() const { return PA250Works::StandingShare(Built, Health, MaxHealth); }
	EPA250WorkKind GetKind() const { return Kind; }
	FName GetFaction() const { return Faction; }
	float GetBuilt() const { return Built; }
	/** Where a sapper stands to work on it (behind its front). */
	FVector GetWorkPoint() const;

private:
	void Refresh();

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Banks;
	EPA250WorkKind Kind = EPA250WorkKind::Breastwork;
	FName Faction;
	float Built = 1.f;
	float Health = 1.f;
	float MaxHealth = 1.f;
	float FullHeightCm = 120.f;
	bool bWasCover = false;
};
