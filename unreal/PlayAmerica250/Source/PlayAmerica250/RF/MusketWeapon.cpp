#include "MusketWeapon.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MusketProjectile.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PA250ThirdPartyAssets.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

AMusketWeapon::AMusketWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	MusketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MusketMesh"));
	MusketMesh->SetupAttachment(RootComponent);
	MusketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MusketMesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	MusketMesh->SetRelativeScale3D(FVector(0.03f, 0.03f, 1.2f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		MusketMesh->SetStaticMesh(Cyl.Object);
	}

	MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	MuzzleLocation->SetupAttachment(RootComponent);
	MuzzleLocation->SetRelativeLocation(FVector(65.f, 0.f, 0.f));

	ProjectileClass = AMusketProjectile::StaticClass();
}

void AMusketWeapon::BeginPlay()
{
	Super::BeginPlay();
	SyncTuningToSequencer();
}

void AMusketWeapon::SyncTuningToSequencer()
{
	const FMusketTuningValues& T = GetTuning();
	FActiveReloadTuningPlain P;
	P.PhasePrimePan = T.ActiveReload.PhasePrimePan;
	P.PhaseRamCharge = T.ActiveReload.PhaseRamCharge;
	P.PhaseShoulderAim = T.ActiveReload.PhaseShoulderAim;
	P.SweetZoneFraction = T.ActiveReload.SweetZoneFraction;
	P.PerfectZoneFraction = T.ActiveReload.PerfectZoneFraction;
	P.PerfectReloadScale = T.ActiveReload.PerfectReloadScale;
	P.MissDelaySeconds = T.ActiveReload.MissDelaySeconds;
	P.JamChanceOnMiss = T.ActiveReload.JamChanceOnMiss;
	P.JamClearHoldSeconds = T.ActiveReload.JamClearHoldSeconds;
	P.PerfectDamageBonus = T.ActiveReload.PerfectDamageBonus;
	Sequencer.Configure(P);
}

void AMusketWeapon::SetState(EMusketState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(State);
	}
}

void AMusketWeapon::SyncStateFromSequencer()
{
	if (Sequencer.IsJammed())
	{
		SetState(EMusketState::Jammed);
	}
	else if (Sequencer.IsComplete() || Sequencer.GetPhase() == EActiveReloadPhase::Idle)
	{
		if (State != EMusketState::Loaded)
		{
			Sequencer.Reset();
			SetState(EMusketState::Loaded);
		}
	}
	else if (Sequencer.IsReloading())
	{
		SetState(EMusketState::Reloading);
	}
}

bool AMusketWeapon::Fire(FVector Direction, bool bIsAiming, float OwnerSpeed, float AccuracyScale)
{
	if (State != EMusketState::Loaded || !ProjectileClass || !GetWorld())
	{
		return false;
	}

	SyncTuningToSequencer();
	const FMusketTuningValues& T = GetTuning();
	FVector Dir = Direction.IsNearlyZero() ? MuzzleLocation->GetForwardVector() : Direction.GetSafeNormal();

	float Spread = T.Accuracy.BaseSpreadDegrees;
	if (bIsAiming) Spread *= T.Accuracy.AimingSpreadScale;
	if (OwnerSpeed < T.Accuracy.MoveSpeedThreshold) Spread *= T.Accuracy.StillSpreadScale;
	else Spread *= T.Accuracy.MovingSpreadScale;
	Spread *= FMath::Max(0.05f, AccuracyScale);
	if (Spread > 0.01f)
	{
		Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(Spread));
	}

	const FVector Location = MuzzleLocation->GetComponentLocation();
	const FRotator Rotation = Dir.Rotation();
	const float Bonus = Sequencer.ConsumeDamageBonus();

	FTransform SpawnTransform(Rotation, Location);
	AMusketProjectile* Ball = GetWorld()->SpawnActorDeferred<AMusketProjectile>(
		ProjectileClass, SpawnTransform, this, GetInstigator(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Ball)
	{
		Ball->Damage = T.Damage * (1.f + Bonus);
		Ball->GetProjectileMovement()->InitialSpeed = T.ProjectileSpeed;
		Ball->GetProjectileMovement()->MaxSpeed = T.ProjectileSpeed;
		Ball->FinishSpawning(SpawnTransform);
	}

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Location);
	}
	SpawnMuzzleSmoke(Location, Rotation);

	StartReload();
	return Ball != nullptr;
}

void AMusketWeapon::StartReload()
{
	SyncTuningToSequencer();
	Sequencer.BeginReload();
	SetState(EMusketState::Reloading);
}

EActiveReloadHit AMusketWeapon::CommitReload()
{
	if (!Sequencer.IsReloading() || Sequencer.IsJammed())
	{
		return EActiveReloadHit::None;
	}
	const EActiveReloadHit Hit = Sequencer.TryCommit();
	SyncStateFromSequencer();
	return Hit;
}

void AMusketWeapon::TickBotReload(float Skill01, FRandomStream& Rng)
{
	if (!Sequencer.IsReloading() || Sequencer.IsJammed())
	{
		return;
	}
	if (BotCommitCooldown > 0.f)
	{
		return;
	}
	// Commit near the sweet center.
	if (Sequencer.GetPhaseProgress() >= 0.45f)
	{
		Sequencer.BotCommit(Skill01, Rng);
		BotCommitCooldown = 0.15f;
		SyncStateFromSequencer();
	}
}

void AMusketWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BotCommitCooldown = FMath::Max(0.f, BotCommitCooldown - DeltaSeconds);
	if (State == EMusketState::Loaded)
	{
		return;
	}
	Sequencer.Tick(DeltaSeconds);
	if (Sequencer.IsJammed())
	{
		Sequencer.TryClearJam(bReloadHeld, DeltaSeconds);
	}
	SyncStateFromSequencer();
}

float AMusketWeapon::GetReloadProgress() const
{
	if (State == EMusketState::Loaded)
	{
		return 1.f;
	}
	return Sequencer.GetPhaseProgress();
}

void AMusketWeapon::SpawnMuzzleSmoke(const FVector& Location, const FRotator& Rotation) const
{
	// Visual only: Fab muzzle smoke from Content/ThirdParty/FX when present; otherwise none.
	static TWeakObjectPtr<UNiagaraSystem> Smoke;
	static bool bResolved = false;
	if (!bResolved)
	{
		bResolved = true;
		Smoke = PA250ThirdParty::LoadFirstOf<UNiagaraSystem>(PA250ThirdParty::MuzzleSmoke);
	}
	if (UNiagaraSystem* System = Smoke.Get())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Location, Rotation);
	}
}
