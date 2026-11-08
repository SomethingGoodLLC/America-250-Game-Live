#include "PA250SoldierAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

void FPA250SoldierAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UPA250SoldierAnimInstance* Inst = CastChecked<UPA250SoldierAnimInstance>(InAnimInstance);
	const UAnimSequence* Wanted = Inst->GetLocomotion();
	if (Wanted != Sequence)
	{
		Sequence = Wanted;
		Time = 0.0;
	}
	Hand = Inst->GetSupportHandTarget();
}

void FPA250SoldierAnimProxy::Update(float DeltaSeconds)
{
	if (!Sequence) return;
	const double Length = Sequence->GetPlayLength();
	Time = Length > UE_KINDA_SMALL_NUMBER ? FMath::Fmod(Time + DeltaSeconds, Length) : 0.0;
}

bool FPA250SoldierAnimProxy::Evaluate(FPoseContext& Output)
{
	if (Sequence)
	{
		FAnimationPoseData PoseData(Output);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time, false, {}, true));
	}
	else
	{
		Output.ResetToRefPose();
	}
	if (Hand.bEnabled)
	{
		SolveSupportHand(Output, Hand, this);
	}
	return true;
}

bool FPA250SoldierAnimProxy::SolveSupportHand(FPoseContext& Output, const FPA250SupportHandTarget& Target, FAnimInstanceProxy* Proxy)
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Compact = [&Bones](FName Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};
	const FCompactPoseBoneIndex Upper = Compact(TEXT("upperarm_l"));
	const FCompactPoseBoneIndex Lower = Compact(TEXT("lowerarm_l"));
	const FCompactPoseBoneIndex HandBone = Compact(TEXT("hand_l"));
	const FCompactPoseBoneIndex Grip = Compact(Target.GripBone);
	// LODs can strip bones; the chain must be intact and direct.
	if (!Upper.IsValid() || !Lower.IsValid() || !HandBone.IsValid() || !Grip.IsValid()) return false;
	if (Bones.GetParentBoneIndex(Lower) != Upper || Bones.GetParentBoneIndex(HandBone) != Lower) return false;

	auto Finite = [](const FVector& V) { return !V.ContainsNaN() && V.GetAbsMax() < 1.e6; };
	if (!Finite(Target.SpotFromGripCS) || !Finite(Target.GunUpCS) || !Finite(Target.PoleFromShoulderCS)) return false;

	FComponentSpacePoseContext CS(Proxy);
	CS.Pose.InitPose(Output.Pose);
	FTransform UpperT = CS.Pose.GetComponentSpaceTransform(Upper);
	FTransform LowerT = CS.Pose.GetComponentSpaceTransform(Lower);
	FTransform HandT = CS.Pose.GetComponentSpaceTransform(HandBone);
	const FTransform GripT = Target.GripSocketLocal * CS.Pose.GetComponentSpaceTransform(Grip);

	// The spot is where the palm sits; the wrist bone is a short way back toward the shoulder.
	const FVector Shoulder = UpperT.GetLocation();
	const FVector Spot = GripT.GetLocation() + Target.SpotFromGripCS;
	const FVector Effector = Spot + (Shoulder - Spot).GetSafeNormal() * 10.f;
	const FVector JointTarget = Shoulder + Target.PoleFromShoulderCS;
	if (!Finite(Shoulder) || !Finite(Effector)) return false;

	AnimationCore::SolveTwoBoneIK(UpperT, LowerT, HandT, JointTarget, Effector, false, 1.0, 1.1);

	// Back of the hand toward the barrel's up side.
	const FVector Along = (HandT.GetLocation() - LowerT.GetLocation()).GetSafeNormal();
	const FVector GunUp = Target.GunUpCS.GetSafeNormal();
	if (!Along.IsNearlyZero() && !GunUp.IsNearlyZero() && !Target.HandBackAxis.IsNearlyZero()
		&& FMath::Abs(FVector::DotProduct(Along, GunUp)) < 0.99f)
	{
		FQuat GripRot = FRotationMatrix::MakeFromXZ(Along, GunUp).ToQuat();
		const FVector Current = FVector::VectorPlaneProject(GripRot.RotateVector(Target.HandBackAxis), Along).GetSafeNormal();
		const FVector Wanted = FVector::VectorPlaneProject(GunUp, Along).GetSafeNormal();
		if (!Current.IsNearlyZero() && !Wanted.IsNearlyZero())
		{
			GripRot = FQuat::FindBetweenNormals(Current, Wanted) * GripRot;
		}
		GripRot.Normalize();
		HandT.SetRotation(GripRot);
	}

	for (FTransform* T : {&UpperT, &LowerT, &HandT})
	{
		T->NormalizeRotation();
		if (T->ContainsNaN() || !T->IsRotationNormalized()) return false;
	}
	// Parent before child: SafeSetCSBoneTransforms requires ascending bone order.
	const FBoneTransform Solved[] = {{Upper, UpperT}, {Lower, LowerT}, {HandBone, HandT}};
	CS.Pose.SafeSetCSBoneTransforms(Solved);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS.Pose), Output.Pose);
	Output.Pose.NormalizeRotations();
	return true;
}
