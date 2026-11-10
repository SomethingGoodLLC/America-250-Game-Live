#include "Mounted/PA250MountedSoldier.h"

#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

APA250MountedSoldier::APA250MountedSoldier()
{
    PrimaryActorTick.bCanEverTick = false;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Horse = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Horse"));
    Horse->SetupAttachment(Root);
    // The source art faces +Y.
    Horse->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    Rider = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Rider"));
    Rider->SetupAttachment(Horse, TEXT("rider_seat"));
    // Counter the saddle bone rotation so the rider remains upright.
    Rider->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
    Rider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> HorseMesh(TEXT("/Game/PA250/Characters/WarHorse/SK_WarHorse.SK_WarHorse"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> SoldierMesh(TEXT("/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER.SK_AMERICAN_SOLDIER"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HorseAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Idle.A_WarHorse_Idle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Walk.A_WarHorse_Walk"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAnim(TEXT("/Game/PA250/Characters/WarHorse/Rider/A_Continental_MountedIdle_V2.A_Continental_MountedIdle_V2"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> SlashAnim(TEXT("/Game/PA250/Characters/WarHorse/Rider/A_Continental_MountedSlash_V2.A_Continental_MountedSlash_V2"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Coat(TEXT("/Game/PA250/Materials/M_PA250_SoldierUniform.M_PA250_SoldierUniform"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Under(TEXT("/Game/PA250/Materials/M_PA250_SoldierUnder.M_PA250_SoldierUnder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (HorseMesh.Succeeded()) Horse->SetSkeletalMeshAsset(HorseMesh.Object);
    if (SoldierMesh.Succeeded()) Rider->SetSkeletalMeshAsset(SoldierMesh.Object);
    if (Coat.Succeeded()) Rider->SetMaterial(1, Coat.Object);
    if (Under.Succeeded()) Rider->SetMaterial(0, Under.Object);
    HorseIdle = HorseAnim.Object;
    HorseWalk = WalkAnim.Object;
    RiderIdle = IdleAnim.Object;
    RiderSlash = SlashAnim.Object;

    // A simple editable sword prop. Its grip position can be tuned on the components.
    SwordGrip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordGrip"));
    SwordGrip->SetupAttachment(Rider, TEXT("hand_r"));
    SwordGrip->SetRelativeScale3D(FVector(.04f, .04f, .14f));
    SwordGuard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordGuard"));
    SwordGuard->SetupAttachment(Rider, TEXT("hand_r"));
    SwordGuard->SetRelativeLocation(FVector(0.f, 0.f, 7.f));
    SwordGuard->SetRelativeScale3D(FVector(.18f, .035f, .025f));
    SwordBlade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordBlade"));
    SwordBlade->SetupAttachment(Rider, TEXT("hand_r"));
    SwordBlade->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
    SwordBlade->SetRelativeScale3D(FVector(.05f, .015f, .85f));
    for (UStaticMeshComponent* Part : {SwordGrip.Get(), SwordGuard.Get(), SwordBlade.Get()})
    {
        if (Cube.Succeeded()) Part->SetStaticMesh(Cube.Object);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void APA250MountedSoldier::BeginPlay()
{
    Super::BeginPlay();
    if (HorseIdle) Horse->PlayAnimation(HorseIdle, true);
    ResumeIdle();
}

void APA250MountedSoldier::Slash()
{
    if (!RiderSlash || GetWorldTimerManager().IsTimerActive(SlashTimer)) return;
    Rider->PlayAnimation(RiderSlash, false);
    GetWorldTimerManager().SetTimer(SlashTimer, this, &APA250MountedSoldier::ResumeIdle,
                                   RiderSlash->GetPlayLength(), false);
}

void APA250MountedSoldier::ResumeIdle()
{
    if (RiderIdle) Rider->PlayAnimation(RiderIdle, true);
}

void APA250MountedSoldier::SetMoving(bool bMoving)
{
    if (bMoving == bIsMoving) return;
    bIsMoving = bMoving;
    UAnimSequence* Next = bMoving && HorseWalk ? HorseWalk.Get() : HorseIdle.Get();
    if (Next) Horse->PlayAnimation(Next, true);
}
