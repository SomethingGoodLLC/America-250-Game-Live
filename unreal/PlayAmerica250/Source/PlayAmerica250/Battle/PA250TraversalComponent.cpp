#include "PA250TraversalComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "RF/PA250SoldierAnimInstance.h"
#include "RF/RFCharacter.h"

const TCHAR* PA250TraversalStartName(EPA250TraversalStart Why)
{
	switch (Why)
	{
	case EPA250TraversalStart::Started: return TEXT("started");
	case EPA250TraversalStart::Busy: return TEXT("busy");
	case EPA250TraversalStart::NoClips: return TEXT("no_clips");
	case EPA250TraversalStart::NotGrounded: return TEXT("not_grounded");
	case EPA250TraversalStart::NoObstacle: return TEXT("no_obstacle");
	case EPA250TraversalStart::WrongObstacle: return TEXT("wrong_obstacle");
	case EPA250TraversalStart::BadApproach: return TEXT("bad_approach");
	case EPA250TraversalStart::NotCrossable: return TEXT("not_crossable");
	case EPA250TraversalStart::BlockedPath: return TEXT("blocked_path");
	default: return TEXT("blocked_landing");
	}
}

UPA250TraversalComponent::UPA250TraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

EPA250TraversalStart UPA250TraversalComponent::Measure(const FVector& Direction, float ReachCm, FPA250Obstacle& Out, EPA250Traversal& OutAction,
	UPrimitiveComponent*& OutComponent) const
{
	OutAction = EPA250Traversal::None;
	OutComponent = nullptr;
	const ARFCharacter* Man = Cast<ARFCharacter>(GetOwner());
	const UWorld* World = GetWorld();
	const FVector Dir = Direction.GetSafeNormal2D();
	if (!Man || !World || Dir.IsNearlyZero()) return EPA250TraversalStart::NoObstacle;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const UCapsuleComponent* Capsule = Man->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Feet = Man->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Traversal), false, Man);

	// 1. What stands ahead, low enough that the lowest hurdle is met.
	const FVector Low = Feet + FVector(0.f, 0.f, FMath::Max(15.f, Feel.HurdleMinCm * 0.75f));
	FHitResult Front;
	if (!World->LineTraceSingleByChannel(Front, Low, Low + Dir * (Radius + ReachCm), ECC_Visibility, Params) || !Front.GetComponent())
	{
		return EPA250TraversalStart::NoObstacle;
	}
	const AActor* Thing = Front.GetActor();
	if (!Thing || !(Thing->ActorHasTag(TEXT("PA250Cover")) || Thing->ActorHasTag(TEXT("PA250FieldWork"))))
	{
		UE_LOG(LogTemp, Verbose, TEXT("PA250 traversal: %s met %s (%s), which is not a wall or a work"), *Man->GetName(), Thing ? *Thing->GetName() : TEXT("nothing"),
			Thing ? *Thing->GetClass()->GetName() : TEXT("-"));
		return EPA250TraversalStart::WrongObstacle;
	}
	const FVector Across = (-Front.ImpactNormal).GetSafeNormal2D();
	if (Across.IsNearlyZero() || FMath::Abs(Front.ImpactNormal.Z) > 0.5f) return EPA250TraversalStart::WrongObstacle;
	if (FVector::DotProduct(Across, Dir) < FMath::Cos(FMath::DegreesToRadians(Feel.TraversalMaxApproachAngleDeg))) return EPA250TraversalStart::BadApproach;
	UPrimitiveComponent* Wall = Front.GetComponent();

	// 2. Its top, from above the highest thing a man can get over.
	const FVector Inside = Front.ImpactPoint + Across * 4.f;
	const float ProbeTop = Feet.Z + Feel.MantleMaxHeightCm + 40.f;
	FHitResult Top;
	if (!Wall->LineTraceComponent(Top, FVector(Inside.X, Inside.Y, ProbeTop), FVector(Inside.X, Inside.Y, Feet.Z - 5.f), Params) || Top.bStartPenetrating)
	{
		return EPA250TraversalStart::NotCrossable;
	}
	Out.Across = Across;
	Out.HeightCm = static_cast<float>(Top.ImpactPoint.Z - Feet.Z);
	Out.FrontLedge = FVector(Front.ImpactPoint.X, Front.ImpactPoint.Y, Top.ImpactPoint.Z);

	// 3. How far through it is, from beyond the deepest thing he can get over back to the near face.
	const float ReachThrough = Feel.MantleMaxDepthCm + 60.f;
	const float UnderTop = static_cast<float>(FMath::Max(Feet.Z + 10.f, Top.ImpactPoint.Z - 5.f));
	const FVector Near(Front.ImpactPoint.X, Front.ImpactPoint.Y, UnderTop);
	FHitResult Back;
	if (Wall->LineTraceComponent(Back, Near + Across * ReachThrough, Near, Params))
	{
		Out.DepthCm = Back.bStartPenetrating ? ReachThrough : static_cast<float>(FVector::DotProduct(Back.ImpactPoint - Near, Across));
	}
	else
	{
		Out.DepthCm = 10.f; // a rail: nothing at that height beyond the near face
	}
	OutAction = PA250BattleFeel::ClassifyTraversal(Out.HeightCm, Out.DepthCm, Feel);
	if (OutAction == EPA250Traversal::None) return EPA250TraversalStart::NotCrossable;

	// 4. Room over the top for a man folded small.
	Params.AddIgnoredComponent(Wall);
	const float Fold = FMath::Max(Radius, Feel.TraversalCapsuleHalfHeightCm);
	const FVector Over = Out.FrontLedge + FVector(0.f, 0.f, Fold + 5.f);
	FHitResult Roof;
	if (World->SweepSingleByChannel(Roof, Over - Across * Radius, Over + Across * (Out.DepthCm + Radius), FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeCapsule(Radius, Fold), Params))
	{
		return EPA250TraversalStart::BlockedPath;
	}

	// 5. Where he comes down: the ground beyond, or for a mantle the top itself; room to stand there.
	FCollisionQueryParams LandParams(SCENE_QUERY_STAT(PA250TraversalLanding), false, Man);
	FHitResult Floor;
	if (OutAction == EPA250Traversal::Mantle)
	{
		Out.BackFloor = Out.FrontLedge + Across * (Radius + 10.f);
	}
	else
	{
		const FVector Beyond = Out.FrontLedge + Across * (Out.DepthCm + Feel.TraversalLandingClearCm);
		if (!World->LineTraceSingleByChannel(Floor, Beyond + FVector(0.f, 0.f, 30.f), FVector(Beyond.X, Beyond.Y, Feet.Z - Feel.MantleMaxHeightCm), ECC_Visibility, LandParams) ||
			Floor.bStartPenetrating)
		{
			return EPA250TraversalStart::BlockedLanding;
		}
		Out.BackFloor = Floor.ImpactPoint;
		LandParams.AddIgnoredComponent(Wall);
	}
	// Solid things only: a man standing there gives way (the men who crossed before him wait just beyond).
	if (World->OverlapBlockingTestByChannel(Out.BackFloor + FVector(0.f, 0.f, HalfHeight + 4.f), FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), LandParams))
	{
		return EPA250TraversalStart::BlockedLanding;
	}
	OutComponent = Wall;
	return EPA250TraversalStart::Started;
}

EPA250TraversalStart UPA250TraversalComponent::TryStart(const FVector& Direction, float ReachCm, FName Gait)
{
	ARFCharacter* Man = Cast<ARFCharacter>(GetOwner());
	UCharacterMovementComponent* Move = Man ? Man->GetCharacterMovement() : nullptr;
	auto Result = [this](EPA250TraversalStart Why) { LastResult = Why; return Why; };
	if (!Man || !Move || bActive || Cooldown > 0.f || Man->IsDead() || Man->GetMountedVisual()) return Result(EPA250TraversalStart::Busy);
	if (!Move->IsMovingOnGround()) return Result(EPA250TraversalStart::NotGrounded);
	const TArray<FPA250TraversalClip>& All = PA250Traversal::Clips();
	if (All.IsEmpty()) return Result(EPA250TraversalStart::NoClips);
	UMotionWarpingComponent* Warp = Man->FindComponentByClass<UMotionWarpingComponent>();
	UAnimInstance* Anim = Man->GetMesh() ? Man->GetMesh()->GetAnimInstance() : nullptr;
	if (!Warp || !Anim) return Result(EPA250TraversalStart::NoClips);

	FPA250Obstacle Found;
	EPA250Traversal Kind = EPA250Traversal::None;
	UPrimitiveComponent* Wall = nullptr;
	const EPA250TraversalStart Why = Measure(Direction, ReachCm, Found, Kind, Wall);
	if (Why != EPA250TraversalStart::Started) return Result(Why);
	// A clip that carries him clear of the far face (his own width past it) before it may be left.
	const FPA250TraversalClip* Chosen = PA250Traversal::PickClip(All, Kind, Gait, Started + static_cast<int32>(Man->GetUniqueID()),
		Found.DepthCm + 2.f * Man->GetCapsuleComponent()->GetScaledCapsuleRadius());
	if (!Chosen || !Chosen->Montage) return Result(EPA250TraversalStart::NoClips);

	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const FVector Feet = Man->GetActorLocation() - FVector(0.f, 0.f, Man->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float Distance = static_cast<float>(FVector::DotProduct(Found.FrontLedge - Feet, Found.Across));
	const float StartAt = PA250Traversal::StartTimeFor(*Chosen, Distance, Feel.TraversalStartLeadSeconds);
	const FQuat Facing = FRotationMatrix::MakeFromX(Found.Across).ToQuat();
	for (int32 i = 0; i < Chosen->Windows.Num(); ++i)
	{
		Warp->AddOrUpdateWarpTargetFromTransform(Chosen->Windows[i].Name, FTransform(Facing, PA250Traversal::WindowTarget(*Chosen, i, Found, Feel.TraversalLandingClearCm)));
	}
	if (Anim->Montage_Play(Chosen->Montage, 1.f, EMontagePlayReturnType::MontageLength, StartAt) <= 0.f)
	{
		for (const FPA250TraversalWindow& W : Chosen->Windows) Warp->RemoveWarpTarget(W.Name);
		return Result(EPA250TraversalStart::NoClips);
	}
	Clip = Chosen;
	Montage = Chosen->Montage;
	EnteredAt = LastClipTime = StartAt;
	Obstacle = Found;
	Action = Kind;
	Ignored = Wall;
	bActive = true;
	bPosing = true;
	++Started;
	Man->GetCapsuleComponent()->IgnoreComponentWhenMoving(Wall, true);
	bSavedYaw = Man->bUseControllerRotationYaw;
	bSavedOrient = Move->bOrientRotationToMovement;
	Man->bUseControllerRotationYaw = false;
	Move->bOrientRotationToMovement = false;
	Move->SetMovementMode(MOVE_Flying);
	UE_LOG(LogTemp, Display, TEXT("PA250 traversal: %s %s over %s, height %.0f cm, depth %.0f cm, %.0f cm off, clip %s from %.2f s"), *Man->GetName(),
		PA250BattleFeel::TraversalName(Kind), Wall && Wall->GetOwner() ? *Wall->GetOwner()->GetName() : TEXT("?"), Found.HeightCm, Found.DepthCm, Distance,
		*Chosen->Montage->GetName(), StartAt);
	return Result(EPA250TraversalStart::Started);
}

void UPA250TraversalComponent::Finish()
{
	ARFCharacter* Man = Cast<ARFCharacter>(GetOwner());
	if (!bActive || !Man) return;
	bActive = false;
	Cooldown = PA250BattleFeel::Get().TraversalCooldownSeconds;
	if (UMotionWarpingComponent* Warp = Man->FindComponentByClass<UMotionWarpingComponent>())
	{
		if (Clip)
		{
			for (const FPA250TraversalWindow& W : Clip->Windows) Warp->RemoveWarpTarget(W.Name);
		}
	}
	UCapsuleComponent* Capsule = Man->GetCapsuleComponent();
	if (Ignored) Capsule->IgnoreComponentWhenMoving(Ignored, false);
	// Left inside the obstacle (a clip that ended short of a deep wall): put him where the landing was measured.
	if (Ignored && Action != EPA250Traversal::Mantle && GetWorld() && !Man->IsDead())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250TraversalEnd), false, Man);
		if (Ignored->OverlapComponent(Man->GetActorLocation(), FQuat::Identity, FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight())))
		{
			Man->SetActorLocation(Obstacle.BackFloor + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight() + 4.f), false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogTemp, Display, TEXT("PA250 traversal: %s ended inside the obstacle and was set down beyond it (%s, clip %s entered at %.2f s, left at %.2f s of %.2f)"), *Man->GetName(),
				PA250BattleFeel::TraversalName(Action), Montage ? *Montage->GetName() : TEXT("?"), EnteredAt, LastClipTime, Clip ? Clip->BlendOutS : 0.f);
		}
	}
	Ignored = nullptr;
	if (!Man->IsDead())
	{
		Man->bUseControllerRotationYaw = bSavedYaw;
		if (UCharacterMovementComponent* Move = Man->GetCharacterMovement())
		{
			Move->bOrientRotationToMovement = bSavedOrient;
			Move->SetMovementMode(MOVE_Falling); // he finds the ground (or the top he stands on) himself
		}
	}
}

void UPA250TraversalComponent::Cancel()
{
	ARFCharacter* Man = Cast<ARFCharacter>(GetOwner());
	if (Man && Montage && Man->GetMesh() && Man->GetMesh()->GetAnimInstance())
	{
		Man->GetMesh()->GetAnimInstance()->Montage_Stop(0.1f, Montage);
	}
	Finish();
}

void UPA250TraversalComponent::PublishPose()
{
	ARFCharacter* Man = Cast<ARFCharacter>(GetOwner());
	UPA250SoldierAnimInstance* Inst = Man && Man->GetMesh() ? Cast<UPA250SoldierAnimInstance>(Man->GetMesh()->GetAnimInstance()) : nullptr;
	if (!Inst) return;
	const FAnimMontageInstance* Playing = Montage ? Inst->GetInstanceForMontage(Montage) : nullptr;
	if (Playing && Clip)
	{
		Inst->SetTraversal(Clip->Sequence, Playing->GetPosition(), Playing->GetWeight());
		LastClipTime = Playing->GetPosition();
		// The movement is given back where the sample lets the clip be left (what follows is its
		// run-out), or when the montage leaves by itself; the pose blends out after.
		if (bActive && Playing->GetDesiredWeight() > 0.f && Clip->BlendOutS > 0.f && Playing->GetPosition() >= Clip->BlendOutS)
		{
			Inst->Montage_Stop(Montage->GetDefaultBlendOutTime(), Montage);
		}
		if (bActive && Playing->GetDesiredWeight() <= 0.f) Finish();
	}
	else
	{
		Inst->SetTraversal(nullptr, 0.f, 0.f);
		if (bActive) Finish();
		bPosing = false;
		Montage = nullptr;
		Clip = nullptr;
	}
}

void UPA250TraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Cooldown = FMath::Max(0.f, Cooldown - DeltaTime);
	ARFCharacter* Man = Cast<ARFCharacter>(GetOwner());
	if (!Man) return;
	if (bPosing)
	{
		if (Man->IsDead() && bActive) Cancel();
		PublishPose();
		return;
	}
	// By himself: only the player, and only at a sprint. Bots are sent over by the traversal links.
	if (!Man->IsPlayerControlled() || Man->IsDead()) return;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	CheckTimer -= DeltaTime;
	if (CheckTimer > 0.f || Feel.TraversalCheckHz <= 0.f) return;
	CheckTimer = 1.f / Feel.TraversalCheckHz;
	const FVector Velocity = Man->GetVelocity();
	if (Velocity.Size2D() < Feel.TraversalMinSpeedCmS) return;
	static const FName Run(TEXT("run"));
	TryStart(Velocity, Feel.TraversalDetectDistanceCm, Run);
}

void UPA250TraversalComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bActive) Cancel();
	Super::EndPlay(Reason);
}
