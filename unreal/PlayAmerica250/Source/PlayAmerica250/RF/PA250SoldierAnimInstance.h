#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BonePose.h"
#include "PA250AnimNode_Locomotion.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PA250SoldierAnimInstance.generated.h"

class UAnimSequence;
class UPoseSearchDatabase;
struct FPA250MountedAim
{
    bool bEnabled = false;
    bool bPistol = false;
    float AimAlpha = 0.f;
    float ReloadAlpha = 0.f;
    float RecoilAlpha = 0.f;
    FVector DirectionCS = FVector::YAxisVector;
};

/** Left hand on the musket stock. Game thread fills it; the anim thread reads its own copy. */
struct FPA250SupportHandTarget
{
	bool bEnabled = false;
	/** Bone (or socket) the musket root is snapped to. */
	FName GripBone = TEXT("hand_r");
	/** Socket offset from GripBone to the musket root (identity when the grip is the bone itself). */
	FTransform GripSocketLocal = FTransform::Identity;
	/** Musket root -> b_gun_LeftHandSpot, in body component space. */
	FVector SpotFromGripCS = FVector::ZeroVector;
	/** Musket up axis in body component space (back of the left hand faces it). */
	FVector GunUpCS = FVector::UpVector;
	/** Elbow pole, relative to the shoulder, in body component space. */
	FVector PoleFromShoulderCS = FVector::ZeroVector;
	/** Hand-bone axis that faced up in the bind pose (the back of the hand). */
	FVector HandBackAxis = FVector::ZeroVector;
	/** Left-hand spot in body component space, used instead of SpotFromGripCS when bSpotAbsolute
	 *  (reloading: the right hand is busy and no longer carries the musket). */
	FVector SpotCS = FVector::ZeroVector;
	bool bSpotAbsolute = false;
	/** Musket forward (butt to muzzle) in body component space. */
	FVector GunForwardCS = FVector::ZeroVector;
	/** Same as HandBackAxis, for hand_r. */
	FVector RightHandBackAxis = FVector::ZeroVector;
};

/**
 * Procedural melee body pose (the packs ship no thrust, charge or hit-reaction clips).
 * Game thread publishes weights only; the bones are written inside pose evaluation.
 */
struct FPA250MeleePose
{
	/** Soldier facing and right-hand side, unit vectors in body component space. */
	FVector ForwardCS = FVector::YAxisVector;
	FVector RightCS = FVector(-1.0, 0.0, 0.0);
	/** 0..1 drawn back before a thrust: weight on the rear foot, right shoulder turned away. */
	float Coil = 0.f;
	/** 0..1 full lunge: front foot out, hips low, arms straight behind the bayonet. */
	float Lunge = 0.f;
	/** 0..1 butt-stroke: the right hand punches up and forward. */
	float Butt = 0.f;
	/** 0..1 charge carriage: body low and forward, musket tucked at the hip. */
	float Level = 0.f;
	/** 0..1 clubbed musket raised over the right shoulder (wind-up, or carried so in a rush). */
	float Raise = 0.f;
	/** 0..1 clubbed musket brought down on him: hands driven forward and down, body thrown in behind it. */
	float Chop = 0.f;
	/** 0..1 reload drill: the right hand leaves the stock and works at ReloadRightHandCS (cartridge box,
	 *  mouth, pan, muzzle, rammer); the left holds the musket. */
	float Reload = 0.f;
	FVector ReloadRightHandCS = FVector::ZeroVector;
	/** 0..1 musket at the shoulder (aiming, presenting, the moment after the shot). */
	float Shoulder = 0.f;
	/** 0..1 guard: musket carried high across the body, weight back, knees soft. */
	float Guard = 0.f;
	/** 0..1 armed and on foot: the right hand holds the musket in front of the right hip, muzzle up and to the
	 *  left, so the left hand reaches it without folding across the chest (the clips are unarmed). */
	/** 0..1 down behind cover: hips lowered KneelDropCm (feet kept on the ground), the right foot
	 *  drawn back KneelRearFootBackCm, the body leant forward KneelLeanDeg (battle_feel.json kneel). */
	float Kneel = 0.f;
	float KneelDropCm = 0.f;
	float KneelRearFootBackCm = 0.f;
	float KneelLeanDeg = 0.f;
	float Carry = 0.f;
	/** Where the right hand holds the carried musket, in cm from the chest (right, up, forward): the
	 *  carry in force (trail, shoulder or charge; battle_feel.json carry), blended by the game thread. */
	float CarryRightCm = 0.f;
	float CarryUpCm = 0.f;
	float CarryForwardCm = 0.f;
	/** How much of the carry is left once every other hold has taken its share. */
	float CarryWeight() const
	{
		return FMath::Clamp(Carry, 0.f, 1.f) * (1.f - FMath::Clamp(Coil + Lunge + Butt + Level + Guard + Raise + Chop + Reload + Shoulder, 0.f, 1.f));
	}
	/** Cm the rear foot trails behind the body while a lunge step carries it forward. */
	float RearFootLagCm = 0.f;
	/** Hit reaction: the way the blow drives the body (component space), length 0..1. */
	FVector FlinchCS = FVector::ZeroVector;

	bool IsActive() const
	{
		return Kneel > 1.e-3f || Coil > 1.e-3f || Lunge > 1.e-3f || Butt > 1.e-3f || Level > 1.e-3f || Guard > 1.e-3f || Carry > 1.e-3f || Reload > 1.e-3f || Shoulder > 1.e-3f || Raise > 1.e-3f || Chop > 1.e-3f || RearFootLagCm > 0.1f || !FlinchCS.IsNearlyZero(1.e-3);
	}
};

/**
 * Anim-thread side of UPA250SoldierAnimInstance. The pack only ships unarmed idle/walk/run,
 * so the left arm is solved here, inside pose evaluation, where the engine expects bone writes.
 * Writing GetComponentSpaceTransforms() from an actor tick raced the parallel evaluation task and
 * crashed skinning (IsRotationNormalized in UpdateRefToLocalMatricesInner).
 */
struct FPA250SoldierAnimProxy : public FAnimInstanceProxy
{
	FPA250SoldierAnimProxy() = default;
	explicit FPA250SoldierAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	/** Pure: the next share of the full-quality pose (0 = cheap path, 1 = motion matched), moving
	 *  toward 1 over BlendSeconds while bWantFull and dropping to 0 at once when not. */
	static float StepTierAlpha(float Alpha, bool bWantFull, float DeltaSeconds, float BlendSeconds);

	/** Pure: two-bone solve onto the musket. Returns false (pose untouched) on any invalid input. */
	static bool SolveSupportHand(FCSPose<FCompactPose>& Pose, const FPA250SupportHandTarget& Target);
	/** Pure: turn the right hand onto the wrist of the stock (palm in, knuckles round it) and close its fingers. */
	static bool GripRightHand(FCSPose<FCompactPose>& Pose, const FPA250SupportHandTarget& Target);
	/** Pure: hips, spine, head, legs and right arm for a thrust / charge / butt-stroke / flinch.
	 *  Feet keep the clip's ground contact (leg IK). Returns false when the skeleton lacks a pelvis. */
	static bool ApplyMountedAim(FCSPose<FCompactPose>& Pose, const FPA250MountedAim& Aim);
	static bool ApplyMeleePose(FCSPose<FCompactPose>& Pose, const FPA250MeleePose& Melee);
	/** Pure: how much of the new clip shows Elapsed seconds into a cross-fade of Seconds (0..1, eased;
	 *  1 at once when Seconds is 0). */
	static float CrossfadeAlpha(float Elapsed, float Seconds);
	/** Pure: one bone between the old clip's pose and the new one's (shortest way round, normalized). */
	static FTransform BlendBone(const FTransform& Old, const FTransform& New, float Alpha);

protected:
	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;
	// The motion-matching nodes, hosted natively (no Animation Blueprint): the engine initializes
	// and updates them as the proxy's root.
	virtual FAnimNode_Base* GetCustomRootNode() override { return &History; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override
	{
		OutNodes.Add(&History);
		OutNodes.Add(&Locomotion);
	}

private:
	const UAnimSequence* Sequence = nullptr;
	double Time = 0.0;
	float Rate = 1.f;
	/** The clip being left, while the cheap path cross-fades (battle_feel.json cheap_crossfade_seconds). */
	const UAnimSequence* FadeFrom = nullptr;
	double FadeFromTime = 0.0;
	float FadeFromRate = 1.f;
	float FadeElapsed = 0.f;
	float FadeSeconds = 0.f;
	/** Full quality (TRIAL): the pose history the search reads, and the motion-matched player under it. */
	FAnimNode_PoseSearchHistoryCollector History;
	FPA250AnimNode_Locomotion Locomotion;
	bool bFull = false;
	/** 0 = the cheap path shows, 1 = the motion-matched pose shows. */
	float TierAlpha = 0.f;
	float TierBlendSeconds = 0.f;
	FPA250SupportHandTarget Hand;
	FPA250MeleePose Melee;
	FPA250MountedAim MountedAim;
};

/** Native body anim for the period soldier: one looping locomotion clip (cross-faded when it changes), the melee pose layer and the support-hand IK. */
UCLASS(Transient, NotBlueprintable)
class PLAYAMERICA250_API UPA250SoldierAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Game thread. Switching clips cross-fades to the new one, entered at the same share of its stride. */
	void SetLocomotion(UAnimSequence* InSequence) { Locomotion = InSequence; }
	UAnimSequence* GetLocomotion() const { return Locomotion; }
	/** Game thread. Scales clip sampling speed from movement (walk/run). */
	void SetPlayRate(float InRate) { PlayRate = FMath::Clamp(InRate, 0.1f, 2.f); }
	float GetPlayRate() const { return PlayRate; }
	/** Game thread. Copied to the proxy at the start of the next animation update. */
	void SetSupportHandTarget(const FPA250SupportHandTarget& InTarget) { SupportHand = InTarget; }
	void ClearSupportHandTarget() { SupportHand.bEnabled = false; }
	const FPA250SupportHandTarget& GetSupportHandTarget() const { return SupportHand; }
	/** Game thread. Melee body pose weights for the next animation update. */
	void SetMountedAim(const FPA250MountedAim& In) { MountedAim = In; }
	const FPA250MountedAim& GetMountedAim() const { return MountedAim; }
	void SetMeleePose(const FPA250MeleePose& InPose) { MeleePose = InPose; }
	const FPA250MeleePose& GetMeleePose() const { return MeleePose; }

	/** Game thread. Full quality: motion matching over these databases (what the gait asks for).
	 *  An empty list, or bFull false, leaves the soldier on the cheap path. */
	void SetFullQuality(bool bInFull, const TArray<UPoseSearchDatabase*>& InDatabases, float InSearchThrottleSeconds);
	bool WantsFullQuality() const { return bFullQuality; }
	const TArray<TObjectPtr<UPoseSearchDatabase>>& GetLocomotionDatabases() const { return LocomotionDatabases; }
	float GetSearchThrottleSeconds() const { return SearchThrottleSeconds; }

	/** What the anim thread did last update (copied back for the probe and the cases). */
	struct FLocomotionReport
	{
		bool bShowingFull = false;   // the motion-matched pose is what is drawn
		bool bHadHistory = false;    // the node found its pose history
		int32 Searches = 0;
		int32 Jumps = 0;
	};
	const FLocomotionReport& GetLocomotionReport() const { return LocomotionReport; }
	void SetLocomotionReport(const FLocomotionReport& In) { LocomotionReport = In; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FPA250SoldierAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FPA250SoldierAnimProxy*>(InProxy); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Locomotion;
	/** Kept alive here while the anim thread searches them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPoseSearchDatabase>> LocomotionDatabases;
	bool bFullQuality = false;
	float SearchThrottleSeconds = 0.f;
	FLocomotionReport LocomotionReport;
	float PlayRate = 1.f;
	FPA250SupportHandTarget SupportHand;
	FPA250MeleePose MeleePose;
	FPA250MountedAim MountedAim;
};
