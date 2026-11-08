#include "PA250ControlPoint.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "ProceduralMeshComponent.h"
#include "RFCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "PA250FlagArt.h"
#include "PA250Wind.h"

APA250ControlPoint::APA250ControlPoint()
{
	PrimaryActorTick.bCanEverTick = true;
	// Every frame so the flag moves smoothly; capture logic still steps at 0.1 s (TickCapture).

	Zone = CreateDefaultSubobject<USphereComponent>(TEXT("Zone"));
	Zone->InitSphereRadius(Radius);
	Zone->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Zone->SetGenerateOverlapEvents(true);
	RootComponent = Zone;

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	Disc->SetupAttachment(RootComponent);
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetRelativeLocation(FVector(0.f, 0.f, -Radius + 8.f));
	Disc->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 0.05f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Disc->SetStaticMesh(Cyl.Object);
	}

	// Flagpole (basic cylinder: 100 cm tall, centred) and a flag (basic cube) that slides up and down it.
	FlagPole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagPole"));
	FlagPole->SetupAttachment(RootComponent);
	FlagPole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlagPole->SetRelativeScale3D(FVector(0.12f, 0.12f, PoleHeight / 100.f));
	if (Cyl.Succeeded()) FlagPole->SetStaticMesh(Cyl.Object);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Finial = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Finial"));
	Finial->SetupAttachment(RootComponent);
	Finial->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Finial->SetRelativeScale3D(FVector(0.22f));
	if (Ball.Succeeded()) Finial->SetStaticMesh(Ball.Object);
	FlagCloth = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FlagCloth"));
	FlagCloth->SetupAttachment(RootComponent);
	FlagCloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlagCloth->bUseAsyncCooking = false;
	PlaceFlag();
}

void APA250ControlPoint::FlagTarget(FName InHolder, FName InCapturer, float Progress, FName& OutFaction, float& OutHeight01)
{
	const float P = FMath::Clamp(Progress, 0.f, 1.f);
	const bool bEnemyWorking = !InCapturer.IsNone() && InCapturer != InHolder;
	if (!InHolder.IsNone())
	{
		OutFaction = InHolder;
		OutHeight01 = bEnemyWorking ? 1.f - P : 1.f;
	}
	else
	{
		OutFaction = bEnemyWorking ? InCapturer : NAME_None;
		OutHeight01 = bEnemyWorking ? P : 0.f;
	}
}

void APA250ControlPoint::UpdateFlag(float DeltaSeconds)
{
	FName Want;
	float WantHeight;
	FlagTarget(Holder, Capturer, CaptureProgress, Want, WantHeight);
	if (Want != FlagFaction)
	{
		// Lower the old colours before the new ones go up.
		FlagHeight = FMath::FInterpConstantTo(FlagHeight, 0.f, DeltaSeconds, 2.f);
		if (FlagHeight <= KINDA_SMALL_NUMBER)
		{
			FlagFaction = Want;
		}
	}
	else
	{
		FlagHeight = FMath::FInterpConstantTo(FlagHeight, WantHeight, DeltaSeconds, 2.f);
	}
	PlaceFlag();
	ClothTime += DeltaSeconds;
	// Only re-shape cloth someone can see (cheap anyway: 153 vertices).
	if (GetClothVertexCount() == 0) UpdateCloth(true);
	else if (!FlagFaction.IsNone() && FlagCloth->WasRecentlyRendered(0.5f)) UpdateCloth(false);
}

void APA250ControlPoint::PlaceFlag()
{
	if (!FlagCloth || !FlagPole) return;
	FlagPole->SetRelativeLocation(FVector(PoleOffset.X, PoleOffset.Y, PoleBaseZ + 0.5f * PoleHeight));
	if (Finial) Finial->SetRelativeLocation(FVector(PoleOffset.X, PoleOffset.Y, PoleBaseZ + PoleHeight + 8.f));
	const float Bottom = PoleBaseZ + 80.f;
	const float Top = PoleBaseZ + PoleHeight - ClothHeight - 15.f;
	// The flag flies downwind (the same breeze that drifts the smoke).
	const FRotator Downwind(0.f, PA250Wind::Yaw() - GetActorRotation().Yaw, 0.f);
	FlagCloth->SetRelativeRotation(Downwind);
	FlagCloth->SetRelativeLocation(FVector(PoleOffset.X, PoleOffset.Y, 0.f) + Downwind.RotateVector(FVector(7.f, 0.f, 0.f))
		+ FVector(0.f, 0.f, FMath::Lerp(Bottom, Top, FlagHeight)));
	FlagCloth->SetVisibility(!FlagFaction.IsNone());
	const UWorld* World = GetWorld();
	if (FlagColorFaction != FlagFaction && !IsTemplate() && World && World->IsGameWorld())
	{
		FlagColorFaction = FlagFaction;
		static TWeakObjectPtr<UMaterialInterface> FlagMat;
		if (!FlagMat.IsValid())
			FlagMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_PA250_Flag.M_PA250_Flag"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		UTexture2D* Art = FlagFaction.IsNone() ? nullptr : PA250FlagArt::GetTexture(FlagFaction);
		if (FlagMat.IsValid() && Art)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FlagMat.Get(), this);
			MID->SetTextureParameterValue(TEXT("Flag"), Art);
			FlagCloth->SetMaterial(0, MID);
		}
		else if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FlagPole->GetMaterial(0), this))
		{
			// Fallback without the flag material: plain faction colour.
			MID->SetVectorParameterValue(TEXT("Color"), FlagFaction == TEXT("british") ? FLinearColor(0.75f, 0.03f, 0.03f) : FLinearColor(0.06f, 0.14f, 0.7f));
			FlagCloth->SetMaterial(0, MID);
		}
		if (UMaterialInstanceDynamic* PoleMID = FlagPole->CreateAndSetMaterialInstanceDynamic(0))
		{
			PoleMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.32f, 0.22f, 0.12f));
		}
		if (Finial)
			if (UMaterialInstanceDynamic* BallMID = Finial->CreateAndSetMaterialInstanceDynamic(0))
				BallMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 0.6f, 0.2f));
	}
}

void APA250ControlPoint::UpdateCloth(bool bCreate)
{
	if (!FlagCloth) return;
	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	const int32 NX = ClothSegX + 1, NZ = ClothSegZ + 1;
	Verts.Reserve(NX * NZ); Normals.Reserve(NX * NZ); UVs.Reserve(NX * NZ);
	for (int32 iz = 0; iz < NZ; ++iz)
	{
		for (int32 ix = 0; ix < NX; ++ix)
		{
			const float U = float(ix) / ClothSegX;
			const float Z01 = float(iz) / ClothSegZ;
			const FVector Off = PA250FlagArt::ClothOffset(U, Z01, ClothTime, ClothLength);
			Verts.Add(FVector(U * ClothLength, 0.f, Z01 * ClothHeight) + Off);
			// Normal from the sideways slope along the fly.
			const float Du = 1.f / ClothSegX;
			const float Slope = (PA250FlagArt::ClothOffset(FMath::Min(U + Du, 1.f), Z01, ClothTime, ClothLength).Y
				- PA250FlagArt::ClothOffset(FMath::Max(U - Du, 0.f), Z01, ClothTime, ClothLength).Y) / (2.f * Du * ClothLength);
			Normals.Add(FVector(-Slope, 1.f, 0.f).GetSafeNormal());
			UVs.Add(FVector2D(U, 1.f - Z01));
		}
	}
	if (bCreate)
	{
		TArray<int32> Tris;
		for (int32 iz = 0; iz < ClothSegZ; ++iz)
			for (int32 ix = 0; ix < ClothSegX; ++ix)
			{
				const int32 A = iz * NX + ix, B = A + 1, C = A + NX, D = C + 1;
				Tris.Append({A, C, B, B, C, D});
			}
		FlagCloth->CreateMeshSection(0, Verts, Tris, Normals, UVs, {}, Tangents, false);
	}
	else
	{
		FlagCloth->UpdateMeshSection(0, Verts, Normals, UVs, {}, Tangents);
	}
}

int32 APA250ControlPoint::GetClothVertexCount() const
{
	return FlagCloth && FlagCloth->GetNumSections() > 0 ? FlagCloth->GetProcMeshSection(0)->ProcVertexBuffer.Num() : 0;
}

FVector APA250ControlPoint::GetMarkerLocation() const
{
	return GetActorLocation() + FVector(PoleOffset.X, PoleOffset.Y, PoleBaseZ + PoleHeight + 80.f);
}

void APA250ControlPoint::FindClearPoleSpot()
{
	UWorld* World = GetWorld();
	if (!World) return;
	const FVector Origin = GetActorLocation();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(PA250PoleSpot), false, this);
	// Explicit: ECC_WorldStatic is 0, so the int32 constructor would silently query no types at all.
	FCollisionObjectQueryParams Statics;
	Statics.AddObjectTypesToQuery(ECC_WorldStatic);
	// Candidates nearest first: the post itself, then rings of 8 at 2.5 / 5 / 7.5 m (deterministic order).
	TArray<FVector2D> Candidates = {FVector2D::ZeroVector};
	for (float R : {250.f, 500.f, 750.f})
		for (int32 k = 0; k < 8; ++k)
			Candidates.Add(FVector2D(FMath::Cos(k * PI / 4.f), FMath::Sin(k * PI / 4.f)) * R);
	for (const FVector2D& Off : Candidates)
	{
		const FVector Top = Origin + FVector(Off.X, Off.Y, 300.f);
		FHitResult Ground;
		if (!World->LineTraceSingleByChannel(Ground, Top, Top - FVector(0.f, 0.f, 5000.f), ECC_WorldStatic, Q))
		{
			continue;
		}
		// Volume swept by pole + flag, from 2 m up (clear of the terrain) to the finial, plus 2.5 m of
		// open air all round so the flag reads against the sky rather than a wall.
		const float Lo = 200.f, Hi = PoleHeight + 30.f, Air = 250.f;
		const FQuat Downwind(FRotator(0.f, PA250Wind::Yaw(), 0.f));
		const FVector Center = Ground.ImpactPoint + Downwind.RotateVector(FVector(0.5f * ClothLength - 10.f, 0.f, 0.f)) + FVector(0.f, 0.f, 0.5f * (Lo + Hi));
		const FVector Half(0.5f * ClothLength + Air, Air, 0.5f * (Hi - Lo));
		if (World->OverlapAnyTestByObjectType(Center, Downwind, Statics, FCollisionShape::MakeBox(Half), Q))
		{
			continue;
		}
		PoleOffset = Off;
		PoleBaseZ = Ground.ImpactPoint.Z - Origin.Z;
		UE_LOG(LogTemp, Display, TEXT("PA250 post %s: flagpole %s"), *PostId.ToString(),
			Off.IsZero() ? TEXT("on the post (clear)") : *FString::Printf(TEXT("moved %.0f cm to open ground"), Off.Size()));
		return;
	}
	// Nothing clear: keep the pole on the post.
	UE_LOG(LogTemp, Warning, TEXT("PA250 post %s: no clear spot within 7.5 m; flagpole stays on the post"), *PostId.ToString());
	FHitResult Ground;
	if (World->LineTraceSingleByChannel(Ground, Origin, Origin - FVector(0.f, 0.f, 5000.f), ECC_WorldStatic, Q))
	{
		PoleBaseZ = Ground.ImpactPoint.Z - Origin.Z;
	}
}

FString APA250ControlPoint::DisplayName(FName InPostId)
{
	if (InPostId == TEXT("green")) return TEXT("Lexington Green");
	if (InPostId == TEXT("meetinghouse")) return TEXT("Meetinghouse");
	if (InPostId == TEXT("buckman")) return TEXT("Buckman Tavern");
	FString S = InPostId.ToString();
	if (!S.IsEmpty()) S[0] = FChar::ToUpper(S[0]);
	return S;
}

float APA250ControlPoint::CaptureRate(int32 Attackers, int32 Defenders, float PerAttackerScale, float PerDefenderScale)
{
	if (Attackers <= 0) return 0.f;
	const float Atk = 1.f + FMath::Max(0, Attackers - 1) * PerAttackerScale;
	const float Def = 1.f + FMath::Max(0, Defenders) * PerDefenderScale;
	return Atk / Def;
}

void APA250ControlPoint::ApplyTuning(float InRadius, float InCaptureSeconds, float InPerAttackerScale)
{
	Radius = InRadius;
	CaptureSeconds = InCaptureSeconds;
	CapturePerAttackerScale = InPerAttackerScale;
	if (Zone)
	{
		Zone->SetSphereRadius(Radius);
	}
	if (Disc)
	{
		Disc->SetRelativeLocation(FVector(0.f, 0.f, -Radius + 8.f));
		Disc->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 0.05f));
	}
}

void APA250ControlPoint::BuildSlotRing(int32 SlotCount, float MinRadiusCm, float MaxRadiusCm)
{
	// Deterministic ring: 3 radii (min, mid, max) x SlotCount/3 angles, staggered per ring so files
	// don't line up radially. Sector = angular octant, so one squad's sector is one arc of the ring.
	Slots.Reset();
	const FVector Center = GetActorLocation();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	TArray<FPA250PostSlot> Unfiltered;
	constexpr int32 NumRings = 3;
	const int32 PerRing = FMath::Max(1, SlotCount / NumRings);
	for (int32 Ring = 0; Ring < NumRings; ++Ring)
	{
		const float R = FMath::Lerp(MinRadiusCm, MaxRadiusCm, Ring / float(NumRings - 1));
		for (int32 k = 0; k < PerRing; ++k)
		{
			const float T = (k + 0.5f * (Ring % 2)) / PerRing;
			const float Angle = T * 2.f * PI;
			FVector Candidate = Center + FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, 0.f);
			FPA250PostSlot S;
			S.Location = Candidate;
			S.Sector = FMath::Clamp(FMath::FloorToInt(T * NumSectors), 0, NumSectors - 1);
			S.bClaimed = false;
			Unfiltered.Add(S);
			if (Nav)
			{
				FNavLocation Projected;
				if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(200.f, 200.f, 500.f)))
				{
					continue; // off the navmesh (fence, wall, pond) - not a slot
				}
				S.Location = Projected.Location;
			}
			Slots.Add(S);
		}
	}
	if (Slots.Num() == 0)
	{
		// No navmesh yet (or none built): keep the raw ring rather than no slots at all.
		Slots = MoveTemp(Unfiltered);
	}
}

int32 APA250ControlPoint::SectorDistance(int32 A, int32 B)
{
	const int32 D = FMath::Abs(((A % NumSectors) + NumSectors) % NumSectors - ((B % NumSectors) + NumSectors) % NumSectors);
	return FMath::Min(D, NumSectors - D);
}

bool APA250ControlPoint::ClaimSlot(int32 Index, AActor* Claimant)
{
	if (!Slots.IsValidIndex(Index) || !Claimant) return false;
	if (Slots[Index].bClaimed &&Slots[Index].Claimant.IsValid() &&Slots[Index].Claimant.Get() != Claimant)
	{
		return false;
	}
	Slots[Index].bClaimed = true;
	Slots[Index].Claimant = Claimant;
	return true;
}

void APA250ControlPoint::ReleaseSlot(AActor* Claimant)
{
	for (FPA250PostSlot& S : Slots)
	{
		if (S.Claimant.Get() == Claimant)
		{
			S.bClaimed = false;
			S.Claimant = nullptr;
		}
	}
}

int32 APA250ControlPoint::FindFreeSlot(int32 PreferredSector) const
{
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].bClaimed && Slots[i].Claimant.IsValid()) continue;
		// Wrap-around sector distance first, then inner rings, then index (deterministic ties).
		const float Score = SectorDistance(Slots[i].Sector, PreferredSector) * 1.0e6f
			+ FVector::DistSquared2D(Slots[i].Location, GetActorLocation()) * 1.0e-3f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	return Best;
}

FVector APA250ControlPoint::GetSlotLocation(int32 Index) const
{
	return Slots.IsValidIndex(Index) ? Slots[Index].Location : GetActorLocation();
}

void APA250ControlPoint::BeginPlay()
{
	Super::BeginPlay();
	Zone->SetSphereRadius(Radius);
	if (!bHoldersAssigned)
	{
		Holder = InitialOwner;
	}
	UpdateColor();
	// Stand the pole on the ground, clear of buildings (the actor floats ~1.4 m above the ground).
	FindClearPoleSpot();
	FName F; float H;
	FlagTarget(Holder, Capturer, CaptureProgress, F, H);
	FlagFaction = F;
	FlagHeight = H;
	PlaceFlag();
	UpdateCloth(true);
	if (Slots.Num() == 0)
	{
		BuildSlotRing(48, 600.f, 2500.f);
	}
}

void APA250ControlPoint::SetHolder(FName NewHolder)
{
	if (Holder == NewHolder)
	{
		return;
	}
	Holder = NewHolder;
	bFlippedSinceLastCheck = true;
	UpdateColor();
}

void APA250ControlPoint::UpdateColor()
{
	if (UMaterialInstanceDynamic* MID = Disc->CreateAndSetMaterialInstanceDynamic(0))
	{
		const FLinearColor C = Holder == TEXT("british") ? FLinearColor(0.8f, 0.05f, 0.05f)
		                     : Holder == TEXT("continental") ? FLinearColor(0.1f, 0.2f, 0.85f)
		                                                     : FLinearColor(0.8f, 0.8f, 0.8f);
		MID->SetVectorParameterValue(TEXT("Color"), C);
	}
}

void APA250ControlPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LogicAccum += DeltaSeconds;
	if (LogicAccum >= 0.1f)
	{
		TickCapture(LogicAccum);
		LogicAccum = 0.f;
	}
	UpdateFlag(DeltaSeconds);
}

void APA250ControlPoint::TickCapture(float DeltaSeconds)
{
	Presence.Reset();
	TArray<AActor*> Overlapping;
	Zone->GetOverlappingActors(Overlapping, ARFCharacter::StaticClass());
	for (AActor* A : Overlapping)
	{
		const ARFCharacter* C = Cast<ARFCharacter>(A);
		if (C && !C->IsDead() && !C->FactionId.IsNone())
		{
			Presence.FindOrAdd(C->FactionId)++;
		}
	}

	const int32 Brit = GetPresence(TEXT("british"));
	const int32 Cont = GetPresence(TEXT("continental"));
	bContested = Brit > 0 && Cont > 0;

	FName Sole = NAME_None;
	int32 Attackers = 0;
	int32 Defenders = 0;
	if (Brit > 0 && Cont == 0)
	{
		Sole = TEXT("british");
		Attackers = Brit;
		Defenders = 0;
	}
	else if (Cont > 0 && Brit == 0)
	{
		Sole = TEXT("continental");
		Attackers = Cont;
		Defenders = 0;
	}
	else if (bContested)
	{
		// Contested: majority side advances slowly; minority counts as defenders.
		if (Brit > Cont)
		{
			Sole = TEXT("british");
			Attackers = Brit;
			Defenders = Cont;
		}
		else if (Cont > Brit)
		{
			Sole = TEXT("continental");
			Attackers = Cont;
			Defenders = Brit;
		}
		else
		{
			Capturer = NAME_None;
			return;
		}
	}

	ApplyCapturePressure(Sole, Attackers, Defenders, DeltaSeconds);
}

bool APA250ControlPoint::ApplyCapturePressure(FName Sole, int32 Attackers, int32 Defenders, float DeltaSeconds)
{
	if (bContestLocked)
	{
		CaptureProgress = 0.f;
		Capturer = NAME_None;
		return false;
	}

	if (Sole.IsNone())
	{
		Capturer = NAME_None;
		return false;
	}

	if (Sole == Holder)
	{
		CaptureProgress = 0.f;
		Capturer = NAME_None;
		return false;
	}

	if (Capturer != Sole)
	{
		Capturer = Sole;
		CaptureProgress = 0.f;
	}

	const float Rate = CaptureRate(Attackers, Defenders, CapturePerAttackerScale, CapturePerDefenderScale)
		* FMath::Max(0.05f, CaptureRateScale);
	CaptureProgress += (DeltaSeconds * Rate) / FMath::Max(0.1f, CaptureSeconds);
	if (CaptureProgress < 1.f)
	{
		return true;
	}

	CaptureProgress = 0.f;
	if (!Holder.IsNone() && Holder != Sole)
	{
		SetHolder(NAME_None);
		Capturer = Sole;
		return true;
	}
	SetHolder(Sole);
	Capturer = NAME_None;
	return true;
}
