#include "MusketProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PA250BattleGameMode.h"
#include "PA250WeaponTuning.h"
#include "PA250BattleAudio.h"
#include "PA250Smoke.h"
#include "RFCharacter.h"
#include "Mounted/PA250MountedSoldier.h"
#include "UObject/ConstructorHelpers.h"

AMusketProjectile::AMusketProjectile()
{
	PrimaryActorTick.bCanEverTick = true; // tracer length

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
	CollisionComponent->InitSphereRadius(5.f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	CollisionComponent->OnComponentHit.AddDynamic(this, &AMusketProjectile::OnHit);
	RootComponent = CollisionComponent;

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	BallMesh->SetupAttachment(RootComponent);
	BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BallMesh->SetRelativeScale3D(FVector(0.06f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
	}

	TracerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TracerMesh"));
	TracerMesh->SetupAttachment(RootComponent);
	TracerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TracerMesh->SetCastShadow(false);
	TracerMesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f)); // cylinder axis (Z) -> flight axis (X)
	TracerMesh->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TracerCyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (TracerCyl.Succeeded())
	{
		TracerMesh->SetStaticMesh(TracerCyl.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	if (Glow.Succeeded())
	{
		TracerMesh->SetMaterial(0, Glow.Object);
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->InitialSpeed = 30000.f;
	ProjectileMovement->MaxSpeed = 30000.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.3f;

	InitialLifeSpan = 2.f; // 200 m max range at 300 m/s is ~0.7 s
}

void AMusketProjectile::BeginPlay()
{
	Super::BeginPlay();
	SpawnLocation = GetActorLocation();
	if (UMaterialInstanceDynamic* MID = TracerMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"), TracerColor);
	}
	UpdateTracer();
	if (AActor* Shooter = GetInstigator())
	{
		CollisionComponent->IgnoreActorWhenMoving(Shooter, true);
	}
}

void AMusketProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                              FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority())
	{
		Destroy();
		return;
	}
	// A mounted man is hit through his horse's volumes: the rider volume is the man himself,
	// the rest is the horse (flesh, but not a man for the shooter's marker or kill credit).
	ARFCharacter* HitMan = Cast<ARFCharacter>(OtherActor);
	bool bFlesh = HitMan != nullptr;
	if (const APA250MountedSoldier* Mount = Cast<APA250MountedSoldier>(OtherActor))
	{
		bFlesh = !Mount->IsHorseDead() || Mount->IsRiderComponent(OtherComp);
		if (Mount->IsRiderComponent(OtherComp)) HitMan = Cast<ARFCharacter>(Mount->GetOwner());
	}
	// The player sees where his own ball went - the feedback that teaches the ballistics.
	if (const APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (GetInstigator() && GetInstigator() == LocalPC->GetPawn())
		{
			const ARFCharacter* VictimChar = HitMan;
			const ARFCharacter* ShooterChar = Cast<ARFCharacter>(GetInstigator());
			const bool bEnemy = VictimChar && ShooterChar && VictimChar->FactionId != ShooterChar->FactionId;
			SpawnImpactMarker(GetWorld(), Hit.ImpactPoint, bEnemy);
		}
	}
	// A ball that finds a man already down does nothing more than move him.
	if (const ARFCharacter* Down = Cast<ARFCharacter>(OtherActor); Down && Down->IsDead())
	{
		if (OtherComp && OtherComp->IsSimulatingPhysics())
		{
			OtherComp->AddImpulseAtLocation(GetVelocity() * 0.5f, Hit.ImpactPoint);
		}
		Destroy();
		return;
	}
	// Seen as well as heard: dirt thrown up where a ball goes to ground, a red mist on a man.
	if (UPA250SmokeSubsystem* Bank = GetWorld() ? GetWorld()->GetSubsystem<UPA250SmokeSubsystem>() : nullptr)
	{
		const bool bMan = bFlesh;
		Bank->AddImpact(Hit.ImpactPoint, bMan ? -GetVelocity().GetSafeNormal() : FVector(Hit.ImpactNormal), bMan);
	}
	// Impact sound: a dull thud on a man, a dirt burst on anything else.
	FPA250BattleAudio::PlayAt(this, bFlesh ? TEXT("impact_body") : TEXT("impact_ground"),
		Hit.ImpactPoint, EPA250SoundRange::Near, 1.f, 0.1f);
	if (OtherActor && OtherActor != this && OtherActor != GetInstigator())
	{
		bool bApply = true;
		if (!bFriendlyFire)
		{
			const ARFCharacter* ShooterChar = Cast<ARFCharacter>(GetInstigator());
			const ARFCharacter* VictimChar = HitMan;
			if (ShooterChar && VictimChar && ShooterChar->FactionId == VictimChar->FactionId)
			{
				bApply = false;
			}
		}
		if (bApply)
		{
			float Applied = Damage;
			const float DistCm = FVector::Dist(SpawnLocation, Hit.ImpactPoint);
			if (WeaponTuning)
			{
				Applied = WeaponTuning->Values.DamageAtDistanceCm(DistCm / FMath::Max(0.1f, RangeScale));
				if (Applied <= 0.f)
				{
					Destroy();
					return;
				}
				// Head heuristic: hits above mid-capsule count as headshots.
				if (const APawn* Pawn = Cast<APawn>(OtherActor))
				{
					const float MidZ = Pawn->GetActorLocation().Z + 40.f;
					if (Hit.ImpactPoint.Z >= MidZ + 35.f)
					{
						Applied *= WeaponTuning->Values.HeadMultiplier;
					}
				}
				// Preserve perfect-reload bonus baked into Damage relative to near torso.
				const float Near = FMath::Max(1.f, WeaponTuning->Values.TorsoDamageNear);
				Applied *= (Damage / Near);
			}
			UGameplayStatics::ApplyPointDamage(OtherActor, Applied, GetVelocity().GetSafeNormal(), Hit,
			                                   GetInstigatorController(), this, nullptr);
			if (OtherComp && OtherComp->IsSimulatingPhysics())
			{
				OtherComp->AddImpulseAtLocation(GetVelocity() * 0.5f, Hit.ImpactPoint);
			}
			if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
			{
				GM->NotifyProjectileHit(Cast<ARFCharacter>(GetInstigator()), HitMan, Applied);
			}
		}
	}
	Destroy();
}

void AMusketProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateTracer();
	// Whiz when the ball passes within 5 m of the listener (not for the listener's own shots).
	const FVector Now = GetActorLocation();
	FVector Listener;
	if (!bWhizzed && bHavePrev && FPA250BattleAudio::GetListener(this, Listener))
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const bool bOwnShot = PC && GetInstigator() && GetInstigator() == PC->GetPawn();
		const FVector Closest = FMath::ClosestPointOnSegment(Listener, PrevLocation, Now);
		if (!bOwnShot && FVector::Dist(Closest, Listener) < 500.f && FVector::Dist(SpawnLocation, Listener) > 800.f)
		{
			bWhizzed = true;
			// A near miss makes the player start.
			if (ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr; Me && !Me->IsDead())
			{
				Me->PunchFOV(2.f);
			}
			FPA250BattleAudio::PlayAt(this, TEXT("ball_whiz"), Closest, EPA250SoundRange::Whiz, 1.f, 0.12f);
		}
	}
	PrevLocation = Now;
	bHavePrev = true;
}

void AMusketProjectile::UpdateTracer()
{
	if (!TracerMesh) return;
	// The streak trails the ball and never reaches back past the muzzle (no streak in the shooter's face).
	const float Len = FMath::Min(TracerLengthCm, FVector::Dist(SpawnLocation, GetActorLocation()));
	if (Len < 20.f)
	{
		TracerMesh->SetVisibility(false);
		return;
	}
	TracerMesh->SetRelativeLocation(FVector(-0.5f * Len, 0.f, 0.f));
	TracerMesh->SetRelativeScale3D(FVector(TracerThicknessCm / 100.f, TracerThicknessCm / 100.f, Len / 100.f));
	TracerMesh->SetVisibility(true);
}

AActor* AMusketProjectile::SpawnImpactMarker(UWorld* World, const FVector& At, bool bHitMan)
{
	if (!World) return nullptr;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* M = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, P);
	if (!M) return nullptr;
	UStaticMeshComponent* C = M->GetStaticMeshComponent();
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetWorldScale3D(FVector(bHitMan ? 0.3f : 0.2f));
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial")))
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, M))
		{
			MID->SetVectorParameterValue(TEXT("Color"), bHitMan ? FLinearColor(12.f, 0.6f, 0.4f) : FLinearColor(6.f, 6.f, 5.f));
			C->SetMaterial(0, MID);
		}
	}
	M->SetLifeSpan(1.5f);
	return M;
}
