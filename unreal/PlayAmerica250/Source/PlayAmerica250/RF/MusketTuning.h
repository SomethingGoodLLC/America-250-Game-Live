// MusketTuning.h — data asset holding the musket numbers (reload time, speed, damage).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MusketTuning.generated.h"

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FMusketTuningValues
{
	GENERATED_BODY()

	/** Seconds to reload. Historical ~20 s for the campaign-scale model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket", meta = (ClampMin = "0.1"))
	float ReloadTime = 20.f;

	/** Projectile muzzle speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	float ProjectileSpeed = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	float Damage = 40.f;
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UMusketTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	FMusketTuningValues Values;
};
