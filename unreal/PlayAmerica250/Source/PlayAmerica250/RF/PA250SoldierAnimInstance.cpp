#include "PA250SoldierAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

namespace
{
FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, FName Name)
{
	const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
	return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
}

bool Finite(const FVector& V) { return !V.ContainsNaN() && V.GetAbsMax() < 1.e6; }

/** Rotation that tips unit axis From by Degrees toward the perpendicular unit axis Toward. */
FQuat Tip(const FVector& From, const FVector& Toward, float Degrees)
{
	const float Rad = FMath::DegreesToRadians(Degrees);
	return FQuat::FindBetweenNormals(From, (From * FMath::Cos(Rad) + Toward * FMath::Sin(Rad)).GetSafeNormal());
}

/** Every bone write goes through here: unit rotation, finite, parent before child. */
bool SetBone(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Bone, FTransform T)
{
	T.NormalizeRotation();
	if (!Bone.IsValid() || T.ContainsNaN() || !T.IsRotationNormalized() || !Finite(T.GetLocation())) return false;
	const FBoneTransform One[] = {{Bone, T}};
	Pose.SafeSetCSBoneTransforms(One);
	return true;
}

bool RotateBone(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Bone, const FQuat& Delta)
{
	if (!Bone.IsValid()) return false;
	FTransform T = Pose.GetComponentSpaceTransform(Bone);
	T.SetRotation(Delta * T.GetRotation());
	return SetBone(Pose, Bone, T);
}

/** Two-bone IK on a direct Upper > Mid > End chain. bKeepLevel pulls an out-of-reach target in
 *  along the ground instead of lifting it (feet). EndRotation keeps the foot flat / hand as it was. */
bool SolveLimb(FCSPose<FCompactPose>& Pose, FCompactPoseBoneIndex Upper, FCompactPoseBoneIndex Mid, FCompactPoseBoneIndex End,
	FVector Effector, const FVector& Pole, const FQuat* EndRotation, bool bKeepLevel)
{
	const FBoneContainer& Bones = Pose.GetPose().GetBoneContainer();
	if (!Upper.IsValid() || !Mid.IsValid() || !End.IsValid()) return false;
	if (Bones.GetParentBoneIndex(Mid) != Upper || Bones.GetParentBoneIndex(End) != Mid) return false;
	FTransform A = Pose.GetComponentSpaceTransform(Upper);
	FTransform B = Pose.GetComponentSpaceTransform(Mid);
	FTransform C = Pose.GetComponentSpaceTransform(End);
	if (!Finite(Effector) || !Finite(Pole) || !Finite(A.GetLocation())) return false;

	const double Reach = 0.99 * (FVector::Dist(A.GetLocation(), B.GetLocation()) + FVector::Dist(B.GetLocation(), C.GetLocation()));
	if (bKeepLevel && Reach > 1.0)
	{
		const FVector Root = A.GetLocation();
		const double Dz = Effector.Z - Root.Z;
		const double MaxFlat = FMath::Sqrt(FMath::Max(0.0, Reach * Reach - Dz * Dz));
		const FVector Flat(Effector.X - Root.X, Effector.Y - Root.Y, 0.0);
		if (Flat.Size() > MaxFlat)
		{
			const FVector In = Flat.GetSafeNormal() * MaxFlat;
			Effector = FVector(Root.X + In.X, Root.Y + In.Y, Effector.Z);
		}
	}
	AnimationCore::SolveTwoBoneIK(A, B, C, Pole, Effector, false, 1.0, 1.1);
	if (EndRotation) C.SetRotation(*EndRotation);
	for (FTransform* T : {&A, &B, &C})
	{
		T->NormalizeRotation();
		if (T->ContainsNaN() || !T->IsRotationNormalized()) return false;
	}
	const FBoneTransform Solved[] = {{Upper, A}, {Mid, B}, {End, C}};
	Pose.SafeSetCSBoneTransforms(Solved);
	return true;
}
}

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
	Rate = Inst->GetPlayRate();
	Hand = Inst->GetSupportHandTarget();
	Melee = Inst->GetMeleePose();
}

void FPA250SoldierAnimProxy::Update(float DeltaSeconds)
{
	if (!Sequence) return;
	const double Length = Sequence->GetPlayLength();
	Time = Length > UE_KINDA_SMALL_NUMBER ? FMath::Fmod(Time + DeltaSeconds * Rate, Length) : 0.0;
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
	const bool bMelee = Melee.IsActive();
	if (bMelee || Hand.bEnabled)
	{
		// One trip through component space: body first, then the left hand onto the moved musket.
		FComponentSpacePoseContext CS(this);
		CS.Pose.InitPose(Output.Pose);
		if (bMelee) ApplyMeleePose(CS.Pose, Melee);
		if (Hand.bEnabled) SolveSupportHand(CS.Pose, Hand);
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS.Pose), Output.Pose);
		Output.Pose.NormalizeRotations();
	}
	return true;
}

bool FPA250SoldierAnimProxy::ApplyMeleePose(FCSPose<FCompactPose>& Pose, const FPA250MeleePose& Melee)
{
	const FVector F = FVector(Melee.ForwardCS.X, Melee.ForwardCS.Y, 0.0).GetSafeNormal();
	const FVector R = FVector(Melee.RightCS.X, Melee.RightCS.Y, 0.0).GetSafeNormal();
	const FVector U = FVector::UpVector;
	if (F.IsNearlyZero() || R.IsNearlyZero() || FMath::Abs(FVector::DotProduct(F, R)) > 0.1 || !Finite(Melee.FlinchCS)) return false;
	auto Unit = [](float V) { return FMath::IsFinite(V) ? FMath::Clamp(V, 0.f, 1.f) : 0.f; };
	const float Coil = Unit(Melee.Coil), Lunge = Unit(Melee.Lunge), Butt = Unit(Melee.Butt), Level = Unit(Melee.Level);
	const float Lag = FMath::IsFinite(Melee.RearFootLagCm) ? FMath::Clamp(Melee.RearFootLagCm, 0.f, 60.f) : 0.f;
	const FVector FlinchFlat(Melee.FlinchCS.X, Melee.FlinchCS.Y, 0.0);
	const float Flinch = FMath::Min(1.f, static_cast<float>(FlinchFlat.Size()));
	const FVector FlinchDir = FlinchFlat.GetSafeNormal();

	const FBoneContainer& Bones = Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex Pelvis = FindBone(Bones, TEXT("pelvis"));
	if (!Pelvis.IsValid()) return false;

	// Where the clip put the feet and knees, before the hips move.
	struct FLeg { FCompactPoseBoneIndex Thigh, Calf, Foot; FTransform Foot0; FVector Knee0; bool bOK; };
	auto ReadLeg = [&](const TCHAR* Thigh, const TCHAR* Calf, const TCHAR* Foot)
	{
		FLeg L{FindBone(Bones, Thigh), FindBone(Bones, Calf), FindBone(Bones, Foot), FTransform::Identity, FVector::ZeroVector, false};
		L.bOK = L.Thigh.IsValid() && L.Calf.IsValid() && L.Foot.IsValid();
		if (L.bOK)
		{
			L.Foot0 = Pose.GetComponentSpaceTransform(L.Foot);
			L.Knee0 = Pose.GetComponentSpaceTransform(L.Calf).GetLocation();
		}
		return L;
	};
	const FLeg Front = ReadLeg(TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l")); // left foot leads a right-handed thrust
	const FLeg Rear = ReadLeg(TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"));

	// Hips: back and up on the coil, forward and low on the lunge, shoved by a blow.
	const FVector Shift = F * (-10.f * Coil + 20.f * Lunge + 8.f * Butt + 6.f * Level) + FlinchDir * (14.f * Flinch)
		- U * (4.f * Coil + 15.f * Lunge + 4.f * Butt + 9.f * Level + 6.f * Flinch);
	const float Turn = 20.f * Coil - 26.f * Lunge - 8.f * Butt;   // + = right shoulder drawn back
	const float Lean = -5.f * Coil + 28.f * Lunge + 10.f * Butt + 20.f * Level; // forward
	{
		FTransform T = Pose.GetComponentSpaceTransform(Pelvis);
		T.AddToTranslation(Shift);
		T.SetRotation(Tip(F, R, 0.3f * Turn) * T.GetRotation());
		if (!SetBone(Pose, Pelvis, T)) return false;
	}

	// Spine carries the rest of the turn and the lean; a blow folds it the way it was struck.
	FQuat Third = Tip(F, R, 0.7f * Turn / 3.f) * Tip(U, F, Lean / 3.f);
	if (Flinch > 0.f) Third = Tip(U, FlinchDir, 26.f * Flinch / 3.f) * Third;
	FCompactPoseBoneIndex Chest(INDEX_NONE);
	for (const TCHAR* Name : {TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03")})
	{
		const FCompactPoseBoneIndex Bone = FindBone(Bones, Name);
		if (RotateBone(Pose, Bone, Third)) Chest = Bone;
	}
	// Eyes stay on the enemy through the lean; the head rocks with a blow.
	{
		FQuat Head = Tip(U, F, -0.7f * Lean) * Tip(F, R, -0.8f * Turn);
		if (Flinch > 0.f) Head = Tip(U, FlinchDir, 16.f * Flinch) * Head;
		RotateBone(Pose, FindBone(Bones, TEXT("neck_01")), Head);
	}

	// Legs: the front foot steps out (and lifts mid-step), the rear foot trails and stays down.
	if (Front.bOK)
	{
		const FVector Step = F * (42.f * Lunge - 4.f * Coil) + U * (7.f * FMath::Sin(UE_PI * Lunge));
		const FQuat Flat = Front.Foot0.GetRotation();
		SolveLimb(Pose, Front.Thigh, Front.Calf, Front.Foot, Front.Foot0.GetLocation() + Step, Front.Knee0 + Shift + Step + F * 60.f, &Flat, true);
	}
	if (Rear.bOK)
	{
		const FQuat Flat = Rear.Foot0.GetRotation();
		SolveLimb(Pose, Rear.Thigh, Rear.Calf, Rear.Foot, Rear.Foot0.GetLocation() - F * Lag, Rear.Knee0 + Shift + F * 50.f + R * 15.f, &Flat, true);
	}

	// Right hand drives the musket (it is attached to hand_r); the left is solved onto it afterwards.
	const FCompactPoseBoneIndex UpperArm = FindBone(Bones, TEXT("upperarm_r"));
	const FCompactPoseBoneIndex HandBone = FindBone(Bones, TEXT("hand_r"));
	if (UpperArm.IsValid() && HandBone.IsValid())
	{
		const FVector Shoulder = Pose.GetComponentSpaceTransform(UpperArm).GetLocation();
		FVector Base = Pose.GetComponentSpaceTransform(HandBone).GetLocation();
		if (Chest.IsValid())
		{
			// Charging, the hand is pinned at the right hip instead of swinging with the run clip.
			const FVector Tucked = Pose.GetComponentSpaceTransform(Chest).GetLocation() + R * 24.f - U * 26.f + F * 10.f;
			Base = FMath::Lerp(Base, Tucked, Level);
		}
		const FVector Target = Base
			+ Coil * (-F * 22.f + U * 10.f + R * 4.f)
			+ Lunge * (F * 40.f + U * 22.f - R * 14.f)
			+ Butt * (F * 30.f + U * 38.f - R * 10.f);
		SolveLimb(Pose, UpperArm, FindBone(Bones, TEXT("lowerarm_r")), HandBone, Target, Shoulder - F * 30.f + R * 20.f - U * 25.f, nullptr, false);
	}
	return true;
}

bool FPA250SoldierAnimProxy::SolveSupportHand(FCSPose<FCompactPose>& Pose, const FPA250SupportHandTarget& Target)
{
	const FBoneContainer& Bones = Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex Upper = FindBone(Bones, TEXT("upperarm_l"));
	const FCompactPoseBoneIndex Lower = FindBone(Bones, TEXT("lowerarm_l"));
	const FCompactPoseBoneIndex HandBone = FindBone(Bones, TEXT("hand_l"));
	const FCompactPoseBoneIndex Grip = FindBone(Bones, Target.GripBone);
	// LODs can strip bones; the chain must be intact and direct.
	if (!Upper.IsValid() || !Lower.IsValid() || !HandBone.IsValid() || !Grip.IsValid()) return false;
	if (Bones.GetParentBoneIndex(Lower) != Upper || Bones.GetParentBoneIndex(HandBone) != Lower) return false;

	if (!Finite(Target.SpotFromGripCS) || !Finite(Target.GunUpCS) || !Finite(Target.PoleFromShoulderCS)) return false;

	FTransform UpperT = Pose.GetComponentSpaceTransform(Upper);
	FTransform LowerT = Pose.GetComponentSpaceTransform(Lower);
	FTransform HandT = Pose.GetComponentSpaceTransform(HandBone);
	const FTransform GripT = Target.GripSocketLocal * Pose.GetComponentSpaceTransform(Grip);
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
	Pose.SafeSetCSBoneTransforms(Solved);
	return true;
}
