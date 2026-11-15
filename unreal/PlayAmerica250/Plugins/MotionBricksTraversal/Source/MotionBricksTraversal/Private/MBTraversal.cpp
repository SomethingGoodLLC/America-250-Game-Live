#include "MBTraversal.h"
#include "MBTraversalComponent.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
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

FMBVaultTarget AMBTestCourse::GetVaultTarget() const {
    FMBVaultTarget Target;
    Target.CourseFrame = GetActorTransform();
    Target.Wall = Wall;
    Target.LandingSurface = Floor;
    return Target;
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
    Traversal = CreateDefaultSubobject<UMBTraversalComponent>(TEXT("SharedTraversal"));
    GetMesh()->SetRelativeLocationAndRotation(FVector(0,0,-88),FRotator(0,-90,0));
    GetMesh()->AddTickPrerequisiteActor(this);
    static ConstructorHelpers::FClassFinder<UAnimInstance> AnimBP(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    GetMesh()->SetAnimInstanceClass(AnimBP.Class);
    USpringArmComponent* Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength = 360.f;
    Arm->SocketOffset = FVector(0,0,15);
    Arm->SetRelativeRotation(FRotator(-17,60,0));
    UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Arm);
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0.f;
}

void AMBTestCharacter::BeginPlay() {
    Super::BeginPlay();
    bVerify = FParse::Param(FCommandLine::Get(),TEXT("MBVerify"));
    FString Soldier;
    FParse::Value(FCommandLine::Get(),TEXT("MBSoldier="),Soldier);
    ConfigureSoldier(Soldier != TEXT("Quinn"));
    Traversal->OnVaultFinished.AddDynamic(this,&AMBTestCharacter::HandleVaultFinished);
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
    SetResult(Clip ? FString::Printf(TEXT("%s | R reset, E vault, 1 Manny, 2 Quinn, B block landing"),*SoldierName) :
        TEXT("Missing per-rig clip: run build_test_map.py"));
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
    Input->BindKey(EKeys::One, IE_Pressed, this, &AMBTestCharacter::SelectManny);
    Input->BindKey(EKeys::Two, IE_Pressed, this, &AMBTestCharacter::SelectQuinn);
}
void AMBTestCharacter::Trigger() { TryTraversal(); }
void AMBTestCharacter::SetResult(const FString& Message) {
    LastResult = Message;
    UE_LOG(LogTemp, Display, TEXT("MotionBricks: %s"), *Message);
    if (GEngine) GEngine->AddOnScreenDebugMessage(250, 8.f, FColor::Cyan, Message);
}
void AMBTestCharacter::ConfigureSoldier(bool bManny) {
    const TCHAR* Rig = bManny ? TEXT("Manny") : TEXT("Quinn");
    const FString MeshPath = FString::Printf(TEXT("/Game/Characters/Mannequins/Meshes/SKM_%s_Simple"),Rig);
    const FString ClipPath = FString::Printf(TEXT("/Game/Developers/MotionBricksEvaluation/DA_GeneratedTraversal_%s"),Rig);
    GetMesh()->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr,*MeshPath));
    Clip = LoadObject<UMBGeneratedClip>(nullptr,*ClipPath);
    if (!Clip && !bManny) Clip = LoadObject<UMBGeneratedClip>(nullptr,
        TEXT("/Game/Developers/MotionBricksEvaluation/DA_GeneratedTraversal"));
    SoldierName = bManny ? TEXT("Manny - Continental stand-in, slung musket") : TEXT("Quinn - Militia stand-in, unarmed");
    BuildStandInGear(bManny);
    UE_LOG(LogTemp, Display, TEXT("MB_SOLDIER %s clip=%s"),*SoldierName,*GetNameSafe(Clip));
}

void AMBTestCharacter::SelectManny() {
    if (bVerify || Traversal->IsVaulting()) return;
    ConfigureSoldier(true);
    ResetAttempt();
}

void AMBTestCharacter::SelectQuinn() {
    if (bVerify || Traversal->IsVaulting()) return;
    ConfigureSoldier(false);
    ResetAttempt();
}

void AMBTestCharacter::HandleVaultFinished(bool bCompleted) {
    SetResult(bCompleted ? TEXT("Complete: movement restored") : TEXT("Interrupted: movement restored"));
}

bool AMBTestCharacter::TryTraversal() {
    AMBTestCourse* Course = nullptr;
    for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) { Course = *It; break; }
    if (!Course) { SetResult(TEXT("Rejected: no test wall")); return false; }
    const EMBVaultStart Result = Traversal->TryStart(Clip,Course->GetVaultTarget());
    switch (Result) {
    case EMBVaultStart::Started:
        ActiveCourse = Course;
        SetResult(TEXT("Traversing - MotionBricks generated motion"));
        return true;
    case EMBVaultStart::Busy: SetResult(TEXT("Rejected: already traversing")); break;
    case EMBVaultStart::InvalidApproach: SetResult(TEXT("Rejected: approach the marked region facing the wall")); break;
    case EMBVaultStart::BlockedLanding: SetResult(TEXT("Rejected: blocked landing")); break;
    case EMBVaultStart::BlockedPath: SetResult(TEXT("Rejected: obstructed generated path")); break;
    case EMBVaultStart::WrongObstacle: SetResult(TEXT("Rejected: incompatible wall")); break;
    case EMBVaultStart::IncompatibleMesh: SetResult(TEXT("Rejected: clip belongs to another rig")); break;
    case EMBVaultStart::NotGrounded: SetResult(TEXT("Rejected: not grounded")); break;
    default: SetResult(TEXT("Rejected: missing vault data")); break;
    }
    return false;
}

void AMBTestCharacter::ResetAttempt() {
    Traversal->CancelVault();
    AMBTestCourse* Course = nullptr;
    for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) { Course = *It; break; }
    if (!Course) return;
    const float Distance = Clip ? Clip->WallDistance : 167.f;
    FVector Start = Course->GetActorTransform().TransformPosition(FVector(-Distance,0,NormalHalfHeight+2));
    const UCapsuleComponent* Capsule = GetCapsuleComponent();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetUnscaledCapsuleRadius(),NormalHalfHeight);
    if (GetWorld()->OverlapBlockingTestByProfile(Start,FQuat::Identity,Capsule->GetCollisionProfileName(),Shape,
        FCollisionQueryParams(SCENE_QUERY_STAT(MBReset),false,this))) { SetResult(TEXT("Reset rejected: approach occupied")); return; }
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
    if (Traversal->IsVaulting()) {
        if (bVerify) TraversalFrameTimes.Add(Dt*1000.f);
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
