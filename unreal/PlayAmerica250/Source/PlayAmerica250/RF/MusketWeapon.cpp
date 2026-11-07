#include "MusketWeapon.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MusketProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMusketWeapon::AMusketWeapon()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	MusketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MusketMesh"));
	MusketMesh->SetupAttachment(RootComponent);
	MusketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Graybox musket: a long thin cylinder lying along +X.
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

void AMusketWeapon::SetState(EMusketState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(State);
	}
}

bool AMusketWeapon::Fire(FVector Direction)
{
	if (State != EMusketState::Loaded || !ProjectileClass || !GetWorld())
	{
		return false;
	}

	const FVector Location = MuzzleLocation->GetComponentLocation();
	const FRotator Rotation = Direction.IsNearlyZero() ? MuzzleLocation->GetComponentRotation() : Direction.Rotation();

	const FMusketTuningValues& T = GetTuning();
	FTransform SpawnTransform(Rotation, Location);
	AMusketProjectile* Ball = GetWorld()->SpawnActorDeferred<AMusketProjectile>(
		ProjectileClass, SpawnTransform, this, GetInstigator(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Ball)
	{
		Ball->Damage = T.Damage;
		Ball->GetProjectileMovement()->InitialSpeed = T.ProjectileSpeed;
		Ball->GetProjectileMovement()->MaxSpeed = T.ProjectileSpeed;
		Ball->FinishSpawning(SpawnTransform);
	}

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Location);
	}

	SetState(EMusketState::Reloading);
	StartReload();
	return Ball != nullptr;
}

void AMusketWeapon::StartReload()
{
	if (State == EMusketState::Loaded)
	{
		return;
	}
	GetWorldTimerManager().SetTimer(ReloadTimerHandle, this, &AMusketWeapon::OnReloadComplete, GetTuning().ReloadTime, false);
}

void AMusketWeapon::OnReloadComplete()
{
	SetState(EMusketState::Loaded);
}

float AMusketWeapon::GetReloadProgress() const
{
	if (State == EMusketState::Loaded)
	{
		return 1.f;
	}
	const FTimerManager& TM = GetWorldTimerManager();
	if (TM.IsTimerActive(ReloadTimerHandle))
	{
		return FMath::Clamp(TM.GetTimerElapsed(ReloadTimerHandle) / FMath::Max(GetTuning().ReloadTime, 0.01f), 0.f, 1.f);
	}
	return 0.f;
}
