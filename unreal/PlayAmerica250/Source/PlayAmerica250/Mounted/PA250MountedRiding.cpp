// ARFCharacter riding: gaits, the sabre from the saddle, and the horse going down under him.
// Kept apart from RFCharacter.cpp; the infantry code only calls in through a few hooks.

#include "RF/RFCharacter.h"

#include "Battle/PA250BattleAudio.h"
#include "Battle/PA250BattleGameMode.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Mounted/PA250MountedSoldier.h"
#include "RF/MusketWeapon.h"
#include "Battle/PA250MeleeComponent.h"

namespace
{
// Riding feel (GAME INVENTION): a horse takes a few strides to reach a gait and to pull up.
constexpr float RidingAcceleration = 420.f;
constexpr float RidingBraking = 520.f;
constexpr float RidingTurnRate = 170.f;
constexpr float RemountDelaySeconds = 10.f;
// The throw: how long the body is loose before he gets up, and how long getting up takes.
constexpr float ThrowMinLooseSeconds = 1.6f;
constexpr float ThrowMaxLooseSeconds = 3.5f;
constexpr float ThrowSettledSpeed = 60.f;
constexpr float ThrowRecoverSeconds = 0.8f;
// Share of the horse's speed his legs keep (checked with the saddle) and his upper body adds on top.
constexpr float ThrowLegCarry = 0.3f;
constexpr float ThrowUpperCarry = 0.45f;
constexpr float ThrowLinearDamping = 0.9f;
// GAME INVENTION: a fall at speed hurts (0.02 health per cm/s above a trot: ~14 at a gallop).
constexpr float FallDamagePerSpeed = 0.02f;

float GallopSpeed() { return APA250MountedSoldier::GaitClipSpeed(EPA250Gait::Gallop); }
} // namespace

float ARFCharacter::MountedFireSwayDegrees(float GroundSpeed)
{
    return 4.f * FMath::Clamp(GroundSpeed / GallopSpeed(), 0.f, 1.f);
}

float ARFCharacter::MountedSwordDamage(float GroundSpeed)
{
    return 55.f + 65.f * FMath::Clamp(GroundSpeed / GallopSpeed(), 0.f, 1.f);
}

void ARFCharacter::BeginRidingMovement()
{
    UCharacterMovementComponent* Move = GetCharacterMovement();
    if (!bRidingMovementSaved)
    {
        SavedMaxAcceleration = Move->MaxAcceleration;
        SavedBrakingDecel = Move->BrakingDecelerationWalking;
        SavedRotationRate = Move->RotationRate;
        SavedCapsuleWorldDynamic = GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldDynamic);
        bRidingMovementSaved = true;
    }
    Move->MaxAcceleration = RidingAcceleration;
    Move->BrakingDecelerationWalking = RidingBraking;
    Move->RotationRate = FRotator(0.f, RidingTurnRate, 0.f);
    // Balls are WorldDynamic sweeps: while he rides, the horse and rider volumes take them, not his
    // foot capsule (which sits inside the horse's barrel).
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
    MountedGait = 0;
    ApplyMountedGaitSpeed();
}

void ARFCharacter::ApplyMountedGaitSpeed()
{
    GetCharacterMovement()->MaxWalkSpeed = APA250MountedSoldier::GaitClipSpeed(static_cast<EPA250Gait>(MountedGait));
}

void ARFCharacter::MountedGaitUp()
{
    if (!MountedVisual || MountedGait >= 3) return;
    ++MountedGait;
    ApplyMountedGaitSpeed();
    static const TCHAR* Names[] = {TEXT("Walk"), TEXT("Trot"), TEXT("Canter"), TEXT("Gallop")};
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: gait %s"), Names[MountedGait]);
    if (GEngine && IsPlayerControlled()) GEngine->AddOnScreenDebugMessage(25072, 1.5f, FColor::White, Names[MountedGait]);
}

void ARFCharacter::MountedGaitDown()
{
    if (!MountedVisual || MountedGait == 0) return;
    --MountedGait;
    ApplyMountedGaitSpeed();
    static const TCHAR* Names[] = {TEXT("Walk"), TEXT("Trot"), TEXT("Canter"), TEXT("Gallop")};
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: gait %s"), Names[MountedGait]);
    if (GEngine && IsPlayerControlled()) GEngine->AddOnScreenDebugMessage(25072, 1.5f, FColor::White, Names[MountedGait]);
}

void ARFCharacter::TickMounted(float DeltaTime)
{
    if (!MountedVisual) return;
    const float Speed = GetVelocity().Size2D();
    MountedVisual->SetGroundSpeed(Speed);
    // Off the reins and nearly stopped: he settles back to a walk, as a horse does.
    if (MountedGait != 0 && Speed < 40.f && GetLastMovementInputVector().IsNearlyZero())
    {
        MountedGait = 0;
        ApplyMountedGaitSpeed();
    }
}

bool ARFCharacter::MountHorse(APA250MountedSoldier* Existing, bool bForce)
{
    if (MountedVisual || bIsDead || bThrownFromHorse || !GetWorld()) return false;
    // Not in the middle of a charge, jump or reload (spawning straight into the saddle skips this).
    if (!bForce && (!GetCharacterMovement()->IsMovingOnGround() || bIsCrouched || (Musket && Musket->IsReloading())
                    || (MeleeComponent && MeleeComponent->IsCharging())))
    {
        return false;
    }
    APA250MountedSoldier* Horse = Existing;
    if (Horse)
    {
        if (Horse->IsHorseDead()) return false;
        // Step up beside it: he takes the horse's place and heading, so the horse does not jump.
        FVector Loc = Horse->GetActorLocation();
        Loc.Z += GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        SetActorLocationAndRotation(Loc, FRotator(0.f, Horse->GetActorRotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
        if (Controller) Controller->SetControlRotation(FRotator(0.f, Horse->GetActorRotation().Yaw, 0.f));
        Horse->Remount();
    }
    else
    {
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Horse = GetWorld()->SpawnActor<APA250MountedSoldier>(APA250MountedSoldier::StaticClass(), GetActorTransform(), Params);
        if (!Horse) return false;
    }
    MountedVisual = Horse;
    if (KeepsOwnHorse()) OwnHorse = Horse;
    Horse->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    Horse->SetActorRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    // The horse's and rider's hit volumes take musket balls; the meshes themselves never collide.
    Horse->SetActorEnableCollision(true);
    Horse->DressRiderLike(GetMesh());
    UnCrouch();
    StopAim();
    bSprinting = false;
    BeginRidingMovement();
    GetMesh()->SetHiddenInGame(true, true);
    if (Musket)
    {
        Musket->SetReloadHeld(false);
        Musket->SetActorHiddenInGame(true);
    }
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: %s mounts %s horse"), *GetName(), Existing ? TEXT("his own") : TEXT("a"));
    return true;
}

void ARFCharacter::PicketHorse()
{
    if (!MountedVisual) return;
    APA250MountedSoldier* Horse = LeaveSaddle();
    if (!Horse) return;
    if (!KeepsOwnHorse()) OwnHorse = Horse;
    Horse->Picket();
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: %s dismounts, horse picketed at %s"), *GetName(), *Horse->GetActorLocation().ToCompactString());
}

void ARFCharacter::SetMountedGait(uint8 Gait)
{
    if (!MountedVisual) return;
    const uint8 Want = FMath::Min<uint8>(Gait, 3);
    if (Want == MountedGait) return;
    MountedGait = Want;
    ApplyMountedGaitSpeed();
}

void ARFCharacter::ReleaseOwnHorse()
{
    if (MountedVisual)
    {
        if (APA250MountedSoldier* Horse = LeaveSaddle()) Horse->Destroy();
    }
    if (OwnHorse && !OwnHorse->IsHorseDead()) OwnHorse->Destroy();
    OwnHorse = nullptr;
}

APA250MountedSoldier* ARFCharacter::LeaveSaddle()
{
    APA250MountedSoldier* Horse = MountedVisual;
    if (!Horse) return nullptr;
    Horse->SetFirearm(nullptr, false);
    if (MountedPistol)
    {
        MountedPistol->SetReloadHeld(false);
        MountedPistol->SetActorHiddenInGame(true);
        MountedPistol->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
    }
    if (Musket)
    {
        Musket->SetReloadHeld(false);
        Musket->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_r"));
    }
    MountedWeaponIndex = 0;
    MountedVisual = nullptr;
    GetMesh()->SetHiddenInGame(false, true);
    if (Musket) Musket->SetActorHiddenInGame(false);
    UCharacterMovementComponent* Move = GetCharacterMovement();
    if (bRidingMovementSaved)
    {
        Move->MaxAcceleration = SavedMaxAcceleration;
        Move->BrakingDecelerationWalking = SavedBrakingDecel;
        Move->RotationRate = SavedRotationRate;
        GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, SavedCapsuleWorldDynamic);
        bRidingMovementSaved = false;
    }
    Move->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
    MountedGait = 0;
    return Horse;
}

float ARFCharacter::OnMountedSwordHit(ARFCharacter* Victim, const FVector& Where)
{
    if (!Victim || Victim == this || Victim->IsDead() || bIsDead) return 0.f;
    if (FactionId != NAME_None && Victim->FactionId == FactionId) return 0.f;
    const float Speed = GetVelocity().Size2D();
    const float Damage = MountedSwordDamage(Speed);
    const FHitResult Hit(Victim, Victim->GetCapsuleComponent(), Where, FVector::UpVector);
    const FVector Dir = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
    UGameplayStatics::ApplyPointDamage(Victim, Damage, Dir, Hit, GetController(), this, nullptr);
    FPA250BattleAudio::PlayAt(this, TEXT("impact_body"), Where, EPA250SoundRange::Near, 1.f, 0.1f);
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: sabre hit %s for %.0f at %.0f cm/s"), *Victim->GetName(), Damage, Speed);
    if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
    {
        GM->NotifyProjectileHit(this, Victim, Damage);
    }
    return Damage;
}

void ARFCharacter::KillHorse()
{
    if (MountedVisual) OnHorseKilled();
}

void ARFCharacter::OnHorseKilled()
{
    if (!MountedVisual || bThrownFromHorse) return;
    const FVector Velocity = GetVelocity();
    APA250MountedSoldier* Horse = LeaveSaddle();
    Horse->BeginHorseDeath(Velocity);
    RemountBlockedUntil = GetWorld()->GetTimeSeconds() + RemountDelaySeconds;
    if (bIsDead) return;
    ThrowRider(Velocity);
}

void ARFCharacter::ThrowRider(const FVector& HorseVelocity)
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp) return;
    const float Speed = HorseVelocity.Size2D();
    const FVector Fwd = GetActorForwardVector();
    const FVector Right = GetActorRightVector();
    bThrownFromHorse = true;
    ThrownTime = 0.f;
    ThrowRecoverAlpha = -1.f;
    StopAim();
    if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->SetIgnoreMoveInput(true);
    SavedMeshProfile = MeshComp->GetCollisionProfileName();
    SavedMeshRelative = MeshComp->GetRelativeTransform();

    // He starts in the saddle: lift the body to seat height so it falls from there (same order as
    // OnDeath: the capsule goes first, or the ragdoll collides with it).
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorLocation(GetActorLocation() + FVector(0.f, 0.f, 70.f), false, nullptr, ETeleportType::TeleportPhysics);
    // Loose from the capsule while he flies: the capsule follows the pelvis (for the camera), and an
    // attached body would be dragged along with it.
    MeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    MeshComp->SetSimulatePhysics(true);
    MeshComp->WakeAllRigidBodies();
    MeshComp->SetEnableGravity(true);

    // At a walk he slides off the high side (the horse falls on its left); at speed he keeps the
    // horse's momentum, goes over its neck and tumbles head first.
    const bool bFast = Speed > APA250MountedSoldier::GaitClipSpeed(EPA250Gait::Trot) * 0.8f;
    // The horse stops under him: his legs are checked with it while his shoulders and head carry on.
    // Each body part is launched by its height above the pelvis, and that difference is what pitches
    // him over the neck (at speed) or topples him off the high side (at a walk; the horse falls left).
    const FVector Carry = FVector(HorseVelocity.X, HorseVelocity.Y, 0.f) * (bFast ? ThrowLegCarry : 0.2f);
    const FVector Topple = bFast ? Fwd * (Speed * ThrowUpperCarry + 150.f) : Right * 230.f;
    const FVector Lift(0.f, 0.f, bFast ? 180.f + Speed * 0.12f : 90.f);
    const float PelvisZ = MeshComp->GetSocketLocation(TEXT("pelvis")).Z;
    SavedBodyDamping = -1.f;
    for (FBodyInstance* Body : MeshComp->Bodies)
    {
        if (!Body || !Body->IsValidBodyInstance()) continue;
        if (SavedBodyDamping < 0.f) SavedBodyDamping = Body->LinearDamping;
        const float Up = FMath::Clamp((Body->GetUnrealWorldTransform().GetLocation().Z - PelvisZ) / 70.f, 0.f, 1.f);
        Body->SetLinearVelocity(Carry + Topple * Up + Lift, false);
        Body->SetAngularVelocityInRadians(FVector::ZeroVector, false);
        // Grass and a tumbling body: he comes to rest a few metres on, not skating across the field.
        Body->LinearDamping = ThrowLinearDamping;
        Body->UpdateDampingProperties();
    }
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: thrown at %.0f cm/s (%s), legs %s, head %s"), Speed,
           bFast ? TEXT("over the neck") : TEXT("slid off"), *(Carry + Lift).ToCompactString(), *(Carry + Topple + Lift).ToCompactString());

    if (Speed > APA250MountedSoldier::GaitClipSpeed(EPA250Gait::Trot))
    {
        UGameplayStatics::ApplyDamage(this, FallDamagePerSpeed * (Speed - APA250MountedSoldier::GaitClipSpeed(EPA250Gait::Trot)),
                                      GetController(), this, nullptr);
    }
}

void ARFCharacter::TickThrown(float DeltaTime)
{
    if (!bThrownFromHorse) return;
    if (bIsDead)
    {
        // The fall (or a ball while he was down) killed him: OnDeath owns the body now.
        bThrownFromHorse = false;
        return;
    }
    // Physics advances at most MaxPhysicsDeltaTime per frame: count the loose body in physics time,
    // so a slow frame rate cannot stand him up before he has even landed.
    ThrownTime += FMath::Min(DeltaTime, UPhysicsSettings::Get()->MaxPhysicsDeltaTime);
    USkeletalMeshComponent* MeshComp = GetMesh();
    const FVector Pelvis = MeshComp->GetSocketLocation(TEXT("pelvis"));
    if (ThrowRecoverAlpha < 0.f)
    {
        // The camera rides with the body: the (collision-free) capsule follows the pelvis.
        SetActorLocation(Pelvis + FVector(0.f, 0.f, 30.f), false, nullptr, ETeleportType::TeleportPhysics);
        const float BodySpeed = MeshComp->GetPhysicsLinearVelocity(TEXT("pelvis")).Size();
        if ((ThrownTime > ThrowMinLooseSeconds && BodySpeed < ThrowSettledSpeed) || ThrownTime > ThrowMaxLooseSeconds)
        {
            // Stand the capsule on the ground where he lies, then blend the body from its fallen pose
            // up into the standing pose.
            FVector Stand = Pelvis;
            FHitResult Hit;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250ThrowGround), false, this);
            if (GetWorld()->LineTraceSingleByChannel(Hit, Pelvis + FVector(0.f, 0.f, 100.f), Pelvis - FVector(0.f, 0.f, 400.f), ECC_WorldStatic, Params))
            {
                Stand.Z = Hit.ImpactPoint.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
            }
            SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
            // Back on the capsule; the simulated bodies stay where they lie (no teleport) and blend up.
            MeshComp->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepWorldTransform);
            MeshComp->SetRelativeTransform(SavedMeshRelative, false, nullptr, ETeleportType::None);
            ThrowRecoverAlpha = 0.f;
            UE_LOG(LogTemp, Display, TEXT("PA250 riding: getting up after %.1f s at %s"), ThrownTime, *Stand.ToCompactString());
        }
        return;
    }
    ThrowRecoverAlpha = FMath::Min(1.f, ThrowRecoverAlpha + DeltaTime / ThrowRecoverSeconds);
    // Ease out: most of the rise early, settling onto his feet at the end.
    const float Physics = FMath::Square(1.f - ThrowRecoverAlpha);
    MeshComp->SetAllBodiesPhysicsBlendWeight(Physics);
    if (ThrowRecoverAlpha >= 1.f) FinishThrowRecovery();
}

void ARFCharacter::FinishThrowRecovery()
{
    USkeletalMeshComponent* MeshComp = GetMesh();
    if (SavedBodyDamping >= 0.f)
    {
        for (FBodyInstance* Body : MeshComp->Bodies)
        {
            if (!Body) continue;
            Body->LinearDamping = SavedBodyDamping;
            Body->UpdateDampingProperties();
        }
    }
    MeshComp->SetSimulatePhysics(false);
    MeshComp->SetAllBodiesPhysicsBlendWeight(0.f);
    MeshComp->SetCollisionProfileName(SavedMeshProfile);
    MeshComp->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    MeshComp->SetRelativeTransform(SavedMeshRelative);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->SetIgnoreMoveInput(false);
    bThrownFromHorse = false;
    ThrowRecoverAlpha = -1.f;
    UE_LOG(LogTemp, Display, TEXT("PA250 riding: back on his feet after %.1f s"), ThrownTime);
}
