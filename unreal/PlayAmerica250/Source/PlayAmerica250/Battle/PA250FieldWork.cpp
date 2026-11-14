#include "PA250FieldWork.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "pa250/sim.hpp"

void PA250Works::RingSlots(const FVector& Post, const FVector& TowardEnemy, float RingCm, int32 Count, float SpacingCm,
	TArray<FVector>& OutPlaces, TArray<FVector>& OutFronts)
{
	OutPlaces.Reset();
	OutFronts.Reset();
	FVector Fwd = TowardEnemy.GetSafeNormal2D();
	if (Fwd.IsNearlyZero()) Fwd = FVector::ForwardVector;
	const float Ring = FMath::Max(100.f, RingCm);
	// The angle one work takes up on the ring, with a gap a man can pass through.
	const float StepDeg = FMath::RadiansToDegrees(FMath::Max(100.f, SpacingCm) / Ring);
	for (int32 i = 0; i < Count; ++i)
	{
		// 0, +1, -1, +2, -2 ... steps either side of the middle.
		const int32 K = (i + 1) / 2;
		const float Deg = (i % 2 == 1 ? 1.f : -1.f) * K * StepDeg;
		const FVector Front = Fwd.RotateAngleAxis(Deg, FVector::UpVector);
		OutPlaces.Add(Post + Front * Ring);
		OutFronts.Add(Front);
	}
}

APA250FieldWork::APA250FieldWork()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void APA250FieldWork::Configure(EPA250WorkKind InKind, FName InFaction, float InBuilt)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	Kind = InKind;
	Faction = InFaction;
	Built = FMath::Clamp(InBuilt, 0.f, 1.f);
	MaxHealth = FMath::Max(1.f, Kind == EPA250WorkKind::Redoubt ? R.works_redoubt_health : R.works_breastwork_health);
	Health = MaxHealth;
	FullHeightCm = Kind == EPA250WorkKind::Redoubt ? R.works_redoubt_height_cm : R.works_breastwork_height_cm;
	// PLACEHOLDER ART: banks are the engine cube in the battlefield's dirt material. An earthwork mesh
	// (fascines, a firing step, a ditch) is listed in PROGRESS.md as art still needed.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Dirt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/MI_PA250_Dirt.MI_PA250_Dirt"));
	struct FBank
	{
		FVector Center; // actor space, on the ground
		float LengthCm;
		float YawDeg;   // the bank's long axis is its local Y
	};
	TArray<FBank> Plan;
	const float Thick = FMath::Max(20.f, R.works_breastwork_thickness_cm);
	if (Kind == EPA250WorkKind::Breastwork)
	{
		Plan.Add({FVector::ZeroVector, R.works_breastwork_length_cm, 0.f});
	}
	else
	{
		// Front and two flanks; the rear is open so its garrison can come and go.
		const float Side = FMath::Max(300.f, R.works_redoubt_side_cm);
		Plan.Add({FVector(Side * 0.5f, 0.f, 0.f), Side, 0.f});
		Plan.Add({FVector(0.f, Side * 0.5f, 0.f), Side, 90.f});
		Plan.Add({FVector(0.f, -Side * 0.5f, 0.f), Side, 90.f});
	}
	for (UStaticMeshComponent* Old : Banks)
	{
		if (Old) Old->DestroyComponent();
	}
	Banks.Reset();
	for (const FBank& B : Plan)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetupAttachment(RootComponent);
		M->SetStaticMesh(Cube);
		if (Dirt) M->SetMaterial(0, Dirt);
		M->SetMobility(EComponentMobility::Movable);
		M->SetRelativeLocation(B.Center);
		M->SetRelativeRotation(FRotator(0.f, B.YawDeg, 0.f));
		// The engine cube is 100 cm a side: X = thickness, Y = length; Z is set by Refresh.
		M->SetRelativeScale3D(FVector(Thick / 100.f, B.LengthCm / 100.f, 0.01f));
		M->SetCollisionProfileName(TEXT("BlockAll"));
		M->RegisterComponent();
		Banks.Add(M);
	}
	Tags.AddUnique(TEXT("PA250FieldWork"));
	Refresh();
}

bool APA250FieldWork::GivesCover() const
{
	return PA250Works::GivesCover(Built, Health, MaxHealth, pa250::battle_rules().works_cover_built_share);
}

FVector APA250FieldWork::GetWorkPoint() const
{
	const float Back = Kind == EPA250WorkKind::Redoubt ? 0.f : pa250::battle_rules().works_breastwork_thickness_cm * 0.5f + 90.f;
	return GetActorLocation() - GetActorForwardVector() * Back;
}

void APA250FieldWork::AddWork(float Share, FName ByFaction)
{
	if (Share <= 0.f) return;
	// Mending what is battered comes first; what is left of the labour raises the work.
	const float Mend = FMath::Min(Share, 1.f - Health / MaxHealth);
	Health = FMath::Min(MaxHealth, Health + Mend * MaxHealth);
	Built = FMath::Min(1.f, Built + (Share - Mend));
	if (!ByFaction.IsNone()) Faction = ByFaction;
	Refresh();
}

bool APA250FieldWork::Batter(float Damage)
{
	if (Damage <= 0.f || Health <= 0.f) return false;
	const bool bCoverBefore = GivesCover();
	Health = FMath::Max(0.f, Health - Damage);
	Refresh();
	return bCoverBefore && !GivesCover();
}

void APA250FieldWork::Refresh()
{
	const float Share = GetStandingShare();
	const bool bCover = GivesCover();
	const float Height = FMath::Max(1.f, FullHeightCm * Share);
	for (UStaticMeshComponent* M : Banks)
	{
		if (!M) continue;
		FVector Scale = M->GetRelativeScale3D();
		Scale.Z = Height / 100.f;
		M->SetRelativeScale3D(Scale);
		FVector Loc = M->GetRelativeLocation();
		Loc.Z = Height * 0.5f; // the cube's pivot is its centre: sit it on the ground
		M->SetRelativeLocation(Loc);
		// A trace of earth on the ground is seen but stops nothing; a standing bank stops men and ball.
		M->SetVisibility(Share > 0.02f);
		M->SetCollisionEnabled(bCover ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (bCover) Tags.AddUnique(TEXT("PA250Cover"));
	else Tags.Remove(TEXT("PA250Cover"));
	bWasCover = bCover;
}
