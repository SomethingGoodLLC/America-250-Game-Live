// PA250MeleeComponent.h — bayonet thrust, butt-stroke, bayonet charge (dash). Damage only on authority.
// The body motion (coil, lunge, flinch) is a procedural pose: this component owns the timing and
// publishes weights; UPA250SoldierAnimInstance writes the bones during pose evaluation.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PA250MeleeTuning.h"
#include "PA250MeleeComponent.generated.h"

class ARFCharacter;
class UPA250MeleeTuning;
struct FPA250MeleePose;

UENUM(BlueprintType)
enum class EPA250MeleeAction : uint8
{
	None,
	Thrust,
	ButtStroke,
	Charge
};

/** What the body is doing right now (visual timeline). */
enum class EPA250MeleeVisual : uint8
{
	None,
	Thrust,       // coil, lunge, hold, recover
	ButtStroke,
	Charge,       // running with the bayonet levelled
	ChargeStrike, // the charge met a man: drive it home, then recover
	Whiff         // the charge met nobody: stumble out of it
};

UCLASS(ClassGroup = (PA250), meta = (BlueprintSpawnableComponent))
class PLAYAMERICA250_API UPA250MeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPA250MeleeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	TObjectPtr<UPA250MeleeTuning> Tuning;

	/** E: bayonet lunge. Turns onto the enemy in front, coils, then steps in; the point lands
	 *  0.35 s after the press, 2.2 m reach. x2 from behind or vs a reloading man. */
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
	/** Nearest visible enemy within thrust-assist range and cone (what a thrust would lunge at). */
	ARFCharacter* FindThrustTarget() const;

	/** Victim side: a blow that did not kill. Rocks the body along WorldPushDir, breaks a thrust
	 *  wind-up, and plants the feet for StaggerSeconds. */
	void ReceiveHit(const FVector& WorldPushDir, float Strength, float StaggerSeconds);
	bool IsFlinching() const { return FlinchTime >= 0.f; }
	/** Freeze this soldier for Seconds of real time (hit-stop). */
	void StartHitStop(float Seconds);

	EPA250MeleeVisual GetVisual() const { return Visual; }
	/** True while the pose layer places the feet (lunge, charge strike, stumble): the body plays the idle clip under it. */
	bool IsPlantingFeet() const;
	/** Current body weights (directions unset; PublishPose fills them). */
	FPA250MeleePose GetBodyPose() const;

	/** Pure: thrust/charge damage. x BackstabOrReloadMultiplier when the attacker is more than
	 *  BackstabAngleDegrees off the victim's facing, or the victim is reloading. */
	static float ComputeHitDamage(float BaseDamage, bool bAllowBonus, const FVector& VictimForward,
		const FVector& VictimToAttacker, bool bVictimReloading, const FPA250MeleeTuningValues& V);

	// Pure pose curves, T = seconds since the action began.
	/** Coil over the first CoilFraction of the wind-up, then the lunge accelerates to full extension
	 *  exactly at Windup (when the hit is resolved), holds, and recovers. */
	static FPA250MeleePose ThrustPose(float T, float Windup, float Hold);
	static FPA250MeleePose ButtPose(float T);
	static FPA250MeleePose ChargePose(float T);
	static FPA250MeleePose StrikePose(float T, float Hold);
	static FPA250MeleePose WhiffPose(float T, float Recovery);
	/** 0..1: snaps to 1 in 0.05 s, then eases out over Duration. */
	static float FlinchWeight(float T, float Duration);

	static constexpr float CoilFraction = 0.55f;
	static constexpr float ThrustRecoverSeconds = 0.3f;
	static constexpr float ButtSeconds = 0.4f;
	static constexpr float StrikeDriveSeconds = 0.06f;
	static constexpr float StrikeRecoverSeconds = 0.35f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;

private:
	/** Sweep ahead of the owner and strike the first enemy. Returns the victim or nullptr. */
	ARFCharacter* SweepAttack(float ReachCm, float RadiusCm, float Damage, bool bAllowBackstabBonus, EPA250MeleeAction Action);
	ARFCharacter* FindAssistTarget(float RangeCm, float ConeDegrees) const;
	ARFCharacter* OwnerChar() const;
	void EndCharge(bool bHit);
	void SetVisual(EPA250MeleeVisual InVisual, float Hold = 0.f);
	void TickVisual(float DeltaTime);
	void TickHitStop();
	/** Turn the soldier (and, when the view steers him, the view) onto Target. */
	void FaceTarget(const ARFCharacter* Target, bool bTurnView);
	/** Root-motion override: carry the soldier DistanceCm along Dir over Seconds (0 cm = hold him still). */
	void ApplyStep(const FVector& Dir, float DistanceCm, float Seconds);
	void StartThrustStep();
	void PublishPose();
	/** Soldiers without the native body anim (mannequin fallback): slide the musket along the facing. */
	void SlideMusket(float OffsetCm);
	void SetFeedback(const FString& Text, bool bHit);

	float BusyTimer = 0.f;
	float ChargeCooldown = 0.f;
	float PendingThrustTimer = -1.f;
	float ChargeTimer = 0.f;
	uint16 ChargeRootMotionId = 0;

	EPA250MeleeVisual Visual = EPA250MeleeVisual::None;
	float VisualTime = 0.f;
	float VisualHold = 0.f;
	bool bStepStarted = false;
	bool bStepping = false;
	FVector StepDir = FVector::ForwardVector;
	FVector LastStepLocation = FVector::ZeroVector;
	float RearFootLag = 0.f;
	TWeakObjectPtr<ARFCharacter> ThrustTarget;
	bool bLastBlowKilled = false;

	float FlinchTime = -1.f;
	float FlinchStrength = 0.f;
	FVector FlinchDirWorld = FVector::ZeroVector;
	double HitStopEndSeconds = 0.0;

	FVector MusketRestRelative = FVector::ZeroVector;
	bool bHaveMusketRest = false;

	FString FeedbackText;
	float FeedbackTimer = 0.f;
	bool bFeedbackHit = false;
};
