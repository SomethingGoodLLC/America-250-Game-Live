#include "MBTraversal.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UObject/ConstructorHelpers.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, MotionBricksTraversal)

namespace {
const FName TraversalSlot(TEXT("DefaultSlot"));
constexpr float TraversalBlendIn = 0.09f;
constexpr float TraversalBlendOut = 0.12f;
}

FVector UMBGeneratedClip::Sample(float Time) const {
    if (RootOffsets.IsEmpty()) return FVector::ZeroVector;
    const float Frame = FMath::Clamp(Time*FPS, 0.f, float(RootOffsets.Num()-1));
    const int32 A = FMath::FloorToInt(Frame), B = FMath::Min(A+1, RootOffsets.Num()-1);
    return FMath::Lerp(RootOffsets[A], RootOffsets[B], Frame-A);
}

AMBTestCourse::AMBTestCourse() {
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto AddBox = [&](const TCHAR* Name, FVector Location, FVector Scale) {
        UStaticMeshComponent* Box = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Box->SetupAttachment(RootComponent);
        Box->SetStaticMesh(Cube.Object);
        Box->SetRelativeLocation(Location);
        Box->SetRelativeScale3D(Scale);
        Box->SetCollisionProfileName(TEXT("BlockAll"));
        return Box;
    };
    Floor = AddBox(TEXT("Floor"), FVector(0,0,-10), FVector(20,12,0.2));
    Wall = AddBox(TEXT("Wall"), FVector(0,0,34), FVector(0.30,0.70,0.68));
    LandingBlocker = AddBox(TEXT("LandingBlocker"), FVector(115,0,70), FVector(0.95,1.4,1.4));
    LandingBlocker->SetVisibility(false);
    LandingBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMBTestCourse::ToggleLanding() {
    bool bBlock = LandingBlocker->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
    LandingBlocker->SetCollisionEnabled(bBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    LandingBlocker->SetVisibility(bBlock);
}

AMBTestCharacter::AMBTestCharacter() {
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(24.f,88.f);
    GetCharacterMovement()->MaxWalkSpeed = 220.f;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    bUseControllerRotationYaw = false;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Quinn(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
    GetMesh()->SetSkeletalMesh(Quinn.Object);
    GetMesh()->SetRelativeLocationAndRotation(FVector(0,0,-88), FRotator(0,-90,0));
    // Evaluate the visible pose after Tick sets the capsule and paused montage positions.
    GetMesh()->AddTickPrerequisiteActor(this);
    static ConstructorHelpers::FClassFinder<UAnimInstance> AnimBP(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    LocomotionClass = AnimBP.Class;
    GetMesh()->SetAnimInstanceClass(LocomotionClass);
    USpringArmComponent* Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength = 360.f;
    Arm->SetRelativeRotation(FRotator(-17,60,0));
    UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Arm);
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0.f;
}

void AMBTestCharacter::BeginPlay() {
    Super::BeginPlay();
    if (!Clip) Clip = LoadObject<UMBGeneratedClip>(nullptr, TEXT("/Game/Developers/MotionBricksEvaluation/DA_GeneratedTraversal.DA_GeneratedTraversal"));
    bVerify = FParse::Param(FCommandLine::Get(),TEXT("MBVerify"));
    if (bVerify && Clip) {
        ObservedContacts.SetNum(Clip->Contacts.Num());
        ContactVerificationTick = FWorldDelegates::OnWorldPostActorTick.AddUObject(
            this,&AMBTestCharacter::SampleVerificationContacts);
    }
    NormalHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    if (APlayerController* PC = Cast<APlayerController>(Controller)) {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
    SetResult(Clip ? TEXT("Ready - WASD move, arrows turn, E traverse, R reset, B block landing, V debug") : TEXT("Missing generated clip: run build_test_map.py"));
}

void AMBTestCharacter::EndPlay(const EEndPlayReason::Type Reason) {
    if (ContactVerificationTick.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(ContactVerificationTick);
    Super::EndPlay(Reason);
}

void AMBTestCharacter::SetupPlayerInputComponent(UInputComponent* Input) {
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::E, IE_Pressed, this, &AMBTestCharacter::Trigger);
    Input->BindKey(EKeys::R, IE_Pressed, this, &AMBTestCharacter::ResetAttempt);
    Input->BindKey(EKeys::B, IE_Pressed, this, &AMBTestCharacter::ToggleBlockedLanding);
}
void AMBTestCharacter::Trigger() { TryTraversal(); }
void AMBTestCharacter::SetResult(const FString& Message) {
    LastResult = Message;
    UE_LOG(LogTemp, Display, TEXT("MotionBricks: %s"), *Message);
    if (GEngine) GEngine->AddOnScreenDebugMessage(250, 8.f, FColor::Cyan, Message);
}
bool AMBTestCharacter::ClearAt(const FVector& Position, float HalfHeight) const {
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MBLanding), false, this);
    return !GetWorld()->OverlapBlockingTestByChannel(Position, FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeCapsule(24.f, HalfHeight), Params);
}

bool AMBTestCharacter::TryTraversal() {
    if (bTraversing) { SetResult(TEXT("Rejected: already traversing")); return false; }
    if (!Clip || !Clip->Animation || Clip->Duration() <= 0.f) { SetResult(TEXT("Rejected: no generated motion")); return false; }
    if (!GetCharacterMovement()->IsMovingOnGround()) { SetResult(TEXT("Rejected: not grounded")); return false; }
    AMBTestCourse* Course = nullptr;
    for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) { Course = *It; break; }
    if (!Course) { SetResult(TEXT("Rejected: no test wall")); return false; }
    const FVector Local = Course->GetActorTransform().InverseTransformPosition(GetActorLocation());
    const FVector Forward = Course->GetActorForwardVector();
    if (FVector::DotProduct(GetActorForwardVector(),Forward) < 0.95f || FMath::Abs(Local.Y) > 12.f || FMath::Abs(Local.X + Clip->WallDistance) > 20.f) {
        UE_LOG(LogTemp, Verbose, TEXT("MotionBricks approach: location %s, facing %.3f"),*Local.ToCompactString(),
            FVector::DotProduct(GetActorForwardVector(),Forward));
        SetResult(TEXT("Rejected: approach the marked region facing the wall")); return false;
    }
    Origin = Course->GetActorTransform().TransformPosition(FVector(-Clip->WallDistance,0,Local.Z));
    TraversalRotation = Course->GetActorRotation();
    ActiveCourse = Course;
    FVector Landing = Origin + TraversalRotation.RotateVector(Clip->RootOffsets.Last());
    FHitResult Ground;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MBPath), false, this);
    if (!GetWorld()->LineTraceSingleByChannel(Ground, Landing+FVector(0,0,100), Landing-FVector(0,0,180), ECC_Visibility, Params) || Ground.ImpactNormal.Z < 0.9f) {
        SetResult(TEXT("Rejected: no safe landing ground")); return false;
    }
    if (Ground.GetComponent() != Course->Floor) { SetResult(TEXT("Rejected: blocked landing")); return false; }
    Landing.Z = Ground.ImpactPoint.Z + NormalHalfHeight + 2.f;
    if (!ClearAt(Landing, NormalHalfHeight)) { SetResult(TEXT("Rejected: blocked landing")); return false; }
    // Check every recorded segment with the same reduced capsule used in playback.
    FVector Previous = Origin;
    for (const FVector& Offset : Clip->RootOffsets) {
        const FVector Next = Origin + TraversalRotation.RotateVector(Offset);
        FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit, Previous, Next, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeCapsule(24.f,36.f), Params)) {
            SetResult(TEXT("Rejected: obstructed generated path")); return false;
        }
        Previous = Next;
    }
    FHitResult AlignmentHit;
    const FVector BeforeAlignment = GetActorLocation();
    SetActorLocation(Origin,true,&AlignmentHit);
    if (AlignmentHit.bBlockingHit) {
        SetActorLocation(BeforeAlignment,false);
        SetResult(TEXT("Rejected: approach alignment blocked"));
        return false;
    }
    Elapsed = 0.f;
    bTraversing = true;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->StopMovementImmediately();
    // The vault owns collision for this interval. Expose the airborne state to
    // the locomotion graph so its ground foot traces do not pull down the vault pose.
    Movement->SetMovementMode(MOVE_Falling);
    Movement->SetComponentTickEnabled(false);
    GetCapsuleComponent()->SetCapsuleHalfHeight(36.f);
    SetActorRotation(TraversalRotation);
    if (UAnimInstance* Anim = GetMesh()->GetAnimInstance()) {
        TraversalMontage = Anim->PlaySlotAnimationAsDynamicMontage(
            Clip->Animation, TraversalSlot, TraversalBlendIn, TraversalBlendOut);
        // Pause time advancement. Tick sets the montage position from the swept movement clock.
        if (TraversalMontage) Anim->Montage_Pause(TraversalMontage);
    }
    if (!TraversalMontage) {
        GetMesh()->PlayAnimation(Clip->Animation, false);
        GetMesh()->SetPosition(0.f);
        GetMesh()->SetPlayRate(0.f);
    }
    SetResult(TEXT("Traversing - MotionBricks generated motion"));
    return true;
}

void AMBTestCharacter::Finish(bool bSuccess) {
    bTraversing = false;
    FVector Safe = bSuccess ? GetActorLocation() : Origin;
    FHitResult Ground;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MBRestore), false, this);
    if (GetWorld()->LineTraceSingleByChannel(Ground, Safe+FVector(0,0,90), Safe-FVector(0,0,200), ECC_Visibility, Params))
        Safe.Z = Ground.ImpactPoint.Z + NormalHalfHeight + 2.f;
    if (!ClearAt(Safe, NormalHalfHeight)) {
        Safe = Origin;
        bSuccess = false;
    }
    SetActorLocation(Safe, false);
    GetCapsuleComponent()->SetCapsuleHalfHeight(NormalHalfHeight);
    if (TraversalMontage && GetMesh()->GetAnimInstance()) {
        GetMesh()->GetAnimInstance()->Montage_Stop(TraversalBlendOut, TraversalMontage);
    } else {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(LocomotionClass);
    }
    TraversalMontage = nullptr;
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->SetComponentTickEnabled(true);
    SetResult(bSuccess ? TEXT("Complete: movement restored") : TEXT("Interrupted: returned safely to approach"));
}

void AMBTestCharacter::ResetAttempt() {
    if (bTraversing) Finish(false);
    AMBTestCourse* Course = nullptr;
    for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) { Course = *It; break; }
    if (!Course) return;
    const float Distance = Clip ? Clip->WallDistance : 167.f;
    FVector Start = Course->GetActorTransform().TransformPosition(FVector(-Distance,0,NormalHalfHeight+2));
    if (!ClearAt(Start,NormalHalfHeight)) { SetResult(TEXT("Reset rejected: approach occupied")); return; }
    SetActorLocationAndRotation(Start,Course->GetActorRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately();
    SetResult(TEXT("Reset: press E to traverse"));
}
void AMBTestCharacter::ToggleBlockedLanding() {
    for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) It->ToggleLanding();
    SetResult(TEXT("Landing blocker toggled"));
}

void AMBTestCharacter::Tick(float Dt) {
    Super::Tick(Dt);
    APlayerController* PC = Cast<APlayerController>(Controller);
    if (PC && PC->WasInputKeyJustPressed(EKeys::V)) bDebug = !bDebug;
    if (bTraversing) {
        if (bVerify) TraversalFrameTimes.Add(Dt * 1000.f);
        // Substep at sample boundaries; low frame rates must not skip curved-path collisions.
        const float End = FMath::Min(Elapsed+Dt,Clip->Duration());
        while (Elapsed < End-KINDA_SMALL_NUMBER && bTraversing) {
            Elapsed = FMath::Min(End, Elapsed+1.f/120.f);
            const FVector Target = Origin+TraversalRotation.RotateVector(Clip->Sample(Elapsed));
            FHitResult Hit;
            SetActorLocation(Target,true,&Hit);
            if (Hit.bBlockingHit) { Finish(false); break; }
        }
        if (bTraversing) {
            if (TraversalMontage && GetMesh()->GetAnimInstance()) {
                GetMesh()->GetAnimInstance()->Montage_SetPosition(TraversalMontage, Elapsed);
            } else {
                GetMesh()->SetPosition(Elapsed,false);
            }
        }
        if (bTraversing && Elapsed >= Clip->Duration()-KINDA_SMALL_NUMBER) Finish(true);
    } else if (PC && !bVerify) {
        const float Forward = float(PC->IsInputKeyDown(EKeys::W))-float(PC->IsInputKeyDown(EKeys::S));
        const float Right = float(PC->IsInputKeyDown(EKeys::D))-float(PC->IsInputKeyDown(EKeys::A));
        AddMovementInput(GetActorForwardVector(),Forward);
        AddMovementInput(GetActorRightVector(),Right);
        const float Turn = float(PC->IsInputKeyDown(EKeys::Right))-float(PC->IsInputKeyDown(EKeys::Left));
        AddActorWorldRotation(FRotator(0,Turn*120.f*Dt,0));
    }
    if (bDebug && Clip) {
        for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) {
            const FVector Start = It->GetActorTransform().TransformPosition(FVector(-Clip->WallDistance,0,2));
            DrawDebugBox(GetWorld(),Start,FVector(20,12,2),It->GetActorQuat(),FColor::Green,false,-1,0,2);
            const FVector Exit = Start+It->GetActorRotation().RotateVector(Clip->RootOffsets.Last());
            DrawDebugCircle(GetWorld(),FVector(Exit.X,Exit.Y,2.0),24.f,24,FColor::Cyan,false,-1.f,0,1.5f,
                It->GetActorForwardVector(),It->GetActorRightVector());
        }
    }
    if (bVerify) TickVerification(Dt);
}

AMBTestGameMode::AMBTestGameMode() { DefaultPawnClass = AMBTestCharacter::StaticClass(); }
