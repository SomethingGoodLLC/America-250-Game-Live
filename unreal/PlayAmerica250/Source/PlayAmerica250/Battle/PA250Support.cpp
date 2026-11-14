#include "PA250Support.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PA250FlagArt.h"
#include "RFCharacter.h"

APA250Colours::APA250Colours()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// PLACEHOLDER ART: a staff (engine cylinder) and the faction's flag on a plane. A regimental
	// colour with its cords and a spear-point finial is listed in PROGRESS.md as art still needed.
	Pole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pole"));
	Pole->SetupAttachment(RootComponent);
	Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pole->SetRelativeScale3D(FVector(0.035f, 0.035f, 2.9f));
	Pole->SetRelativeLocation(FVector(0.f, 0.f, 145.f));
	Cloth = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cloth"));
	Cloth->SetupAttachment(RootComponent);
	Cloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The engine plane lies flat (100 x 100 cm): stood on edge, 160 x 80 cm, at the head of the staff.
	Cloth->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
	Cloth->SetRelativeScale3D(FVector(1.6f, 0.8f, 1.f));
	Cloth->SetRelativeLocation(FVector(82.f, 0.f, 245.f));
}

void APA250Colours::Raise(FName InFaction, ARFCharacter* InBearer)
{
	Faction = InFaction;
	if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))) Pole->SetStaticMesh(Cylinder);
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"))) Cloth->SetStaticMesh(Plane);
	UMaterialInterface* FlagMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_PA250_Flag.M_PA250_Flag"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UTexture2D* Art = PA250FlagArt::GetTexture(Faction);
	if (FlagMat && Art)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FlagMat, this);
		MID->SetTextureParameterValue(TEXT("Flag"), Art);
		Cloth->SetMaterial(0, MID);
	}
	TakeUp(InBearer);
}

void APA250Colours::Drop()
{
	if (State != EPA250ColoursState::Carried) return;
	const FVector Where = GetActorLocation();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	// Fallen: the staff lies on the ground where the bearer stood.
	SetActorLocationAndRotation(FVector(Where.X, Where.Y, Where.Z - 60.f), FRotator(80.f, GetActorRotation().Yaw, 0.f));
	Bearer = nullptr;
	State = EPA250ColoursState::Fallen;
	EnemyOverSeconds = 0.f;
}

void APA250Colours::TakeUp(ARFCharacter* NewBearer)
{
	if (!NewBearer || State == EPA250ColoursState::Taken) return;
	Bearer = NewBearer;
	State = EPA250ColoursState::Carried;
	EnemyOverSeconds = 0.f;
	SetActorRotation(FRotator(0.f, NewBearer->GetActorRotation().Yaw, 0.f));
	AttachToActor(NewBearer, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	// Held at his left side, clear of the musket, feet of the staff near the ground.
	SetActorRelativeLocation(FVector(-10.f, -38.f, -88.f));
}

void APA250Colours::CarryOff()
{
	State = EPA250ColoursState::Taken;
	Bearer = nullptr;
	SetActorHiddenInGame(true);
}
