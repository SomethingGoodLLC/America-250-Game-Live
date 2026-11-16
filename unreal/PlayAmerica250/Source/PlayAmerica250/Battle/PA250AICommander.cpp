#include "PA250AICommander.h"
#include "PA250BattleGameMode.h"
#include "PA250BotController.h"
#include "PA250ControlPoint.h"
#include "PA250FieldGun.h"
#include "PA250VolleyCoordinator.h"
#include "PA250UnitClasses.h"
#include "RFCharacter.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

void UPA250AICommander::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SpawnSalt = 0;
}

void UPA250AICommander::Deinitialize()
{
	IdleSeconds.Reset();
	LastLoc.Reset();
	RecentDeaths.Reset();
	AssignedSquads.Reset();
	Super::Deinitialize();
}

TStatId UPA250AICommander::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPA250AICommander, STATGROUP_Tickables);
}

uint32 UPA250AICommander::NextSpawnSalt()
{
	return ++SpawnSalt;
}

FVector UPA250AICommander::FormationOffset(int32 SlotIndex, const FVector& Forward, float SpacingCm)
{
	const FVector Fwd = Forward.GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	const int32 Rank = SlotIndex % 2; // 0 front, 1 rear
	const int32 File = SlotIndex / 2;
	const int32 Side = (File % 2 == 0) ? (File / 2) : -(File / 2 + 1);
	return Right * (float(Side) * SpacingCm) - Fwd * (float(Rank) * SpacingCm);
}

float UPA250AICommander::ScoreSpawn(const FVector& At, const FVector& Front, const TArray<FVector>& RecentDeathsIn,
	const TArray<FVector>& EnemyGunMuzzles, const TArray<FVector>& EnemyLocs, UWorld* World)
{
	float Score = FVector::Dist2D(At, Front) * -0.01f; // closer to front is better (less negative when near)
	Score += 5000.f - FVector::Dist2D(At, Front); // distance-to-front bonus
	for (const FVector& D : RecentDeathsIn)
	{
		const float Dist = FVector::Dist2D(At, D);
		// Heavy penalty inside 25 m so death sites dominate the front-distance bonus.
		if (Dist < 2500.f) Score -= 6000.f + (2500.f - Dist);
	}
	for (const FVector& Gun : EnemyGunMuzzles)
	{
		if (!World) break;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250SpawnGunLOS), false);
		if (!World->LineTraceSingleByChannel(Hit, Gun, At + FVector(0, 0, 100.f), ECC_Visibility, Params))
		{
			Score -= 1200.f; // exposed to enemy gun
		}
	}
	for (const FVector& E : EnemyLocs)
	{
		const float D = FVector::Dist2D(At, E);
		if (D < 3000.f) Score -= (3000.f - D) * 0.2f;
	}
	return Score;
}

void UPA250AICommander::OnSquadMemberSpawned(ARFCharacter* Soldier)
{
	if (!Soldier) return;
	IdleSeconds.FindOrAdd(Soldier) = 0.f;
	LastLoc.FindOrAdd(Soldier) = Soldier->GetActorLocation();
	AssignObjective(Soldier);
}

void UPA250AICommander::AssignObjective(ARFCharacter* Soldier)
{
	APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr;
	if (!GM || !Soldier || !GM->GetVolleys()) return;
	UPA250VolleyCoordinator* Volleys = GM->GetVolleys();
	const FName Faction = Soldier->FactionId;
	const FName Enemy = Faction == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));

	APA250ControlPoint* Contested = nullptr;
	float BestContested = TNumericLimits<float>::Max();
	APA250ControlPoint* EnemyAdj = nullptr;
	float BestEnemy = TNumericLimits<float>::Max();
	APA250ControlPoint* WeakOwn = nullptr;
	float WeakScore = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<APA250ControlPoint>& WP : GM->GetPosts())
	{
		APA250ControlPoint* P = WP.Get();
		if (!P) continue;
		const float D = FVector::Dist2D(Soldier->GetActorLocation(), P->GetActorLocation());
		const FName H = P->GetHolder();
		if (H.IsNone() || (H != Faction && H != Enemy))
		{
			// Contested / neutral
			if (D < BestContested) { BestContested = D; Contested = P; }
		}
		else if (H == Enemy)
		{
			if (D < BestEnemy) { BestEnemy = D; EnemyAdj = P; }
		}
		else if (H == Faction)
		{
			// Weakest = fewest friendlies nearby
			int32 Friends = 0;
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				if (!It->IsDead() && It->FactionId == Faction
					&& FVector::Dist2D(It->GetActorLocation(), P->GetActorLocation()) < P->Radius)
					++Friends;
			}
			if (float(Friends) < WeakScore) { WeakScore = float(Friends); WeakOwn = P; }
		}
	}

	APA250FieldGun* HotGun = nullptr;
	int32 HotKills = -1;
	for (APA250FieldGun* Gun : GM->GetGuns())
	{
		if (!Gun || Gun->GetFaction() != Enemy) continue;
		const int32 K = Gun->GetShotsFired(); // proxy for recent pressure
		if (K > HotKills) { HotKills = K; HotGun = Gun; }
	}

	FVector Aim = Soldier->GetActorLocation() + Soldier->GetActorForwardVector() * 2000.f;
	EPA250SquadOrder Order = EPA250SquadOrder::FormLine;
	if (Contested)
	{
		Aim = Contested->GetActorLocation();
		Order = EPA250SquadOrder::FormLine;
	}
	else if (EnemyAdj)
	{
		Aim = EnemyAdj->GetActorLocation();
		Order = EPA250SquadOrder::FormLine;
	}
	else if (HotGun && PA250UnitClasses::KitFor(Faction, Soldier->UnitClass).bForwardSpawn)
	{
		Aim = HotGun->GetActorLocation();
		Order = EPA250SquadOrder::Charge;
	}
	else if (WeakOwn)
	{
		Aim = WeakOwn->GetActorLocation();
		Order = EPA250SquadOrder::Hold;
	}

	// Open order beyond 300 m of an enemy gun arc.
	float Spacing = 150.f;
	for (APA250FieldGun* Gun : GM->GetGuns())
	{
		if (!Gun || Gun->GetFaction() != Enemy) continue;
		const float D = FVector::Dist2D(Soldier->GetActorLocation(), Gun->GetActorLocation());
		if (D > 30000.f) Spacing = 300.f;
	}
	(void)Spacing;

	Volleys->GiveOrder(Soldier, Order, Aim);
	AssignedSquads.Add(Soldier->SquadId);
	UE_LOG(LogTemp, Display, TEXT("PA250 AICommander: %s squad %d ordered toward (%.0f,%.0f)"),
		*Faction.ToString(), Soldier->SquadId, Aim.X, Aim.Y);
}

void UPA250AICommander::TickIdleWatch(float DeltaTime)
{
	APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr;
	if (!GM) return;
	TArray<TWeakObjectPtr<ARFCharacter>> Keys;
	IdleSeconds.GetKeys(Keys);
	for (const TWeakObjectPtr<ARFCharacter>& WK : Keys)
	{
		ARFCharacter* S = WK.Get();
		if (!S || S->IsDead())
		{
			IdleSeconds.Remove(WK);
			LastLoc.Remove(WK);
			continue;
		}
		const FVector Now = S->GetActorLocation();
		const FVector Prev = LastLoc.FindRef(WK);
		const float Moved = FVector::Dist2D(Now, Prev);
		LastLoc.FindOrAdd(WK) = Now;
		bool bHasTarget = false;
		if (APA250BotController* Bot = Cast<APA250BotController>(S->GetController()))
		{
			bHasTarget = Bot->GetFormationOrder().Contact.IsValid() || !Bot->GetFormationTarget().IsNearlyZero();
		}
		if (Moved < 20.f && !bHasTarget) IdleSeconds.FindOrAdd(WK) += DeltaTime;
		else IdleSeconds.FindOrAdd(WK) = 0.f;

		if (IdleSeconds.FindRef(WK) >= 8.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("PA250 AICommander: idle reassign %s class=%s squad=%d controller=%s"),
				*S->FactionId.ToString(), *S->UnitClass.ToString(), S->SquadId,
				S->GetController() ? *S->GetController()->GetName() : TEXT("none"));
			AssignObjective(S);
			IdleSeconds.FindOrAdd(WK) = 0.f;
		}
	}
}

void UPA250AICommander::Tick(float DeltaTime)
{
	TickIdleWatch(DeltaTime);
}
