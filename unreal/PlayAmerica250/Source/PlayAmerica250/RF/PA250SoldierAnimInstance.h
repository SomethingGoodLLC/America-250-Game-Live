#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
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
	static bool SolveSupportHand(FPoseContext& Output, const FPA250SupportHandTarget& Target, FAnimInstanceProxy* Proxy);

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	const UAnimSequence* Sequence = nullptr;
	double Time = 0.0;
	FPA250SupportHandTarget Hand;
};

/** Native body anim for the period soldier: one looping locomotion clip plus the support-hand IK. */
UCLASS(Transient, NotBlueprintable)
class PLAYAMERICA250_API UPA250SoldierAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Game thread. Switching clips restarts the new one from 0 (same as PlayAnimation did). */
	void SetLocomotion(UAnimSequence* InSequence) { Locomotion = InSequence; }
	UAnimSequence* GetLocomotion() const { return Locomotion; }
	/** Game thread. Copied to the proxy at the start of the next animation update. */
	void SetSupportHandTarget(const FPA250SupportHandTarget& InTarget) { SupportHand = InTarget; }
	void ClearSupportHandTarget() { SupportHand.bEnabled = false; }
	const FPA250SupportHandTarget& GetSupportHandTarget() const { return SupportHand; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FPA250SoldierAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FPA250SoldierAnimProxy*>(InProxy); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Locomotion;
	FPA250SupportHandTarget SupportHand;
};
