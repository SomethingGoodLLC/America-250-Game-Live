// MusketTuning.h — battle tuning data asset (musket, conquest, active reload, accuracy, volley, bots).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MusketTuning.generated.h"

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FMusketCoreTuning
{
	GENERATED_BODY()

	/** Sweet-path reload total (sum of active-reload phases). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket", meta = (ClampMin = "0.1"))
	float ReloadTime = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	float ProjectileSpeed = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	float Damage = 40.f;
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FConquestTuning
{
	GENERATED_BODY()

	/** Base seconds of uncontested sole presence to advance one capture step (to neutral or owned). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	float CaptureSeconds = 3.5f;

	/** Extra capture speed per additional attacker beyond the first (1 + (n-1)*scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	float CapturePerAttackerScale = 0.35f;

	/** Seconds between ticket bleeds for the side holding fewer posts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	float BleedIntervalSeconds = 12.f;

	/** Ticket exhaustion cannot end the battle before this many seconds (default ~2 minutes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest", meta = (ClampMin = "0"))
	float MinSecondsBeforeExhaustion = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	float PostRadius = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	float TimeLimitSeconds = 720.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	int32 BotsPerSide = 16;

	/** GAME INVENTION: battle respawns bought by one campaign unit. Losses are converted back to
	 *  campaign units (rounded, capped at the pool) when the BattleResult is written. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest", meta = (ClampMin = "1"))
	int32 SoldiersPerUnit = 4;
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FActiveReloadTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float PhasePrimePan = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float PhaseRamCharge = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float PhaseShoulderAim = 2.f;

	/** Fraction of phase length that is the sweet zone, centered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float SweetZoneFraction = 0.35f;

	/** Fraction of phase length that is the perfect zone, centered inside sweet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float PerfectZoneFraction = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float PerfectReloadScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float MissDelaySeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float JamChanceOnMiss = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float JamClearHoldSeconds = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float PerfectDamageBonus = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float ReloadVulnerability = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float LineInfantryReloadSkill = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	float MilitiaReloadSkill = 0.45f;
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FAccuracyTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float BaseSpreadDegrees = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float AimingSpreadScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float StillSpreadScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float MovingSpreadScale = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float MoveSpeedThreshold = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float LineRadius = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float LineFacingDegrees = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	int32 LineMinSize = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	float LineAccuracyBonus = 0.2f;
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FVolleyTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	int32 SquadMinSize = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	int32 SquadMaxSize = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	float VolleyWindowSeconds = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	int32 VolleyMinShooters = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	float SuppressionSeconds = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	float SuppressionSwayDegrees = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	float SuppressionCaptureScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	float MakeReadySeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	float PresentSeconds = 0.9f;
};

/** Legacy flat struct kept for weapon DefaultTuning compatibility; mirrors core musket fields. */
USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FMusketTuningValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket", meta = (ClampMin = "0.1"))
	float ReloadTime = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	float ProjectileSpeed = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	float Damage = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conquest")
	FConquestTuning Conquest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ActiveReload")
	FActiveReloadTuning ActiveReload;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy")
	FAccuracyTuning Accuracy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volley")
	FVolleyTuning Volley;
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UMusketTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Musket")
	FMusketTuningValues Values;
};
