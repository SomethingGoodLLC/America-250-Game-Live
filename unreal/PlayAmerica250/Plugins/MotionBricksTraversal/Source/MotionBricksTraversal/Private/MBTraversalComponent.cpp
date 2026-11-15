#include "MBTraversalComponent.h"

#include "MBTraversal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace {
constexpr float BlendInSeconds = 0.09f;
constexpr float BlendOutSeconds = 0.12f;
constexpr float FloorMargin = 2.f;
}

UMBTraversalComponent::UMBTraversalComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMBTraversalComponent::BeginPlay() {
    Super::BeginPlay();
    if (ACharacter* Pawn = Cast<ACharacter>(GetOwner())) {
        Pawn->GetMesh()->AddTickPrerequisiteComponent(this);
        Pawn->AddTickPrerequisiteComponent(this);
    }
}

void UMBTraversalComponent::EndPlay(const EEndPlayReason::Type Reason) {
    if (ACharacter* Pawn = Cast<ACharacter>(GetOwner())) {
        Pawn->GetMesh()->RemoveTickPrerequisiteComponent(this);
        Pawn->RemoveTickPrerequisiteComponent(this);
    }
    if (Reason == EEndPlayReason::RemovedFromWorld || Reason == EEndPlayReason::Destroyed) {
        Finish(false);
    }
    Super::EndPlay(Reason);
}

bool UMBTraversalComponent::HasCapsuleRoom(const ACharacter& Pawn, FVector At, float HalfHeight) const {
    const UCapsuleComponent* Capsule = Pawn.GetCapsuleComponent();
    return !GetWorld()->OverlapBlockingTestByProfile(At,FQuat::Identity,Capsule->GetCollisionProfileName(),
        FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius(),HalfHeight),
        FCollisionQueryParams(SCENE_QUERY_STAT(MBVaultRoom),false,&Pawn));
}

bool UMBTraversalComponent::FindLanding(const ACharacter& Pawn, const FMBVaultTarget& Target, FVector& At, float HalfHeight) const {
    FHitResult Hit;
    if (!IsValid(Target.LandingSurface) || !GetWorld()->LineTraceSingleByChannel(Hit,At+FVector(0,0,100),
        At-FVector(0,0,180),ECC_Visibility,FCollisionQueryParams(SCENE_QUERY_STAT(MBVaultLanding),false,&Pawn)) ||
        Hit.GetComponent() != Target.LandingSurface || Hit.ImpactNormal.Z < 0.98f) return false;
    At.Z = Hit.ImpactPoint.Z + HalfHeight + FloorMargin;
    return HasCapsuleRoom(Pawn,At,HalfHeight);
}

EMBVaultStart UMBTraversalComponent::TryStart(UMBGeneratedClip* Clip, const FMBVaultTarget& Target) {
    if (IsVaulting() || bRecoverStandingCapsule) return EMBVaultStart::Busy;
    ACharacter* Pawn = Cast<ACharacter>(GetOwner());
    if (!Pawn || !Clip || !Clip->Animation || Clip->Duration() <= 0.f || !IsValid(Target.Wall) ||
        !IsValid(Target.LandingSurface) || !Pawn->GetActorScale3D().Equals(FVector::OneVector) ||
        !Target.CourseFrame.GetScale3D().Equals(FVector::OneVector) ||
        !Target.CourseFrame.GetUnitAxis(EAxis::Z).Equals(FVector::UpVector,0.001)) return EMBVaultStart::InvalidSetup;

    const USkeletalMesh* Mesh = Pawn->GetMesh()->GetSkeletalMeshAsset();
    if (!Mesh || Clip->Animation->GetSkeleton() != Mesh->GetSkeleton() ||
        (!Clip->TargetMesh.IsNull() && Clip->TargetMesh.ToSoftObjectPath() != FSoftObjectPath(Mesh))) {
        return EMBVaultStart::IncompatibleMesh;
    }

    if (Clip->bMatchWallDimensions) {
        const FBoxSphereBounds Wall = Target.Wall->CalcBounds(Target.Wall->GetComponentTransform().GetRelativeTransform(Target.CourseFrame));
        if (!Wall.BoxExtent.Equals(FVector(Clip->WallDepth,Clip->WallWidth,Clip->WallHeight)*0.5,2.0) ||
            !Wall.Origin.Equals(FVector(0,0,Clip->WallHeight*0.5),2.0)) return EMBVaultStart::WrongObstacle;
    }

    UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
    UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
    if (Movement->MovementMode != MOVE_Walking) return EMBVaultStart::NotGrounded;
    const FVector Local = Target.CourseFrame.InverseTransformPosition(Pawn->GetActorLocation());
    if (FVector::DotProduct(Pawn->GetActorForwardVector(),Target.CourseFrame.GetUnitAxis(EAxis::X)) < 0.95 ||
        FMath::Abs(Local.Y) > 12.0 || FMath::Abs(Local.X+Clip->WallDistance) > 20.0) return EMBVaultStart::InvalidApproach;

    StandingHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
    const float Radius = Capsule->GetUnscaledCapsuleRadius();
    const float ReducedHalfHeight = FMath::Clamp(VaultHalfHeight,Radius,StandingHalfHeight);
    const FVector Start = Target.CourseFrame.TransformPosition(FVector(-Clip->WallDistance,0,Local.Z));
    auto WorldPoint = [&](const FVector& Offset) { return Start+Target.CourseFrame.TransformVectorNoScale(Offset); };
    FVector Landing = WorldPoint(Clip->RootOffsets.Last());
    if (!FindLanding(*Pawn,Target,Landing,StandingHalfHeight)) return EMBVaultStart::BlockedLanding;

    FVector Previous = Start;
    const FCollisionQueryParams Params(SCENE_QUERY_STAT(MBVaultPath),false,Pawn);
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius,ReducedHalfHeight);
    for (const FVector& Offset : Clip->RootOffsets) {
        const FVector Next = WorldPoint(Offset);
        FHitResult Hit;
        if (Offset.ContainsNaN() || GetWorld()->SweepSingleByProfile(Hit,Previous,Next,FQuat::Identity,
            Capsule->GetCollisionProfileName(),Shape,Params)) return EMBVaultStart::BlockedPath;
        Previous = Next;
    }

    FHitResult AlignmentHit;
    const FVector BeforeAlignment = Pawn->GetActorLocation();
    Pawn->SetActorLocation(Start,true,&AlignmentHit);
    if (AlignmentHit.bBlockingHit) {
        Pawn->SetActorLocation(BeforeAlignment,false);
        return EMBVaultStart::BlockedPath;
    }

    Elapsed = 0.f;
    ActiveClip = Clip;
    ActiveTarget = Target;
    Origin = Start;
    bMovementTickWasEnabled = Movement->IsComponentTickEnabled();
    Movement->StopMovementImmediately();
    Movement->SetMovementMode(MOVE_Falling);
    Movement->SetComponentTickEnabled(false);
    Capsule->SetCapsuleHalfHeight(ReducedHalfHeight);
    Pawn->SetActorRotation(Target.CourseFrame.GetRotation());
    USkeletalMeshComponent* MeshComponent = Pawn->GetMesh();
    PriorAnimClass = MeshComponent->GetAnimClass();
    if (UAnimInstance* Anim = MeshComponent->GetAnimInstance()) {
        Montage = Anim->PlaySlotAnimationAsDynamicMontage(Clip->Animation,MontageSlot,BlendInSeconds,BlendOutSeconds);
        if (Montage) Anim->Montage_Pause(Montage);
    }
    if (!Montage) {
        MeshComponent->PlayAnimation(Clip->Animation,false);
        MeshComponent->SetPosition(0.f,false);
        MeshComponent->SetPlayRate(0.f);
    }
    SetComponentTickEnabled(true);
    return EMBVaultStart::Started;
}

bool UMBTraversalComponent::IsPoseSynchronized() const {
    const ACharacter* Pawn = Cast<ACharacter>(GetOwner());
    const UAnimInstance* Anim = Pawn ? Pawn->GetMesh()->GetAnimInstance() : nullptr;
    return Montage && Anim && FMath::IsNearlyEqual(Anim->Montage_GetPosition(Montage),Elapsed,0.001f) &&
        Anim->GetSlotMontageGlobalWeight(MontageSlot) > 0.9f;
}

void UMBTraversalComponent::CancelVault() {
    Finish(false);
}

void UMBTraversalComponent::Finish(bool bCompleted) {
    ACharacter* Pawn = Cast<ACharacter>(GetOwner());
    if (!Pawn || !IsVaulting()) return;
    FVector Safe = Pawn->GetActorLocation();
    const bool bLanded = bCompleted && FindLanding(*Pawn,ActiveTarget,Safe,StandingHalfHeight);
    if (!bLanded) {
        Safe = HasCapsuleRoom(*Pawn,Origin,StandingHalfHeight) ? Origin : Pawn->GetActorLocation();
    }
    const bool bStandingFits = HasCapsuleRoom(*Pawn,Safe,StandingHalfHeight);
    if (bStandingFits) {
        Pawn->SetActorLocation(Safe,false,nullptr,ETeleportType::TeleportPhysics);
        Pawn->GetCapsuleComponent()->SetCapsuleHalfHeight(StandingHalfHeight);
    }

    USkeletalMeshComponent* Mesh = Pawn->GetMesh();
    if (Montage && Mesh->GetAnimInstance()) {
        Mesh->GetAnimInstance()->Montage_Stop(BlendOutSeconds,Montage);
    } else {
        Mesh->SetAnimInstanceClass(PriorAnimClass);
    }
    Montage = nullptr;
    ActiveClip = nullptr;
    ActiveTarget = FMBVaultTarget{};
    bRecoverStandingCapsule = !bStandingFits;

    UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
    Movement->SetMovementMode(bLanded || Safe.Equals(Origin) ? MOVE_Walking : MOVE_Falling);
    Movement->SetComponentTickEnabled(bMovementTickWasEnabled);
    SetComponentTickEnabled(bRecoverStandingCapsule);
    OnVaultFinished.Broadcast(bLanded);
}

void UMBTraversalComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) {
    Super::TickComponent(Delta,TickType,TickFunction);
    ACharacter* Pawn = Cast<ACharacter>(GetOwner());
    if (!Pawn) return;
    if (bRecoverStandingCapsule && !IsVaulting()) {
        const float CurrentHalfHeight = Pawn->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        const FVector Raised = Pawn->GetActorLocation()+FVector(0,0,StandingHalfHeight-CurrentHalfHeight);
        if (HasCapsuleRoom(*Pawn,Raised,StandingHalfHeight)) {
            Pawn->SetActorLocation(Raised,false,nullptr,ETeleportType::TeleportPhysics);
            Pawn->GetCapsuleComponent()->SetCapsuleHalfHeight(StandingHalfHeight);
            bRecoverStandingCapsule = false;
            SetComponentTickEnabled(false);
        }
        return;
    }
    if (!IsVaulting()) return;
    if (!IsValid(ActiveTarget.Wall) || !IsValid(ActiveTarget.LandingSurface)) { Finish(false); return; }

    const float End = FMath::Min(Elapsed+Delta,ActiveClip->Duration());
    while (Elapsed < End-KINDA_SMALL_NUMBER) {
        Elapsed = FMath::Min(End,Elapsed+1.f/120.f);
        const FVector Next = Origin+ActiveTarget.CourseFrame.TransformVectorNoScale(ActiveClip->Sample(Elapsed));
        FHitResult Hit;
        Pawn->SetActorLocation(Next,true,&Hit);
        if (Hit.bBlockingHit) { Finish(false); return; }
    }
    USkeletalMeshComponent* Mesh = Pawn->GetMesh();
    if (Montage && Mesh->GetAnimInstance()) Mesh->GetAnimInstance()->Montage_SetPosition(Montage,Elapsed);
    else Mesh->SetPosition(Elapsed,false);
    if (Elapsed >= ActiveClip->Duration()-KINDA_SMALL_NUMBER) Finish(true);
}
