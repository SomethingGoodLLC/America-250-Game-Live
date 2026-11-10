#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BonePose.h"
#include "PA250SoldierAnimInstance.generated.h"

class UAnimSequence;

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
	/** Cm the rear foot trails behind the body while a lunge step carries it forward. */
	float RearFootLagCm = 0.f;
	/** Hit reaction: the way the blow drives the body (component space), length 0..1. */
	FVector FlinchCS = FVector::ZeroVector;

	bool IsActive() const
	{
		return Coil > 1.e-3f || Lunge > 1.e-3f || Butt > 1.e-3f || Level > 1.e-3f || RearFootLagCm > 0.1f || !FlinchCS.IsNearlyZero(1.e-3);
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

	/** Pure: two-bone solve onto the musket. Returns false (pose untouched) on any invalid input. */
	static bool SolveSupportHand(FCSPose<FCompactPose>& Pose, const FPA250SupportHandTarget& Target);
	/** Pure: hips, spine, head, legs and right arm for a thrust / charge / butt-stroke / flinch.
	 *  Feet keep the clip's ground contact (leg IK). Returns false when the skeleton lacks a pelvis. */
	static bool ApplyMeleePose(FCSPose<FCompactPose>& Pose, const FPA250MeleePose& Melee);

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	const UAnimSequence* Sequence = nullptr;
	double Time = 0.0;
	float Rate = 1.f;
	FPA250SupportHandTarget Hand;
	FPA250MeleePose Melee;
};

/** Native body anim for the period soldier: one looping locomotion clip, the melee pose layer and the support-hand IK. */
UCLASS(Transient, NotBlueprintable)
class PLAYAMERICA250_API UPA250SoldierAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Game thread. Switching clips restarts the new one from 0 (same as PlayAnimation did). */
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
	void SetMeleePose(const FPA250MeleePose& InPose) { MeleePose = InPose; }
	const FPA250MeleePose& GetMeleePose() const { return MeleePose; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FPA250SoldierAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FPA250SoldierAnimProxy*>(InProxy); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Locomotion;
	float PlayRate = 1.f;
	FPA250SupportHandTarget SupportHand;
	FPA250MeleePose MeleePose;
};
