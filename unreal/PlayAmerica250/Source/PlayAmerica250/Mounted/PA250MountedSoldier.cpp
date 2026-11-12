#include "Mounted/PA250MountedSoldier.h"

#include "Animation/AnimSequence.h"
#include "RF/MusketWeapon.h"
#include "RF/PA250SoldierAnimInstance.h"
#include "RF/RFCharacter.h"
#include "GameFramework/Pawn.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// Authored ground speeds (cm/s), SourceArt/warhorse/WarHorse_clips.json.
constexpr float WalkSpeed = 143.3f;
constexpr float TrotSpeed = 352.5f;
constexpr float CanterSpeed = 550.f;
constexpr float GallopSpeed = 1028.6f;
constexpr float StandStill = 15.f;
constexpr float GaitHysteresis = 25.f;
// The blade only cuts in the body of the swing, not the wind-up or the recovery.
constexpr float SlashCutFrom = 0.25f;
constexpr float SlashCutTo = 0.8f;
constexpr float SwordSweepRadius = 22.f;
constexpr float DeathSlideSeconds = 1.2f;
constexpr float DeathSlideDamping = 3.2f;

void MakeHitVolume(UPrimitiveComponent* C)
{
    // Musket balls are WorldDynamic query sweeps (AMusketProjectile); nothing else should touch these.
    C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    C->SetCollisionObjectType(ECC_Pawn);
    C->SetCollisionResponseToAllChannels(ECR_Ignore);
    C->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    C->SetGenerateOverlapEvents(false);
    C->SetCanEverAffectNavigation(false);
}
} // namespace

APA250MountedSoldier::APA250MountedSoldier()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Horse = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Horse"));
    Horse->SetupAttachment(Root);
    // The source art faces +Y.
    Horse->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    Horse->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rider = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Rider"));
    Rider->SetupAttachment(Horse, TEXT("rider_seat"));
    // Counter the saddle bone rotation so the rider remains upright.
    Rider->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
    Rider->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Hit volumes in actor space (X forward, feet at Z 0). Horse reference pose: withers ~157 cm,
    // nose ~+130 cm, rump ~-120 cm; the seat (rider_seat) is at Z 157.
    HorseBody = CreateDefaultSubobject<UBoxComponent>(TEXT("HorseBody"));
    HorseBody->SetupAttachment(Root);
    HorseBody->SetBoxExtent(FVector(118.f, 40.f, 42.f));
    HorseBody->SetRelativeLocation(FVector(12.f, 0.f, 128.f));
    MakeHitVolume(HorseBody);
    RiderBody = CreateDefaultSubobject<UCapsuleComponent>(TEXT("RiderBody"));
    RiderBody->SetupAttachment(Root);
    RiderBody->InitCapsuleSize(24.f, 48.f);
    RiderBody->SetRelativeLocation(FVector(-5.f, 0.f, 210.f));
    MakeHitVolume(RiderBody);

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> HorseMesh(TEXT("/Game/PA250/Characters/WarHorse/SK_WarHorse.SK_WarHorse"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> SoldierMesh(TEXT("/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER.SK_AMERICAN_SOLDIER"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HorseAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Idle.A_WarHorse_Idle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Walk.A_WarHorse_Walk"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> TrotAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Trot.A_WarHorse_Trot"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> CanterAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Canter.A_WarHorse_Canter"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> GallopAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Gallop.A_WarHorse_Gallop"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HitAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_HitReact.A_WarHorse_HitReact"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> DeathAnim(TEXT("/Game/PA250/Characters/WarHorse/Animations/A_WarHorse_Death.A_WarHorse_Death"));
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
    HorseTrot = TrotAnim.Object;
    HorseCanter = CanterAnim.Object;
    HorseGallop = GallopAnim.Object;
    HorseHitReact = HitAnim.Object;
    HorseDeath = DeathAnim.Object;
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

float APA250MountedSoldier::GaitClipSpeed(EPA250Gait Gait)
{
    switch (Gait)
    {
    case EPA250Gait::Trot: return TrotSpeed;
    case EPA250Gait::Canter: return CanterSpeed;
    case EPA250Gait::Gallop: return GallopSpeed;
    default: return WalkSpeed;
    }
}

EPA250Gait APA250MountedSoldier::GaitForSpeed(float Speed, EPA250Gait Current)
{
    // Boundaries halfway between authored speeds; a gait is kept until the speed is clearly past one.
    const float Up[] = {(WalkSpeed + TrotSpeed) * .5f, (TrotSpeed + CanterSpeed) * .5f, (CanterSpeed + GallopSpeed) * .5f};
    int32 G = static_cast<int32>(Current);
    while (G < 3 && Speed > Up[G] + GaitHysteresis) ++G;
    while (G > 0 && Speed < Up[G - 1] - GaitHysteresis) --G;
    return static_cast<EPA250Gait>(G);
}

float APA250MountedSoldier::GaitPlayRate(float Speed, EPA250Gait Gait)
{
    return FMath::Clamp(Speed / GaitClipSpeed(Gait), 0.6f, 1.3f);
}

void APA250MountedSoldier::BeginPlay()
{
    Super::BeginPlay();
    SetGroundSpeed(0.f);
    if (!CurrentHorseClip && HorseIdle) PlayHorseClip(HorseIdle, true, 1.f);
    ResumeIdle();
}

void APA250MountedSoldier::PlayHorseClip(UAnimSequence* Clip, bool bLoop, float Rate)
{
    if (!Clip) return;
    if (Clip != CurrentHorseClip)
    {
        Horse->PlayAnimation(Clip, bLoop);
        CurrentHorseClip = Clip;
    }
    Horse->SetPlayRate(Rate);
}

float APA250MountedSoldier::GetHorsePlayRate() const
{
    return Horse ? Horse->GetPlayRate() : 0.f;
}

void APA250MountedSoldier::SetGroundSpeed(float Speed)
{
    GroundSpeed = Speed;
    if (bHorseDead || bHitReacting) return;
    if (Speed < StandStill)
    {
        CurrentGait = EPA250Gait::Walk;
        PlayHorseClip(HorseIdle, true, 1.f);
        return;
    }
    CurrentGait = GaitForSpeed(Speed, CurrentGait);
    UAnimSequence* Clip = CurrentGait == EPA250Gait::Gallop ? HorseGallop.Get()
                        : CurrentGait == EPA250Gait::Canter ? HorseCanter.Get()
                        : CurrentGait == EPA250Gait::Trot   ? HorseTrot.Get()
                                                            : HorseWalk.Get();
    PlayHorseClip(Clip ? Clip : HorseWalk.Get(), true, GaitPlayRate(Speed, CurrentGait));
}

void APA250MountedSoldier::SetMoving(bool bMoving)
{
    if (bMoving == bIsMoving && CurrentHorseClip) return;
    bIsMoving = bMoving;
    SetGroundSpeed(bMoving ? WalkSpeed : 0.f);
}

void APA250MountedSoldier::Slash()
{
    if (bHorseDead || ActiveFirearm || !RiderSlash || GetWorldTimerManager().IsTimerActive(SlashTimer)) return;
    Rider->PlayAnimation(RiderSlash, false);
    SlashElapsed = 0.f;
    SlashVictims.Reset();
    LastSwordTip = SwordTip();
    GetWorldTimerManager().SetTimer(SlashTimer, this, &APA250MountedSoldier::ResumeIdle,
                                   RiderSlash->GetPlayLength(), false);
}

bool APA250MountedSoldier::IsSlashing() const
{
    return GetWorldTimerManager().IsTimerActive(SlashTimer);
}

void APA250MountedSoldier::ResumeIdle()
{
    if (RiderIdle) Rider->PlayAnimation(RiderIdle, true);
}

FVector APA250MountedSoldier::SwordTip() const
{
    // The blade is an engine cube (local half-height 50) scaled along Z: its far end is local +50.
    return SwordBlade->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, 50.f));
}

void APA250MountedSoldier::SweepSword()
{
    const FVector Tip = SwordTip();
    const float Len = RiderSlash ? RiderSlash->GetPlayLength() : 1.f;
    const float Phase = SlashElapsed / FMath::Max(.01f, Len);
    if (Phase >= SlashCutFrom && Phase <= SlashCutTo)
    {
        TArray<FHitResult> Hits;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250MountedSword), false, this);
        Params.AddIgnoredActor(GetOwner());
        FCollisionObjectQueryParams Objects(ECC_Pawn);
        GetWorld()->SweepMultiByObjectType(Hits, LastSwordTip, Tip, FQuat::Identity, Objects,
                                           FCollisionShape::MakeSphere(SwordSweepRadius), Params);
        ARFCharacter* Me = Cast<ARFCharacter>(GetOwner());
        for (const FHitResult& H : Hits)
        {
            ARFCharacter* Victim = Cast<ARFCharacter>(H.GetActor());
            if (!Victim || Victim == Me || Victim->IsDead() || SlashVictims.Contains(Victim)) continue;
            SlashVictims.Add(Victim);
            if (Me) Me->OnMountedSwordHit(Victim, H.ImpactPoint);
        }
    }
    LastSwordTip = Tip;
}

bool APA250MountedSoldier::IsRiderComponent(const UPrimitiveComponent* Comp) const
{
    return Comp && Comp == RiderBody;
}

float APA250MountedSoldier::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator,
                                       AActor* DamageCauser)
{
    if (bHorseDead || DamageAmount <= 0.f) return 0.f;
    ARFCharacter* Me = Cast<ARFCharacter>(GetOwner());
    const UPrimitiveComponent* HitComp = nullptr;
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        HitComp = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
    }
    // A ball in the man is the man's wound.
    if (bRidden && IsRiderComponent(HitComp))
    {
        return Me ? Me->TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser) : 0.f;
    }
    // No friendly fire on horses: his own side's balls (and his own) do not wound his mount.
    if (const ARFCharacter* Shooter = EventInstigator ? Cast<ARFCharacter>(EventInstigator->GetPawn()) : nullptr)
    {
        if (Me && (Shooter == Me || (Shooter->FactionId != NAME_None && Shooter->FactionId == Me->FactionId))) return 0.f;
    }
    Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    HorseHealth = FMath::Max(0.f, HorseHealth - DamageAmount);
    UE_LOG(LogTemp, Display, TEXT("PA250 horse: hit %.0f, health %.0f/%.0f"), DamageAmount, HorseHealth, HorseMaxHealth);
    if (HorseHealth <= 0.f)
    {
        // Ridden: the soldier is thrown with it. Picketed: it simply goes down where it stands.
        if (Me && bRidden && Me->GetMountedVisual() == this) Me->OnHorseKilled();
        else BeginHorseDeath(FVector::ZeroVector);
        return DamageAmount;
    }
    // Flinch only at the slower gaits; the authored hit reaction stands in place.
    if (HorseHitReact && GroundSpeed < TrotSpeed + GaitHysteresis && !bHitReacting)
    {
        bHitReacting = true;
        PlayHorseClip(HorseHitReact, false, 1.f);
        GetWorldTimerManager().SetTimer(HitReactTimer, this, &APA250MountedSoldier::EndHitReact,
                                       HorseHitReact->GetPlayLength(), false);
    }
    return DamageAmount;
}

void APA250MountedSoldier::EndHitReact()
{
    bHitReacting = false;
    CurrentHorseClip = nullptr;
    SetGroundSpeed(GroundSpeed);
}

void APA250MountedSoldier::BeginHorseDeath(const FVector& Velocity)
{
    if (bHorseDead) return;
    bHorseDead = true;
    HorseHealth = 0.f;
    GetWorldTimerManager().ClearTimer(SlashTimer);
    GetWorldTimerManager().ClearTimer(HitReactTimer);
    bHitReacting = false;
    DeathVelocity = FVector(Velocity.X, Velocity.Y, 0.f);
    DeathSlideTime = 0.f;
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
    HorseBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RiderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // The man is thrown clear (his own body takes over); the saddle is empty from here.
    Rider->SetHiddenInGame(true, true);
    // At speed the legs go from under him: the collapse plays faster.
    const float Rate = DeathVelocity.Size() > CanterSpeed ? 1.3f : 1.f;
    CurrentHorseClip = nullptr;
    PlayHorseClip(HorseDeath, false, Rate);
    SetLifeSpan(120.f);
    UE_LOG(LogTemp, Display, TEXT("PA250 horse: down at %.0f cm/s"), DeathVelocity.Size());
}

void APA250MountedSoldier::Picket()
{
    if (bHorseDead || !bRidden) return;
    bRidden = false;
    GetWorldTimerManager().ClearTimer(SlashTimer);
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
    // Standing on the ground where he got down.
    // Ground only (static world), never its own rider's capsule standing in it.
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250HorsePicket), false, this);
    Params.AddIgnoredActor(GetOwner());
    const FVector Loc = GetActorLocation();
    if (GetWorld()->LineTraceSingleByObjectType(Hit, Loc + FVector(0.f, 0.f, 150.f), Loc - FVector(0.f, 0.f, 400.f),
                                                FCollisionObjectQueryParams(ECC_WorldStatic), Params))
    {
        SetActorLocation(FVector(Loc.X, Loc.Y, Hit.ImpactPoint.Z));
    }
    Rider->SetHiddenInGame(true, true);
    RiderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CurrentHorseClip = nullptr;
    SetGroundSpeed(0.f);
}

void APA250MountedSoldier::Remount()
{
    if (bHorseDead) return;
    bRidden = true;
    Rider->SetHiddenInGame(false, true);
    // Sword parts follow the selected weapon (SetFirearm hides them again for a gun).
    for (UStaticMeshComponent* Part : {SwordGrip.Get(), SwordGuard.Get(), SwordBlade.Get()}) Part->SetHiddenInGame(ActiveFirearm != nullptr);
    RiderBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ResumeIdle();
}

void APA250MountedSoldier::DressRiderLike(const USkeletalMeshComponent* Soldier)
{
    if (!Soldier || !Rider || Soldier->GetSkeletalMeshAsset() != Rider->GetSkeletalMeshAsset()) return;
    for (int32 i = 0; i < Soldier->GetNumMaterials(); ++i)
    {
        if (UMaterialInterface* M = Soldier->GetMaterial(i)) Rider->SetMaterial(i, M);
    }
}

void APA250MountedSoldier::TickDeathSlide(float DeltaSeconds)
{
    if (DeathSlideTime >= DeathSlideSeconds || DeathVelocity.IsNearlyZero(1.f)) return;
    DeathSlideTime += DeltaSeconds;
    FVector Loc = GetActorLocation() + DeathVelocity * DeltaSeconds;
    DeathVelocity *= FMath::Exp(-DeathSlideDamping * DeltaSeconds);
    // Keep the carcass on the ground it slides over.
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250HorseSlide), false, this);
    Params.AddIgnoredActor(GetOwner());
    if (GetWorld()->LineTraceSingleByObjectType(Hit, Loc + FVector(0.f, 0.f, 150.f), Loc - FVector(0.f, 0.f, 300.f),
                                                FCollisionObjectQueryParams(ECC_WorldStatic), Params))
    {
        Loc.Z = Hit.ImpactPoint.Z;
    }
    SetActorLocation(Loc);
}

void APA250MountedSoldier::SetFirearm(AMusketWeapon* Weapon, bool bPistol)
{
    GetWorldTimerManager().ClearTimer(SlashTimer);
    ActiveFirearm = Weapon;
    bPistolAim = bPistol;
    bWantsAim = false;
    RecoilAlpha = 0.f;
    ReloadAlpha = 0.f;
    AimAlpha = 0.f;
    for (UStaticMeshComponent* Part : {SwordGrip.Get(), SwordGuard.Get(), SwordBlade.Get()})
        Part->SetHiddenInGame(Weapon != nullptr);
    if (Weapon)
    {
        Rider->SetAnimInstanceClass(UPA250SoldierAnimInstance::StaticClass());
        if (auto* Anim = Cast<UPA250SoldierAnimInstance>(Rider->GetAnimInstance())) Anim->SetLocomotion(RiderIdle);
        Weapon->AttachToComponent(Rider, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_r"));
        Weapon->SetActorHiddenInGame(false);
    }
    else ResumeIdle();
    UpdateFirearmPose(0.f);
}

void APA250MountedSoldier::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bHorseDead)
    {
        TickDeathSlide(DeltaSeconds);
        return;
    }
    if (IsSlashing())
    {
        SlashElapsed += DeltaSeconds;
        SweepSword();
    }
    UpdateFirearmPose(DeltaSeconds);
}

void APA250MountedSoldier::PrepareShot()
{
    // Switching and firing can occur before the mounted actor's first tick.
    UpdateFirearmPose(0.f);
}

void APA250MountedSoldier::NotifyFirearmShot()
{
    RecoilAlpha = 1.f;
}

void APA250MountedSoldier::UpdateFirearmPose(float DeltaSeconds)
{
    const APawn* Pawn = Cast<APawn>(GetOwner());
    if (!ActiveFirearm || !Pawn) return;
    FRotator Aim = Pawn->GetBaseAimRotation();
    AimAlpha = FMath::FInterpTo(AimAlpha, bWantsAim ? 1.f : 0.f, DeltaSeconds, 10.f);
    ReloadAlpha = FMath::FInterpTo(ReloadAlpha, ActiveFirearm->IsReloading() ? 1.f : 0.f, DeltaSeconds, 8.f);
    RecoilAlpha = FMath::Max(0.f, RecoilAlpha - DeltaSeconds * 5.f);
    Aim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -AMusketWeapon::MaxBarrelPitch, AMusketWeapon::MaxBarrelPitch);
    Aim.Roll = 0.f;
    USceneComponent* GunRoot = ActiveFirearm->GetRootComponent();
    if (!GunRoot->IsUsingAbsoluteRotation())
    {
        // Switching to absolute keeps the cached hand-bone rotation until the next transform update;
        // if the aim then equals the stored relative rotation, SetWorldRotation is skipped. Refresh now.
        GunRoot->SetUsingAbsoluteRotation(true);
        GunRoot->UpdateComponentToWorld();
    }
    FRotator VisibleAim = Aim;
    VisibleAim.Pitch += RecoilAlpha * (bPistolAim ? 7.f : 3.f) - ReloadAlpha * 22.f;
    // No sweep: the gun sits inside the rider's hit volume, and a swept rotation would be blocked there.
    GunRoot->SetWorldRotation(VisibleAim, false, nullptr, ETeleportType::TeleportPhysics);
    if (auto* Anim = Cast<UPA250SoldierAnimInstance>(Rider->GetAnimInstance()))
    {
        FPA250MountedAim Target;
        Target.bEnabled = true;
        Target.bPistol = bPistolAim;
        Target.AimAlpha = AimAlpha;
        Target.ReloadAlpha = ReloadAlpha;
        Target.RecoilAlpha = RecoilAlpha;
        Target.DirectionCS = Rider->GetComponentTransform().InverseTransformVectorNoScale(Aim.Vector());
        Anim->SetMountedAim(Target);
    }
}
