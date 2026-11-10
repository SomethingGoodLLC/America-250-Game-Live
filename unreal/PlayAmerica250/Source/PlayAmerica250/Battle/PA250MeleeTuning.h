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

	/** The strike carries the soldier this far toward his man (less if he is already close). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustStepCm = 110.f;

	/** Thrust assist: the lunge turns onto an enemy within this range... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustAssistRangeCm = 380.f;

	/** ...and within this angle of where you face (and in sight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustAssistConeDegrees = 50.f;

	/** The step stops with this much ground between the two men, so the point lands in him, not past him. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustStandoffCm = 185.f;

	/** A man who survives a thrust is rocked back and cannot walk this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrust")
	float ThrustHitStaggerSeconds = 0.35f;

	/** Hit reaction (flinch) length for any melee blow that does not kill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel")
	float FlinchSeconds = 0.6f;

	/** Both men freeze this long (real time) when a blow lands in a fight the player is part of. 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel")
	float HitStopSeconds = 0.07f;

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
