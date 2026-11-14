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
	Charge,
	Club
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

/** How a guard met a blow. */
enum class EPA250Guard : uint8 { None, Block, Parry };

/** Outcome of the last swing. */
enum class EPA250Blow : uint8 { Miss, Hit, Kill, Blocked, Parried };

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

	/** G (or Shift+E): dash ~6 m with the bayonet levelled, snapping toward an enemy in front; the first enemy met takes 70. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool DoCharge();

	/** Most Patriots of 1775 had no bayonet. Without one, E swings the clubbed musket (gripped by the
	 *  barrel, butt brought down on him) and G is a rush that ends in the same blow. */
	bool HasBayonet() const;
	/** Pure: British regulars carry bayonets; Patriot militia do not; one Continental line soldier in four does (by his seed). */
	static bool IssuedBayonet(FName Faction, FName UnitClass, uint32 Seed);
	/** Tests / probe: force the answer. */
	void SetBayonetOverride(bool bHas) { BayonetOverride = bHas ? 1 : 0; }
	/** Class kit: multiplies every blow this man lands (a grenadier hits harder, a rifleman lighter). */
	float DamageScale = 1.f;
	bool IsClubSwing() const { return bClubSwing; }
	static FPA250MeleePose ClubPose(float T, float Windup, float Hold);

	/** Bring the musket to the shoulder for Seconds (bots presenting; the player's aim and every shot do it themselves). */
	void Present(float Seconds) { PresentTimer = FMath::Max(PresentTimer, Seconds); }

	/** Player thrust: if he is still busy the press is remembered for 0.3 s and thrown as soon as he is free. */
	void RequestThrust();

	/** X (held): musket up across the body. Frontal thrusts are blocked (a little gets through) and a
	 *  charge is blunted. Raised just before the blow lands it is a parry: no damage, the attacker is
	 *  thrown off balance, and the next thrust is a riposte. A butt-stroke goes through a guard. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void SetGuardHeld(bool bHeld) { bGuardHeld = bHeld; }
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsGuarding() const { return bGuarding; }
	/** What this soldier's guard does to a blow coming from AttackerLocation right now. */
	EPA250Guard ResolveGuard(const FVector& AttackerLocation) const;
	/** An enemy has launched a thrust or charge at this soldier that lands in Seconds (HUD warning; bots may guard). */
	void NotifyIncoming(float Seconds);
	bool IsThreatened() const { return ThreatTimer > 0.f; }
	bool CanRiposte() const { return RiposteTimer > 0.f; }
	EPA250Blow GetLastBlow() const { return LastBlow; }
	/** Pure: attacker is more than BackstabAngleDegrees off the victim's facing. */
	static bool IsBehind(const FVector& VictimForward, const FVector& VictimToAttacker, const FPA250MeleeTuningValues& V);

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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	/** Sweep ahead of the owner and strike the first enemy. Returns the victim or nullptr. */
	ARFCharacter* SweepAttack(float ReachCm, float RadiusCm, float Damage, bool bAllowBackstabBonus, EPA250MeleeAction Action, const FVector* SweepFrom = nullptr);
	void TickGuard(float DeltaTime);
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
	/** AbsoluteWorld (when given) pins the musket in the world instead of hanging it off the hand. */
	void OffsetMusket(const FVector& WorldOffset, const FVector* AbsoluteWorld = nullptr);
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
	float StepRemaining = 0.f;
	FVector StepDir = FVector::ForwardVector;
	FVector LastStepLocation = FVector::ZeroVector;
	float RearFootLag = 0.f;
	TWeakObjectPtr<ARFCharacter> ThrustTarget;
	TWeakObjectPtr<ARFCharacter> ChargeVictim;
	bool bLastBlowKilled = false;

	/** Reload drill pose: weight, which anchor (0 = priming at the waist, 1 = cast about, butt grounded), and
	 *  the last drill phase / progress seen (held through a fumble or a jam). */
	float ReloadWeight = 0.f;
	float RamBlend = 0.f;
	/** Visual only: the drill pose is shown below this ground speed (cm/s); faster, the musket stays at the carry. */
	static constexpr float ReloadDrillMaxSpeed = 60.f;
	/** Visual only: how far the carried musket points up and to the left of the aim (degrees). */
	static constexpr float CarryPitchDegrees = 22.f;
	static constexpr float CarryYawDegrees = -20.f;
	uint8 ReloadPhase = 0;
	float ReloadProgress = 0.f;
	void TickReloadPose(float DeltaTime);
	/** Where the right hand is in the drill, in soldier space (forward, right, up from the feet). */
	static FVector ReloadHandLocal(uint8 Phase, float Progress, const FVector& Root, const FVector& GunForward, float MuzzleCm, float RamrodCm = -1.f);
	float PresentTimer = 0.f;
	float ShoulderWeight = 0.f;
	bool bMusketWasLoaded = false;
	double SlowMoEndSeconds = 0.0;
	int8 BayonetOverride = -1;
	bool bClubSwing = false;
	bool bClubCharge = false;
	bool bGuardHeld = false;
	bool bGuarding = false;
	float GuardAge = 0.f;
	float GuardWeight = 0.f;
	float GuardLockout = 0.f;
	float AutoGuardTimer = 0.f;
	float ThreatTimer = 0.f;
	float RiposteTimer = 0.f;
	bool bRiposteThrust = false;
	/** Wind-up of the thrust in progress (longer for a bot attacking the player, shorter for a riposte). */
	float ActiveWindup = 0.35f;
	float BufferedThrustTimer = 0.f;
	EPA250Blow LastBlow = EPA250Blow::Miss;
	FVector LastChargeLocation = FVector::ZeroVector;

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
