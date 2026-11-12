// PA250Smoke.h — lingering, drifting black-powder smoke (visual only, never affects the battle).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PA250Smoke.generated.h"

class AActor;
class UProceduralMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

/** One smoke puff. Pure data so its motion can be tested without a renderer. */
struct FPA250SmokePuff
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	float Age = 0.f;
	float Life = 25.f;
	float Size0 = 60.f;
	float Size1 = 450.f;
	float Peak = 0.5f;
	float Spin = 0.f;
	/** Settled colour. Powder smoke by default; dust and blood use their own. */
	FLinearColor Tint = FLinearColor(0.80f, 0.79f, 0.76f);
	/** Starts with the glow of burning powder (muzzle and pan), not for dust. */
	bool bPowder = true;
	/** Vertical drift, cm/s: smoke rises a little, thrown dirt falls back. */
	float Rise = 10.f;
	/** How much of the breeze it takes (heavy dust takes less). */
	float WindShare = 1.f;

	/** Pure: advance by Dt under Wind (cm/s). The muzzle jet decays fast; then the puff drifts and rises. */
	void Step(float Dt, const FVector& Wind);
	float Size() const;     // current diameter (cm): swells quickly, then keeps spreading
	float Opacity() const;  // flashes in, then thins out to nothing at Life
	bool IsDead() const { return Age >= Life; }
};

/** Owns every puff in a game world and draws them all as one camera-facing quad mesh. */
UCLASS()
class PLAYAMERICA250_API UPA250SmokeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A musket shot: three puffs blown out along Direction from the muzzle. */
	void AddShot(const FVector& Muzzle, const FVector& Direction);

	/** The priming pan beside the lock: a small puff thrown up and to the right. With bFlashOnly
	 *  (a misfire) that puff and a weak flash are all there is. */
	void AddPanFlash(const FVector& Lock, const FVector& Direction, bool bFlashOnly);
	/** Where a ball struck: dirt thrown up from the ground or a wall, or a brief red mist on a man. */
	void AddImpact(const FVector& Where, const FVector& Normal, bool bHitMan);

	int32 NumPuffs() const { return Puffs.Num(); }
	const TArray<FPA250SmokePuff>& GetPuffs() const { return Puffs; }
	static constexpr int32 MaxPuffs = 300;
	/** The shared battlefield breeze (PA250Wind.h): smoke drifts the way the flags fly. */
	static FVector Wind();
	/** Muzzle flash: a brief orange light at the muzzle, also lighting the new smoke. */
	static constexpr float FlashSeconds = 0.09f;
	static constexpr int32 MaxFlashes = 12;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void EnsureMesh();
	void RebuildMesh();
	/** Fold lingering puffs that occupy the same air, so a long firefight stays under the cap. */
	void MergeAged();

	void Flash(const FVector& Where, float Strength = 1.f);
	FPA250SmokePuff& NewPuff();
	TArray<float> FlashStrength;
	void TickFlashes(float DeltaTime);

	TArray<FPA250SmokePuff> Puffs;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Flashes;
	TArray<float> FlashAge;
	int32 NextFlash = 0;
	UPROPERTY(Transient)
	TObjectPtr<AActor> Host;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Mesh;
	bool bSectionCreated = false;
	int32 SectionQuads = 0;
	/** A new shot rebuilds immediately; drift updates on the frames between. */
	bool bForceRebuild = false;
	int32 MeshFrame = 0;
	float MergeWait = 0.f;
	FRandomStream Rng{1775};
};
