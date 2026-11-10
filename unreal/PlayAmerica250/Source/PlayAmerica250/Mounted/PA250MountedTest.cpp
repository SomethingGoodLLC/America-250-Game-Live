#include "Mounted/PA250MountedTest.h"
#include "Mounted/PA250MountedSoldier.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

APA250MountedTestPawn::APA250MountedTestPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(55.f, 96.f);
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 180.f, 0.f);
    GetCharacterMovement()->MaxWalkSpeed = 143.f;
    MountedVisual = CreateDefaultSubobject<UChildActorComponent>(TEXT("MountedVisual"));
    MountedVisual->SetupAttachment(GetCapsuleComponent());
    MountedVisual->SetRelativeLocation(FVector(0.f, 0.f, -96.f));
    MountedVisual->SetChildActorClass(APA250MountedSoldier::StaticClass());
    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(GetCapsuleComponent());
    CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
    CameraArm->TargetArmLength = 550.f;
    CameraArm->bUsePawnControlRotation = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraArm);
}

void APA250MountedTestPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &APA250MountedTestPawn::Slash);
    Input->BindKey(EKeys::E, IE_Pressed, this, &APA250MountedTestPawn::Slash);
}

void APA250MountedTestPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC || !PC->IsLocalController()) return;
    const float Forward = (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
    const float Right = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
    const FRotator Yaw(0.f, PC->GetControlRotation().Yaw, 0.f);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Forward);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
    float MouseX = 0.f, MouseY = 0.f;
    PC->GetInputMouseDelta(MouseX, MouseY);
    AddControllerYawInput(MouseX);
    AddControllerPitchInput(-MouseY);
    if (APA250MountedSoldier* Visual = Cast<APA250MountedSoldier>(MountedVisual->GetChildActor()))
        Visual->SetMoving(GetVelocity().SizeSquared2D() > 100.f);
}

void APA250MountedTestPawn::Slash()
{
    if (APA250MountedSoldier* Visual = Cast<APA250MountedSoldier>(MountedVisual->GetChildActor())) Visual->Slash();
}

APA250MountedTestGameMode::APA250MountedTestGameMode()
{
    DefaultPawnClass = APA250MountedTestPawn::StaticClass();
}
