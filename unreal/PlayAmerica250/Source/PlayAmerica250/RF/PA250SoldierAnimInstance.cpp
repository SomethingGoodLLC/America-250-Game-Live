#include "PA250SoldierAnimInstance.h"
#include "Battle/PA250BattleFeel.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PoseSearch/PoseSearchDatabase.h"
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

void UPA250SoldierAnimInstance::SetFullQuality(bool bInFull, const TArray<UPoseSearchDatabase*>& InDatabases, float InSearchThrottleSeconds)
{
	bFullQuality = bInFull && !InDatabases.IsEmpty();
	SearchThrottleSeconds = InSearchThrottleSeconds;
	if (LocomotionDatabases.Num() != InDatabases.Num() || !bFullQuality)
	{
		LocomotionDatabases.Reset();
	}
	if (bFullQuality)
	{
		LocomotionDatabases.SetNum(InDatabases.Num());
		for (int32 i = 0; i < InDatabases.Num(); ++i) LocomotionDatabases[i] = InDatabases[i];
	}
}

float FPA250SoldierAnimProxy::StepTierAlpha(float Alpha, bool bWantFull, float DeltaSeconds, float BlendSeconds)
{
	if (!bWantFull) return 0.f;
	if (BlendSeconds <= UE_KINDA_SMALL_NUMBER) return 1.f;
	return FMath::Clamp(Alpha + DeltaSeconds / BlendSeconds, 0.f, 1.f);
}

void UPA250SoldierAnimInstance::SetUpperBodyClip(UAnimSequence* InClip, float InTime, float InWeight, bool bInLoop)
{
	if (InClip && CurrentSkeleton && InClip->GetSkeleton() != CurrentSkeleton)
	{
		if (!bUpperRefusedLogged)
		{
			bUpperRefusedLogged = true;
			UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: %s is on another skeleton than the soldier's; it is not played"), *InClip->GetPathName());
		}
		InClip = nullptr;
	}
	UpperClip = InClip;
	UpperTime = InTime;
	UpperWeight = InClip ? FMath::Clamp(InWeight, 0.f, 1.f) : 0.f;
	bUpperLoop = bInLoop;
}

float UPA250SoldierAnimInstance::ActionWeightAt(float Elapsed, float Total, float BlendIn, float BlendOut)
{
	if (Total <= 0.f || Elapsed < 0.f || Elapsed >= Total) return 0.f;
	const float In = BlendIn > UE_KINDA_SMALL_NUMBER ? Elapsed / BlendIn : 1.f;
	const float Out = BlendOut > UE_KINDA_SMALL_NUMBER ? (Total - Elapsed) / BlendOut : 1.f;
	return FMath::Clamp(FMath::Min(In, Out), 0.f, 1.f);
}

FName UPA250SoldierAnimInstance::NotifyNameOf(const FAnimNotifyEvent& Event)
{
	// A Blueprint notify class is "Notify_Volley_Discharge_C"; a native one "AnimNotify_VolleyDischarge".
	const UClass* Class = Event.Notify ? Event.Notify->GetClass() : (Event.NotifyStateClass ? Event.NotifyStateClass->GetClass() : nullptr);
	if (!Class) return Event.NotifyName;
	FString Name = Class->GetName();
	Name.RemoveFromEnd(TEXT("_C"));
	return FName(*Name);
}

void UPA250SoldierAnimInstance::NotifiesBetween(const UAnimSequence* Clip, float From, float To, TArray<FName>& OutNames)
{
	OutNames.Reset();
	if (!Clip || To <= From) return;
	TArray<TPair<float, FName>> Found;
	for (const FAnimNotifyEvent& Event : Clip->Notifies)
	{
		const float At = Event.GetTriggerTime();
		// A notify at time 0 belongs to the first step (From is then just below 0).
		if (At > From && At <= To) Found.Emplace(At, NotifyNameOf(Event));
	}
	Found.StableSort([](const TPair<float, FName>& A, const TPair<float, FName>& B) { return A.Key < B.Key; });
	for (const TPair<float, FName>& Pair : Found) OutNames.Add(Pair.Value);
}

float UPA250SoldierAnimInstance::NotifyTime(const UAnimSequence* Clip, FName NotifyName)
{
	if (!Clip) return -1.f;
	for (const FAnimNotifyEvent& Event : Clip->Notifies)
	{
		if (NotifyNameOf(Event) == NotifyName) return Event.GetTriggerTime();
	}
	return -1.f;
}

float UPA250SoldierAnimInstance::PlayAction(UAnimSequence* InClip, const FPA250ActionParams& Params, TFunction<void(FName)> OnNotify)
{
	if (!InClip || (CurrentSkeleton && InClip->GetSkeleton() != CurrentSkeleton))
	{
		if (InClip && !bUpperRefusedLogged)
		{
			bUpperRefusedLogged = true;
			UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: %s is on another skeleton than the soldier's; it is not played"), *InClip->GetPathName());
		}
		return 0.f;
	}
	const float Rate = FMath::Max(0.05f, Params.Rate);
	const float Length = InClip->GetPlayLength();
	const float Start = FMath::Clamp(Params.StartTime, 0.f, Length);
	ActionClip = InClip;
	ActionParams = Params;
	ActionNotify = MoveTemp(OnNotify);
	ActionTime = Start;
	ActionElapsed = 0.f;
	ActionTotal = Params.bLoop ? FMath::Max(0.1f, Params.LoopSeconds) : (Length - Start) / Rate;
	ActionWeight = Params.BlendIn > UE_KINDA_SMALL_NUMBER ? 0.f : 1.f;
	bActionLoop = Params.bLoop;
	bActionPosed = false;
	ActionClock = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return ActionTotal;
}

void UPA250SoldierAnimInstance::StopAction(float BlendOutSeconds)
{
	if (!ActionClip || bActionPosed) return;
	// What is left of it is the blend out.
	ActionParams.BlendOut = FMath::Max(0.f, BlendOutSeconds);
	ActionTotal = FMath::Min(ActionTotal, ActionElapsed + ActionParams.BlendOut);
	ActionNotify = nullptr;
}

void UPA250SoldierAnimInstance::SetPosedAction(UAnimSequence* InClip, float InTime, float InWeight, bool bInLoop)
{
	if (ActionClip && !bActionPosed) return; // a played action (a flinch, a fall) is not interrupted by a held pose
	if (!InClip || (CurrentSkeleton && InClip->GetSkeleton() != CurrentSkeleton))
	{
		if (bActionPosed) PosedAge = 1.f; // let it fade
		return;
	}
	ActionClip = InClip;
	ActionParams = FPA250ActionParams();
	ActionNotify = nullptr;
	ActionTime = InTime;
	ActionWeight = FMath::Clamp(InWeight, 0.f, 1.f);
	bActionLoop = bInLoop;
	bActionPosed = true;
	PosedAge = 0.f;
}

void UPA250SoldierAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (!ActionClip) return;
	if (bActionPosed)
	{
		// Whoever holds the pose sets it every frame; left alone it fades in a quarter of a second.
		PosedAge += DeltaSeconds;
		if (PosedAge > 0.1f) ActionWeight = FMath::Max(0.f, ActionWeight - DeltaSeconds * 4.f);
		if (ActionWeight <= 0.f)
		{
			ActionClip = nullptr;
			bActionPosed = false;
		}
		return;
	}
	// By the world's clock, not this update's step: a man out of sight is not updated every frame,
	// and his action must not be found half-played when he comes back into view.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : ActionClock + DeltaSeconds;
	const float Step = FMath::Max(0.f, static_cast<float>(Now - ActionClock));
	ActionClock = Now;
	const float Length = ActionClip->GetPlayLength();
	const float From = ActionTime;
	const float Left = FMath::Max(0.f, ActionTotal - ActionElapsed);
	const float Advance = FMath::Min(Step, Left) * FMath::Max(0.05f, ActionParams.Rate);
	ActionElapsed += Step;
	float To = From + Advance;
	UAnimSequence* Clip = ActionClip;
	TArray<FName> Fired;
	if (bActionLoop && Length > UE_KINDA_SMALL_NUMBER)
	{
		To = FMath::Fmod(To, Length);
	}
	else
	{
		To = FMath::Min(To, Length);
		NotifiesBetween(Clip, From <= 0.f ? -1.f : From, To, Fired);
	}
	ActionTime = To;
	ActionWeight = ActionWeightAt(ActionElapsed, ActionTotal, ActionParams.BlendIn, ActionParams.BlendOut);
	if (ActionParams.bRootMotion && Advance > 0.f)
	{
		if (USkeletalMeshComponent* Mesh = GetSkelMeshComponent())
		{
			if (AActor* Owner = Mesh->GetOwner())
			{
				// A wrapped loop is two ranges: to the end, then from the start.
				FTransform Local = (bActionLoop && To < From)
					? Clip->ExtractRootMotionFromRange(From, Length, FAnimExtractContext()) * Clip->ExtractRootMotionFromRange(0.0, To, FAnimExtractContext())
					: Clip->ExtractRootMotionFromRange(From, To, FAnimExtractContext());
				const FTransform World = Mesh->ConvertLocalRootMotionToWorld(Local);
				FVector Move = World.GetTranslation();
				Move.Z = 0.f; // the capsule keeps to the ground by itself
				if (!Move.IsNearlyZero(0.01)) Owner->AddActorWorldOffset(Move, true);
				const float Yaw = World.GetRotation().Rotator().Yaw;
				if (!FMath::IsNearlyZero(Yaw, 0.01f)) Owner->AddActorWorldRotation(FRotator(0.f, Yaw, 0.f));
			}
		}
	}
	const bool bDone = ActionElapsed >= ActionTotal;
	// The callback may start another action (a fall, then the crawl): it is called last, from a copy.
	TFunction<void(FName)> Notify = ActionNotify;
	if (bDone)
	{
		ActionClip = nullptr;
		ActionNotify = nullptr;
		ActionWeight = 0.f;
	}
	if (Notify)
	{
		for (const FName Name : Fired) Notify(Name);
		if (bDone) Notify(NAME_None); // NAME_None: the action is over
	}
}

void FPA250SoldierAnimProxy::UpperBodyMask(const FBoneContainer& Bones, FName UpperRoot, TArray<bool>& OutMask)
{
	OutMask.Reset();
	const FCompactPoseBoneIndex Root = FindBone(Bones, UpperRoot);
	if (Root == INDEX_NONE) return;
	OutMask.Init(false, Bones.GetCompactPoseNumBones());
	for (int32 i = 0; i < OutMask.Num(); ++i)
	{
		const FCompactPoseBoneIndex Bone(i);
		OutMask[i] = Bone == Root || Bones.BoneIsChildOf(Bone, Root);
	}
}

void FPA250SoldierAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	// The search reads the pose history through the graph message this node publishes, so the
	// motion-matched player sits under it. The collector makes the trajectory from the character.
	History.Source.SetLinkNode(&Locomotion);
	History.PoseCount = 2;
	History.SamplingInterval = 0.04f;
	History.bGenerateTrajectory = true;
	if (History.CollectedBones.IsEmpty())
	{
		for (const TCHAR* Bone : {TEXT("pelvis"), TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r"), TEXT("hand_l"), TEXT("hand_r"), TEXT("head")})
		{
			History.CollectedBones.Add(FBoneReference(Bone));
		}
	}
	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FPA250SoldierAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	// Only a soldier at full quality pays for the pose history and the search.
	if (bFull)
	{
		FAnimInstanceProxy::UpdateAnimationNode(InContext);
	}
}

void FPA250SoldierAnimProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimInstanceProxy::PostUpdate(InAnimInstance);
	UPA250SoldierAnimInstance::FLocomotionReport Report;
	Report.bShowingFull = bFull && TierAlpha > 0.f;
	Report.bHadHistory = Locomotion.bHadHistory;
	Report.Searches = Locomotion.Searches;
	Report.Jumps = Locomotion.Jumps;
	CastChecked<UPA250SoldierAnimInstance>(InAnimInstance)->SetLocomotionReport(Report);
}

void FPA250SoldierAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UPA250SoldierAnimInstance* Inst = CastChecked<UPA250SoldierAnimInstance>(InAnimInstance);
	const UAnimSequence* Wanted = Inst->GetLocomotion();
	if (Wanted != Sequence)
	{
		// Cross-fade out of the clip being left, and enter the new one at the same share of its
		// length: walk and run then change on the same foot, where they used to cut to frame 0.
		FadeSeconds = PA250BattleFeel::Get().CheapCrossfadeSeconds;
		const double OldLength = Sequence ? Sequence->GetPlayLength() : 0.0;
		const double Phase = OldLength > UE_KINDA_SMALL_NUMBER ? Time / OldLength : 0.0;
		if (Sequence && Wanted && FadeSeconds > 0.f)
		{
			FadeFrom = Sequence;
			FadeFromTime = Time;
			FadeFromRate = Rate;
			FadeElapsed = 0.f;
		}
		else
		{
			FadeFrom = nullptr;
		}
		Sequence = Wanted;
		Time = Wanted ? Phase * Wanted->GetPlayLength() : 0.0;
	}
	Rate = Inst->GetPlayRate();
	// Full quality (TRIAL): what to search this frame, and the numbers from battle_feel.json.
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const bool bWasFull = bFull;
	bFull = Inst->WantsFullQuality();
	Locomotion.Databases.Reset();
	if (bFull)
	{
		for (const TObjectPtr<UPoseSearchDatabase>& Database : Inst->GetLocomotionDatabases()) Locomotion.Databases.Add(Database.Get());
		Locomotion.BlendSeconds = Feel.MMBlendSeconds;
		Locomotion.SearchThrottleSeconds = Inst->GetSearchThrottleSeconds();
		Locomotion.PlayRate = FFloatInterval(Feel.PlayRateMin, Feel.PlayRateMax);
	}
	else if (bWasFull)
	{
		Locomotion.ResetLocomotion();
	}
	TierBlendSeconds = Feel.TierBlendSeconds;
	Hand = Inst->GetSupportHandTarget();
	Melee = Inst->GetMeleePose();
	MountedAim = Inst->GetMountedAim();
	TraversalClip = Inst->GetTraversalClip();
	TraversalTime = Inst->GetTraversalTime();
	TraversalWeight = Inst->GetTraversalWeight();
	UpperClip = Inst->GetUpperBodyClip();
	UpperTime = Inst->GetUpperBodyTime();
	UpperWeight = Inst->GetUpperBodyWeight();
	bUpperLoop = Inst->GetUpperBodyLoop();
	ActionClip = Inst->GetActionClip();
	ActionTime = Inst->GetActionTime();
	ActionWeight = ActionClip ? Inst->GetActionWeight() : 0.f;
	bActionLoop = Inst->GetActionLoop();
	bActionKeepsHand = Inst->ActionKeepsSupportHand();
}

void FPA250SoldierAnimProxy::Update(float DeltaSeconds)
{
	// The cheap path shows until the motion-matched player has a clip; then it blends in.
	TierAlpha = StepTierAlpha(TierAlpha, bFull && Locomotion.HasPose(), DeltaSeconds, TierBlendSeconds);
	if (!Sequence) return;
	const double Length = Sequence->GetPlayLength();
	Time = Length > UE_KINDA_SMALL_NUMBER ? FMath::Fmod(Time + DeltaSeconds * Rate, Length) : 0.0;
	if (FadeFrom)
	{
		const double FromLength = FadeFrom->GetPlayLength();
		FadeFromTime = FromLength > UE_KINDA_SMALL_NUMBER ? FMath::Fmod(FadeFromTime + DeltaSeconds * FadeFromRate, FromLength) : 0.0;
		FadeElapsed += DeltaSeconds;
		if (FadeElapsed >= FadeSeconds) FadeFrom = nullptr;
	}
}

float FPA250SoldierAnimProxy::CrossfadeAlpha(float Elapsed, float Seconds)
{
	if (Seconds <= UE_KINDA_SMALL_NUMBER) return 1.f;
	const float T = FMath::Clamp(Elapsed / Seconds, 0.f, 1.f);
	return T * T * (3.f - 2.f * T);
}

FTransform FPA250SoldierAnimProxy::BlendBone(const FTransform& Old, const FTransform& New, float Alpha)
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	const FQuat From = Old.GetRotation();
	FQuat To = New.GetRotation();
	if ((From | To) < 0.0) To = FQuat(-To.X, -To.Y, -To.Z, -To.W);
	FTransform Out;
	Out.SetRotation(FQuat::FastLerp(From, To, A).GetNormalized());
	Out.SetTranslation(FMath::Lerp(Old.GetTranslation(), New.GetTranslation(), static_cast<double>(A)));
	Out.SetScale3D(FMath::Lerp(Old.GetScale3D(), New.GetScale3D(), static_cast<double>(A)));
	return Out;
}

bool FPA250SoldierAnimProxy::Evaluate(FPoseContext& Output)
{
	if (TierAlpha >= 1.f)
	{
		// Full quality: the motion-matched pose. The body passes below still run on top of it.
		History.Evaluate_AnyThread(Output);
	}
	else if (Sequence)
	{
		FAnimationPoseData PoseData(Output);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time, false, {}, true));
		if (FadeFrom)
		{
			// The clip being left, sampled into a pose of its own and blended bone by bone.
			FCompactPose OldPose;
			OldPose.SetBoneContainer(&Output.Pose.GetBoneContainer());
			FBlendedCurve OldCurve;
			OldCurve.InitFrom(Output.Curve);
			UE::Anim::FStackAttributeContainer OldAttributes;
			FAnimationPoseData OldData(OldPose, OldCurve, OldAttributes);
			FadeFrom->GetAnimationPose(OldData, FAnimExtractContext(FadeFromTime, false, {}, true));
			const float Alpha = CrossfadeAlpha(FadeElapsed, FadeSeconds);
			for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
			{
				Output.Pose[Bone] = BlendBone(OldPose[Bone], Output.Pose[Bone], Alpha);
			}
		}
		if (TierAlpha > 0.f)
		{
			// Coming up to full quality: the motion-matched pose over the cheap one, bone by bone.
			FPoseContext Full(Output);
			History.Evaluate_AnyThread(Full);
			for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
			{
				Output.Pose[Bone] = BlendBone(Output.Pose[Bone], Full.Pose[Bone], TierAlpha);
			}
		}
	}
	else
	{
		Output.ResetToRefPose();
	}
	if (UpperClip && UpperWeight > 0.f)
	{
		// A registered clip over the upper body (the loading drill): spine_01 and everything above
		// it take the clip's local pose; hips and legs stay with the movement under them.
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		if (UpperMaskSerial != static_cast<int32>(Bones.GetSerialNumber()) || UpperMask.Num() != Bones.GetCompactPoseNumBones())
		{
			UpperMaskSerial = static_cast<int32>(Bones.GetSerialNumber());
			UpperBodyMask(Bones, TEXT("spine_01"), UpperMask);
		}
		if (UpperMask.Num() == Bones.GetCompactPoseNumBones())
		{
			FCompactPose Upper;
			Upper.SetBoneContainer(&Bones);
			FBlendedCurve UpperCurve;
			UpperCurve.InitFrom(Output.Curve);
			UE::Anim::FStackAttributeContainer UpperAttributes;
			FAnimationPoseData UpperData(Upper, UpperCurve, UpperAttributes);
			UpperClip->GetAnimationPose(UpperData, FAnimExtractContext(UpperTime, false, {}, bUpperLoop));
			for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
			{
				if (UpperMask[Bone.GetInt()]) Output.Pose[Bone] = BlendBone(Output.Pose[Bone], Upper[Bone], UpperWeight);
			}
		}
	}
	if (TraversalClip && TraversalWeight > 0.f)
	{
		// Over an obstacle: the montage's clip over the locomotion pose, by the montage's weight. Its
		// root motion is taken out here (the montage moves the capsule with it).
		FCompactPose Over;
		Over.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve OverCurve;
		OverCurve.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer OverAttributes;
		FAnimationPoseData OverData(Over, OverCurve, OverAttributes);
		TraversalClip->GetAnimationPose(OverData, FAnimExtractContext(TraversalTime, true, {}, false));
		for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
		{
			Output.Pose[Bone] = BlendBone(Output.Pose[Bone], Over[Bone], TraversalWeight);
		}
	}
	if (ActionClip && ActionWeight > 0.f)
	{
		// A whole-body action over everything so far. Its root is locked here: the game thread moves
		// the capsule by the clip's root motion (UPA250SoldierAnimInstance::NativeUpdateAnimation).
		FCompactPose Act;
		Act.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve ActCurve;
		ActCurve.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer ActAttributes;
		FAnimationPoseData ActData(Act, ActCurve, ActAttributes);
		ActionClip->GetAnimationPose(ActData, FAnimExtractContext(ActionTime, true, {}, bActionLoop));
		for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
		{
			Output.Pose[Bone] = BlendBone(Output.Pose[Bone], Act[Bone], ActionWeight);
		}
	}
	// While an action has the body, the poses made in code (carry, shoulder, lunge) stand down by
	// its weight: they were made for the movement clips and would bend the action out of shape.
	const bool bActing = ActionClip && ActionWeight > 0.5f;
	const bool bMelee = Melee.IsActive() && !bActing;
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
			// Over an obstacle the left hand is the clip's (it goes to the wall); the right keeps the musket.
			if (TraversalWeight < 0.5f && (!bActing || bActionKeepsHand)) SolveSupportHand(CS.Pose, Support);
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
	// Down behind cover. The numbers are the data's, kept inside what the legs can reach.
	const float Kneel = Unit(Melee.Kneel);
	const float KneelDrop = Kneel * (FMath::IsFinite(Melee.KneelDropCm) ? FMath::Clamp(Melee.KneelDropCm, 0.f, 70.f) : 0.f);
	const float KneelBack = Kneel * (FMath::IsFinite(Melee.KneelRearFootBackCm) ? FMath::Clamp(Melee.KneelRearFootBackCm, 0.f, 80.f) : 0.f);
	const float KneelLean = Kneel * (FMath::IsFinite(Melee.KneelLeanDeg) ? FMath::Clamp(Melee.KneelLeanDeg, -30.f, 45.f) : 0.f);
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
		- U * (4.f * Coil + 15.f * Lunge + 4.f * Butt + 9.f * Level + 6.f * Flinch + 7.f * Guard + 9.f * Chop + KneelDrop);
	const float Turn = 20.f * Coil - 26.f * Lunge - 8.f * Butt + 12.f * Guard + 24.f * Raise - 22.f * Chop + 16.f * Shouldering;   // + = right shoulder drawn back
	const float Lean = -5.f * Coil + 28.f * Lunge + 10.f * Butt + 20.f * Level + 4.f * Guard - 8.f * Raise + 24.f * Chop + KneelLean; // forward
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
	// A plain carry moves neither hips nor feet: the clip's legs are left exactly as they are.
	const bool bLegs = !Shift.IsNearlyZero(0.01) || Lunge > 0.f || Coil > 0.f || Chop > 0.f || Lag > 0.f || Kneel > 0.f;
	if (bLegs && Front.bOK)
	{
		const FVector Step = F * (42.f * Lunge - 4.f * Coil + 26.f * Chop) + U * (7.f * FMath::Sin(UE_PI * FMath::Max(Lunge, Chop)));
		const FQuat Flat = Front.Foot0.GetRotation();
		SolveLimb(Pose, Front.Thigh, Front.Calf, Front.Foot, Front.Foot0.GetLocation() + Step, Front.Knee0 + Shift + Step + F * 60.f, &Flat, true);
	}
	if (bLegs && Rear.bOK)
	{
		const FQuat Flat = Rear.Foot0.GetRotation();
		// Kneeling, the right foot goes back and its knee points down and forward toward the ground.
		SolveLimb(Pose, Rear.Thigh, Rear.Calf, Rear.Foot, Rear.Foot0.GetLocation() - F * (Lag + KneelBack), Rear.Knee0 + Shift + F * 50.f + R * 15.f - U * (0.6f * KneelDrop), &Flat, true);
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
			// Carried: the unarmed clips swing the hand at the thigh, which leaves the left hand nothing to reach.
			const float CarryW = FMath::IsFinite(Melee.CarryWeight()) ? Melee.CarryWeight() : 0.f;
			const FVector Carried = Pose.GetComponentSpaceTransform(Chest).GetLocation() + R * Melee.CarryRightCm + U * Melee.CarryUpCm + F * Melee.CarryForwardCm;
			Base = FMath::Lerp(Base, Carried, CarryW);
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
