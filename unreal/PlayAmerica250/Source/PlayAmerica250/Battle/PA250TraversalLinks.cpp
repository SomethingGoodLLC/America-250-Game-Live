#include "PA250TraversalLinks.h"

#include "AIController.h"
#include "Battle/PA250BattleFeel.h"
#include "Battle/PA250BattleGameMode.h"
#include "Battle/PA250TraversalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "NavLinkCustomComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "RF/RFCharacter.h"

UPA250NavArea_Traversal::UPA250NavArea_Traversal()
{
	// From data; open ground's cost when the file gave nothing (the loader has already logged that).
	DefaultCost = FMath::Max(1.f, PA250BattleFeel::Get().BotLinkCost);
	DrawColor = FColor(200, 160, 40);
}

APA250TraversalLink::APA250TraversalLink()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Link = CreateDefaultSubobject<UNavLinkCustomComponent>(TEXT("Link"));
	Link->SetNavigationRelevancy(true);
	Link->SetEnabledArea(UPA250NavArea_Traversal::StaticClass());
	Link->SetMoveReachedLink(this, &APA250TraversalLink::OnReached);
}

void APA250TraversalLink::SetEnds(const FVector& A, const FVector& B)
{
	SetActorLocation(0.5 * (A + B));
	Link->SetLinkData(A - GetActorLocation(), B - GetActorLocation(), ENavLinkDirection::BothWays);
}

void APA250TraversalLink::OnReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint)
{
	const UPathFollowingComponent* Following = Cast<UPathFollowingComponent>(PathComp);
	const AController* Controller = Following ? Cast<AController>(Following->GetOwner()) : nullptr;
	ARFCharacter* Man = Controller ? Cast<ARFCharacter>(Controller->GetPawn()) : nullptr;
	UPA250TraversalComponent* Traversal = Man ? Man->GetTraversal() : nullptr;
	if (!Traversal || !GetWorld()) return;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastStart < Feel.BotStaggerSeconds)
	{
		++Held; // the man before him is still going over; his next path brings him here again
		return;
	}
	const FVector Way = DestPoint - Man->GetActorLocation();
	const float Speed = static_cast<float>(Man->GetVelocity().Size2D());
	const FName Gait = Speed > Feel.WalkMaxCmS ? FName(TEXT("run")) : Speed > Feel.IdleMaxCmS ? FName(TEXT("walk")) : FName(TEXT("stand"));
	const EPA250TraversalStart Why = Traversal->TryStart(Way, static_cast<float>(Way.Size2D()), Gait);
	if (Why == EPA250TraversalStart::Started)
	{
		LastStart = Now;
		++Crossings;
	}
	else
	{
		++Refused;
		UE_LOG(LogTemp, Log, TEXT("PA250 traversal link: %s refused (%s), %.0f cm from the far end, speed %.0f"), *Man->GetName(), PA250TraversalStartName(Why), Way.Size2D(), Speed);
	}
}

void PA250Traversal::LinkEnds(const FVector& Centre, const FVector& AxisLong, float LengthCm, float DepthCm, float SpacingCm, float StandOffCm,
	TArray<TPair<FVector, FVector>>& Out)
{
	const FVector Along = AxisLong.GetSafeNormal2D();
	if (Along.IsNearlyZero() || SpacingCm <= 0.f || LengthCm <= 0.f) return;
	const FVector Side(-Along.Y, Along.X, 0.f);
	const int32 N = FMath::Max(1, FMath::RoundToInt(LengthCm / SpacingCm));
	const float Step = LengthCm / N;
	const float Off = 0.5f * DepthCm + StandOffCm;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector At = Centre + Along * (-0.5f * LengthCm + (i + 0.5f) * Step);
		Out.Emplace(At + Side * Off, At - Side * Off);
	}
}

void UPA250TraversalLinks::Totals(int32& OutCrossings, int32& OutRefused, int32& OutHeld) const
{
	OutCrossings = PastCrossings;
	OutRefused = PastRefused;
	OutHeld = PastHeld;
	for (const APA250TraversalLink* L : Links)
	{
		if (!L) continue;
		OutCrossings += L->Crossings;
		OutRefused += L->Refused;
		OutHeld += L->Held;
	}
}

int32 UPA250TraversalLinks::Rebuild()
{
	UWorld* World = GetWorld();
	if (!World) return 0;
	Totals(PastCrossings, PastRefused, PastHeld);
	for (APA250TraversalLink* L : Links)
	{
		if (L) L->Destroy();
	}
	Links.Reset();
	bDirty = false;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const float Spacing = Feel.BotLinkSpacingM * 100.f;
	if (!Nav || Spacing <= 0.f) return 0;
	int32 Obstacles = 0, NotCrossable = 0, OffNav = 0;
	TArray<FVector> Placed;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!(It->ActorHasTag(TEXT("PA250Cover")) || It->ActorHasTag(TEXT("PA250FieldWork")))) continue;
		++Obstacles;
		// The block as the cover places read it: its long side from the mesh's own box, so a turned wall reads right.
		FVector Centre, Extent;
		It->GetActorBounds(true, Centre, Extent);
		FVector Axis = Extent.X >= Extent.Y ? FVector::XAxisVector : FVector::YAxisVector;
		float Length = 2.f * static_cast<float>(FMath::Max(Extent.X, Extent.Y)), Depth = 2.f * static_cast<float>(FMath::Min(Extent.X, Extent.Y));
		const float TopZ = static_cast<float>(Centre.Z + Extent.Z);
		if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(It->GetRootComponent()); Mesh && Mesh->GetStaticMesh())
		{
			const FVector Size = Mesh->GetStaticMesh()->GetBoundingBox().GetSize() * Mesh->GetComponentScale().GetAbs();
			const bool bAlongX = Size.X >= Size.Y;
			Axis = bAlongX ? Mesh->GetComponentQuat().GetAxisX() : Mesh->GetComponentQuat().GetAxisY();
			Length = static_cast<float>(bAlongX ? Size.X : Size.Y);
			Depth = static_cast<float>(bAlongX ? Size.Y : Size.X);
			Centre = Mesh->Bounds.Origin;
		}
		TArray<TPair<FVector, FVector>> Ends;
		PA250Traversal::LinkEnds(Centre, Axis, Length, Depth, Spacing, Feel.TraversalLandingClearCm, Ends);
		for (const TPair<FVector, FVector>& E : Ends)
		{
			const FVector Mid = 0.5 * (E.Key + E.Value);
			// A wall of many blocks end to end still gets one link every spacing, not one a block.
			if (Placed.ContainsByPredicate([&Mid, Spacing](const FVector& P) { return FVector::DistSquared2D(P, Mid) < FMath::Square(0.9f * Spacing); })) continue;
			FNavLocation A, B;
			const FVector Reach(80.f, 80.f, 250.f);
			if (!Nav->ProjectPointToNavigation(E.Key, A, Reach) || !Nav->ProjectPointToNavigation(E.Value, B, Reach))
			{
				++OffNav;
				continue;
			}
			// Crossable from both sides, by the heights the ground gives each.
			if (PA250BattleFeel::ClassifyTraversal(TopZ - static_cast<float>(A.Location.Z), Depth, Feel) == EPA250Traversal::None ||
				PA250BattleFeel::ClassifyTraversal(TopZ - static_cast<float>(B.Location.Z), Depth, Feel) == EPA250Traversal::None)
			{
				++NotCrossable;
				continue;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APA250TraversalLink* L = World->SpawnActor<APA250TraversalLink>(Mid, FRotator::ZeroRotator, Params))
			{
				L->SetEnds(A.Location, B.Location);
				Links.Add(L);
				Placed.Add(Mid);
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 traversal links: %d links over %d obstacles (%d places not crossable, %d off the navmesh)"), Links.Num(), Obstacles,
		NotCrossable, OffNav);
	// Places off the navmesh usually mean it is still being built (a headless run outpaces it): look
	// again in real time, while the count keeps falling, a few times at most.
	if (OffNav > 0 && Retries < 6 && (Retries == 0 || OffNav < LastOffNav))
	{
		++Retries;
		RetryAt = FPlatformTime::Seconds() + 2.0;
	}
	else
	{
		RetryAt = 0.0;
	}
	LastOffNav = OffNav;
	return Links.Num();
}

void UPA250TraversalLinks::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode<APA250BattleGameMode>()) return;
	if (!bDirty && RetryAt > 0.0 && FPlatformTime::Seconds() >= RetryAt) bDirty = true;
	if (!bDirty) return;
	// Once the navmesh stands (or after ten seconds of waiting for it to settle).
	Waited += DeltaTime;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || !Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) return;
	if (Waited < 1.f || (Nav->IsNavigationBuildInProgress() && Waited < 10.f)) return;
	Waited = 0.f;
	Rebuild();
}
