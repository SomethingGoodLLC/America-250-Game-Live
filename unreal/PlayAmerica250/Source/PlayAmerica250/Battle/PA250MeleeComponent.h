// PA250MeleeComponent.h — bayonet thrust, butt-stroke, bayonet charge (dash). Damage only on authority.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PA250MeleeTuning.h"
#include "PA250MeleeComponent.generated.h"

class ARFCharacter;
class UPA250MeleeTuning;

UENUM(BlueprintType)
enum class EPA250MeleeAction : uint8
{
	None,
	Thrust,
	ButtStroke,
	Charge
};

UCLASS(ClassGroup = (PA250), meta = (BlueprintSpawnableComponent))
class PLAYAMERICA250_API UPA250MeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPA250MeleeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	TObjectPtr<UPA250MeleeTuning> Tuning;

	/** E: bayonet jab after a 0.35 s wind-up, 2.2 m reach. x2 from behind or vs a reloading man. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool DoThrust();

	/** Q: instant short butt-stroke; staggers. Usable mid-reload without losing reload progress. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool DoButtStroke();

	/** G (or Shift+E): dash ~6 m with the bayonet levelled, snapping toward an enemy in front; the first enemy met takes 100. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool DoCharge();

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsBusy() const { return BusyTimer > 0.f || IsCharging(); }

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsCharging() const { return ChargeTimer > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetChargeCooldownRemaining() const { return ChargeCooldown; }

	/** Short HUD line for the last melee outcome ("Charge hit!", "Charge missed"...), empty once faded. */
	FString GetFeedbackText() const { return FeedbackTimer > 0.f ? FeedbackText : FString(); }
	bool WasFeedbackHit() const { return bFeedbackHit; }

	const FPA250MeleeTuningValues& GetValues() const;
	/** Nearest visible enemy within charge-assist range and cone (what a charge would snap to). */
	ARFCharacter* FindChargeTarget() const;

	/** Pure: thrust/charge damage. x BackstabOrReloadMultiplier when the attacker is more than
	 *  BackstabAngleDegrees off the victim's facing, or the victim is reloading. */
	static float ComputeHitDamage(float BaseDamage, bool bAllowBonus, const FVector& VictimForward,
		const FVector& VictimToAttacker, bool bVictimReloading, const FPA250MeleeTuningValues& V);

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;

private:
	/** Sweep ahead of the owner and strike the first enemy. Returns the victim or nullptr. */
	ARFCharacter* SweepAttack(float ReachCm, float RadiusCm, float Damage, bool bAllowBackstabBonus, EPA250MeleeAction Action);
	ARFCharacter* OwnerChar() const;
	void EndCharge(bool bHit);
	void StartJab(float DistanceCm, float Seconds);
	void ApplyJabPose();
	void SetFeedback(const FString& Text, bool bHit);

	float BusyTimer = 0.f;
	float ChargeCooldown = 0.f;
	float PendingThrustTimer = -1.f;
	float ChargeTimer = 0.f;
	uint16 ChargeRootMotionId = 0;

	float JabTimer = 0.f;
	float JabDuration = 0.f;
	float JabDistance = 0.f;
	FVector MusketRestRelative = FVector::ZeroVector;
	bool bHaveMusketRest = false;

	FString FeedbackText;
	float FeedbackTimer = 0.f;
	bool bFeedbackHit = false;
};
