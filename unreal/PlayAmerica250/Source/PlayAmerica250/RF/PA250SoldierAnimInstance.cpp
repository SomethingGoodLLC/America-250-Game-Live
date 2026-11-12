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

/** Roll the hand about the forearm line so the back of the hand (BackAxis, bone space) faces WantedBack. */
void AlignHandBack(FTransform& HandT, const FVector& Along, const FVector& WantedBack, const FVector& BackAxis)
{
	if (Along.IsNearlyZero() || WantedBack.IsNearlyZero() || BackAxis.IsNearlyZero()) return;
	const FVector Wanted = FVector::VectorPlaneProject(WantedBack.GetSafeNormal(), Along).GetSafeNormal();
	if (Wanted.IsNearlyZero()) return;
	FQuat GripRot = FRotationMatrix::MakeFromXZ(Along, Wanted).ToQuat();
	const FVector Current = FVector::VectorPlaneProject(GripRot.RotateVector(BackAxis), Along).GetSafeNormal();
	if (!Current.IsNearlyZero()) GripRot = FQuat::FindBetweenNormals(Current, Wanted) * GripRot;
	GripRot.Normalize();
	HandT.SetRotation(GripRot);
}

/** Close a hand round what it holds. Works from geometry (each joint bends about the axis across the
 *  finger, toward the palm side), so it does not depend on how the skeleton's finger bones are oriented. */
void CurlFingers(FCSPose<FCompactPose>& Pose, const TCHAR* Side, const FVector& BackOfHandCS)
{
	const FBoneContainer& Bones = Pose.GetPose().GetBoneContainer();
	const FVector Back = BackOfHandCS.GetSafeNormal();
	if (Back.IsNearlyZero() || !Finite(Back)) return;
	struct FFinger { const TCHAR* Name; float Degrees[3]; };
	for (const FFinger& Finger : {FFinger{TEXT("index"), {48.f, 70.f, 40.f}}, FFinger{TEXT("middle"), {52.f, 74.f, 42.f}},
		FFinger{TEXT("ring"), {56.f, 76.f, 44.f}}, FFinger{TEXT("pinky"), {60.f, 78.f, 44.f}}, FFinger{TEXT("thumb"), {14.f, 26.f, 30.f}}})
	{
		FCompactPoseBoneIndex Joint[3] = {FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE)};
		bool bOK = true;
		for (int32 i = 0; i < 3; ++i)
		{
			Joint[i] = FindBone(Bones, *FString::Printf(TEXT("%s_0%d_%s"), Finger.Name, i + 1, Side));
			bOK &= Joint[i].IsValid();
		}
		if (!bOK) continue;
		for (int32 i = 0; i < 3; ++i)
		{
			// Direction of this segment: to the next joint (the last one carries on from the one before).
			const FVector Here = Pose.GetComponentSpaceTransform(Joint[i]).GetLocation();
			const FVector Dir = i < 2 ? (Pose.GetComponentSpaceTransform(Joint[i + 1]).GetLocation() - Here).GetSafeNormal()
				: (Here - Pose.GetComponentSpaceTransform(Joint[1]).GetLocation()).GetSafeNormal();
			const FVector Axis = FVector::CrossProduct(Dir, Back).GetSafeNormal();
			if (Dir.IsNearlyZero() || Axis.IsNearlyZero()) break;
			FQuat Bend(Axis, FMath::DegreesToRadians(Finger.Degrees[i]));
			if (FVector::DotProduct(Bend.RotateVector(Dir), Back) > 0.f) Bend = FQuat(Axis, -FMath::DegreesToRadians(Finger.Degrees[i]));
			if (!RotateBone(Pose, Joint[i], Bend)) break;
		}
	}
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
	MountedAim = Inst->GetMountedAim();
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
	if (bMelee || Hand.bEnabled || MountedAim.bEnabled)
	{
		// One trip through component space: body first, then the left hand onto the moved musket.
		FComponentSpacePoseContext CS(this);
		CS.Pose.InitPose(Output.Pose);
		if (MountedAim.bEnabled) ApplyMountedAim(CS.Pose, MountedAim);
		if (bMelee) ApplyMeleePose(CS.Pose, Melee);
		if (Hand.bEnabled)
		{
			FPA250SupportHandTarget Support = Hand;
			const float Club = FMath::Clamp(FMath::Max(Melee.Raise, Melee.Chop), 0.f, 1.f);
			if (Club > 0.01f)
			{
				// Clubbed, the left hand sits on the barrel just beyond the right. The published target is
				// measured from the musket a frame ago, and the musket turns ~600 deg/s in the swing, so the
				// hand trailed off into the air; here it is placed from the same weights that turn the musket.
				const float Pitch = FMath::DegreesToRadians(-70.f * Melee.Raise - 190.f * Melee.Chop);
				const FVector ButtDir = -(Melee.ForwardCS.GetSafeNormal() * FMath::Cos(Pitch) + FVector::UpVector * FMath::Sin(Pitch));
				Support.SpotFromGripCS = FMath::Lerp(Support.SpotFromGripCS, ButtDir * 18.f, FMath::Min(1.f, Club * 3.f));
			}
			GripRightHand(CS.Pose, Support);
			SolveSupportHand(CS.Pose, Support);
		}
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
	const float Coil = Unit(Melee.Coil), Lunge = Unit(Melee.Lunge), Butt = Unit(Melee.Butt), Level = Unit(Melee.Level), Guard = Unit(Melee.Guard), Raise = Unit(Melee.Raise), Chop = Unit(Melee.Chop), Shouldering = Unit(Melee.Shoulder);
	const float Reloading = Finite(Melee.ReloadRightHandCS) ? Unit(Melee.Reload) : 0.f;
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
	const FVector Shift = F * (-10.f * Coil + 20.f * Lunge + 8.f * Butt + 6.f * Level - 5.f * Guard - 6.f * Raise + 14.f * Chop) + FlinchDir * (14.f * Flinch)
		- U * (4.f * Coil + 15.f * Lunge + 4.f * Butt + 9.f * Level + 6.f * Flinch + 7.f * Guard + 9.f * Chop);
	const float Turn = 20.f * Coil - 26.f * Lunge - 8.f * Butt + 12.f * Guard + 24.f * Raise - 22.f * Chop + 16.f * Shouldering;   // + = right shoulder drawn back
	const float Lean = -5.f * Coil + 28.f * Lunge + 10.f * Butt + 20.f * Level + 4.f * Guard - 8.f * Raise + 24.f * Chop; // forward
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
		// Reloading he watches his hands.
		FQuat Head = Tip(U, F, -0.7f * Lean + 16.f * Reloading) * Tip(F, R, -0.8f * Turn);
		if (Flinch > 0.f) Head = Tip(U, FlinchDir, 16.f * Flinch) * Head;
		RotateBone(Pose, FindBone(Bones, TEXT("neck_01")), Head);
	}

	// Legs: the front foot steps out (and lifts mid-step), the rear foot trails and stays down.
	if (Front.bOK)
	{
		const FVector Step = F * (42.f * Lunge - 4.f * Coil + 26.f * Chop) + U * (7.f * FMath::Sin(UE_PI * FMath::Max(Lunge, Chop)));
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
			// Shouldered: the butt sits in the right shoulder, so the trigger hand is a forearm's length ahead of it.
			const FVector Shouldered = Pose.GetComponentSpaceTransform(Chest).GetLocation() + R * 14.f + U * 4.f + F * 34.f;
			Base = FMath::Lerp(Base, Shouldered, Shouldering);
		}
		FVector Target = Base
			+ Coil * (-F * 22.f + U * 10.f + R * 4.f)
			+ Lunge * (F * 40.f + U * 22.f - R * 14.f)
			+ Butt * (F * 30.f + U * 38.f - R * 10.f)
			+ Guard * (F * 20.f + U * 26.f - R * 10.f)
			+ Raise * (-F * 4.f + U * 58.f + R * 8.f)
			+ Chop * (F * 40.f + U * 22.f - R * 8.f);
		Target = FMath::Lerp(Target, Melee.ReloadRightHandCS, Reloading);
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
	const FVector Spot = Target.bSpotAbsolute && Finite(Target.SpotCS) ? Target.SpotCS : GripT.GetLocation() + Target.SpotFromGripCS;
	// The left hand cradles the stock from underneath: wrist below the spot and a little back up the arm.
	const FVector Effector = Spot + (Shoulder - Spot).GetSafeNormal() * 8.f - Target.GunUpCS.GetSafeNormal() * 4.f;
	const FVector JointTarget = Shoulder + Target.PoleFromShoulderCS;
	if (!Finite(Shoulder) || !Finite(Effector)) return false;

	AnimationCore::SolveTwoBoneIK(UpperT, LowerT, HandT, JointTarget, Effector, false, 1.0, 1.1);

	// Palm up under the stock: the back of the hand faces down and a little outward (the soldier's left).
	const FVector Along = (HandT.GetLocation() - LowerT.GetLocation()).GetSafeNormal();
	const FVector GunUp = Target.GunUpCS.GetSafeNormal();
	const FVector GunRight = FVector::CrossProduct(GunUp, Target.GunForwardCS.GetSafeNormal());
	AlignHandBack(HandT, Along, -GunUp * 0.8f - GunRight * 0.6f, Target.HandBackAxis);

	for (FTransform* T : {&UpperT, &LowerT, &HandT})
	{
		T->NormalizeRotation();
		if (T->ContainsNaN() || !T->IsRotationNormalized()) return false;
	}
	// Parent before child: SafeSetCSBoneTransforms requires ascending bone order.
	const FBoneTransform Solved[] = {{Upper, UpperT}, {Lower, LowerT}, {HandBone, HandT}};
	Pose.SafeSetCSBoneTransforms(Solved);
	CurlFingers(Pose, TEXT("l"), HandT.GetRotation().RotateVector(Target.HandBackAxis));
	return true;
}

bool FPA250SoldierAnimProxy::GripRightHand(FCSPose<FCompactPose>& Pose, const FPA250SupportHandTarget& Target)
{
	const FBoneContainer& Bones = Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex Lower = FindBone(Bones, TEXT("lowerarm_r"));
	const FCompactPoseBoneIndex HandBone = FindBone(Bones, TEXT("hand_r"));
	if (!Lower.IsValid() || !HandBone.IsValid() || Target.RightHandBackAxis.IsNearlyZero()) return false;
	if (!Finite(Target.GunUpCS) || !Finite(Target.GunForwardCS) || Target.GunForwardCS.IsNearlyZero()) return false;
	FTransform HandT = Pose.GetComponentSpaceTransform(HandBone);
	// The unarmed clips leave the hand hanging open beside the stock. Roll it so the palm lies against
	// the right side of the stock (back of the hand out to the right and up), then close the fingers.
	const FVector Along = (HandT.GetLocation() - Pose.GetComponentSpaceTransform(Lower).GetLocation()).GetSafeNormal();
	const FVector GunUp = Target.GunUpCS.GetSafeNormal();
	const FVector GunRight = FVector::CrossProduct(GunUp, Target.GunForwardCS.GetSafeNormal());
	AlignHandBack(HandT, Along, GunRight * 0.8f + GunUp * 0.6f, Target.RightHandBackAxis);
	if (!SetBone(Pose, HandBone, HandT)) return false;
	CurlFingers(Pose, TEXT("r"), HandT.GetRotation().RotateVector(Target.RightHandBackAxis));
	return true;
}


bool FPA250SoldierAnimProxy::ApplyMountedAim(FCSPose<FCompactPose>& Pose, const FPA250MountedAim& Aim)
{
    if (!Finite(Aim.DirectionCS)) return false;
    const FVector F = Aim.DirectionCS.GetSafeNormal();
    const FVector U = FVector::UpVector;
    const FVector R = FVector::CrossProduct(U, F).GetSafeNormal();
    if (F.IsNearlyZero() || R.IsNearlyZero()) return false;
    const FBoneContainer& Bones = Pose.GetPose().GetBoneContainer();
    auto Arm = [&](const TCHAR* Upper, const TCHAR* Mid, const TCHAR* Hand, FVector Target, FVector Pole) {
        return SolveLimb(Pose, FindBone(Bones, Upper), FindBone(Bones, Mid), FindBone(Bones, Hand), Target, Pole, nullptr, false);
    };
    const auto Shoulder = FindBone(Bones, TEXT("upperarm_r"));
    if (!Shoulder.IsValid()) return false;
    const FVector S = Pose.GetComponentSpaceTransform(Shoulder).GetLocation();
    const float A = FMath::Clamp(Aim.AimAlpha, 0.f, 1.f);
    const float Reload = FMath::Clamp(Aim.ReloadAlpha, 0.f, 1.f);
    const float Kick = FMath::Clamp(Aim.RecoilAlpha, 0.f, 1.f);
    const float Reach = FMath::Lerp(Aim.bPistol ? 30.f : 22.f, Aim.bPistol ? 44.f : 30.f, A);
    // Relax the arms at the saddle while reloading; keep the seated lower body intact.
    const FVector Grip = S + F * (Reach - Reload * 12.f - Kick * 5.f)
        - U * (FMath::Lerp(24.f, 10.f, A) + Reload * 12.f - Kick * 3.f);
    const bool bSolved = Arm(TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), Grip, S + R * 30.f - U * 30.f);
    if (!Aim.bPistol)
        Arm(TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), Grip + F * 28.f - U * 3.f, S - R * 40.f - U * 25.f);
    return bSolved;
}
