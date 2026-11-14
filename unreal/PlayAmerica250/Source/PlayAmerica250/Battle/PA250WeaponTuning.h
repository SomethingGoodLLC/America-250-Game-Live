// PA250WeaponTuning.h — musket spread, falloff, recoil, Arcade/Authentic reload presets.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PA250WeaponTuning.generated.h"

UENUM(BlueprintType)
enum class EPA250ReloadPreset : uint8
{
	Arcade UMETA(DisplayName = "Arcade"),
	Authentic UMETA(DisplayName = "Authentic")
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250ReloadPresetValues
{
	GENERATED_BODY()

	/** Total base reload seconds (sweet-path sum of phases). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float BaseSeconds = 4.f;

	/** Perfect-path total seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float PerfectSeconds = 2.5f;

	/** Failed-path total seconds (miss stretches). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float FailedSeconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float PerfectDamageBonus = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float JamChanceOnMiss = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	float JamClearSeconds = 1.5f;
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250WeaponTuningValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float HipSpreadDegrees = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float AdsSpreadDegrees = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float MovingSpreadAddDegrees = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float MoveSpeedThreshold = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float TorsoDamageNear = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float FalloffStartMeters = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float TorsoDamageFar = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float FalloffEndMeters = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	/** A ball in the head kills any man at any range (2.5 x the far torso damage clears 120 health). */
	float HeadMultiplier = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float MaxRangeMeters = 200.f;

	/** cm/s. 300 m/s: fast enough that the ball lands on the reticle with only a slight, compensated drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics")
	float ProjectileSpeed = 30000.f;

	/** Firing plants the shooter's feet: no walking for this long after the shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel")
	float FireStillSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel")
	float RecoilPitchDegrees = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel")
	float RecoilYawDegrees = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel")
	float RecoilRecoverySeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	FPA250ReloadPresetValues Arcade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	FPA250ReloadPresetValues Authentic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload")
	EPA250ReloadPreset DefaultPreset = EPA250ReloadPreset::Authentic;

	float DamageAtDistanceCm(float DistanceCm) const
	{
		const float M = DistanceCm * 0.01f;
		if (M <= FalloffStartMeters) return TorsoDamageNear;
		if (M >= MaxRangeMeters) return 0.f;
		if (M >= FalloffEndMeters) return TorsoDamageFar;
		const float Alpha = (M - FalloffStartMeters) / FMath::Max(0.01f, FalloffEndMeters - FalloffStartMeters);
		return FMath::Lerp(TorsoDamageNear, TorsoDamageFar, Alpha);
	}

	float CurrentSpreadDegrees(bool bAiming, float OwnerSpeed) const
	{
		float Spread = bAiming ? AdsSpreadDegrees : HipSpreadDegrees;
		if (OwnerSpeed >= MoveSpeedThreshold) Spread += MovingSpreadAddDegrees;
		return Spread;
	}

	const FPA250ReloadPresetValues& Preset(EPA250ReloadPreset P) const
	{
		return P == EPA250ReloadPreset::Authentic ? Authentic : Arcade;
	}
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UPA250WeaponTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Brief values live here (not only in MakeRuntimeDefaults): DA_WeaponTuning is created from the CDO. */
	UPA250WeaponTuning();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FPA250WeaponTuningValues Values;

	static UPA250WeaponTuning* MakeRuntimeDefaults();
};
