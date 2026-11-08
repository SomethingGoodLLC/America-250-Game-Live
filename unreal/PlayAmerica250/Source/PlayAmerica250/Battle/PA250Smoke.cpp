#include "PA250Smoke.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "PA250Wind.h"

void FPA250SmokePuff::Step(float Dt, const FVector& Wind)
{
	Age += Dt;
	// Muzzle jet: the cloud is blown out a few metres and stops within about a second.
	Vel *= FMath::Exp(-2.2f * Dt);
	Pos += (Vel + Wind * FMath::Min(1.f, Age / 2.f) + FVector(0.f, 0.f, 10.f)) * Dt;
}

float FPA250SmokePuff::Size() const
{
	const float T = FMath::Clamp(Age / Life, 0.f, 1.f);
	// Fast initial billow (first ~2 s), then slow spreading for the rest of its life.
	const float Billow = 1.f - FMath::Exp(-Age * 1.6f);
	return FMath::Lerp(Size0, Size1, 0.55f * Billow + 0.45f * T);
}

float FPA250SmokePuff::Opacity() const
{
	if (Age >= Life) return 0.f;
	const float FadeIn = FMath::Clamp(Age / 0.12f, 0.f, 1.f);
	const float Remaining = 1.f - Age / Life;
	return Peak * FadeIn * FMath::Pow(Remaining, 1.4f);
}

FVector UPA250SmokeSubsystem::Wind()
{
	return PA250Wind::Velocity();
}

bool UPA250SmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Nothing to see without a renderer (commandlets, -nullrhi autoplay).
	return FApp::CanEverRender() && Super::ShouldCreateSubsystem(Outer);
}

bool UPA250SmokeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPA250SmokeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPA250SmokeSubsystem, STATGROUP_Tickables);
}

void UPA250SmokeSubsystem::AddShot(const FVector& Muzzle, const FVector& Direction)
{
	const FVector Dir = Direction.GetSafeNormal();
	const float Ahead[3] = {50.f, 160.f, 300.f};
	const float Speed[3] = {260.f, 170.f, 90.f};
	const float Start[3] = {55.f, 100.f, 150.f};
	for (int32 i = 0; i < 3; ++i)
	{
		if (Puffs.Num() >= MaxPuffs)
		{
			Puffs.RemoveAt(0, EAllowShrinking::No); // oldest goes first
		}
		FPA250SmokePuff P;
		P.Pos = Muzzle + Dir * Ahead[i] + Rng.GetUnitVector() * 15.f;
		P.Vel = Dir * Speed[i] + Rng.GetUnitVector() * 25.f;
		P.Life = Rng.FRandRange(22.f, 32.f);
		P.Size0 = Start[i];
		P.Size1 = Rng.FRandRange(420.f, 600.f);
		P.Peak = Rng.FRandRange(0.42f, 0.6f);
		P.Spin = Rng.FRandRange(0.f, 2.f * PI);
		Puffs.Add(P);
	}
	bForceRebuild = true;
	Flash(Muzzle + Dir * 40.f);
}

void UPA250SmokeSubsystem::MergeAged()
{
	// Fresh muzzle jets stay separate. Only the lingering bank (older than 2.5 s) collapses,
	// and only when it is already thick, so a volley still reads as a volley.
	const int32 N = Puffs.Num();
	if (N < 96) return;
	TArray<uint8> Drop;
	Drop.SetNumZeroed(N);
	int32 Merged = 0;
	constexpr float RadiusSq = 160.f * 160.f;
	for (int32 i = 0; i < N && Merged < 24; ++i)
	{
		if (Drop[i] || Puffs[i].Age < 2.5f) continue;
		for (int32 j = i + 1; j < N; ++j)
		{
			if (Drop[j] || Puffs[j].Age < 2.5f) continue;
			if (FVector::DistSquared(Puffs[i].Pos, Puffs[j].Pos) > RadiusSq) continue;
			FPA250SmokePuff& Keep = Puffs[i].Size() >= Puffs[j].Size() ? Puffs[i] : Puffs[j];
			const FPA250SmokePuff& Lose = (&Keep == &Puffs[i]) ? Puffs[j] : Puffs[i];
			Keep.Pos = (Puffs[i].Pos + Puffs[j].Pos) * 0.5f;
			Keep.Peak = FMath::Min(0.7f, Keep.Peak + 0.5f * Lose.Peak);
			Keep.Life = FMath::Max(Keep.Life, Lose.Life);
			Drop[(&Keep == &Puffs[i]) ? j : i] = 1;
			++Merged;
			break;
		}
	}
	if (Merged == 0) return;
	for (int32 i = N - 1; i >= 0; --i)
	{
		if (Drop[i]) Puffs.RemoveAt(i, 1, EAllowShrinking::No);
	}
}

void UPA250SmokeSubsystem::Flash(const FVector& Where)
{
	EnsureMesh();
	if (!Host) return;
	if (Flashes.Num() < MaxFlashes)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(Host);
		L->SetCastShadows(false);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetLightColor(FLinearColor(1.f, 0.62f, 0.28f));
		L->SetAttenuationRadius(900.f);
		L->SetSourceRadius(20.f);
		L->SetupAttachment(Host->GetRootComponent());
		L->RegisterComponent();
		L->SetVisibility(false);
		Flashes.Add(L);
		FlashAge.Add(FlashSeconds);
	}
	const int32 I = NextFlash++ % Flashes.Num();
	Flashes[I]->SetWorldLocation(Where);
	Flashes[I]->SetVisibility(true);
	FlashAge[I] = 0.f;
}

void UPA250SmokeSubsystem::TickFlashes(float DeltaTime)
{
	for (int32 i = 0; i < Flashes.Num(); ++i)
	{
		if (FlashAge[i] >= FlashSeconds) continue;
		FlashAge[i] += DeltaTime;
		const float K = FMath::Clamp(1.f - FlashAge[i] / FlashSeconds, 0.f, 1.f);
		Flashes[i]->SetIntensity(1200.f * K * K);
		if (K <= 0.f) Flashes[i]->SetVisibility(false);
	}
}

void UPA250SmokeSubsystem::EnsureMesh()
{
	if (Mesh) return;
	UWorld* World = GetWorld();
	if (!World) return;
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Host = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Host) return;
	Mesh = NewObject<UProceduralMeshComponent>(Host, TEXT("PA250Smoke"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = false;
	Host->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_PA250_Smoke.M_PA250_Smoke"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Mesh->SetMaterial(0, Mat);
	}
}

void UPA250SmokeSubsystem::Tick(float DeltaTime)
{
	TickFlashes(DeltaTime);
	if (Puffs.Num() == 0 && (!bSectionCreated || SectionQuads == 0)) return;
	const FVector W = Wind();
	for (FPA250SmokePuff& P : Puffs) P.Step(DeltaTime, W);
	Puffs.RemoveAll([](const FPA250SmokePuff& P) { return P.IsDead(); });
	MergeWait -= DeltaTime;
	if (MergeWait <= 0.f)
	{
		MergeWait = 0.35f;
		MergeAged();
	}
	EnsureMesh();
	// The bank drifts about 0.8 m/s, so rebuilding on alternate frames is not visible,
	// and a fresh shot still appears the frame it is fired.
	++MeshFrame;
	if (Mesh && (bForceRebuild || (MeshFrame & 1) == 0 || Puffs.Num() == 0))
	{
		bForceRebuild = false;
		RebuildMesh();
	}
	SectionQuads = Puffs.Num(); // 0 after the last puff fades: stop rebuilding until the next shot
}

void UPA250SmokeSubsystem::RebuildMesh()
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager) return;
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FRotationMatrix CamRot(PC->PlayerCameraManager->GetCameraRotation());
	const FVector Right = CamRot.GetUnitAxis(EAxis::Y);
	const FVector Up = CamRot.GetUnitAxis(EAxis::Z);
	const FVector Facing = -CamRot.GetUnitAxis(EAxis::X);

	// Back to front, so the soft edges of near puffs blend over far ones.
	// Puffs past 70 m are not drawn: at that range a 5 m quad is a few pixels and still
	// costs a full translucent overdraw under Lumen. They keep simulating, so walking
	// back into the bank brings them up again.
	constexpr float CullSq = 7000.f * 7000.f;
	TArray<int32> Order;
	Order.Reserve(Puffs.Num());
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		if (FVector::DistSquared(Puffs[i].Pos, Cam) <= CullSq) Order.Add(i);
	}
	Order.Sort([this, &Cam](int32 A, int32 B) {
		return FVector::DistSquared(Puffs[A].Pos, Cam) > FVector::DistSquared(Puffs[B].Pos, Cam);
	});

	// Fixed-size buffers (MaxPuffs quads) so the section is only created once; unused quads collapse.
	TArray<FVector> V; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C;
	V.Reserve(MaxPuffs * 4); N.Reserve(MaxPuffs * 4); UV.Reserve(MaxPuffs * 4); C.Reserve(MaxPuffs * 4);
	const FLinearColor Smoke(0.80f, 0.79f, 0.76f);
	for (int32 q = 0; q < MaxPuffs; ++q)
	{
		if (q < Order.Num())
		{
			const FPA250SmokePuff& P = Puffs[Order[q]];
			const float H = 0.5f * P.Size();
			const float S = FMath::Sin(P.Spin), Co = FMath::Cos(P.Spin);
			const FVector R = (Right * Co + Up * S) * H;
			const FVector U = (Up * Co - Right * S) * H;
			V.Append({P.Pos - R - U, P.Pos + R - U, P.Pos + R + U, P.Pos - R + U});
			// The first instant glows with the burning charge, then it is plain smoke.
			FLinearColor Col = FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.78f, 0.45f), Smoke, FMath::Clamp(P.Age / 0.12f, 0.f, 1.f));
			Col.A = P.Opacity();
			C.Append({Col, Col, Col, Col});
		}
		else
		{
			V.Append({FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector});
			C.Append({FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent});
		}
		N.Append({Facing, Facing, Facing, Facing});
		UV.Append({FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0)});
	}
	if (!bSectionCreated)
	{
		TArray<int32> Tris;
		Tris.Reserve(MaxPuffs * 6);
		for (int32 q = 0; q < MaxPuffs; ++q)
		{
			const int32 B = q * 4;
			Tris.Append({B, B + 2, B + 1, B, B + 3, B + 2});
		}
		Mesh->CreateMeshSection_LinearColor(0, V, Tris, N, UV, C, {}, false);
		bSectionCreated = true;
	}
	else
	{
		Mesh->UpdateMeshSection_LinearColor(0, V, N, UV, C, {});
	}
}
