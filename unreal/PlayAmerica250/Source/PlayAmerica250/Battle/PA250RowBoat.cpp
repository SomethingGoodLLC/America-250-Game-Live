#include "PA250RowBoat.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "PA250BotController.h"
#include "RFCharacter.h"
#include "pa250/sim.hpp"

APA250RowBoat::APA250RowBoat()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(RootComponent);
	Bow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bow"));
	Bow->SetupAttachment(RootComponent);
	for (UStaticMeshComponent* C : {Hull.Get(), Bow.Get()})
	{
		C->SetMobility(EComponentMobility::Movable);
		// The men stand in it and are moved with it: nothing of the boat pushes them or stops a ball.
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void APA250RowBoat::Launch(FName InFaction, const FVector& Start, const FVector& InBeach, const FVector& InInland, int32 InSeats)
{
	Faction = InFaction;
	Beach = InBeach;
	Inland = InInland.GetSafeNormal2D();
	if (Inland.IsNearlyZero()) Inland = (InBeach - Start).GetSafeNormal2D();
	WaterZ = Start.Z;
	Seats.SetNum(FMath::Max(1, InSeats));
	// PLACEHOLDER ART: a box hull 6 m by 2 m with a pointed bow, in the battlefield's dark wood.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Wood = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/MI_PA250_WoodDark.MI_PA250_WoodDark"));
	if (Cube)
	{
		Hull->SetStaticMesh(Cube);
		Hull->SetRelativeScale3D(FVector(6.0f, 2.0f, 0.5f));
		Hull->SetRelativeLocation(FVector(-40.f, 0.f, 5.f));
		Bow->SetStaticMesh(Cube);
		Bow->SetRelativeScale3D(FVector(1.41f, 1.41f, 0.5f));
		Bow->SetRelativeRotation(FRotator(0.f, 45.f, 0.f));
		Bow->SetRelativeLocation(FVector(260.f, 0.f, 5.f));
		if (Wood)
		{
			Hull->SetMaterial(0, Wood);
			Bow->SetMaterial(0, Wood);
		}
	}
	SetActorLocationAndRotation(Start, FRotator(0.f, (Beach - Start).GetSafeNormal2D().Rotation().Yaw, 0.f));
	State = EPA250BoatState::Waiting;
	Tags.AddUnique(TEXT("PA250RowBoat"));
}

bool APA250RowBoat::HasRoom() const
{
	if (State != EPA250BoatState::Waiting && State != EPA250BoatState::Rowing) return false;
	for (const TWeakObjectPtr<ARFCharacter>& S : Seats)
	{
		if (!S.IsValid() || S->IsDead()) return true;
	}
	return false;
}

int32 APA250RowBoat::GetAboard() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<ARFCharacter>& S : Seats) N += (S.IsValid() && !S->IsDead()) ? 1 : 0;
	return N;
}

bool APA250RowBoat::Embark(ARFCharacter* Man)
{
	if (!Man || Man->IsDead() || Man->bEmbarked || !HasRoom()) return false;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		if (Seats[i].IsValid() && !Seats[i]->IsDead()) continue;
		Seats[i] = Man;
		Man->bEmbarked = true;
		if (UCharacterMovementComponent* Move = Man->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		if (APA250BotController* Bot = Cast<APA250BotController>(Man->GetController())) Bot->bScriptedHold = true;
		const float HalfHeight = Man->GetCapsuleComponent() ? Man->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		const FVector At = GetActorTransform().TransformPosition(PA250Boats::SeatOffset(i, Seats.Num()) + FVector(0.f, 0.f, 30.f + HalfHeight));
		Man->SetActorLocationAndRotation(At, GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		Man->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		if (AController* C = Man->GetController()) C->SetControlRotation(GetActorRotation());
		return true;
	}
	return false;
}

void APA250RowBoat::StartRowing()
{
	if (State == EPA250BoatState::Waiting) State = EPA250BoatState::Rowing;
}

void APA250RowBoat::Release(ARFCharacter* Man, const FVector& At, const FRotator& Facing)
{
	Man->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Man->SetActorLocationAndRotation(At, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = Man->GetCharacterMovement()) Move->SetMovementMode(MOVE_Walking);
	if (APA250BotController* Bot = Cast<APA250BotController>(Man->GetController())) Bot->bScriptedHold = false;
	if (AController* C = Man->GetController()) C->SetControlRotation(Facing);
	Man->bEmbarked = false;
}

void APA250RowBoat::SetMenAshore()
{
	UWorld* World = GetWorld();
	const FRotator Facing(0.f, Inland.Rotation().Yaw, 0.f);
	const FVector Right(-Inland.Y, Inland.X, 0.f);
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		ARFCharacter* Man = Seats[i].Get();
		Seats[i] = nullptr;
		if (!Man || Man->IsDead()) continue;
		const FVector Off = PA250Boats::AshoreOffset(i);
		FVector At = Beach + Inland * Off.X + Right * Off.Y;
		// On the ground in front of the bow (the boat and the men are not the ground).
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Landing), false, this);
		Params.AddIgnoredActor(Man);
		const float HalfHeight = Man->GetCapsuleComponent() ? Man->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		if (World && World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 800.f), At - FVector(0.f, 0.f, 1500.f), ECC_WorldStatic, Params)) At.Z = Hit.ImpactPoint.Z;
		Release(Man, At + FVector(0.f, 0.f, HalfHeight + 8.f), Facing);
		++SetAshore;
	}
	State = EPA250BoatState::Empty;
	UE_LOG(LogTemp, Display, TEXT("PA250 landing: a boat sets %d %s men ashore"), SetAshore, *Faction.ToString());
}

void APA250RowBoat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const pa250::BattleRules& R = pa250::battle_rules();
	// A man killed in the boat is no longer aboard.
	for (TWeakObjectPtr<ARFCharacter>& S : Seats)
	{
		if (ARFCharacter* Man = S.Get(); Man && Man->IsDead())
		{
			Man->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Man->bEmbarked = false;
			S = nullptr;
		}
	}
	if (State == EPA250BoatState::Rowing)
	{
		BobTime += DeltaSeconds;
		FVector At = GetActorLocation();
		const FVector To = FVector(Beach.X, Beach.Y, At.Z) - At;
		const float Step = R.landing_boat_speed_cm * DeltaSeconds;
		if (To.Size2D() <= Step)
		{
			SetActorLocation(FVector(Beach.X, Beach.Y, WaterZ));
			State = EPA250BoatState::Beached;
			BeachedSeconds = 0.f;
		}
		else
		{
			At += To.GetSafeNormal2D() * Step;
			At.Z = WaterZ + 4.f * FMath::Sin(BobTime * 2.1f); // she lifts a little to each stroke
			SetActorLocation(At);
		}
	}
	else if (State == EPA250BoatState::Beached)
	{
		BeachedSeconds += DeltaSeconds;
		if (BeachedSeconds >= R.landing_disembark_seconds) SetMenAshore();
	}
}
