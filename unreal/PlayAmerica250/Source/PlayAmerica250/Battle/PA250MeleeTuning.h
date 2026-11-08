// PA250MeleeTuning.h — bayonet thrust, butt-stroke, charge.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PA250MeleeTuning.generated.h"

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250MeleeTuningValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustWindupSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustReachCm = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustRadiusCm = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustDamage = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float BackstabAngleDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float BackstabOrReloadMultiplier = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ButtStroke")
	float ButtStrokeDamage = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ButtStroke")
	float ButtStrokeStaggerSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ButtStroke")
	float ButtStrokeReachCm = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeLungeCm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeDamage = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeCooldownSeconds = 4.f;

	/** The dash covers ChargeLungeCm over this long; the first enemy met on the way is struck. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeDurationSeconds = 0.6f;

	/** How far ahead of the charging soldier the bayonet connects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeHitReachCm = 170.f;

	/** A charge that meets nobody leaves the soldier off balance (no walking) this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeWhiffRecoverySeconds = 0.5f;

	/** A charge hit shoves the victim back this far (cm/s launch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeKnockback = 450.f;

	/** Width of the bayonet sweep during a charge (radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeHitRadiusCm = 70.f;

	/** Charge assist: snap the dash toward an enemy within this range... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeAssistRangeCm = 900.f;

	/** ...and within this angle of where you face (and in sight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeAssistConeDegrees = 40.f;
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UPA250MeleeTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	FPA250MeleeTuningValues Values;

	static UPA250MeleeTuning* MakeRuntimeDefaults();
};
