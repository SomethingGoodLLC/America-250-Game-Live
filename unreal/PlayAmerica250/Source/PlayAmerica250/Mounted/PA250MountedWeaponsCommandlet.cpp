#include "Mounted/PA250MountedWeaponsCommandlet.h"
#include "RF/RFCharacter.h"
#include "Mounted/PA250MountedSoldier.h"
#include "RF/PA250SoldierAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "RF/MusketWeapon.h"
#include "RF/MusketProjectile.h"
#include "Battle/PA250WeaponTuning.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "RF/RFHealthComponent.h"
#include "Battle/PA250BotController.h"
#include "Battle/PA250UnitClasses.h"

int32 UPA250MountedWeaponsCommandlet::Main(const FString& Params)
{
    int32 Fails = 0;
    auto Check = [&Fails](bool OK, const TCHAR* Label) {
        UE_LOG(LogTemp, Display, TEXT("MountedWeapons %s: %s"), OK ? TEXT("PASS") : TEXT("FAIL"), Label);
        if (!OK) ++Fails;
    };
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MountedWeapons"));
    if (!World || !GEngine) return 1;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn = World->SpawnActor<ARFCharacter>(FVector(0,0,200), FRotator::ZeroRotator, Spawn);
    auto* PC = World->SpawnActor<APlayerController>();
    if (!Pawn || !PC) return 1;
    PC->PlayerState = World->SpawnActor<APlayerState>();
    PC->Possess(Pawn);
    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (!Pawn->Musket)
    {
        Spawn.Owner = Pawn;
        Spawn.Instigator = Pawn;
        Pawn->Musket = World->SpawnActor<AMusketWeapon>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    }
    Pawn->ToggleMounted();
    Check(Pawn->IsMounted(), TEXT("player mounts"));
    Pawn->SelectMountedRifle();
    APA250MountedSoldier* Visual = nullptr;
    for (TActorIterator<APA250MountedSoldier> It(World); It; ++It) if (It->GetOwner() == Pawn) Visual = *It;
    if (Visual)
    {
        PC->SetControlRotation(FRotator(25.f, 100.f, 0.f));
        Visual->PrepareShot();
        UE_LOG(LogTemp, Display, TEXT("MountedWeapons info: controller=%s control=%s visual owner=%s"),
               Pawn->GetController() ? *Pawn->GetController()->GetName() : TEXT("none"), *PC->GetControlRotation().ToCompactString(),
               Visual->GetOwner() ? *Visual->GetOwner()->GetName() : TEXT("none"));
        UE_LOG(LogTemp, Display, TEXT("MountedWeapons info: gun root mobility=%d abs_rot=%d parent=%s socket=%s"),
               static_cast<int32>(Pawn->Musket->GetRootComponent()->Mobility.GetValue()),
               Pawn->Musket->GetRootComponent()->IsUsingAbsoluteRotation() ? 1 : 0,
               Pawn->Musket->GetRootComponent()->GetAttachParent() ? *Pawn->Musket->GetRootComponent()->GetAttachParent()->GetName() : TEXT("none"),
               *Pawn->Musket->GetRootComponent()->GetAttachSocketName().ToString());
        UE_LOG(LogTemp, Display, TEXT("MountedWeapons info: gun fwd %s aim %s dot %.4f"), *Pawn->Musket->GetActorForwardVector().ToCompactString(),
               *Pawn->GetBaseAimRotation().Vector().ToCompactString(),
               FVector::DotProduct(Pawn->Musket->GetActorForwardVector(), Pawn->GetBaseAimRotation().Vector()));
        const FRotator RawAim = Pawn->GetBaseAimRotation();
        FRotator ExpectedAim = RawAim;
        ExpectedAim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(ExpectedAim.Pitch), -AMusketWeapon::MaxBarrelPitch, AMusketWeapon::MaxBarrelPitch);
        ExpectedAim.Roll = 0.f;
        UE_LOG(LogTemp, Display, TEXT("MountedWeapons alignment: raw=%s actual=%s expected=%s"), *RawAim.ToString(), *Pawn->Musket->GetActorRotation().ToString(), *ExpectedAim.ToString());
        Check(FVector::DotProduct(Pawn->Musket->GetActorForwardVector(), ExpectedAim.Vector()) > .999,
              TEXT("newly equipped gun aligns before its first tick"));
        Visual->SetAiming(true);
        Visual->Tick(.2f);
        auto* Anim = Cast<UPA250SoldierAnimInstance>(Visual->GetRiderMesh()->GetAnimInstance());
        Check(Anim && Anim->GetMountedAim().AimAlpha > .9f, TEXT("aim pose raises smoothly"));
        Visual->NotifyFirearmShot();
        Visual->Tick(.01f);
        Check(Anim && Anim->GetMountedAim().RecoilAlpha > .8f, TEXT("recoil reaches rider pose"));
        Visual->Tick(1.f);
        Check(Anim && Anim->GetMountedAim().RecoilAlpha == 0.f, TEXT("recoil settles"));
    }
    Check(Pawn->GetEquippedFirearm() == Pawn->Musket.Get(), TEXT("mounted rifle retains infantry weapon and ammunition"));
    Pawn->SelectMountedPistol();
    AMusketWeapon* Pistol = Pawn->GetEquippedFirearm();
    Check(Pistol && Pistol != Pawn->Musket, TEXT("pistol selected independently"));
    if (Pistol && Pistol != Pawn->Musket.Get())
    {
        const int32 Before = [World]() { int32 N=0; for (TActorIterator<AMusketProjectile> It(World); It; ++It) ++N; return N; }();
        Check(Pawn->FireMusketAt(FVector(5000,0,200), false), TEXT("mounted pistol fires projectile"));
        int32 After=0; for (TActorIterator<AMusketProjectile> It(World); It; ++It) ++After;
        Check(After == Before+1, TEXT("one ball spawned"));
        Check(!Pistol->CanFire(), TEXT("single-shot pistol needs reload"));
        if (Visual)
        {
            Visual->Tick(.2f);
            auto* Anim = Cast<UPA250SoldierAnimInstance>(Visual->GetRiderMesh()->GetAnimInstance());
            Check(Anim && Anim->GetMountedAim().ReloadAlpha > .9f, TEXT("reload lowers the rider's arms"));
        }
        Check(!Pawn->FireMusketAt(FVector(5000,0,200), false), TEXT("cannot fire unloaded pistol"));
        Pawn->SelectMountedSword();
        Check(!Pawn->GetEquippedFirearm(), TEXT("sword has no active firearm"));
        Pawn->ToggleMounted();
        Pawn->ToggleMounted();
        Pawn->SelectMountedPistol();
        Check(Pawn->GetEquippedFirearm() == Pistol && !Pistol->CanFire(), TEXT("dismount and remount preserve pistol load state"));
    }
    Pawn->SelectMountedRifle();
    Check(Pawn->FireMusketAt(FVector(5000,0,200), false), TEXT("mounted rifle fires"));
    Check(!Pawn->Musket->CanFire(), TEXT("rifle now needs reload"));
    Pawn->ToggleMounted();
    Check(Pawn->GetEquippedFirearm() == Pawn->Musket.Get() && !Pawn->Musket->CanFire(), TEXT("dismount preserves rifle load state"));

    // --- Gaits (pure rules) ---
    using G = EPA250Gait;
    Check(APA250MountedSoldier::GaitForSpeed(143.f, G::Walk) == G::Walk && APA250MountedSoldier::GaitForSpeed(353.f, G::Walk) == G::Trot
              && APA250MountedSoldier::GaitForSpeed(550.f, G::Walk) == G::Canter && APA250MountedSoldier::GaitForSpeed(1029.f, G::Walk) == G::Gallop,
          TEXT("gait: each authored speed picks its own clip"));
    Check(APA250MountedSoldier::GaitForSpeed(260.f, G::Walk) == G::Walk && APA250MountedSoldier::GaitForSpeed(240.f, G::Trot) == G::Trot,
          TEXT("gait: hysteresis holds the current gait near a boundary"));
    Check(FMath::IsNearlyEqual(APA250MountedSoldier::GaitPlayRate(1028.6f, G::Gallop), 1.f, .01f)
              && APA250MountedSoldier::GaitPlayRate(5000.f, G::Gallop) <= 1.3f,
          TEXT("gait: play rate matches speed and is clamped"));
    Check(ARFCharacter::MountedFireSwayDegrees(0.f) == 0.f && FMath::IsNearlyEqual(ARFCharacter::MountedFireSwayDegrees(1028.6f), 4.f, .01f),
          TEXT("saddle sway: none standing, 4 deg at a gallop"));
    Check(ARFCharacter::MountedSwordDamage(0.f) == 55.f && FMath::IsNearlyEqual(ARFCharacter::MountedSwordDamage(1028.6f), 120.f, .1f),
          TEXT("sabre: 55 standing, 120 at a gallop"));

    // --- Riding ---
    Pawn->FactionId = TEXT("continental");
    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking); // floating test pawn: mounting needs ground
    // Firing starts a reload and mounting mid-reload is refused: ride on with a fresh, loaded musket.
    if (Pawn->Musket) Pawn->Musket->Destroy();
    Spawn.Owner = Pawn;
    Spawn.Instigator = Pawn;
    Pawn->Musket = World->SpawnActor<AMusketWeapon>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    UE_LOG(LogTemp, Display, TEXT("MountedWeapons info: before remount controller=%s player=%d ground=%d reloading=%d dead=%d"),
           Pawn->GetController() ? *Pawn->GetController()->GetName() : TEXT("none"), Pawn->IsPlayerControlled() ? 1 : 0,
           Pawn->GetCharacterMovement()->IsMovingOnGround() ? 1 : 0, Pawn->Musket && Pawn->Musket->IsReloading() ? 1 : 0,
           Pawn->IsDead() ? 1 : 0);
    Pawn->ToggleMounted();
    Visual = nullptr;
    for (TActorIterator<APA250MountedSoldier> It(World); It; ++It) if (It->GetOwner() == Pawn) Visual = *It;
    UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
    Check(Pawn->IsMounted() && Visual && FMath::IsNearlyEqual(Move->MaxWalkSpeed, 143.3f, .1f), TEXT("riding: mounts at a walk"));
    Check(Pawn->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Ignore,
          TEXT("riding: foot capsule lets balls through to the horse/rider volumes"));
    Pawn->MountedGaitUp(); Pawn->MountedGaitUp(); Pawn->MountedGaitUp(); Pawn->MountedGaitUp();
    Check(Pawn->GetMountedGait() == 3 && FMath::IsNearlyEqual(Move->MaxWalkSpeed, 1028.6f, .1f), TEXT("riding: Shift x3 reaches a gallop and stops there"));
    Pawn->MountedGaitDown();
    Check(Pawn->GetMountedGait() == 2 && FMath::IsNearlyEqual(Move->MaxWalkSpeed, 550.f, .1f), TEXT("riding: Ctrl reins down to a canter"));
    if (Visual)
    {
        auto ClipIs = [Visual](const TCHAR* Part) { return Visual->GetHorseClip() && Visual->GetHorseClip()->GetName().Contains(Part); };
        Visual->SetGroundSpeed(1028.6f);
        Check(ClipIs(TEXT("Gallop")) && FMath::IsNearlyEqual(Visual->GetHorsePlayRate(), 1.f, .01f), TEXT("horse: gallop clip at gallop speed"));
        Visual->SetGroundSpeed(352.5f);
        Check(ClipIs(TEXT("Trot")), TEXT("horse: trot clip at trot speed"));
        Visual->SetGroundSpeed(0.f);
        Check(ClipIs(TEXT("Idle")), TEXT("horse: idle when standing"));

        // Hit routing: horse volume wounds the horse; rider volume wounds the man.
        // (A pawn takes no damage in this bare world - APawn::ShouldTakeDamage needs a game mode - so the
        // man's side is checked as "not the horse"; his wound is seen in the rendered battle run.)
        UBoxComponent* HorseVol = Visual->FindComponentByClass<UBoxComponent>();
        UCapsuleComponent* RiderVol = Visual->FindComponentByClass<UCapsuleComponent>();
        FHitResult HorseHit(Visual, HorseVol, Visual->GetActorLocation() + FVector(60, 0, 120), FVector::UpVector);
        UGameplayStatics::ApplyPointDamage(Visual, 100.f, FVector::ForwardVector, HorseHit, nullptr, nullptr, nullptr);
        Check(FMath::IsNearlyEqual(Visual->GetHorseHealth(), APA250MountedSoldier::HorseMaxHealth - 100.f, .1f),
              TEXT("hits: a ball in the horse wounds the horse"));
        FHitResult RiderHit(Visual, RiderVol, Visual->GetActorLocation() + FVector(0, 0, 210), FVector::UpVector);
        UGameplayStatics::ApplyPointDamage(Visual, 10.f, FVector::ForwardVector, RiderHit, nullptr, nullptr, nullptr);
        Check(FMath::IsNearlyEqual(Visual->GetHorseHealth(), APA250MountedSoldier::HorseMaxHealth - 100.f, .1f) && !Visual->IsHorseDead(),
              TEXT("hits: a ball in the rider volume goes to the man, not the horse"));

        // Sabre rule: enemies take speed-scaled damage, his own side none.
        Spawn.Owner = nullptr;
        Spawn.Instigator = nullptr;
        auto* Foe = World->SpawnActor<ARFCharacter>(FVector(300, 200, 200), FRotator::ZeroRotator, Spawn);
        auto* Friend = World->SpawnActor<ARFCharacter>(FVector(300, -200, 200), FRotator::ZeroRotator, Spawn);
        if (Foe && Friend)
        {
            Foe->FactionId = TEXT("british");
            Friend->FactionId = TEXT("continental");
            const float ToFoe = Pawn->OnMountedSwordHit(Foe, Foe->GetActorLocation());
            const float ToFriend = Pawn->OnMountedSwordHit(Friend, Friend->GetActorLocation());
            Check(FMath::IsNearlyEqual(ToFoe, ARFCharacter::MountedSwordDamage(0.f), .5f) && ToFriend == 0.f,
                  TEXT("sabre: cuts an enemy, never his own side"));
            Foe->Destroy();
            Friend->Destroy();
        }

        // The horse goes down: it stays on the field, the man is thrown and gets back up.
        UGameplayStatics::ApplyPointDamage(Visual, 1000.f, FVector::ForwardVector, HorseHit, nullptr, nullptr, nullptr);
        Check(Visual->IsHorseDead() && !Pawn->IsMounted() && Visual->GetAttachParentActor() == nullptr && !Visual->IsActorBeingDestroyed(),
              TEXT("death: horse killed by fire stays on the field, detached from the rider"));
        Check(Visual->GetHorseClip() && Visual->GetHorseClip()->GetName().Contains(TEXT("Death")), TEXT("death: horse plays its death clip"));
        Check(Pawn->IsThrownFromHorse() && Pawn->GetMesh()->IsSimulatingPhysics()
                  && Pawn->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision,
              TEXT("death: rider thrown as a loose body"));
        Check(Pawn->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block, TEXT("death: foot capsule takes balls again"));
        Check(!Pawn->FireMusketAt(FVector(5000, 0, 200), false), TEXT("death: no firing while thrown"));
        Pawn->ToggleMounted();
        Check(!Pawn->IsMounted(), TEXT("death: cannot remount at once"));
        for (int32 i = 0; i < 200 && Pawn->IsThrownFromHorse(); ++i) static_cast<AActor*>(Pawn)->Tick(.1f);
        Check(!Pawn->IsThrownFromHorse() && !Pawn->GetMesh()->IsSimulatingPhysics() && Move->MovementMode == MOVE_Walking
                  && Pawn->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics,
              TEXT("death: rider gets back on his feet (physics off, walking, capsule restored)"));
        Check(Pawn->GetMesh()->GetAttachParent() == Pawn->GetCapsuleComponent(), TEXT("death: body re-attached to the capsule"));
    }
    // --- Dragoons (GAME INVENTION for 1775): decision table, kits, and a horse of his own ---
    {
        auto D = [](bool bMounted, bool bHorse, float Enemy, float Goal, float HorseDist, bool bReturning = false) {
            FPA250DragoonView V; V.bMounted = bMounted; V.bHorseAlive = bHorse; V.EnemyDistCm = Enemy; V.GoalDistCm = Goal; V.HorseDistCm = HorseDist;
            V.bReturning = bReturning;
            return PA250DragoonDecide(V);
        };
        using A = EPA250DragoonAct;
        Check(D(true, true, -1.f, 20000.f, 0.f) == A::Ride && D(true, true, 7000.f, 20000.f, 0.f) == A::Ride,
              TEXT("dragoon: rides while no enemy is within 60 m and the post is far"));
        Check(D(true, true, 5500.f, 20000.f, 0.f) == A::Dismount && D(true, true, -1.f, 2000.f, 0.f) == A::Dismount,
              TEXT("dragoon: gets down inside 60 m of an enemy, or at his post"));
        Check(D(false, true, 5500.f, 20000.f, 5000.f) == A::FightOnFoot && D(false, true, 11000.f, 20000.f, 5000.f) == A::FightOnFoot,
              TEXT("dragoon: on foot he fights while an enemy is inside 120 m (no flip-flop at 60-120 m)"));
        Check(D(false, true, -1.f, 20000.f, 5000.f) == A::ReturnToHorse && D(false, true, 13000.f, 20000.f, 100.f) == A::Mount,
              TEXT("dragoon: ground to cover and nobody near: back to his horse, then up"));
        Check(D(false, true, -1.f, 3000.f, 100.f) == A::FightOnFoot, TEXT("dragoon: at his post he stays on foot"));
        Check(D(false, true, -1.f, 5000.f, 3000.f) == A::FightOnFoot && D(false, true, -1.f, 5000.f, 3000.f, true) == A::ReturnToHorse
                  && D(false, true, -1.f, 3000.f, 3000.f, true) == A::FightOnFoot,
              TEXT("dragoon: sets off for his horse past 60 m, keeps going until within 35 m (no flip-flop at 50 m)"));
        Check(D(false, false, -1.f, 20000.f, 0.f) == A::FightOnFoot, TEXT("dragoon: horse lost, he fights on foot for good"));
        Check(PA250DragoonGait(5000.f) == 3 && PA250DragoonGait(2000.f) == 2 && PA250DragoonGait(500.f) == 1, TEXT("dragoon: gait for the ground to cover"));
        UE_LOG(LogTemp, Display, TEXT("MountedWeapons info: kit brit=%s cont=%s bayonet=%d order=%d"),
               *PA250UnitClasses::KitFor(TEXT("british"), TEXT("dragoon")).Id.ToString(),
               *PA250UnitClasses::KitFor(TEXT("continental"), TEXT("dragoon")).Id.ToString(),
               static_cast<int32>(PA250UnitClasses::KitFor(TEXT("continental"), TEXT("dragoon")).Bayonet),
               PA250UnitClasses::DisplayOrder().Contains(TEXT("dragoon")) ? 1 : 0);
        Check(PA250UnitClasses::KitFor(TEXT("british"), TEXT("dragoon")).Id == TEXT("dragoon")
                  && PA250UnitClasses::KitFor(TEXT("continental"), TEXT("dragoon")).Id == TEXT("dragoon")
                  && PA250UnitClasses::KitFor(TEXT("continental"), TEXT("dragoon")).Bayonet == EPA250Bayonet::None
                  && PA250UnitClasses::DisplayOrder().Contains(TEXT("dragoon")),
              TEXT("dragoon: a kit on both sides (no bayonet) and a place on the spawn screen"));

        Pawn->UnitClass = TEXT("dragoon");
        Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Check(Pawn->MountHorse(nullptr, true) && Pawn->IsMounted() && Pawn->GetOwnHorse() == Pawn->GetMountedVisual(),
              TEXT("dragoon: spawns in the saddle on a horse of his own"));
        APA250MountedSoldier* Own = Pawn->GetOwnHorse();
        Pawn->PicketHorse();
        Check(!Pawn->IsMounted() && Own && !Own->IsRidden() && !Own->IsHorseDead() && Own->GetAttachParentActor() == nullptr,
              TEXT("dragoon: dismounting leaves his horse picketed, riderless"));
        Pawn->SetActorLocation(Pawn->GetActorLocation() + FVector(1000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
        Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Pawn->ToggleMounted();
        Check(!Pawn->IsMounted(), TEXT("dragoon: H from 10 m away does not mount (his horse is over there)"));
        if (Own)
        {
            Pawn->SetActorLocation(Own->GetActorLocation() + FVector(100.f, 0.f, 90.f), false, nullptr, ETeleportType::TeleportPhysics);
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            UE_LOG(LogTemp, Display, TEXT("MountedWeapons info: remount dist=%.0f ground=%d reloading=%d player=%d thrown=%d dead=%d"),
                   FVector::Dist2D(Own->GetActorLocation(), Pawn->GetActorLocation()), Pawn->GetCharacterMovement()->IsMovingOnGround() ? 1 : 0,
                   Pawn->Musket && Pawn->Musket->IsReloading() ? 1 : 0, Pawn->IsPlayerControlled() ? 1 : 0, Pawn->IsThrownFromHorse() ? 1 : 0,
                   Pawn->IsDead() ? 1 : 0);
            Pawn->ToggleMounted();
            Check(Pawn->IsMounted() && Pawn->GetMountedVisual() == Own && Own->IsRidden(), TEXT("dragoon: H beside his horse mounts that same horse"));
            Pawn->PicketHorse();
            FHitResult OwnHit(Own, Own->FindComponentByClass<UBoxComponent>(), Own->GetActorLocation() + FVector(0, 0, 120), FVector::UpVector);
            UGameplayStatics::ApplyPointDamage(Own, 1000.f, FVector::ForwardVector, OwnHit, nullptr, nullptr, nullptr);
            Check(Own->IsHorseDead() && !Pawn->IsThrownFromHorse(), TEXT("dragoon: a picketed horse shot dead goes down alone"));
            Pawn->ToggleMounted();
            Check(!Pawn->IsMounted(), TEXT("dragoon: horse lost, no remount"));
        }
        Pawn->UnitClass = TEXT("line_infantry");
        Pawn->ReleaseOwnHorse();
        Check(Pawn->GetOwnHorse() == nullptr, TEXT("dragoon: re-kitted, his horse is led away"));
    }
    Pawn->Destroy();
    PC->Destroy();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return Fails ? 1 : 0;
}
