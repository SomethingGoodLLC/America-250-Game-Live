#include "PA250VolleyCoordinator.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "MusketTuning.h"
#include "MusketWeapon.h"
#include "PA250BattleGameMode.h"
#include "PA250BotController.h"
#include "PA250BotTuning.h"
#include "PA250ControlPoint.h"
#include "RFCharacter.h"
#include "NavigationSystem.h"
#include "PA250Cover.h"
#include "PA250BattleAudio.h"
#include "GameFramework/PlayerController.h"
#include "WorldCollision.h"

void UPA250VolleyCoordinator::Init(APA250BattleGameMode* InGM)
{
	GameMode = InGM;
}

FString UPA250VolleyCoordinator::GetCallout() const
{
	return CalloutTimer > 0.f ? Callout : FString();
}

void UPA250VolleyCoordinator::NotifyShot(ARFCharacter* Shooter, const FVector& Dir, bool bHit)
{
	if (!Shooter) return;
	FVolleyShot S;
	S.Time = ShotClock;
	S.Origin = Shooter->GetActorLocation();
	S.Direction = Dir.GetSafeNormal();
	S.bHit = bHit;
	RecentShots.FindOrAdd(Shooter->FactionId).Add(S);

	FVolleyTuningPlain VT;
	if (GameMode.IsValid() && GameMode->GetTuning())
	{
		const FVolleyTuning& T = GameMode->GetTuning()->Values.Volley;
		VT.VolleyWindowSeconds = T.VolleyWindowSeconds;
		VT.VolleyMinShooters = T.VolleyMinShooters;
	}
	const FVolleyResult R = EvaluateVolley(RecentShots.FindOrAdd(Shooter->FactionId), ShotClock, VT);
	if (R.bValid)
	{
		++VolleyCount;
		TotalHits += R.Hits;
		Callout = TEXT("Volley!");
		CalloutTimer = 1.5f;
		const FVolleyTuning& Tun = GameMode.IsValid() && GameMode->GetTuning()
			? GameMode->GetTuning()->Values.Volley
			: FVolleyTuning();
		for (TActorIterator<ARFCharacter> It(Shooter->GetWorld()); It; ++It)
		{
			if (It->IsDead() || It->FactionId == Shooter->FactionId) continue;
			const FVector To = (It->GetActorLocation() - S.Origin).GetSafeNormal();
			if (FVector::DotProduct(S.Direction, To) > 0.7f && FVector::Dist(It->GetActorLocation(), S.Origin) < 5000.f)
			{
				It->ApplySuppression(Tun.SuppressionSeconds, Tun.SuppressionSwayDegrees);
			}
		}
		// Slow capture on posts near the volley cone.
		if (GameMode.IsValid())
		{
			for (const TWeakObjectPtr<APA250ControlPoint>& WP : GameMode->GetPosts())
			{
				if (APA250ControlPoint* P = WP.Get())
				{
					if (FVector::Dist2D(P->GetActorLocation(), S.Origin) < 4000.f)
					{
						P->CaptureRateScale = Tun.SuppressionCaptureScale;
					}
				}
			}
		}
		RecentShots.FindOrAdd(Shooter->FactionId).Reset();
	}
}

void UPA250VolleyCoordinator::RefreshFormations()
{
	if (!GameMode.IsValid() || !GameMode->GetWorld()) return;
	FFormationTuningPlain FT;
	if (GameMode->GetTuning())
	{
		const FAccuracyTuning& A = GameMode->GetTuning()->Values.Accuracy;
		FT.LineRadius = A.LineRadius;
		FT.LineFacingDegrees = A.LineFacingDegrees;
		FT.LineMinSize = A.LineMinSize;
		FT.LineAccuracyBonus = A.LineAccuracyBonus;
	}

	TArray<ARFCharacter*> Alive;
	for (TActorIterator<ARFCharacter> It(GameMode->GetWorld()); It; ++It)
	{
		if (!It->IsDead()) Alive.Add(*It);
	}
	for (ARFCharacter* Me : Alive)
	{
		TArray<FFormationMember> Members;
		for (ARFCharacter* O : Alive)
		{
			if (O->FactionId != Me->FactionId) continue;
			FFormationMember M;
			M.Location = O->GetActorLocation();
			M.Forward = O->GetActorForwardVector();
			M.Index = Members.Num();
			Members.Add(M);
		}
		int32 Self = INDEX_NONE;
		for (int32 i = 0; i < Members.Num(); ++i)
		{
			if (FVector::DistSquared(Members[i].Location, Me->GetActorLocation()) < 1.f)
			{
				Self = i;
				break;
			}
		}
		const int32 Size = CountLinemates(Members, Self, FT);
		Me->LineSize = Size;
		Me->bInLine = Size >= FT.LineMinSize;
		Me->FormationAccuracyScale = LineSpreadScale(Size, FT);
	}
}

bool UPA250VolleyCoordinator::VolleyReadyToCall(int32 SafeReady, int32 LiveNum, float ReadyFraction, float NotReadyTime, float MaxWaitSeconds)
{
	if (SafeReady <= 0 || LiveNum <= 0) return false;
	const float Frac = FMath::Clamp(ReadyFraction, 0.f, 1.f);
	const bool bFormed = SafeReady >= FMath::CeilToInt(Frac * LiveNum);
	return bFormed || NotReadyTime >= MaxWaitSeconds;
}

bool UPA250VolleyCoordinator::ShouldDischargePending(int32 SquadEpoch, int32 EntryEpoch, bool bAlive, bool bCanFire, bool bLaneClear, bool bTargetAlive)
{
	return SquadEpoch == EntryEpoch && bAlive && bCanFire && bLaneClear && bTargetAlive;
}

bool UPA250VolleyCoordinator::HasClearFiringLane(UWorld* World, const ARFCharacter* From, const ARFCharacter* Target, const FVector& AimAt)
{
	if (!World || !From) return false;
	const FVector Start = (From->Musket && From->Musket->GetMuzzleLocation())
		? From->Musket->GetMuzzleLocation()->GetComponentLocation()
		: From->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250VolleyLane), false, From);
	if (Target) Params.AddIgnoredActor(Target);
	if (From->Musket) Params.AddIgnoredActor(From->Musket);
	if (Target && Target->Musket) Params.AddIgnoredActor(Target->Musket);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, AimAt, ECC_Visibility, Params))
	{
		if (const ARFCharacter* HitChar = Cast<ARFCharacter>(Hit.GetActor()))
		{
			if (HitChar->FactionId == From->FactionId && !HitChar->IsDead()) return false;
			// Explicit: a different living enemy in the lane is still a hit, not a block.
			// The intended target is ignored by the trace, so any enemy reached first is fair.
			return true;
		}
		else
		{
			return false; // world geometry / props between muzzle and aim
		}
	}

	// Capsules often do not block Visibility; sweep Pawn for friendlies in the lane.
	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_Pawn);
	if (World->LineTraceMultiByObjectType(Hits, Start, AimAt, Obj, Params))
	{
		for (const FHitResult& H : Hits)
		{
			if (const ARFCharacter* HitChar = Cast<ARFCharacter>(H.GetActor()))
			{
				if (HitChar != Target && HitChar->FactionId == From->FactionId && !HitChar->IsDead()) return false;
			}
		}
	}
	return true;
}

void UPA250VolleyCoordinator::CancelVolleyOrders(FSquad& Sq, const TCHAR* Reason)
{
	++Sq.VolleyEpoch;
	Sq.PendingShots.Reset();
	Sq.Stage = EVolleyCall::None;
	Sq.StageTimer = 0.f;
	Sq.NotReadyTime = 0.f;
	Sq.DebugReason = Reason;
}

void UPA250VolleyCoordinator::DischargeOne(ARFCharacter* Shooter, ARFCharacter* Enemy, const FVector& AimAt)
{
	if (!Shooter || !Shooter->Musket || !Shooter->Musket->CanFire()) return;
	const float Dist = FVector::Dist(Shooter->GetActorLocation(), AimAt);
	RecordEngagement(Dist);
	++Engagements;
	if (Shooter->bInLine) ++LineEngagements;
	const APA250BotController* Bot = Cast<APA250BotController>(Shooter->GetController());
	const float AimErr = Bot ? Bot->AimErrorDegrees : 2.5f;
	const FVector Muzzle = Shooter->Musket->GetMuzzleLocation()->GetComponentLocation();
	const FVector Dir = FMath::VRandCone((AimAt - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErr));
	const bool bHit = Shooter->FireMusketAt(Muzzle + Dir * FVector::Dist(Muzzle, AimAt));
	const FVector ShotDir = Enemy
		? (Enemy->GetActorLocation() - Shooter->GetActorLocation()).GetSafeNormal()
		: Dir;
	NotifyShot(Shooter, ShotDir, bHit);
}

ARFCharacter* UPA250VolleyCoordinator::ResolveVolleyTarget(const FSquad& Sq, const ARFCharacter* Shooter, float EngageMaxCm) const
{
	ARFCharacter* Contact = Sq.Contact.Get();
	if (!Shooter || !Contact || Contact->IsDead() || Contact->FactionId == Sq.Faction || Contact->FactionId.IsNone()) return nullptr;
	if (FVector::Dist(Shooter->GetActorLocation(), Contact->GetActorLocation()) > EngageMaxCm) return nullptr;
	return HasLineOfSight(Shooter, Contact) ? Contact : nullptr;
}

void UPA250VolleyCoordinator::ScheduleVolley(FSquad& Sq, const TArray<ARFCharacter*>& EligibleShooters)
{
	++Sq.VolleyEpoch; // a new schedule supersedes any earlier pending shots
	Sq.PendingShots.Reset();
	if (!GameMode.IsValid() || !GameMode->GetWorld()) return;
	const UPA250BotTuning* BT = GameMode->GetBotTuning();
	const float Jitter = BT ? FMath::Max(0.f, BT->Values.VolleyFireJitterSeconds) : 0.1f;
	const float EngageMax = BT ? BT->Values.EngageMaxCm : 8000.f;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	UWorld* World = GameMode->GetWorld();

	for (ARFCharacter* C : EligibleShooters)
	{
		if (!C || C->IsDead() || !C->Musket || !C->Musket->CanFire()) continue;
		ARFCharacter* Enemy = ResolveVolleyTarget(Sq, C, EngageMax);
		if (!Enemy) continue;
		const FVector AimAt = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f);
		if (!HasClearFiringLane(World, C, Enemy, AimAt)) continue; // blocked rear / friendly sit out

		FPendingVolleyShot P;
		P.Shooter = C;
		P.Target = Enemy;
		P.AimPoint = AimAt;
		P.FireAtTime = Now + (Jitter > 0.f ? FMath::FRandRange(0.f, Jitter) : 0.f);
		P.Epoch = Sq.VolleyEpoch;
		Sq.PendingShots.Add(P);
	}
	Sq.DebugReason = Sq.PendingShots.Num() > 0 ? TEXT("firing") : TEXT("waiting_lane");
}

void UPA250VolleyCoordinator::TickPendingVolley(FSquad& Sq)
{
	if (!GameMode.IsValid() || !GameMode->GetWorld() || Sq.PendingShots.Num() == 0) return;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	UWorld* World = GameMode->GetWorld();
	for (int32 i = Sq.PendingShots.Num() - 1; i >= 0; --i)
	{
		const FPendingVolleyShot& P = Sq.PendingShots[i];
		if (P.FireAtTime > Now) continue;
		ARFCharacter* C = P.Shooter.Get();
		ARFCharacter* Enemy = P.Target.Get();
		const bool bAlive = C && !C->IsDead();
		const bool bTargetAlive = Enemy && !Enemy->IsDead();
		const bool bCanFire = bAlive && C->Musket && C->Musket->CanFire();
		const FVector AimAt = bTargetAlive ? Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f) : P.AimPoint;
		const bool bLane = bAlive && bTargetAlive && HasClearFiringLane(World, C, Enemy, AimAt);
		if (ShouldDischargePending(Sq.VolleyEpoch, P.Epoch, bAlive, bCanFire, bLane, bTargetAlive))
		{
			DischargeOne(C, Enemy, AimAt);
		}
		Sq.PendingShots.RemoveAtSwap(i);
	}
	if (Sq.PendingShots.Num() == 0 && Sq.Stage == EVolleyCall::Fire)
	{
		Sq.Stage = EVolleyCall::None;
		Sq.DebugReason = TEXT("idle");
	}
}

bool UPA250VolleyCoordinator::EvaluateSpacing(float& OutMedianNN, float& OutCrowdedFrac, int32& OutClustersBrit, int32& OutClustersCont) const
{
	OutMedianNN = 0.f;
	OutCrowdedFrac = 1.f;
	OutClustersBrit = OutClustersCont = 0;
	if (!GameMode.IsValid()) return false;
	TArray<FVector> Brit, Cont;
	for (TActorIterator<ARFCharacter> It(GameMode->GetWorld()); It; ++It)
	{
		if (It->IsDead()) continue;
		(It->FactionId == TEXT("british") ? Brit : Cont).Add(It->GetActorLocation());
	}
	auto MedianNN = [](const TArray<FVector>& Pts) -> float {
		if (Pts.Num() < 2) return 0.f;
		TArray<float> NNs;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			float Best = TNumericLimits<float>::Max();
			for (int32 j = 0; j < Pts.Num(); ++j)
			{
				if (i == j) continue;
				Best = FMath::Min(Best, FVector::Dist2D(Pts[i], Pts[j]));
			}
			NNs.Add(Best);
		}
		NNs.Sort();
		return NNs[NNs.Num() / 2];
	};
	auto CrowdedFrac = [](const TArray<FVector>& Pts, float R, int32 K) -> float {
		if (Pts.Num() == 0) return 0.f;
		int32 Bad = 0;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			int32 N = 0;
			for (int32 j = 0; j < Pts.Num(); ++j)
			{
				if (i != j && FVector::Dist2D(Pts[i], Pts[j]) < R) ++N;
			}
			if (N > K) ++Bad;
		}
		return (float)Bad / Pts.Num();
	};
	auto Clusters = [](const TArray<FVector>& Pts, float Link) -> int32 {
		TArray<int32> Parent;
		Parent.SetNum(Pts.Num());
		for (int32 i = 0; i < Pts.Num(); ++i) Parent[i] = i;
		auto Find = [&](int32 X) {
			while (Parent[X] != X) X = Parent[X] = Parent[Parent[X]];
			return X;
		};
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			for (int32 j = i + 1; j < Pts.Num(); ++j)
			{
				if (FVector::Dist2D(Pts[i], Pts[j]) <= Link)
				{
					Parent[Find(i)] = Find(j);
				}
			}
		}
		TSet<int32> Roots;
		for (int32 i = 0; i < Pts.Num(); ++i) Roots.Add(Find(i));
		return Roots.Num();
	};
	const TArray<FVector> All = Brit.Num() + Cont.Num() > 0 ? [&]() {
		TArray<FVector> A = Brit; A.Append(Cont); return A;
	}() : TArray<FVector>();
	OutMedianNN = MedianNN(All);
	OutCrowdedFrac = CrowdedFrac(All, 300.f, 4);
	OutClustersBrit = Clusters(Brit, 1200.f);
	OutClustersCont = Clusters(Cont, 1200.f);
	return true;
}

void UPA250VolleyCoordinator::PrintSpacingReport() const
{
	float Median = 0.f, Crowded = 0.f;
	int32 CB = 0, CC = 0;
	EvaluateSpacing(Median, Crowded, CB, CC);
	int32 CrowdedCount = 0, Total = 0;
	if (GameMode.IsValid())
	{
		TArray<ARFCharacter*> Alive;
		for (TActorIterator<ARFCharacter> It(GameMode->GetWorld()); It; ++It)
		{
			if (!It->IsDead()) Alive.Add(*It);
		}
		Total = Alive.Num();
		for (ARFCharacter* A : Alive)
		{
			int32 N = 0;
			for (ARFCharacter* B : Alive)
			{
				if (A != B && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) < 300.f) ++N;
			}
			if (N > 4) ++CrowdedCount;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("pa250.BotSpacingReport medianNN=%.1f crowded(>4 in 3m)=%d/%d (%.1f%%) clusters brit=%d cont=%d squads=%d"),
		Median, CrowdedCount, Total, Crowded * 100.f, CB, CC, Squads.Num());
	for (const FSquad& Sq : Squads)
	{
		int32 Live = 0, Formed = 0, Charging = 0;
		float Lag = 0.f;
		float NearestEnemySquad = TNumericLimits<float>::Max();
		for (const FSquad& O : Squads)
		{
			if (O.Faction != Sq.Faction && O.bAnchored) NearestEnemySquad = FMath::Min(NearestEnemySquad, FVector::Dist2D(O.Anchor, Sq.Anchor));
		}
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			const ARFCharacter* C = W.Get();
			if (!C || C->IsDead()) continue;
			++Live;
			const APA250BotController* Bot = Cast<APA250BotController>(C->GetController());
			if (Bot && Bot->HasFormationOrder())
			{
				++Formed;
				Lag += FVector::Dist2D(C->GetActorLocation(), Bot->GetFormationTarget());
				if (Bot->State == EBotState::Charge) ++Charging;
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("pa250.BotSpacingReport   squad %d %s live=%d formed=%d charging=%d engaged=%d stage=%d lag=%.0fcm enemySquad=%.0fm loaded=%d formedReady=%d safeReady=%d pending=%d wait=%.1fs reason=%s"),
			Sq.Id, *Sq.Faction.ToString(), Live, Formed, Charging, Sq.bEngaged ? 1 : 0, (int32)Sq.Stage,
			Formed > 0 ? Lag / Formed : 0.f, NearestEnemySquad < 1e9f ? NearestEnemySquad / 100.f : -1.f,
			Sq.DebugLoaded, Sq.DebugFormedReady, Sq.DebugSafeReady, Sq.PendingShots.Num(), Sq.NotReadyTime, *Sq.DebugReason);
	}
	const TArray<int32>& H = EngagementHistogram;
	UE_LOG(LogTemp, Display, TEXT("pa250.BotSpacingReport engagement m: <20=%d 20-40=%d 40-80=%d 80-120=%d >120=%d (band %.0f%% of %d)"),
		H[0], H[1], H[2], H[3], H[4], GetMidRangeFraction() * 100.f, GetEngagementSamples());
}

void UPA250VolleyCoordinator::AssignSquadSlots()
{
	if (!GameMode.IsValid()) return;
	UPA250BotTuning* BT = GameMode->GetBotTuning();
	const TArray<TWeakObjectPtr<APA250ControlPoint>>& Posts = GameMode->GetPosts();
	// Spread per side: a British squad on the Green must not push Continentals off it.
	TMap<FName, TSet<APA250ControlPoint*>> UsedPosts;
	TMap<int32, APA250ControlPoint*> PostOf;
	for (int32 Si = 0; Si < Squads.Num(); ++Si)
	{
		FSquad& Sq = Squads[Si];
		if (Sq.Members.Num() == 0 || Posts.Num() == 0) continue;
		APA250ControlPoint* Best = nullptr;
		float BestScore = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			APA250ControlPoint* P = WP.Get();
			if (!P) continue;
			if (P->GetHolder() == Sq.Faction && !P->IsContested()) continue;
			float Score = 0.f;
			if (ARFCharacter* Leader = Sq.Members[0].Get())
			{
				Score = FVector::Dist2D(Leader->GetActorLocation(), P->GetActorLocation());
			}
			Score *= (P->IsContested() || P->GetHolder().IsNone()) ? 0.7f : 1.1f;
			// Prefer a post this pass has not already given to another squad (15 m+ centers via distinct posts).
			if (UsedPosts.FindOrAdd(Sq.Faction).Contains(P)) Score += 50000.f;
			if (Score < BestScore) { BestScore = Score; Best = P; }
		}
		if (!Best) Best = Posts[FMath::Abs(Sq.Id) % Posts.Num()].Get();
		if (!Best) continue;
		UsedPosts.FindOrAdd(Sq.Faction).Add(Best);
		PostOf.Add(Si, Best);
		const int32 Sector = SectorForSquad(Sq.Id);
		const bool bFormed = Sq.Members.Num() >= 2;
		for (int32 Mi = 0; Mi < Sq.Members.Num(); ++Mi)
		{
			ARFCharacter* C = Sq.Members[Mi].Get();
			if (!C) continue;
			C->SquadId = Sq.Id;
			APA250BotController* Bot = Cast<APA250BotController>(C->GetController());
			if (!Bot) continue;
			Bot->BotTuning = BT;
			if (bFormed)
			{
				// Formed squads stand in ranks around their anchor, not on ring slots.
				if (Bot->ClaimedSlot != INDEX_NONE || Bot->ClaimedPost.IsValid()) Bot->AssignSlot(nullptr, INDEX_NONE, Sq.Id, Mi);
				Bot->SquadId = Sq.Id;
				Bot->FileIndex = Mi;
				continue;
			}
			const bool bHeld = Bot->ClaimedPost.Get() == Best
				&& Bot->ClaimedSlot != INDEX_NONE
				&& Best->GetSlots().IsValidIndex(Bot->ClaimedSlot)
				&& Best->GetSlots()[Bot->ClaimedSlot].Claimant.Get() == C;
			if (bHeld)
			{
				// Rebuild must not FindFreeSlot past the bot's own claim and then drop it.
				Bot->SquadId = Sq.Id;
				Bot->FileIndex = Mi;
				continue;
			}
			const int32 Slot = Best->FindFreeSlot(Sector);
			if (Slot == INDEX_NONE) continue;
			Bot->AssignSlot(Best, Slot, Sq.Id, Mi);
		}
	}

	// Squads of one side sent to the same post line up side by side, MinSquadCenterSeparation apart,
	// across their approach direction (attackers end up on the post, defenders around it).
	const float Sep = BT ? BT->Values.MinSquadCenterSeparationCm : 1500.f;
	TMap<TPair<FName, APA250ControlPoint*>, TArray<int32>> Groups;
	for (const TPair<int32, APA250ControlPoint*>& KV : PostOf)
	{
		Groups.FindOrAdd(TPair<FName, APA250ControlPoint*>(Squads[KV.Key].Faction, KV.Value)).Add(KV.Key);
	}
	for (TPair<TPair<FName, APA250ControlPoint*>, TArray<int32>>& G : Groups)
	{
		APA250ControlPoint* P = G.Key.Value;
		TArray<int32>& Ids = G.Value;
		FVector Approach = FVector::ZeroVector;
		for (int32 Si : Ids)
		{
			Approach += (P->GetActorLocation() - (Squads[Si].bAnchored ? Squads[Si].Anchor : P->GetActorLocation())).GetSafeNormal2D();
		}
		Approach = Approach.GetSafeNormal2D();
		if (Approach.IsNearlyZero()) Approach = FVector::ForwardVector;
		const FVector Right(-Approach.Y, Approach.X, 0.f);
		// Stable left-to-right order by where each squad already is.
		Ids.Sort([&](int32 A, int32 B) {
			return FVector::DotProduct(Squads[A].Anchor, Right) < FVector::DotProduct(Squads[B].Anchor, Right);
		});
		for (int32 k = 0; k < Ids.Num(); ++k)
		{
			FSquad& Sq = Squads[Ids[k]];
			Sq.bHasObjective = true;
			Sq.ObjectivePoint = P->GetActorLocation() + Right * ((k - 0.5f * (Ids.Num() - 1)) * Sep);
		}
	}

	// Skirmish roles (prefer cover) take a line behind a tagged wall (PA250Cover) in front of their
	// post - between it and the enemy - instead of standing on the open post. Distinct walls per squad.
	if (!bCoverAnchorsBuilt && GameMode.IsValid())
	{
		bCoverAnchorsBuilt = true;
		for (TActorIterator<AActor> It(GameMode->GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("PA250Cover"))) CoverAnchors.Add(It->GetActorLocation());
		}
	}
	TArray<FVector> Taken;
	for (const TPair<int32, APA250ControlPoint*>& KV : PostOf)
	{
		FSquad& Sq = Squads[KV.Key];
		if (!Sq.Role.bPrefersCover || !KV.Value) continue;
		const FName Enemy = Sq.Faction == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));
		const FVector PostLoc = KV.Value->GetActorLocation();
		const FVector EnemyRear = GameMode->GetRearPoint(Enemy, PostLoc + FVector(10000.f, 0.f, 0.f));
		const FVector OwnRear = GameMode->GetRearPoint(Sq.Faction, PostLoc - (EnemyRear - PostLoc));
		FVector Pick;
		if (PickCoverLine(PostLoc, EnemyRear, OwnRear, BT ? BT->Values.CoverLineSearchCm : 5000.f,
			BT ? BT->Values.MinSquadCenterSeparationCm : 1500.f, Taken, Pick))
		{
			Sq.ObjectivePoint = Pick;
			Sq.bHasObjective = true;
			Taken.Add(Pick);
		}
	}
}

bool UPA250VolleyCoordinator::PickCoverLine(const FVector& PostLoc, const FVector& EnemyRear, const FVector& OwnRear,
	float SearchCm, float MinApartCm, const TArray<FVector>& Taken, FVector& Out) const
{
	const FVector ToEnemy = (EnemyRear - PostLoc).GetSafeNormal2D();
	const FVector Friendly = (OwnRear - EnemyRear).GetSafeNormal2D();
	float Best = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const FVector& C : CoverAnchors)
	{
		const FVector Rel = (C - PostLoc) * FVector(1, 1, 0);
		const float D = Rel.Size();
		if (D > SearchCm) continue;
		if (FVector::DotProduct(Rel, ToEnemy) < -1500.f) continue; // well behind the post: no use
		bool bClash = false;
		for (const FVector& T : Taken) bClash |= FVector::Dist2D(T, C) < MinApartCm;
		if (bClash) continue;
		// Prefer walls toward the enemy (forward of the post), then close ones.
		const float Score = D - 0.5f * FVector::DotProduct(Rel, ToEnemy);
		if (Score < Best)
		{
			Best = Score;
			Out = C + Friendly * 150.f;
			bFound = true;
		}
	}
	return bFound;
}

void UPA250VolleyCoordinator::RebuildSquads(int32 MinSize, int32 MaxSize)
{
	UWorld* World = GameMode.IsValid() ? GameMode->GetWorld() : nullptr;
	if (!World) return;

	TSet<ARFCharacter*> InSquad;
	for (int32 i = Squads.Num() - 1; i >= 0; --i)
	{
		FSquad& Sq = Squads[i];
		Sq.Members.RemoveAll([](const TWeakObjectPtr<ARFCharacter>& W) { return !W.IsValid() || W->IsDead(); });
		// A shattered squad (a full squad cut to a couple of men) disbands; the survivors
		// regroup with others instead of sheltering in cover for the rest of the battle.
		const bool bShattered = Sq.Peak >= MinSize && Sq.Members.Num() < FMath::Max(2, MinSize / 2);
		if (Sq.Members.Num() == 0 || bShattered)
		{
			Squads.RemoveAt(i);
			continue;
		}
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members) InSquad.Add(W.Get());
	}

	TArray<ARFCharacter*> Loose;
	ARFCharacter* Player = nullptr;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		if (It->IsDead() || It->FactionId.IsNone()) continue;
		if (!ShouldJoinSquad(*It)) { Player = *It; continue; }
		if (!InSquad.Contains(*It))
		{
			It->SquadId = -1; // stale id from a squad that no longer exists
			Loose.Add(*It);
		}
	}

	auto Leader = [](const FSquad& Sq) -> ARFCharacter* { return Sq.Members.Num() > 0 ? Sq.Members[0].Get() : nullptr; };
	// Reinforcements join the nearest friendly squad with room, wherever it is, and run to their
	// place in its ranks; a man alone only founds a new squad when every squad is full.
	constexpr float JoinRadius = TNumericLimits<float>::Max();
	for (ARFCharacter* B : Loose)
	{
		// Join the nearest same-side squad with room, else found a new one.
		FSquad* Join = nullptr;
		float BestD = JoinRadius;
		const APA250BotController* BBot = Cast<APA250BotController>(B->GetController());
		const FName BTrain = BBot ? BBot->GetTraining().Name : NAME_None;
		const FName BRole = BBot ? BBot->GetRole().Name : NAME_None;
		for (FSquad& Sq : Squads)
		{
			const ARFCharacter* L = Leader(Sq);
			// A squad is one kind of soldier: same side, same training, same tactical role.
			if (!L || Sq.Faction != B->FactionId || Sq.Members.Num() >= MaxSize
				|| Sq.Training.Name != BTrain || Sq.Role.Name != BRole) continue;
			const float D = FVector::Dist2D(L->GetActorLocation(), B->GetActorLocation());
			if (D < BestD) { BestD = D; Join = &Sq; }
		}
		if (!Join)
		{
			FSquad NewSq;
			NewSq.Id = NextSquadId++;
			NewSq.Faction = B->FactionId;
			if (BBot)
			{
				NewSq.Training = BBot->GetTraining();
				NewSq.Role = BBot->GetRole();
			}
			NewSq.Training.Name = BTrain;
			NewSq.Role.Name = BRole;
			NewSq.Stage = EVolleyCall::MakeReady;
			NewSq.StageTimer = GameMode->GetTuning() ? GameMode->GetTuning()->Values.Volley.MakeReadySeconds : 1.f;
			Join = &Squads.Add_GetRef(NewSq);
		}
		Join->Members.Add(B);
	}

	for (FSquad& Sq : Squads)
	{
		Sq.Peak = FMath::Max(Sq.Peak, Sq.Members.Num());
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			if (ARFCharacter* M = W.Get()) M->SquadId = Sq.Id;
		}
	}

	// The player rides with the nearest friendly squad for nameplates ("always for squadmates").
	if (Player)
	{
		int32 BestId = -1;
		float BestD = TNumericLimits<float>::Max();
		for (const FSquad& Sq : Squads)
		{
			const ARFCharacter* L = Leader(Sq);
			if (!L || Sq.Faction != Player->FactionId) continue;
			const float D = FVector::DistSquared2D(L->GetActorLocation(), Player->GetActorLocation());
			if (D < BestD) { BestD = D; BestId = Sq.Id; }
		}
		Player->SquadId = BestId;
	}
}

float UPA250VolleyCoordinator::GetSquadStrength(int32 SquadId) const
{
	for (const FSquad& Sq : Squads)
	{
		if (Sq.Id != SquadId) continue;
		int32 Live = 0;
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			if (W.IsValid() && !W->IsDead()) ++Live;
		}
		return Sq.Peak > 0 ? (float)Live / (float)Sq.Peak : 1.f;
	}
	return 1.f;
}

int32 UPA250VolleyCoordinator::EngagementBucket(float RangeCm)
{
	const float M = RangeCm * 0.01f;
	if (M < 20.f) return 0;
	if (M < 40.f) return 1;
	if (M <= 80.f) return 2;
	if (M < 120.f) return 3;
	return 4;
}

void UPA250VolleyCoordinator::RecordEngagement(float RangeCm)
{
	if (RangeCm < 0.f) return;
	++EngagementHistogram[EngagementBucket(RangeCm)];
}

int32 UPA250VolleyCoordinator::GetEngagementSamples() const
{
	int32 N = 0;
	for (int32 V : EngagementHistogram) N += V;
	return N;
}

float UPA250VolleyCoordinator::GetMidRangeFraction() const
{
	const int32 N = GetEngagementSamples();
	return N > 0 ? (float)EngagementHistogram[2] / (float)N : 0.f;
}

bool UPA250VolleyCoordinator::HasLineOfSight(const ARFCharacter* From, const ARFCharacter* To) const
{
	if (!From || !To || !From->GetWorld()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250VolleyLOS), false, From);
	Params.AddIgnoredActor(To);
	if (From->Musket) Params.AddIgnoredActor(From->Musket);
	FHitResult Hit;
	return !From->GetWorld()->LineTraceSingleByChannel(Hit,
		From->GetActorLocation() + FVector(0.f, 0.f, 60.f), To->GetActorLocation() + FVector(0.f, 0.f, 40.f),
		ECC_Visibility, Params);
}

void UPA250VolleyCoordinator::TickSquads(float DeltaSeconds)
{
	if (!GameMode.IsValid()) return;
	FVolleyTuning Tun = GameMode->GetTuning() ? GameMode->GetTuning()->Values.Volley : FVolleyTuning();
	if (UPA250BotTuning* BT = GameMode->GetBotTuning())
	{
		Tun.SquadMinSize = BT->Values.SquadMinSize;
		Tun.SquadMaxSize = BT->Values.SquadMaxSize;
		Tun.VolleyWindowSeconds = BT->Values.VolleyWindowSeconds;
	}

	// Re-sort squads periodically. Squads persist (stable ids, sectors and slot claims);
	// only the dead drop out and new or respawned bots join.
	SquadRebuildTimer -= DeltaSeconds;
	if (SquadRebuildTimer <= 0.f || Squads.Num() == 0)
	{
		SquadRebuildTimer = 2.5f;
		RebuildSquads(FMath::Max(1, Tun.SquadMinSize), FMath::Max(1, Tun.SquadMaxSize));
		AssignSquadSlots();
	}

	for (FSquad& Sq : Squads)
	{
		TArray<ARFCharacter*> Live;
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			if (W.IsValid() && !W->IsDead()) Live.Add(W.Get());
		}
		if (Live.Num() < 2)
		{
			// A lone soldier fights on his own; cancel once, not every tick (epoch churn).
			if (Sq.Stage != EVolleyCall::None || Sq.PendingShots.Num() > 0) CancelVolleyOrders(Sq, TEXT("cancelled_size"));
			else Sq.DebugReason = TEXT("cancelled_size");
			continue;
		}
		TickFormation(Sq, Live, DeltaSeconds);
	}
}

void UPA250VolleyCoordinator::FormationOffsets(int32 N, float FileCm, float RankCm, int32 MaxPerRank, TArray<FVector2D>& Out)
{
	Out.Reset();
	if (N <= 0) return;
	const int32 PerRank = N < 4 ? N : FMath::Clamp(FMath::DivideAndRoundUp(N, 2), 1, FMath::Max(1, MaxPerRank));
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Rank = i / PerRank;
		const int32 File = i % PerRank;
		const int32 InRank = FMath::Min(PerRank, N - Rank * PerRank);
		const float Stagger = (Rank % 2) ? 0.5f * FileCm : 0.f; // rear rank covers the gaps
		Out.Add(FVector2D((File - 0.5f * (InRank - 1)) * FileCm + Stagger, Rank * RankCm));
	}
}

bool UPA250VolleyCoordinator::IsNearViewer(const FVector& Where) const
{
	const UWorld* World = GameMode.IsValid() ? GameMode->GetWorld() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AActor* View = PC ? PC->GetViewTarget() : nullptr;
	return View && FVector::Dist2D(View->GetActorLocation(), Where) < 6000.f;
}

void UPA250VolleyCoordinator::SortFiles(FSquad& Sq, const TArray<ARFCharacter*>& Live)
{
	// Front rank = men nearest the enemy, then left to right, so a turning line doesn't cross itself.
	const FVector Right(-Sq.Facing.Y, Sq.Facing.X, 0.f);
	TArray<ARFCharacter*> Order = Live;
	TArray<FVector2D> Offs;
	const UPA250BotTuning* BT = GameMode.IsValid() ? GameMode->GetBotTuning() : nullptr;
	FormationOffsets(Order.Num(), BT ? BT->Values.FileSpacingCm : 200.f, BT ? BT->Values.RankDepthCm : 200.f,
		BT ? BT->Values.MaxFilesPerRank : 6, Offs);
	Order.Sort([&](const ARFCharacter& A, const ARFCharacter& B) {
		const float FA = FVector::DotProduct(A.GetActorLocation() - Sq.Anchor, Sq.Facing);
		const float FB = FVector::DotProduct(B.GetActorLocation() - Sq.Anchor, Sq.Facing);
		return FA > FB;
	});
	// Within each rank, sort by lateral position.
	int32 Start = 0;
	while (Start < Order.Num())
	{
		int32 End = Start;
		while (End < Order.Num() && FMath::IsNearlyEqual(Offs[End].Y, Offs[Start].Y)) ++End;
		TArrayView<ARFCharacter*> RankView(Order.GetData() + Start, End - Start);
		RankView.Sort([&](const ARFCharacter& A, const ARFCharacter& B) {
			return FVector::DotProduct(A.GetActorLocation(), Right) < FVector::DotProduct(B.GetActorLocation(), Right);
		});
		Start = End;
	}
	Sq.FileOrder.Reset();
	for (ARFCharacter* C : Order) Sq.FileOrder.Add(C);
}

void UPA250VolleyCoordinator::TickFormation(FSquad& Sq, const TArray<ARFCharacter*>& Live, float DeltaSeconds)
{
	UWorld* World = GameMode->GetWorld();
	const UPA250BotTuning* BT = GameMode->GetBotTuning();
	const FPA250BotTuningValues Def;
	const FPA250BotTuningValues& T = BT ? BT->Values : Def;
	const FVolleyTuning VT = GameMode->GetTuning() ? GameMode->GetTuning()->Values.Volley : FVolleyTuning();
	const float Now = World->GetTimeSeconds();
	const FPA250TrainingProfile& Tr = Sq.Training;
	const FPA250TacticalRole& Ro = Sq.Role;

	// Cohesion by training: better-trained men stand closer to their places and the line waits
	// for stragglers sooner; worse-trained lines straggle.
	const float Arrive = T.FormationArriveCm * Tr.FormationToleranceScale;
	const float MaxLag = T.FormationMaxLagCm * Tr.StragglerToleranceScale;
	const float FileCm = T.FileSpacingCm * Ro.FileSpacingScale;
	const int32 PerRank = Ro.bOpenOrder ? 99 : T.MaxFilesPerRank; // open order = one loose rank

	FVector Center = FVector::ZeroVector;
	for (const ARFCharacter* C : Live) Center += C->GetActorLocation();
	Center /= Live.Num();
	if (!Sq.bAnchored)
	{
		Sq.bAnchored = true;
		Sq.Anchor = Center;
		Sq.Facing = Sq.bHasObjective ? (Sq.ObjectivePoint - Center).GetSafeNormal2D() : Live[0]->GetActorForwardVector().GetSafeNormal2D();
		if (Sq.Facing.IsNearlyZero()) Sq.Facing = FVector::ForwardVector;
		SortFiles(Sq, Live);
	}

	// Contact: nearest enemy inside the engage band that the front of the line can SEE (no walls).
	ARFCharacter* Contact = nullptr;
	float ContactDist = T.EngageMaxCm;
	int32 EnemiesNear = 0;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		if (It->IsDead() || It->FactionId == Sq.Faction || It->FactionId.IsNone()) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), Sq.Anchor);
		if (D < T.EngageMaxCm) ++EnemiesNear;
		if (D >= ContactDist) continue;
		bool bSeen = false;
		for (int32 i = 0; i < FMath::Min(3, Live.Num()) && !bSeen; ++i) bSeen = HasLineOfSight(Live[i], *It);
		if (bSeen) { ContactDist = D; Contact = *It; }
	}
	if (Contact)
	{
		Sq.LastContactTime = Now;
		Sq.Contact = Contact;
		Sq.bEngaged = true;
	}
	else if (Now - Sq.LastContactTime > T.DisengageSeconds)
	{
		Sq.bEngaged = false;
		Sq.Contact = nullptr;
	}

	// ---- Morale: pressure from casualties, suppression, strength, close / flank contact ----
	if (Sq.LastLive >= 0 && Live.Num() < Sq.LastLive)
	{
		Sq.RecentLoss += float(Sq.LastLive - Live.Num()) / FMath::Max(1, Sq.Peak);
	}
	Sq.LastLive = Live.Num();
	Sq.RecentLoss *= FMath::Pow(0.5f, DeltaSeconds / 12.f); // half-life 12 s
	int32 Suppressed = 0;
	for (const ARFCharacter* C : Live) Suppressed += C->SuppressionTimer > 0.f ? 1 : 0;
	const float Strength = Sq.Peak > 0 ? float(Live.Num()) / Sq.Peak : 1.f;
	const bool bClose = Sq.Contact.IsValid() && ContactDist < T.EngageMinCm;
	const bool bFlanked = Sq.Contact.IsValid()
		&& FVector::DotProduct((Sq.Contact->GetActorLocation() - Sq.Anchor).GetSafeNormal2D(), Sq.Facing) < -0.3f;
	bool bBeingCharged = false;
	for (const FSquad& Other : Squads)
	{
		if (Other.Faction != Sq.Faction && Other.bCharging && Other.bAnchored
			&& FVector::Dist2D(Other.Anchor, Sq.Anchor) < T.ChargeOrderRangeCm * 1.3f)
		{
			bBeingCharged = true;
		}
	}
	// Skirmishers don't stand to receive bayonets: an incoming charge is a reason to fall back in order
	// (and re-form at the next wall), not a blow to their nerve. Line troops feel the full shock.
	const bool bSkirmishEvade = bBeingCharged && Ro.bWithdrawWhenExposed;
	const float Pressure = PA250MoralePressure(Sq.RecentLoss, float(Suppressed) / Live.Num(), Strength, bClose, bFlanked,
		bBeingCharged && !bSkirmishEvade);

	// Deliberate withdrawal orders (never panic): heavily outnumbered while weakened (any role),
	// or exposed in the open at close range (roles that withdraw when exposed). Cooldown = cost.
	const bool bCooldown = Now - Sq.LastWithdrawTime < FMath::Max(Ro.RepositionCooldownSeconds, 1.f);
	const bool bOutnumbered = EnemiesNear >= 2 * Live.Num() && Strength < 0.6f;
	const bool bExposed = Ro.bWithdrawWhenExposed && Sq.bEngaged && ContactDist < Ro.ExposedRangeCm && Sq.CoveredFraction < 0.5f;
	const bool bWithdrawOrder = (!bCooldown && Sq.bEngaged && (bOutnumbered || bExposed)) || bSkirmishEvade;
	const float WithdrawnCm = FVector::Dist2D(Sq.Anchor, Sq.WithdrawStart);
	const bool bWithdrawDone = Sq.Morale.State == EPA250Morale::Withdrawing
		&& (WithdrawnCm >= Ro.RepositionCm || Sq.Morale.TimeInState > 15.f);
	const EPA250Morale Before = Sq.Morale.State;
	if (Sq.Morale.Tick(DeltaSeconds, Pressure, Tr.MoraleResolve, Tr.RallySeconds, Tr.ReformSeconds, bWithdrawOrder, bWithdrawDone))
	{
		MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.%s"), *Sq.Faction.ToString(), MoraleName(Sq.Morale.State)))++;
		if (Sq.Morale.State == EPA250Morale::Withdrawing)
		{
			Sq.WithdrawStart = Sq.Anchor;
			Sq.LastWithdrawTime = Now;
		}
		if (Sq.Morale.State == EPA250Morale::Rallying)
		{
			// Re-form where the men are, facing the last threat.
			Sq.Anchor = Center;
			Sq.RallyPoint = Center;
			SortFiles(Sq, Live);
		}
		if (Before == EPA250Morale::Routing || Sq.Morale.State == EPA250Morale::Routing)
		{
			CancelVolleyOrders(Sq, TEXT("cancelled_morale"));
		}
		AnnounceMorale(Sq, Before);
	}
	const FPA250MoraleOrders Orders = FPA250MoraleOrders::For(Sq.Morale.State);

	// Press the attack: a steady line goes for its objective (and into bayonet range) when the enemy
	// in front is breaking or falling back, or when it clearly outnumbers what it can see nearby.
	bool bEnemyBroken = false;
	if (const ARFCharacter* C = Sq.Contact.Get())
	{
		const EPA250Morale EM = C->SquadId >= 0 ? GetSquadMorale(C->SquadId) : EPA250Morale::Steady;
		bEnemyBroken = EM == EPA250Morale::Routing || EM == EPA250Morale::Withdrawing || EM == EPA250Morale::Rallying;
	}
	const bool bPress = Sq.bEngaged && Sq.bHasObjective && Sq.Morale.State == EPA250Morale::Steady
		&& (bEnemyBroken || (Live.Num() >= 1.5f * FMath::Max(1, EnemiesNear) && Strength > 0.6f));
	if (bPress && !Sq.bPressing && IsNearViewer(Sq.Anchor))
	{
		if (Sq.Faction == GameMode->GetPlayerFaction()) Shout(Sq.Faction, TEXT("forward"), TEXT("Forward! Push them off the ground!"), Sq.Anchor);
		else ShoutNearListener(TEXT("enemy_advancing"), TEXT("Their line is advancing!"));
	}
	Sq.bPressing = bPress;

	// ---- Bayonet charge order (both sides, same rules) ----
	if (Sq.bCharging)
	{
		const bool bTimeout = Now - Sq.ChargeStartTime > T.ChargeOrderMaxSeconds;
		const bool bNoFoe = !Sq.Contact.IsValid() || ContactDist > T.ChargeOrderRangeCm * 1.6f;
		if (bTimeout || bNoFoe || !Orders.bMayCharge)
		{
			Sq.bCharging = false;
			Sq.LastChargeEndTime = Now;
		}
	}
	else if (Sq.bEngaged && Sq.Contact.IsValid() && Now - Sq.LastChargeEndTime > T.ChargeOrderCooldownSeconds)
	{
		int32 Theirs = 0, TheirsReloading = 0, OursLoaded = 0;
		for (TActorIterator<ARFCharacter> It(World); It; ++It)
		{
			if (It->IsDead() || It->FactionId == Sq.Faction || It->FactionId.IsNone()) continue;
			if (FVector::Dist2D(It->GetActorLocation(), Sq.Anchor) > T.ChargeOrderRangeCm * 1.3f) continue;
			++Theirs;
			TheirsReloading += It->Musket && It->Musket->IsReloading() ? 1 : 0;
		}
		for (const ARFCharacter* C : Live) OursLoaded += C->Musket && C->Musket->CanFire() ? 1 : 0;
		if (PA250ShouldOrderCharge(Sq.Morale.State, Ro.bCautiousCharge, ContactDist, T.ChargeOrderRangeCm, bEnemyBroken,
			Theirs ? float(TheirsReloading) / Theirs : 0.f, float(OursLoaded) / Live.Num(), Live.Num(), Theirs))
		{
			Sq.bCharging = true;
			Sq.ChargeStartTime = Now;
			CancelVolleyOrders(Sq, TEXT("cancelled_charge"));
			MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Charge"), *Sq.Faction.ToString()))++;
			if (IsNearViewer(Sq.Anchor)) Shout(Sq.Faction, TEXT("charge_b"), TEXT("Charge! At them!"), Sq.Anchor);
		}
	}

	// ---- Formation spots ----
	TArray<FVector2D> Offs;
	FormationOffsets(Live.Num(), FileCm, T.RankDepthCm, PerRank, Offs);
	if (Sq.FileOrder.Num() != Live.Num()) SortFiles(Sq, Live);
	auto SpotFor = [&](int32 i) {
		const FVector Right(-Sq.Facing.Y, Sq.Facing.X, 0.f);
		return Sq.Anchor + Right * Offs[i].X - Sq.Facing * Offs[i].Y;
	};
	float Lag = 0.f;
	for (int32 i = 0; i < Sq.FileOrder.Num(); ++i)
	{
		if (const ARFCharacter* C = Sq.FileOrder[i].Get()) Lag += FVector::Dist2D(C->GetActorLocation(), SpotFor(i));
	}
	Lag /= FMath::Max(1, Sq.FileOrder.Num());

	FVector WantFacing = Sq.Facing;
	const float March = T.MarchSpeedCm * Orders.MarchScale;
	if (Orders.bFallBack)
	{
		// Withdraw facing the enemy, stepping back along the facing - ordered, formed, slow.
		if (ARFCharacter* C = Sq.Contact.Get()) WantFacing = (C->GetActorLocation() - Sq.Anchor).GetSafeNormal2D();
		if (Lag < MaxLag) Sq.Anchor -= Sq.Facing * March * DeltaSeconds;
	}
	else if (bPress && Orders.bMayAdvance)
	{
		// Advance on the objective between volleys, still facing the enemy; bayonets at < 10 m.
		const FVector ToGoal = FVector(Sq.ObjectivePoint - Sq.Anchor) * FVector(1, 1, 0);
		const float Len = ToGoal.Size();
		if (ARFCharacter* C = Sq.Contact.Get()) WantFacing = (C->GetActorLocation() - Sq.Anchor).GetSafeNormal2D();
		if (Len > 50.f && Sq.Stage == EVolleyCall::None && Lag < MaxLag)
		{
			Sq.Anchor += ToGoal / Len * FMath::Min(Len, 0.8f * March * DeltaSeconds);
		}
	}
	else if (Sq.bEngaged)
	{
		// Halt and face the enemy; between volleys step forward to the near edge of the band + 10 m.
		if (ARFCharacter* C = Sq.Contact.Get())
		{
			FVector ToEnemy = (C->GetActorLocation() - Sq.Anchor) * FVector(1, 1, 0);
			const float D = ToEnemy.Size();
			WantFacing = ToEnemy.GetSafeNormal();
			const float HoldAt = T.EngageMinCm + 1000.f;
			if (Orders.bMayAdvance && Sq.Stage == EVolleyCall::None && D > HoldAt && Lag < MaxLag)
			{
				Sq.Anchor += WantFacing * FMath::Min(D - HoldAt, 0.6f * March * DeltaSeconds);
			}
		}
	}
	else if (Sq.bHasObjective && Orders.bMayAdvance)
	{
		const FVector ToGoal = FVector(Sq.ObjectivePoint - Sq.Anchor) * FVector(1, 1, 0);
		const float Len = ToGoal.Size();
		if (Len > 50.f)
		{
			WantFacing = ToGoal / Len;
			if (Lag < MaxLag) Sq.Anchor += WantFacing * FMath::Min(Len, March * DeltaSeconds);
		}
	}
	if (!WantFacing.IsNearlyZero() && FVector::DotProduct(WantFacing, Sq.Facing) < FMath::Cos(FMath::DegreesToRadians(15.f)))
	{
		Sq.Facing = WantFacing;
		SortFiles(Sq, Live);
	}

	// Keep friendly squads apart.
	for (FSquad& Other : Squads)
	{
		if (&Other == &Sq || Other.Faction != Sq.Faction || !Other.bAnchored) continue;
		FVector Away = (Sq.Anchor - Other.Anchor) * FVector(1, 1, 0);
		const float D = Away.Size();
		if (D < T.MinSquadCenterSeparationCm)
		{
			Away = D > 1.f ? Away / D : FVector(-Sq.Facing.Y, Sq.Facing.X, 0.f);
			Sq.Anchor += Away * FMath::Min(T.MinSquadCenterSeparationCm - D, 400.f * DeltaSeconds);
		}
	}

	// ---- Cover (roles that prefer it): a firing position near each man's place, against the contact ----
	Sq.CoverSpot.SetNum(Sq.FileOrder.Num());
	Sq.bHasCover.SetNum(Sq.FileOrder.Num());
	Sq.CoverQueryTimer -= DeltaSeconds;
	if (Ro.bPrefersCover && Sq.CoverQueryTimer <= 0.f)
	{
		Sq.CoverQueryTimer = 2.f;
		int32 Covered = 0;
		for (int32 i = 0; i < Sq.FileOrder.Num(); ++i)
		{
			Sq.bHasCover[i] = false;
			const ARFCharacter* C = Sq.FileOrder[i].Get();
			if (!C || !Sq.Contact.IsValid() || !Orders.bKeepFormation) continue;
			TArray<const AActor*> Ignore = {C, Sq.Contact.Get(), C->Musket.Get(), Sq.Contact->Musket.Get()};
			FVector Pos;
			if (PA250Cover::FindFiringPosition(World, SpotFor(i), Ro.CoverSearchCm, Sq.Contact->GetActorLocation(), Ignore, Pos))
			{
				Sq.CoverSpot[i] = Pos;
				Sq.bHasCover[i] = true;
				++Covered;
			}
		}
		Sq.CoveredFraction = Sq.FileOrder.Num() ? float(Covered) / Sq.FileOrder.Num() : 0.f;
	}
	else if (!Ro.bPrefersCover)
	{
		for (int32 i = 0; i < Sq.bHasCover.Num(); ++i) Sq.bHasCover[i] = false;
		Sq.CoveredFraction = 0.f;
	}

	// ---- Fire: line roles volley together; skirmish roles fire at will from their positions ----
	auto TargetFor = [&](int32 i) { return Sq.bHasCover.IsValidIndex(i) && Sq.bHasCover[i] ? Sq.CoverSpot[i] : SpotFor(i); };
	int32 Loaded = 0, FormedReady = 0, SafeReady = 0;
	TArray<ARFCharacter*> SafeShooters;
	for (int32 i = 0; i < Sq.FileOrder.Num(); ++i)
	{
		ARFCharacter* C = Sq.FileOrder[i].Get();
		if (!C || !C->Musket) continue;
		if (C->Musket->CanFire()) ++Loaded;
		const bool bAtSpot = FVector::Dist2D(C->GetActorLocation(), TargetFor(i)) <= Arrive * 1.5f;
		if (C->Musket->CanFire() && bAtSpot)
		{
			++FormedReady;
			// Same target rule as discharge: squad contact, in range, seen by this man.
			ARFCharacter* Target = ResolveVolleyTarget(Sq, C, T.EngageMaxCm);
			if (Target && HasClearFiringLane(World, C, Target, Target->GetActorLocation() + FVector(0.f, 0.f, 20.f)))
			{
				++SafeReady;
				SafeShooters.Add(C);
			}
		}
	}
	Sq.DebugLoaded = Loaded;
	Sq.DebugFormedReady = FormedReady;
	Sq.DebugSafeReady = SafeReady;
	const bool bNear = IsNearViewer(Sq.Anchor);
	const bool bVolleys = !Ro.bIndividualFire;
	if (!bVolleys)
	{
		if (Sq.Stage != EVolleyCall::None || Sq.PendingShots.Num() > 0) CancelVolleyOrders(Sq, TEXT("skirmish"));
		else Sq.DebugReason = TEXT("skirmish");
	}
	else if (!Sq.bEngaged || !Orders.bMayVolley || Sq.bCharging)
	{
		const TCHAR* Why = Sq.bCharging ? TEXT("cancelled_charge")
			: !Orders.bMayVolley ? TEXT("cancelled_morale")
			: TEXT("cancelled_contact");
		if (Sq.Stage != EVolleyCall::None || Sq.PendingShots.Num() > 0) CancelVolleyOrders(Sq, Why);
		else { Sq.NotReadyTime = 0.f; Sq.DebugReason = Why; }
	}
	else
	{
		Sq.StageTimer -= DeltaSeconds;
		switch (Sq.Stage)
		{
		case EVolleyCall::None:
		{
			if (Sq.PendingShots.Num() > 0)
			{
				TickPendingVolley(Sq);
				Sq.DebugReason = TEXT("firing");
				break;
			}
			const float Frac = T.VolleyReadyFraction + Orders.ReadyFractionAdd;
			Sq.NotReadyTime += DeltaSeconds;
			if (SafeReady <= 0) Sq.DebugReason = FormedReady > 0 ? TEXT("waiting_lane") : (Loaded > 0 ? TEXT("waiting_form") : TEXT("waiting_load"));
			else if (!VolleyReadyToCall(SafeReady, Live.Num(), Frac, Sq.NotReadyTime, T.VolleyMaxWaitSeconds))
			{
				Sq.DebugReason = TEXT("waiting_form");
			}
			if (VolleyReadyToCall(SafeReady, Live.Num(), Frac, Sq.NotReadyTime, T.VolleyMaxWaitSeconds))
			{
				Sq.Stage = EVolleyCall::MakeReady;
				Sq.StageTimer = VT.MakeReadySeconds;
				Sq.NotReadyTime = 0.f;
				Sq.DebugReason = TEXT("make_ready");
				if (bNear) Shout(Sq.Faction, TEXT("make_ready"), TEXT("Make ready!"), Sq.Anchor, VT.MakeReadySeconds);
			}
			else if (bNear && Sq.NotReadyTime >= 2.f && CalloutTimer <= 0.f)
			{
				Callout = FString::Printf(TEXT("Volley wait: %s (%d safe)"), *Sq.DebugReason, SafeReady);
				CalloutTimer = 1.5f;
			}
			break;
		}
		case EVolleyCall::MakeReady:
			Sq.DebugReason = TEXT("make_ready");
			if (Sq.StageTimer <= 0.f)
			{
				Sq.Stage = EVolleyCall::Present;
				Sq.StageTimer = VT.PresentSeconds;
				Sq.DebugReason = TEXT("present");
				if (bNear) Shout(Sq.Faction, TEXT("present"), TEXT("Present!"), Sq.Anchor, VT.PresentSeconds);
			}
			break;
		case EVolleyCall::Present:
			Sq.DebugReason = TEXT("present");
			if (Sq.StageTimer <= 0.f)
			{
				if (bNear) Shout(Sq.Faction, TEXT("fire"), TEXT("FIRE!"), Sq.Anchor, 1.f);
				ScheduleVolley(Sq, SafeShooters);
				Sq.Stage = Sq.PendingShots.Num() > 0 ? EVolleyCall::Fire : EVolleyCall::None;
				if (Sq.Stage == EVolleyCall::Fire) TickPendingVolley(Sq); // fire any zero-delay shots now
			}
			break;
		case EVolleyCall::Fire:
			Sq.DebugReason = TEXT("firing");
			TickPendingVolley(Sq);
			break;
		default:
			CancelVolleyOrders(Sq, TEXT("cancelled_contact"));
			break;
		}
	}

	// ---- Orders to each man ----
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FVector Rear = GameMode->GetRearPoint(Sq.Faction, Sq.Anchor - Sq.Facing * 6000.f);
	for (int32 i = 0; i < Sq.FileOrder.Num(); ++i)
	{
		ARFCharacter* C = Sq.FileOrder[i].Get();
		APA250BotController* Bot = C ? Cast<APA250BotController>(C->GetController()) : nullptr;
		if (!Bot) continue;
		FPA250FormationOrder O;
		O.Facing = Sq.Facing;
		O.bEngaged = Sq.bEngaged;
		O.Contact = Sq.Contact.Get();
		O.bMayCharge = Orders.bMayCharge;
		O.ArriveCm = Arrive;
		if (Orders.bFleeToRear)
		{
			// Rout: every man for himself toward the rear, spread a little so they don't run as a block.
			O.bRout = true;
			O.Target = Rear + FVector(-Sq.Facing.Y, Sq.Facing.X, 0.f) * Offs[i].X * 2.f;
		}
		else
		{
			FVector Spot = TargetFor(i);
			if (Nav && !(Sq.bHasCover.IsValidIndex(i) && Sq.bHasCover[i]))
			{
				FNavLocation Projected;
				if (Nav->ProjectPointToNavigation(Spot, Projected, FVector(150.f, 150.f, 500.f))) Spot = Projected.Location;
			}
			O.Target = Spot;
			O.bPresent = bVolleys && Sq.bEngaged && (Sq.Stage == EVolleyCall::Present || Sq.Stage == EVolleyCall::MakeReady || Sq.Stage == EVolleyCall::Fire);
			O.bFireAtWill = !bVolleys && Sq.bEngaged && Orders.bMayVolley && !Sq.bCharging;
			O.bCharge = Sq.bCharging;
			O.bInCover = Sq.bHasCover.IsValidIndex(i) && Sq.bHasCover[i];
		}
		Bot->SetFormationOrder(O);
	}
}

void UPA250VolleyCoordinator::AnnounceMorale(const FSquad& Sq, EPA250Morale Before)
{
	const FName Player = GameMode.IsValid() ? GameMode->GetPlayerFaction() : NAME_None;
	const bool bOurs = !Player.IsNone() && Sq.Faction == Player;
	const FString Kind = Sq.Training.Name == TEXT("Recruit") && Sq.Faction == TEXT("continental") ? TEXT("militia")
		: Sq.Faction == TEXT("british") ? TEXT("Regulars") : TEXT("Continentals");
	// Routs are battle news: everyone hears about them in the kill feed.
	if (Sq.Morale.State == EPA250Morale::Routing && GameMode.IsValid())
	{
		GameMode->PushKillFeed(bOurs ? FString::Printf(TEXT("Our %s are routing!"), *Kind) : FString::Printf(TEXT("Enemy %s rout!"), *Kind));
	}
	if (!IsNearViewer(Sq.Anchor)) return;
	// Our men's orders come from our line; news about the enemy is shouted by our men near you.
	switch (Sq.Morale.State)
	{
	case EPA250Morale::Shaken:
		if (bOurs) Shout(Sq.Faction, TEXT("steady"), TEXT("Steady, lads! Hold the line!"), Sq.Anchor);
		break;
	case EPA250Morale::Withdrawing:
		if (bOurs) Shout(Sq.Faction, TEXT("fall_back"), TEXT("Fall back - in order!"), Sq.Anchor);
		else ShoutNearListener(TEXT("enemy_falling_back"), TEXT("They're falling back!"));
		break;
	case EPA250Morale::Routing:
		if (bOurs) Shout(Sq.Faction, TEXT("breaking"), TEXT("The line is breaking - run!"), Sq.Anchor);
		else ShoutNearListener(TEXT("enemy_running"), TEXT("They're running!"));
		break;
	case EPA250Morale::Rallying:
		if (bOurs) Shout(Sq.Faction, TEXT("rally"), TEXT("Rally! Form on me!"), Sq.Anchor);
		break;
	case EPA250Morale::Steady:
		if (bOurs && Before == EPA250Morale::Rallying) Shout(Sq.Faction, TEXT("reformed"), TEXT("Line re-formed!"), Sq.Anchor);
		break;
	}
}

void UPA250VolleyCoordinator::Shout(FName VoiceFaction, const TCHAR* Key, const TCHAR* Text, const FVector& Where, float Seconds)
{
	Callout = Text;
	CalloutTimer = Seconds;
	if (GameMode.IsValid())
	{
		FPA250BattleAudio::PlayVoice(GameMode.Get(), VoiceFaction, Key, Where + FVector(0.f, 0.f, 150.f));
	}
}

void UPA250VolleyCoordinator::ShoutNearListener(const TCHAR* Key, const TCHAR* Text)
{
	FVector Listener = FVector::ZeroVector;
	if (GameMode.IsValid()) FPA250BattleAudio::GetListener(GameMode.Get(), Listener);
	const FVector Offset(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f);
	Shout(GameMode.IsValid() ? GameMode->GetPlayerFaction() : NAME_None, Key, Text, Listener + Offset - FVector(0.f, 0.f, 150.f));
}

float UPA250VolleyCoordinator::GetSquadMoraleStress(int32 SquadId) const
{
	for (const FSquad& Sq : Squads)
	{
		if (Sq.Id == SquadId) return FPA250MoraleOrders::For(Sq.Morale.State).ReloadStress;
	}
	return 0.f;
}

EPA250Morale UPA250VolleyCoordinator::GetSquadMorale(int32 SquadId) const
{
	for (const FSquad& Sq : Squads)
	{
		if (Sq.Id == SquadId) return Sq.Morale.State;
	}
	return EPA250Morale::Steady;
}

FString UPA250VolleyCoordinator::MoraleTraceJson() const
{
	TArray<FString> Keys;
	MoraleTransitions.GetKeys(Keys);
	Keys.Sort();
	FString J = TEXT("{");
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		J += FString::Printf(TEXT("%s\"%s\":%d"), i ? TEXT(",") : TEXT(""), *Keys[i], MoraleTransitions.FindRef(Keys[i]));
	}
	return J + TEXT("}");
}

bool UPA250VolleyCoordinator::ShouldJoinSquad(const ARFCharacter* C)
{
	return C && !C->IsDead() && !C->FactionId.IsNone() && !C->IsPlayerControlled() && !Cast<APlayerController>(C->GetController());
}

void UPA250VolleyCoordinator::Tick(float DeltaSeconds)
{
	ShotClock += DeltaSeconds;
	CalloutTimer = FMath::Max(0.f, CalloutTimer - DeltaSeconds);
	RefreshFormations();
	TickSquads(DeltaSeconds);

	// Decay capture suppression.
	if (GameMode.IsValid())
	{
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : GameMode->GetPosts())
		{
			if (APA250ControlPoint* P = WP.Get())
			{
				if (P->CaptureRateScale < 0.99f)
				{
					P->CaptureRateScale = FMath::FInterpTo(P->CaptureRateScale, 1.f, DeltaSeconds, 0.35f);
				}
			}
		}
	}
}
