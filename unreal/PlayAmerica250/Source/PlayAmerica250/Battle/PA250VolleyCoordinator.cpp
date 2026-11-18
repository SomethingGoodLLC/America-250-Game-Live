#include "PA250VolleyCoordinator.h"
#include "Battle/PA250BattleRng.h"
#include "Battle/PA250BattleFX.h"
#include "Battle/PA250AnimClips.h"
#include "RF/PA250SoldierAnimInstance.h"
#include "Battle/PA250TraversalLinks.h"
#include "PA250MeleeComponent.h"
#include "Battle/PA250BattleFeel.h"
#include "Battle/PA250Intensity.h"
#include "PA250UnitClasses.h"
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
#include "PA250Support.h"
#include "PA250Smoke.h"
#include "PA250BattleAudio.h"
#include "GameFramework/PlayerController.h"
#include "WorldCollision.h"
#include "pa250/sim.hpp"

void UPA250VolleyCoordinator::Init(APA250BattleGameMode* InGM)
{
	GameMode = InGM;
}

FString UPA250VolleyCoordinator::GetCallout() const
{
	return CalloutTimer > 0.f ? Callout : FString();
}

void UPA250VolleyCoordinator::NotifyVolleyDischarge(ARFCharacter* Shooter)
{
	// The recoil frame of a volley clip (Notify_Volley_Discharge). A pending shot is fired by
	// whichever comes first, this notify or its FireAtTime, and is removed as it fires: the
	// other then finds nothing pending, so a man can never fire twice for one word of command.
	// With no volley clip playing the notify never comes and the timer fires as before.
	if (!Shooter || Shooter->IsDead() || !GameMode.IsValid()) return;
	FSquad* Sq = FindSquad(Shooter->SquadId);
	if (!Sq) return;
	for (int32 i = Sq->PendingShots.Num() - 1; i >= 0; --i)
	{
		if (Sq->PendingShots[i].Shooter.Get() != Shooter) continue;
		const FPendingVolleyShot P = Sq->PendingShots[i];
		Sq->PendingShots.RemoveAtSwap(i);
		FirePendingShot(*Sq, P);
		break;
	}
	if (Sq->PendingShots.Num() == 0 && Sq->Stage == EVolleyCall::Fire)
	{
		Sq->Stage = EVolleyCall::None;
		Sq->DebugReason = TEXT("idle");
	}
}

namespace
{
/** A man who can act a volley clip: a bot on foot, alive, with his musket. */
bool ActsVolley(const ARFCharacter* C)
{
	return C && !C->IsDead() && !C->IsPlayerControlled() && !C->IsMounted() && C->Musket && C->IsUsingPeriodSoldier();
}
}

void UPA250VolleyCoordinator::PlayVolleyStageClips(FSquad& Sq, const TArray<ARFCharacter*>& Live)
{
	const bool bReady = Sq.Stage == EVolleyCall::MakeReady;
	FPA250ActionParams Params;
	Params.bRootMotion = false;      // he keeps his place in the rank
	Params.bKeepSupportHand = true;  // clips made for a musket hold: the left hand stays on the stock
	for (int32 i = 0; i < Live.Num(); ++i)
	{
		ARFCharacter* C = Live[i];
		if (!ActsVolley(C) || !C->Musket->CanFire()) continue; // a man still loading goes on loading
		const FName Name = C->bIsCrouched ? (bReady ? FName(TEXT("volley_kneel_make_ready")) : FName(TEXT("volley_kneel_present")))
			: (bReady ? FName(TEXT("volley_make_ready")) : FName(TEXT("volley_present")));
		PA250AnimClips::PlayCombatClip(C, Name, HashCombine(GetTypeHash(Sq.Id), GetTypeHash(i)), &Params);
	}
}

void UPA250VolleyCoordinator::StartFireClip(FSquad& Sq, FPendingVolleyShot& P, float Now)
{
	if (P.bClipStarted) return;
	ARFCharacter* C = P.Shooter.Get();
	if (!ActsVolley(C)) { P.bClipStarted = true; return; }
	const FName Name = C->bIsCrouched ? FName(TEXT("volley_kneel_fire")) : FName(TEXT("volley_fire"));
	const uint32 Seed = HashCombine(GetTypeHash(Sq.Id), HashCombine(GetTypeHash(Sq.VolleyEpoch), GetTypeHash(C->GetUniqueID())));
	UAnimSequence* Take = PA250AnimClips::ClipVariant(Name, Seed);
	if (!Take) { P.bClipStarted = true; return; }
	// The clip is entered so that its discharge frame falls on FireAtTime: begun early when there
	// is time, or part-way in when the shot is nearer than the clip's lead-in.
	const float Lead = FMath::Max(0.f, UPA250SoldierAnimInstance::NotifyTime(Take, TEXT("Notify_Volley_Discharge")));
	const float Until = P.FireAtTime - Now;
	if (Until > Lead) return; // not yet
	P.bClipStarted = true;
	FPA250ActionParams Params;
	Params.bRootMotion = false;
	Params.bKeepSupportHand = true;
	Params.BlendIn = 0.08f;
	Params.StartTime = FMath::Max(0.f, Lead - FMath::Max(0.f, Until));
	TWeakObjectPtr<UPA250VolleyCoordinator> WeakThis(this);
	TWeakObjectPtr<ARFCharacter> WeakMan(C);
	PA250AnimClips::PlayCombatClip(C, Name, Seed, &Params, [WeakThis, WeakMan](FName Notify)
	{
		// Whichever comes first, this or the shot's timer, fires it; the other finds nothing pending.
		if (Notify == TEXT("Notify_Volley_Discharge") && WeakThis.IsValid() && WeakMan.IsValid()) WeakThis->NotifyVolleyDischarge(WeakMan.Get());
	});
}

float UPA250VolleyCoordinator::StageJitterSeconds(int32 SquadId, int32 VolleyEpoch, int32 OrderFromRight, float MaxMs)
{
	FRandomStream Rng(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(SquadId), GetTypeHash(VolleyEpoch)), GetTypeHash(OrderFromRight))));
	return FMath::Max(0.f, MaxMs) * 0.001f * Rng.FRand();
}

void UPA250VolleyCoordinator::FirePendingShot(FSquad& Sq, const FPendingVolleyShot& P)
{
	UWorld* World = GameMode.IsValid() ? GameMode->GetWorld() : nullptr;
	if (!World) return;
	ARFCharacter* C = P.Shooter.Get();
	if (P.bHeldTrigger)
	{
		// The fallen-in man's own shot, held for the volley: he fires where he is aiming.
		bool bWent = false;
		if (C && !C->IsDead() && Sq.VolleyEpoch == P.Epoch && C->Musket && C->Musket->CanFire())
		{
			bWent = C->GetController() ? C->FireMusket() : C->FireMusketAt(C->GetActorLocation() + Sq.Facing * 5000.f); // no controller: tests
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 fall-in: held shot with squad %d's volley: %s"), Sq.Id,
			bWent ? TEXT("fired") : !C || C->IsDead() ? TEXT("he is down") : Sq.VolleyEpoch != P.Epoch ? TEXT("the volley was called off")
			: !(C->Musket && C->Musket->CanFire()) ? TEXT("not loaded") : TEXT("no shot (misfire, or he could not fire there)"));
		return;
	}
	ARFCharacter* Enemy = P.Target.Get();
	const bool bAlive = C && !C->IsDead();
	const bool bTargetAlive = Enemy && !Enemy->IsDead();
	const bool bCanFire = bAlive && C->Musket && C->Musket->CanFire();
	if (P.bAtPoint)
	{
		// The leader's volley at ground with nobody on it: at the point he aimed at.
		if (Sq.VolleyEpoch == P.Epoch && bCanFire) DischargeOne(C, nullptr, P.AimPoint);
		return;
	}
	const FVector AimAt = bTargetAlive ? Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f) : P.AimPoint;
	const bool bLane = bAlive && bTargetAlive && HasClearFiringLane(World, C, Enemy, AimAt);
	if (ShouldDischargePending(Sq.VolleyEpoch, P.Epoch, bAlive, bCanFire, bLane, bTargetAlive))
	{
		DischargeOne(C, Enemy, AimAt);
	}
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
		// The player firing in rank with his line: the bots' own volleys shock when they are
		// scheduled (ScheduleVolley); this is the same verb for a volley the player completes.
		const bool bPlayerVolley = Shooter->IsPlayerControlled() && PA250UnitClasses::KitFor(Shooter->FactionId, Shooter->UnitClass).bVolleyShock;
		const float ReachCm = PA250UnitClasses::Verbs().VolleyReachM * 100.f;
		TSet<int32> Shocked;
		for (TActorIterator<ARFCharacter> It(Shooter->GetWorld()); It; ++It)
		{
			if (It->IsDead() || It->FactionId == Shooter->FactionId) continue;
			const FVector To = (It->GetActorLocation() - S.Origin).GetSafeNormal();
			const float Away = FVector::Dist(It->GetActorLocation(), S.Origin);
			if (FVector::DotProduct(S.Direction, To) > 0.7f && Away < 5000.f)
			{
				It->ApplySuppression(Tun.SuppressionSeconds, Tun.SuppressionSwayDegrees);
			}
			if (bPlayerVolley && It->SquadId >= 0 && !Shocked.Contains(It->SquadId) && FVector::DotProduct(S.Direction, To) > 0.9f && Away < ReachCm)
			{
				Shocked.Add(It->SquadId);
				ApplyVolleyShock(It->SquadId, Shooter->FactionId);
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

float UPA250VolleyCoordinator::VolleyShotDelay(int32 OrderFromRight, float RollingFileSeconds, float JitterSeconds, float Rand01)
{
	return FMath::Max(0, OrderFromRight) * FMath::Max(0.f, RollingFileSeconds)
		+ FMath::Max(0.f, JitterSeconds) * FMath::Clamp(Rand01, 0.f, 1.f);
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
	if (Sq.Id == FallInSquadId) bFallInShotHeld = false; // the volley he held his shot for is called off
}

void UPA250VolleyCoordinator::DischargeOne(ARFCharacter* Shooter, ARFCharacter* Enemy, const FVector& AimAt)
{
	if (!Shooter || !Shooter->Musket || !Shooter->Musket->CanFire()) return;
	if (Shooter->IsPlayerControlled()) return; // a man the player has taken over fires when the player says
	const float Dist = FVector::Dist(Shooter->GetActorLocation(), AimAt);
	RecordEngagement(Dist);
	++Engagements;
	if (Shooter->bInLine) ++LineEngagements;
	const APA250BotController* Bot = Cast<APA250BotController>(Shooter->GetController());
	const float AimErr = (Bot ? Bot->AimErrorDegrees : 2.5f) * Shooter->Musket->RangeSpreadScale(Dist);
	const FVector Muzzle = Shooter->Musket->GetMuzzleLocation()->GetComponentLocation();
	const FVector Dir = PA250BattleRng::Get().VRandCone((AimAt - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErr));
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
	// Artillery M9: VolleyShotDelay jitter is replaced by seeded 0–150 ms stage jitter (do not stack).
	// Rolling file delay stays for subsequent volleys.
	const float Rolling = Sq.VolleysThisFight > 0 ? Sq.Training.RollingFireFileSeconds : 0.f;
	const float StageJitterMs = pa250::battle_rules().blast_volley_stage_jitter_ms;
	const float EngageMax = Sq.FireMaxCm; // the class's own band (unit_classes.json)
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	UWorld* World = GameMode->GetWorld();

	// Right of the line first (X = lateral, right +, as in FormationOffsets).
	const FVector Right(-Sq.Facing.Y, Sq.Facing.X, 0.f);
	TArray<ARFCharacter*> Shooters = EligibleShooters;
	Shooters.RemoveAll([](const ARFCharacter* C) { return C == nullptr; });
	Shooters.StableSort([&](const ARFCharacter& A, const ARFCharacter& B) {
		return FVector::DotProduct(A.GetActorLocation(), Right) > FVector::DotProduct(B.GetActorLocation(), Right);
	});

	for (ARFCharacter* C : Shooters)
	{
		if (!C || C->IsDead() || !C->Musket || !C->Musket->CanFire()) continue;
		if (C == FallenIn.Get()) continue; // his trigger is his own (AddHeldTrigger)
		ARFCharacter* Enemy = ResolveVolleyTarget(Sq, C, EngageMax);
		if (!Enemy) continue;
		const FVector AimAt = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f);
		if (!HasClearFiringLane(World, C, Enemy, AimAt)) continue; // blocked rear / friendly sit out

		FPendingVolleyShot P;
		P.Shooter = C;
		P.Target = Enemy;
		P.AimPoint = AimAt;
		P.FireAtTime = Now + VolleyShotSeconds(Sq, Sq.PendingShots.Num(), Rolling, StageJitterMs);
		P.Epoch = Sq.VolleyEpoch;
		Sq.PendingShots.Add(P);
	}
	AddHeldTrigger(Sq, Now);
	if (Sq.PendingShots.Num() > 0) ++Sq.VolleysThisFight;
	Sq.DebugReason = Sq.PendingShots.Num() > 0 ? TEXT("firing") : TEXT("waiting_lane");
	// The regular's verb: enough muskets in one volley force a morale check on the squad fired on.
	if (Sq.PendingShots.Num() >= PA250UnitClasses::Verbs().VolleyMinShooters && PA250UnitClasses::KitFor(Sq.Faction, Sq.UnitClass).bVolleyShock)
	{
		if (const ARFCharacter* Target = Sq.Contact.Get()) ApplyVolleyShock(Target->SquadId, Sq.Faction);
	}
}

void UPA250VolleyCoordinator::TickPendingVolley(FSquad& Sq)
{
	if (!GameMode.IsValid() || !GameMode->GetWorld() || Sq.PendingShots.Num() == 0) return;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	for (int32 i = Sq.PendingShots.Num() - 1; i >= 0; --i)
	{
		StartFireClip(Sq, Sq.PendingShots[i], Now);
		if (Sq.PendingShots[i].FireAtTime > Now) continue;
		// Removed before it fires, so the clip's discharge notify cannot fire it a second time.
		const FPendingVolleyShot P = Sq.PendingShots[i];
		Sq.PendingShots.RemoveAtSwap(i);
		FirePendingShot(Sq, P);
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
	// The level's cover, read once (and again when works are raised or battered down).
	if (!bCoverAnchorsBuilt)
	{
		bCoverAnchorsBuilt = true;
		// The crossing links follow the same obstacles (a work raised or battered down).
		if (UPA250TraversalLinks* TraversalLinks = GameMode->GetWorld()->GetSubsystem<UPA250TraversalLinks>()) TraversalLinks->RequestRebuild();
		CoverAnchors.Reset();
		WorkAnchors.Reset();
		CoverSlots.Reset();
		CoverSlotOwner.Reset();
		const pa250::BattleRules& CoverRules = GameMode->GetRules();
		for (TActorIterator<AActor> It(GameMode->GetWorld()); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("PA250Cover"))) continue;
			CoverAnchors.Add(It->GetActorLocation());
			// Places along the wall itself: its long side from the mesh's own box, so a turned wall reads right.
			if (CoverRules.cover_slot_spacing_cm > 0.f)
			{
				FVector Centre = It->GetActorLocation(), Axis = It->GetActorForwardVector();
				float Length = 0.f, Depth = 0.f;
				if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(It->GetRootComponent()); Mesh && Mesh->GetStaticMesh())
				{
					const FVector Size = Mesh->GetStaticMesh()->GetBoundingBox().GetSize() * Mesh->GetComponentScale().GetAbs();
					const bool bAlongX = Size.X >= Size.Y;
					Axis = bAlongX ? Mesh->GetComponentQuat().GetAxisX() : Mesh->GetComponentQuat().GetAxisY();
					Length = bAlongX ? Size.X : Size.Y;
					Depth = bAlongX ? Size.Y : Size.X;
					Centre = Mesh->Bounds.Origin;
				}
				else
				{
					FVector Origin, Extent;
					It->GetActorBounds(true, Origin, Extent);
					const bool bAlongX = Extent.X >= Extent.Y;
					Axis = bAlongX ? FVector::XAxisVector : FVector::YAxisVector;
					Length = 2.f * (bAlongX ? Extent.X : Extent.Y);
					Depth = 2.f * (bAlongX ? Extent.Y : Extent.X);
					Centre = Origin;
				}
				PA250Cover::AddWallSlots(Centre, Axis, Length, Depth, CoverRules.cover_slot_spacing_cm, CoverRules.cover_stand_off_cm, CoverSlots);
			}
			if (It->ActorHasTag(TEXT("PA250FieldWork"))) WorkAnchors.Add(It->GetActorLocation());
		}
	}
	// Spread per side: a British squad on the Green must not push Continentals off it.
	TMap<FName, TSet<APA250ControlPoint*>> UsedPosts;
	TMap<int32, APA250ControlPoint*> PostOf;
	for (int32 Si = 0; Si < Squads.Num(); ++Si)
	{
		FSquad& Sq = Squads[Si];
		if (Sq.Members.Num() == 0 || Posts.Num() == 0) continue;
		APA250ControlPoint* Best = nullptr;
		float BestScore = TNumericLimits<float>::Max();
		// A garrison keeps to its works while the post is its side's and a work stands by it.
		if (APA250ControlPoint* Mine = Sq.WorksPost.Get(); Mine && Mine->GetHolder() == Sq.Faction)
		{
			for (const FVector& W : WorkAnchors)
			{
				if (FVector::Dist2D(W, Mine->GetActorLocation()) <= GameMode->GetRules().works_man_radius_cm) Best = Mine;
			}
		}
		if (!Best) Sq.WorksPost = nullptr;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			APA250ControlPoint* P = WP.Get();
			if (!P || Sq.WorksPost.IsValid()) continue;
			if (P->GetHolder() == Sq.Faction && !P->IsContested()) continue;
			if (P->IsContestLocked() && P->GetHolder() != Sq.Faction) continue; // closed by the assault or the landing
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
		// Assault and landing: most of the defender's squads stand at the post the attack must take next.
		if (!Best)
		{
			if (APA250ControlPoint* Open = GameMode->GetModeOpenPost(); Open && Open->GetHolder() == Sq.Faction
				&& (FMath::Abs(Sq.Id) * 37) % 100 < GameMode->GetRules().assault_garrison_share_pct)
			{
				Best = Open;
			}
		}
		if (!Best && WorkAnchors.Num() > 0)
		{
			// Nothing to take: garrison a post of our own, the one with works about it first.
			float BestGarrison = TNumericLimits<float>::Max();
			for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
			{
				APA250ControlPoint* P = WP.Get();
				if (!P || P->GetHolder() != Sq.Faction) continue;
				bool bWorks = false;
				for (const FVector& W : WorkAnchors) bWorks |= FVector::Dist2D(W, P->GetActorLocation()) <= GameMode->GetRules().works_man_radius_cm;
				float Score = (bWorks ? 0.f : 100000.f) + (UsedPosts.FindOrAdd(Sq.Faction).Contains(P) ? 1000.f : 0.f);
				if (ARFCharacter* Leader = Sq.Members[0].Get()) Score += 0.01f * FVector::Dist2D(Leader->GetActorLocation(), P->GetActorLocation());
				if (Score < BestGarrison)
				{
					BestGarrison = Score;
					Best = P;
				}
			}
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
	const pa250::BattleRules& WorkRules = GameMode->GetRules();
	TArray<FVector> Taken;
	for (const TPair<int32, APA250ControlPoint*>& KV : PostOf)
	{
		FSquad& Sq = Squads[KV.Key];
		Sq.bAtWorks = false;
		Sq.bHoldsPost = KV.Value && KV.Value->GetHolder() == Sq.Faction;
		if (!KV.Value) continue;
		const FName Enemy = Sq.Faction == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));
		const FVector PostLoc = KV.Value->GetActorLocation();
		const FVector EnemyRear = GameMode->GetRearPoint(Enemy, PostLoc + FVector(10000.f, 0.f, 0.f));
		const FVector OwnRear = GameMode->GetRearPoint(Sq.Faction, PostLoc - (EnemyRear - PostLoc));
		FVector Pick;
		// Whoever defends a post mans its works, whatever his drill: line troops as well as skirmishers.
		if (!bHoldGround && KV.Value->GetHolder() == Sq.Faction
			&& PickLine(WorkAnchors, PostLoc, EnemyRear, OwnRear, WorkRules.works_man_radius_cm, WorkRules.works_breastwork_length_cm, Taken, Pick))
		{
			Sq.ObjectivePoint = Pick;
			Sq.bHasObjective = true;
			Sq.bAtWorks = true;
			Sq.WorksPost = KV.Value;
			Taken.Add(Pick);
			continue;
		}
		if (!SquadUsesWalls(Sq)) continue;
		if (!bHoldGround && PickCoverLine(PostLoc, EnemyRear, OwnRear, BT ? BT->Values.CoverLineSearchCm : 5000.f,
			BT ? BT->Values.MinSquadCenterSeparationCm : 1500.f, Taken, Pick))
		{
			Sq.ObjectivePoint = Pick;
			Sq.bHasObjective = true;
			Taken.Add(Pick);
		}
	}
}

bool UPA250VolleyCoordinator::SquadUsesWalls(const FSquad& Sq) const
{
	if (Sq.Role.bPrefersCover) return true;
	if (!GameMode.IsValid()) return false;
	const std::string& Line = GameMode->GetRules().cover_line_troops;
	return Line == "always" || (Line == "holding" && Sq.bHoldsPost);
}

bool UPA250VolleyCoordinator::PickCoverLine(const FVector& PostLoc, const FVector& EnemyRear, const FVector& OwnRear,
	float SearchCm, float MinApartCm, const TArray<FVector>& Taken, FVector& Out) const
{
	return PickLine(CoverAnchors, PostLoc, EnemyRear, OwnRear, SearchCm, MinApartCm, Taken, Out);
}

bool UPA250VolleyCoordinator::PickLine(const TArray<FVector>& Anchors, const FVector& PostLoc, const FVector& EnemyRear, const FVector& OwnRear,
	float SearchCm, float MinApartCm, const TArray<FVector>& Taken, FVector& Out)
{
	const FVector ToEnemy = (EnemyRear - PostLoc).GetSafeNormal2D();
	const FVector Friendly = (OwnRear - EnemyRear).GetSafeNormal2D();
	float Best = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const FVector& C : Anchors)
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
		// A file with its leader standing stays his, however few are left of it.
		const bool bLedNow = Sq.Leader.IsValid() && !Sq.Leader->IsDead();
		const bool bShattered = !bLedNow && Sq.Peak >= MinSize && Sq.Members.Num() < FMath::Max(2, MinSize / 2);
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
		if (IsLeader(*It)) continue; // a leader stands before his file, not in anyone's ranks
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
			// A squad is one kind of soldier: same side, same training, same tactical role, and
			// (since each class has its own range and verb) the same class. Officers stand with any.
			// An officer, an ensign, a musician or a surgeon stands with whatever squad is nearest.
			const FPA250UnitClassKit& BKit = PA250UnitClasses::KitFor(B->FactionId, B->UnitClass);
			const bool bAttaches = BKit.bOfficer || BKit.bCarriesColours || BKit.bRelaysOrders || BKit.bTendsWounded;
			if (!L || Sq.Faction != B->FactionId || Sq.Members.Num() >= MaxSize) continue;
			if (!bAttaches && (Sq.Training.Name != BTrain || Sq.Role.Name != BRole || Sq.UnitClass != B->UnitClass)) continue;
			// ...but not with a squad that is itself only such men.
			if (bAttaches && !BKit.bOfficer)
			{
				const FPA250UnitClassKit& SKit = PA250UnitClasses::KitFor(Sq.Faction, Sq.UnitClass);
				if (SKit.bCarriesColours || SKit.bRelaysOrders || SKit.bTendsWounded) continue;
			}
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
			NewSq.UnitClass = B->UnitClass;
			{
				const FPA250UnitClassKit& K = PA250UnitClasses::KitFor(B->FactionId, B->UnitClass);
				NewSq.FireMaxCm = FMath::Max(500.f, K.PreferMaxM * 100.f);
				NewSq.PreferMinCm = FMath::Max(0.f, K.PreferMinM * 100.f);
			}
			NewSq.Stage = EVolleyCall::MakeReady;
			NewSq.StageTimer = pa250::battle_rules().drill_make_ready_s;
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
		if (const FSquad* Mine = FindLedSquad(Player)) BestId = Mine->Id; // his own file
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
	const FVector Eye = PA250Cover::EyePoint(From), Mark = To->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	if (From->GetWorld()->LineTraceSingleByChannel(Hit, Eye, Mark, ECC_Visibility, Params)) return false;
	const UPA250SmokeSubsystem* Smoke = From->GetWorld()->GetSubsystem<UPA250SmokeSubsystem>();
	return !(Smoke && Smoke->BlocksSight(Eye, Mark));
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
		const bool bLedFile = Sq.Leader.IsValid() && !Sq.Leader->IsDead();
		if (Live.Num() < (bLedFile ? 1 : 2))
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
	// Each rank stands in the gaps of the rank before it, so every man has a lane past the men in
	// front. A rank centred on the line has its men on whole files when it holds an odd number and on
	// half files when even; it is shifted half a file only when that puts it off the rank in front's
	// grid. A full rank behind a full rank is shifted (as before 2026-11-17); a short rear rank (5 men:
	// 3 and 2) is already in the gaps centred, and the old fixed shift stood it behind the front men.
	int32 PrevHalf = -1; // 1: the rank in front stands on half files, 0: on whole files
	for (int32 Rank = 0; Rank * PerRank < N; ++Rank)
	{
		const int32 InRank = FMath::Min(PerRank, N - Rank * PerRank);
		const int32 CentredHalf = (InRank % 2 == 0) ? 1 : 0;
		const bool bShift = PrevHalf >= 0 && CentredHalf == PrevHalf;
		const float Stagger = bShift ? 0.5f * FileCm : 0.f;
		for (int32 File = 0; File < InRank; ++File)
		{
			Out.Add(FVector2D((File - 0.5f * (InRank - 1)) * FileCm + Stagger, Rank * RankCm));
		}
		PrevHalf = bShift ? 1 - CentredHalf : CentredHalf;
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
	if (Sq.bAbreast)
	{
		// One rank at the leader's shoulders: left to right as the men stand, so nobody crosses.
		Order.Sort([&](const ARFCharacter& A, const ARFCharacter& B) {
			return FVector::DotProduct(A.GetActorLocation(), Right) < FVector::DotProduct(B.GetActorLocation(), Right);
		});
		Sq.FileOrder.Reset();
		for (ARFCharacter* C : Order) Sq.FileOrder.Add(C);
		return;
	}
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

	// A led squad: its own leader (the player's file), or an officer's order it obeys for a while.
	ARFCharacter* Leader = Sq.Leader.Get();
	const bool bLed = Leader && !Leader->IsDead();
	if (!bLed)
	{
		Sq.bLeaderAiming = false;
		Sq.bLeaderFired = false;
		const ARFCharacter* By = Sq.OrderedBy.Get();
		if (Sq.Order != EPA250SquadOrder::None && (!By || By->IsDead() || Sq.OrderUntil < 0.f || Now >= Sq.OrderUntil))
		{
			Sq.Order = EPA250SquadOrder::None;
		}
	}
	const ARFCharacter* Giver = bLed ? Leader : Sq.OrderedBy.Get();

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
	// A squad sees its enemy at musket range whatever its own band: a grenadier who holds his fire
	// to 25 m still has to know where to march.
	const float DetectCm = FMath::Max(Sq.FireMaxCm, T.EngageMaxCm);
	ARFCharacter* Contact = nullptr;
	float ContactDist = DetectCm;
	int32 EnemiesNear = 0;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		if (It->IsDead() || It->FactionId == Sq.Faction || It->FactionId.IsNone()) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), Sq.Anchor);
		if (D < DetectCm) ++EnemiesNear;
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
	// charge (the played general's verb): the men's blows are heavier for its time.
	{
		const float Blow = Now < Sq.HeroBlowUntil ? Sq.HeroBlowScale : 1.f;
		for (ARFCharacter* C : Live)
		{
			if (C->MeleeComponent) C->MeleeComponent->OrderDamageScale = Blow;
		}
	}
	Sq.RecentLoss *= FMath::Pow(0.5f, DeltaSeconds / 12.f); // half-life 12 s
	int32 Suppressed = 0;
	for (const ARFCharacter* C : Live) Suppressed += C->SuppressionTimer > 0.f ? 1 : 0;
	const float Strength = Sq.Peak > 0 ? float(Live.Num()) / Sq.Peak : 1.f;
	const bool bClose = Sq.Contact.IsValid() && ContactDist < Sq.PreferMinCm;
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
	const float CampaignBias =
		GameMode.IsValid() ? GameMode->GetCampaignConfidencePressure(Sq.Faction) : 0.f;
	// Class verbs on morale. A volley's shock fades out over its seconds. Militia are steadier behind
	// cover and quickly shaken when caught in the open.
	const FPA250UnitClassKit& ClassKit = PA250UnitClasses::KitFor(Sq.Faction, Sq.UnitClass);
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Shock = Now < Sq.ShockUntil ? Sq.ShockPressure * (Sq.ShockUntil - Now) / FMath::Max(0.1f, Sq.ShockSpan) : 0.f;
	const bool bInCover = Sq.CoveredFraction >= Verbs.CoveredFraction;
	const float OpenPressure = (ClassKit.bSteadyInCover && Sq.bEngaged && !bInCover) ? Verbs.OpenPressure : 0.f;
	const float Resolve = Tr.MoraleResolve * ((ClassKit.bSteadyInCover && bInCover) ? 1.f + Verbs.CoverResolveBonus : 1.f);
	// hold_fast (the played general's verb): what the fight does to them weighs less for its time. The
	// campaign's own bias is not his to change.
	const float HeroScale = Now < Sq.HeroPressureUntil ? Sq.HeroPressureScale : 1.f;
	const float Pressure = (PA250MoralePressure(Sq.RecentLoss, float(Suppressed) / Live.Num(), Strength, bClose, bFlanked,
		bBeingCharged && !bSkirmishEvade) + Shock + OpenPressure) * HeroScale + CampaignBias;

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
	// The ensign's verb: with the colours flying near, broken men come back to them sooner.
	const FVector* Flying = ColoursFlying.Find(Sq.Faction);
	const float RallySeconds = PA250Support::RallySecondsNearColours(Tr.RallySeconds,
		Flying && FVector::Dist2D(*Flying, Center) <= Verbs.EnsignRadiusM * 100.f, Verbs.EnsignRallyScale);
	if (Sq.Morale.Tick(DeltaSeconds, Pressure, Resolve, RallySeconds, Tr.ReformSeconds, bWithdrawOrder, bWithdrawDone, Verbs.Morale))
	{
		MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.%s"), *Sq.Faction.ToString(), MoraleName(Sq.Morale.State)))++;
		if (Sq.Morale.State == EPA250Morale::Withdrawing)
		{
			Sq.WithdrawStart = Sq.Anchor;
			Sq.LastWithdrawTime = Now;
			Sq.bDrillWithdrawal = Ro.bWithdrawWhenExposed && !bOutnumbered;
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
	const bool bPress = Sq.bEngaged && Sq.bHasObjective && Sq.Morale.State == EPA250Morale::Steady && !Sq.bAtWorks
		&& (bEnemyBroken || (Live.Num() >= 1.5f * FMath::Max(1, EnemiesNear) && Strength > 0.6f));
	if (bPress && !Sq.bPressing && IsNearViewer(Sq.Anchor))
	{
		// Fife and drum strike up as a line goes forward (either side; at most every 25 s per side).
		if (GameMode.IsValid())
		{
			static TMap<FName, double> LastMarch;
			if (Now - LastMarch.FindRef(Sq.Faction, -1000.0) > 25.0)
			{
				LastMarch.Add(Sq.Faction, Now);
				FPA250BattleAudio::PlayAt(GameMode.Get(), Sq.Faction == TEXT("british") ? TEXT("march_british") : TEXT("march_patriot"),
					Sq.Anchor + FVector(0.f, 0.f, 120.f), EPA250SoundRange::Gunfire, 0.7f, 0.f);
			}
		}
		if (Sq.Faction == GameMode->GetPlayerFaction()) Shout(Sq.Faction, TEXT("forward"), TEXT("Forward! Push them off the ground!"), Sq.Anchor);
		else ShoutNearListener(TEXT("enemy_advancing"), TEXT("Their line is advancing!"));
	}
	Sq.bPressing = bPress;

	// ---- Bayonet charge order (both sides, same rules) ----
	if (Sq.bCharging)
	{
		// The grenadier's verb: the charge reaches a squad, and that squad checks or breaks.
		if (ClassKit.bChargeRouts && !Sq.bRoutCheckDone && Sq.Contact.IsValid())
		{
			float Nearest = TNumericLimits<float>::Max();
			for (const ARFCharacter* C : Live) Nearest = FMath::Min(Nearest, FVector::Dist2D(C->GetActorLocation(), Sq.Contact->GetActorLocation()));
			if (Nearest <= Verbs.GrenadierContactM * 100.f)
			{
				Sq.bRoutCheckDone = true;
				ForceChargeRout(Sq.Contact->SquadId, Sq.Faction);
			}
		}
		const bool bTimeout = Now - Sq.ChargeStartTime > T.ChargeOrderMaxSeconds;
		const bool bOrderedCharge = Sq.Order == EPA250SquadOrder::Charge && Giver;
		const bool bNoFoe = !Sq.Contact.IsValid()
			|| FVector::Dist2D(Sq.Contact->GetActorLocation(), Sq.Anchor) > (bOrderedCharge ? FMath::Max(Sq.FireMaxCm, 6000.f) : T.ChargeOrderRangeCm * 1.6f);
		if (bTimeout || bNoFoe || !Orders.bMayCharge)
		{
			Sq.bCharging = false;
			Sq.LastChargeEndTime = Now;
		}
	}
	else if (Sq.Order != EPA250SquadOrder::None && Giver)
	{
		// Under orders a squad charges when it is told to, not when it sees a chance. A charge that has
		// ended hands the file back to "follow me" (another squad back to its own judgement).
		if (Sq.Order == EPA250SquadOrder::Charge) Sq.Order = bLed ? EPA250SquadOrder::FollowMe : EPA250SquadOrder::None;
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
			StartCharge(Sq, Now);
		}
	}

	// ---- Formation spots ----
	// A file following its leader stands at his shoulders, half to each side of him, where he can
	// see it; every other line stands in its ranks about its anchor.
	const bool bAbreast = bLed && Sq.Order == EPA250SquadOrder::FollowMe && !Sq.bCharging;
	if (bAbreast != Sq.bAbreast)
	{
		Sq.bAbreast = bAbreast;
		Sq.FileOrder.Reset(); // sorted again for the new places
	}
	TArray<FVector2D> Offs;
	if (bAbreast) PA250Squad::AbreastOffsets(Live.Num(), PA250UnitClasses::Verbs().SquadFollowSpacingM * 100.f, Offs);
	else FormationOffsets(Live.Num(), FileCm, T.RankDepthCm, PerRank, Offs);
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
	const float March = bHoldGround ? 0.f : T.MarchSpeedCm * Orders.MarchScale; // a held squad keeps its ground
	// Under orders (and not routing or re-forming): the line stands where the order puts it. The men
	// run to their places at their own pace, so the line keeps up with a leader who moves.
	const bool bObey = Sq.Order != EPA250SquadOrder::None && Giver && !Orders.bFleeToRear && Sq.Morale.State != EPA250Morale::Rallying;
	const bool bLedAim = bLed && bObey && Sq.bLeaderAiming && Sq.Order != EPA250SquadOrder::Charge && Sq.Order != EPA250SquadOrder::FallBack
		&& Orders.bMayVolley && !Sq.bCharging;
	if (bObey && !Sq.bCharging)
	{
		const FVector GiverLoc = Giver->GetActorLocation();
		FVector GiverFacing = Giver->GetActorForwardVector().GetSafeNormal2D();
		if (GiverFacing.IsNearlyZero()) GiverFacing = Sq.Facing;
		if (Sq.Order == EPA250SquadOrder::FallBack)
		{
			// To the nearest post the side holds; when the boxes are full (or there is nothing to
			// draw) the order is done.
			const APA250ControlPoint* Post = GameMode->NearestHeldPost(Sq.Faction, Sq.Anchor);
			const int32 Box = GameMode->GetRoundsPerMan(Sq.Faction);
			bool bAllFull = true;
			for (const ARFCharacter* C : Live) bAllFull &= !C->Musket || Box <= 0 || C->Musket->RoundsInBox >= Box - 1;
			if (!Post || bAllFull || !GameMode->CanResupply(Sq.Faction, false))
			{
				Sq.Order = bLed ? EPA250SquadOrder::FollowMe : EPA250SquadOrder::None;
			}
			else
			{
				Sq.OrderPoint = Post->GetActorLocation();
			}
		}
		if (Sq.Order != EPA250SquadOrder::None)
		{
			// Another squad told to follow an officer keeps the place it had beside him.
			const FVector Want = (!bLed && Sq.Order == EPA250SquadOrder::FollowMe)
				? GiverLoc + Sq.OrderPoint
				: PA250Squad::LedAnchor(Sq.Order, GiverLoc, GiverFacing, Sq.OrderPoint, Sq.Anchor, Verbs.SquadFollowBehindM * 100.f);
			const bool bFollowing = Sq.Order == EPA250SquadOrder::FollowMe;
			const bool bGiverMoving = Giver->GetVelocity().Size2D() > 60.f;
			const float StillBefore = Sq.LeaderStillSeconds;
			Sq.LeaderStillSeconds = bGiverMoving ? 0.f : Sq.LeaderStillSeconds + DeltaSeconds;
			const bool bJustHalted = StillBefore < Verbs.SquadFollowSettleSeconds && Sq.LeaderStillSeconds >= Verbs.SquadFollowSettleSeconds;
			// While he holds his aim the line halts where it is and turns to his aim point.
			if (!bLedAim && !bHoldGround)
			{
				// A following line is not nailed to him: it keeps its ground while he shifts a pace or two,
				// comes after him when he goes further, and closes up at a walk once he has halted.
				const FVector Next = bFollowing
					? PA250Squad::FollowAnchor(Sq.Anchor, Want, Verbs.SquadFollowSlackM * 100.f, Sq.LeaderStillSeconds >= Verbs.SquadFollowSettleSeconds,
						FMath::Max(Verbs.SquadWalkSpeedCm, 100.f) * DeltaSeconds)
					: Want;
				if (bFollowing && bLed && bGiverMoving && !Sq.bCharging && FVector::Dist2D(Next, Sq.Anchor) > 1.f && FVector::Dist2D(Want, Sq.Anchor) > Verbs.SquadFollowSlackM * 100.f)
				{
					CallFileAfter(Sq, TEXT("on_me"), TEXT("On me!"), GiverLoc);
				}
				// Halted with his men strung out behind him: he calls them up.
				if (bFollowing && bLed && !Sq.bCharging && bJustHalted && Lag > Verbs.SquadWalkWithinM * 100.f + 50.f)
				{
					CallFileAfter(Sq, TEXT("form_up"), TEXT("Form up!"), GiverLoc);
				}
				Sq.Anchor = FVector(Next.X, Next.Y, Sq.Anchor.Z);
			}
			if (bLedAim) WantFacing = (Sq.LeaderAimPoint - Sq.Anchor).GetSafeNormal2D();
			else if (bFollowing)
			{
				// The line wheels to his new facing only when he has kept it a moment: on the move past
				// follow_turn_moving_degrees, standing still past follow_turn_degrees (he may look about).
				const bool bWouldTurn = bGiverMoving && Verbs.SquadFollowTurnMovingDegrees > 0.f
					? PA250Squad::FollowTurns(Sq.Facing, GiverFacing, false, Verbs.SquadFollowTurnMovingDegrees)
					: PA250Squad::FollowTurns(Sq.Facing, GiverFacing, bGiverMoving, Verbs.SquadFollowTurnDegrees);
				Sq.FollowTurnSeconds = bWouldTurn ? Sq.FollowTurnSeconds + DeltaSeconds : 0.f;
				const bool bWheel = bWouldTurn && Sq.FollowTurnSeconds >= Verbs.SquadFollowTurnSeconds;
				if (bWheel && bLed && !Sq.bCharging)
				{
					const float Side = Sq.Facing.X * GiverFacing.Y - Sq.Facing.Y * GiverFacing.X; // > 0: he turned to the right
					const bool bCall = FVector::DotProduct(Sq.Facing, GiverFacing) < FMath::Cos(FMath::DegreesToRadians(FMath::Max(1.f, Verbs.SquadCallTurnDegrees)));
					if (bCall) CallFileAfter(Sq, Side > 0.f ? TEXT("to_the_right") : TEXT("to_the_left"), Side > 0.f ? TEXT("To the right!") : TEXT("To the left!"), GiverLoc);
				}
				WantFacing = bWheel ? GiverFacing : Sq.Facing;
				if (bWheel) Sq.FollowTurnSeconds = 0.f;
			}
			else if (ARFCharacter* C = Sq.Contact.Get()) WantFacing = (C->GetActorLocation() - Sq.Anchor).GetSafeNormal2D();
			else WantFacing = Sq.OrderFacing;
		}
	}
	else if (Orders.bFallBack)
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
			const float HoldAt = Sq.PreferMinCm + 1000.f; // the class closes to the near edge of its band
			if (Sq.bAtWorks && Sq.bHasObjective)
			{
				// Behind its works a squad keeps to them: back to its place if it was drawn off.
				const FVector Back = FVector(Sq.ObjectivePoint - Sq.Anchor) * FVector(1, 1, 0);
				if (Back.Size() > 50.f && Lag < MaxLag) Sq.Anchor += Back.GetSafeNormal() * FMath::Min(Back.Size(), March * DeltaSeconds);
			}
			else if (Orders.bMayAdvance && Sq.Stage == EVolleyCall::None && D > HoldAt && Lag < MaxLag)
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
	const bool bOrderedCover = bObey && (Sq.Order == EPA250SquadOrder::TakeCover || (bLedAim && Ro.bIndividualFire));
	const pa250::BattleRules& CoverRules = GameMode->GetRules();
	const bool bSeekCover = SquadUsesWalls(Sq) || bOrderedCover || Sq.bAtWorks;
	if (bSeekCover && Sq.CoverQueryTimer <= 0.f)
	{
		Sq.CoverQueryTimer = 2.f;
		++CoverSearch.SquadQueries;
		int32 Covered = 0;
		// Cover is cover from somewhere: the contact, else where the leader aims, else the front of the line.
		const FVector Threat = Sq.Contact.IsValid() ? Sq.Contact->GetActorLocation()
			: bLedAim ? Sq.LeaderAimPoint : Sq.Anchor + Sq.Facing * 6000.f;
		for (int32 i = 0; i < Sq.FileOrder.Num(); ++i)
		{
			Sq.bHasCover[i] = false;
			const ARFCharacter* C = Sq.FileOrder[i].Get();
			// Whatever place he had is free again until he is given one below.
			for (auto SlotIt = CoverSlotOwner.CreateIterator(); SlotIt; ++SlotIt)
			{
				if (SlotIt.Value().Get() == C || !SlotIt.Value().IsValid() || SlotIt.Value()->IsDead()) SlotIt.RemoveCurrent();
			}
			if (C) ++CoverSearch.Men;
			// Before the enemy is in sight only a squad that is holding ground takes cover (its own post,
			// its works, or the order to): a squad that has somewhere to go must not sit behind a wall out of range.
			const bool bHolding = Sq.bHoldsPost || Sq.bAtWorks || bOrderedCover;
			if (!C || (!Sq.Contact.IsValid() && !(bHolding && (bOrderedCover || CoverRules.cover_before_contact))) || !Orders.bKeepFormation)
			{
				if (C) ++CoverSearch.Skipped;
				continue;
			}
			TArray<const AActor*> Ignore = {C, C->Musket.Get()};
			if (Sq.Contact.IsValid())
			{
				Ignore.Add(Sq.Contact.Get());
				Ignore.Add(Sq.Contact->Musket.Get());
			}
			FVector Pos;
			// First a place along a wall itself (battle_rules.json cover): the nearest free one with the
			// wall between him and the threat, checked by trace. A few tries, then the old ring search.
			bool bWall = false;
			if (CoverRules.cover_slot_spacing_cm > 0.f && CoverRules.cover_search_cm > 0.f)
			{
				TSet<int32> Skip;
				for (const auto& Owned : CoverSlotOwner) Skip.Add(Owned.Key);
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				for (int32 Try = 0; Try < 4 && !bWall; ++Try)
				{
					const int32 Slot = PA250Cover::PickWallSlot(CoverSlots, SpotFor(i), Threat, CoverRules.cover_search_cm, Skip, CoverRules.cover_max_angle_deg);
					if (Slot == INDEX_NONE)
					{
						if (Try == 0) ++CoverSearch.NoSlotNear;
						break;
					}
					Skip.Add(Slot);
					FVector P = CoverSlots[Slot].Pos;
					FVector Feet = P;
					FNavLocation OnNav;
					if (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
					{
						if (!Nav->ProjectPointToNavigation(P, OnNav, FVector(60.f, 60.f, 300.f)))
						{
							++CoverSearch.SlotOffNav;
							continue;
						}
						Feet = OnNav.Location;
						P = OnNav.Location + FVector(0.f, 0.f, 90.f);
					}
					// The same test as the player's fire-over stance: the wall stops a ball at his chest
					// and his shoulder is clear over it. Whether he can see a man to shoot at from there
					// is his own business when he fires; a clear line all the way to the enemy (or to a
					// point 60 m off when no enemy is in sight) refused nearly every place along a wall.
					// ...and, with an enemy in contact, he can see that enemy over it from his shoulder.
					const bool bSeesContact = !Sq.Contact.IsValid() || PA250Cover::HasLineOfSight(World,
						Feet + FVector(0.f, 0.f, 90.f + PA250Cover::FiringOffsetCm), PA250Cover::EyePoint(Sq.Contact.Get()), Ignore);
					// The test is made along the line to the threat, not square to the wall: a ball from
					// where the enemy is must be stopped at his chest.
					if (!bSeesContact || !PA250Cover::IsAtCover(World, Feet, Threat - Feet, Ignore, CoverRules.cover_snap_cm))
					{
						++CoverSearch.SlotNotCover;
						continue;
					}
					Pos = P;
					bWall = true;
					CoverSlotOwner.Add(Slot, const_cast<ARFCharacter*>(C));
				}
			}
			if (bWall)
			{
				Sq.CoverSpot[i] = Pos;
				Sq.bHasCover[i] = true;
				++Covered;
				++CoverSearch.WallPlaces;
			}
			else if (PA250Cover::FindFiringPosition(World, SpotFor(i), FMath::Max(Ro.CoverSearchCm, (bOrderedCover || Sq.bAtWorks) ? 600.f : 0.f), Threat, Ignore, Pos))
			{
				Sq.CoverSpot[i] = Pos;
				Sq.bHasCover[i] = true;
				++Covered;
			}
		}
		Sq.CoveredFraction = Sq.FileOrder.Num() ? float(Covered) / Sq.FileOrder.Num() : 0.f;
	}
	else if (!bSeekCover)
	{
		if (Sq.CoverQueryTimer <= 0.f)
		{
			Sq.CoverQueryTimer = 2.f;
			++CoverSearch.SquadQueriesNot;
		}
		for (int32 i = 0; i < Sq.bHasCover.Num(); ++i) Sq.bHasCover[i] = false;
		Sq.CoveredFraction = 0.f;
	}

	// ---- Fire: line roles volley together; skirmish roles fire at will from their positions ----
	auto TargetFor = [&](int32 i) { return Sq.bHasCover.IsValidIndex(i) && Sq.bHasCover[i] ? Sq.CoverSpot[i] : SpotFor(i); };
	Sq.LastSpots.SetNum(Sq.FileOrder.Num());
	for (int32 i = 0; i < Sq.FileOrder.Num(); ++i) Sq.LastSpots[i] = TargetFor(i);
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
			ARFCharacter* Target = ResolveVolleyTarget(Sq, C, Sq.FireMaxCm);
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
	// The leader's aim. A man standing by the point he aims at is his target; the line presents on it
	// (a volley line) or fires at will on it from cover (skirmishers).
	ARFCharacter* LedTarget = bLedAim ? EnemyNear(Sq.Faction, Sq.LeaderAimPoint, Verbs.SquadAimSnapM * 100.f) : nullptr;
	if (LedTarget)
	{
		Sq.Contact = LedTarget;
		Sq.bEngaged = true;
		Sq.LastContactTime = Now;
	}
	TArray<ARFCharacter*> Presented;
	if (bLed)
	{
		for (int32 i = 0; i < Sq.FileOrder.Num(); ++i)
		{
			ARFCharacter* C = Sq.FileOrder[i].Get();
			if (C && C->Musket && C->Musket->CanFire() && FVector::Dist2D(C->GetActorLocation(), TargetFor(i)) <= Arrive * 1.5f) Presented.Add(C);
		}
	}
	Sq.DebugPresented = (Sq.Stage == EVolleyCall::Present) ? Presented.Num() : 0;
	const bool bFiredNow = Sq.bLeaderFired;
	Sq.bLeaderFired = false;
	const bool bLedVolley = bLed && bVolleys && (bLedAim || (Sq.Stage == EVolleyCall::Fire && Sq.PendingShots.Num() > 0 && Sq.PendingShots[0].bAtPoint));
	if (bLedVolley)
	{
		const EVolleyCall StageBefore = Sq.Stage;
		if (StageBefore == EVolleyCall::MakeReady) Sq.StageTimer -= DeltaSeconds;
		bool bFire = false;
		Sq.Stage = PA250Squad::LedVolleyNext(StageBefore, bLedAim, bFiredNow, Sq.PendingShots.Num() > 0, Loaded > 0, Sq.StageTimer, bFire);
		if (Sq.Stage != StageBefore && Sq.Stage == EVolleyCall::MakeReady)
		{
			// Stage times: battle_rules.drill (moved 1:1 from FVolleyTuning, which no longer has them).
			const float ReadyS = pa250::battle_rules().drill_make_ready_s;
			Sq.StageTimer = ReadyS;
			{ Shout(Sq.Faction, TEXT("make_ready"), TEXT("Make ready!"), Sq.Anchor, ReadyS); SoundDrillCall(Sq, TEXT("make_ready")); }
		}
		else if (Sq.Stage != StageBefore && Sq.Stage == EVolleyCall::Present)
		{
			{ Shout(Sq.Faction, TEXT("present"), TEXT("Present!"), Sq.Anchor, 3.f); SoundDrillCall(Sq, TEXT("present")); }
		}
		if (bFire)
		{
			{ Shout(Sq.Faction, TEXT("fire"), TEXT("FIRE!"), Sq.Anchor, 1.f); SoundDrillCall(Sq, TEXT("fire")); }
			ScheduleLedVolley(Sq, Presented);
			if (Sq.PendingShots.Num() == 0) Sq.Stage = EVolleyCall::None;
		}
		if (Sq.Stage == EVolleyCall::Fire) TickPendingVolley(Sq);
		Sq.DebugReason = Sq.Stage == EVolleyCall::Present ? TEXT("present_for_leader") : Sq.Stage == EVolleyCall::MakeReady ? TEXT("make_ready") : TEXT("led");
	}
	else if (!bVolleys)
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
		if (!Sq.bEngaged) Sq.VolleysThisFight = 0; // the next contact opens with a full volley again
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
				FRandomStream StageSeed(HashCombine(Sq.Id, GetTypeHash(Sq.Faction.GetComparisonIndex())));
				const float JitterSec = FMath::Max(0.f, pa250::battle_rules().blast_volley_stage_jitter_ms) * 0.001f;
				// battle_rules.drill; an officerless squad is slower to the word (ragged_response_scale).
				const float ReadyS = pa250::battle_rules().drill_make_ready_s * (Sq.OfficerReplaceLeft > 0.f ? pa250::battle_rules().drill_ragged_response_scale : 1.f);
				Sq.StageTimer = ReadyS + JitterSec * StageSeed.FRand();
				Sq.NotReadyTime = 0.f;
				Sq.DebugReason = TEXT("make_ready");
				if (bNear) { Shout(Sq.Faction, TEXT("make_ready"), TEXT("Make ready!"), Sq.Anchor, ReadyS); SoundDrillCall(Sq, TEXT("make_ready")); }
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
				FRandomStream StageSeed(HashCombine(Sq.Id + 17, GetTypeHash(Sq.Faction.GetComparisonIndex())));
				const float JitterSec = FMath::Max(0.f, pa250::battle_rules().blast_volley_stage_jitter_ms) * 0.001f;
				const float PresentS = pa250::battle_rules().drill_present_s * (Sq.OfficerReplaceLeft > 0.f ? pa250::battle_rules().drill_ragged_response_scale : 1.f);
				Sq.StageTimer = PresentS + JitterSec * StageSeed.FRand();
				Sq.DebugReason = TEXT("present");
				if (bNear) { Shout(Sq.Faction, TEXT("present"), TEXT("Present!"), Sq.Anchor, PresentS); SoundDrillCall(Sq, TEXT("present")); }
			}
			break;
		case EVolleyCall::Present:
			Sq.DebugReason = TEXT("present");
			if (Sq.StageTimer <= 0.f)
			{
				if (bNear) { Shout(Sq.Faction, TEXT("fire"), TEXT("FIRE!"), Sq.Anchor, 1.f); SoundDrillCall(Sq, TEXT("fire")); }
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

	// The word of command is acted: "Make ready" and "Present" each start their clip on the men
	// (the fire clip is started shot by shot, timed to its discharge; TickPendingVolley).
	if (Sq.Stage != Sq.ClipStage)
	{
		if (Sq.Stage == EVolleyCall::MakeReady || Sq.Stage == EVolleyCall::Present) PlayVolleyStageClips(Sq, Live);
		Sq.ClipStage = Sq.Stage;
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
			O.bPresent = bVolleys && (Sq.bEngaged || bLedAim) && (Sq.Stage == EVolleyCall::Present || Sq.Stage == EVolleyCall::MakeReady || Sq.Stage == EVolleyCall::Fire);
			O.bFireAtWill = !bVolleys && Sq.bEngaged && Orders.bMayVolley && !Sq.bCharging;
			O.bFocusContact = !bVolleys && bLedAim && LedTarget != nullptr;
			O.bAimAtPoint = bLedAim && LedTarget == nullptr;
			O.AimPoint = Sq.LeaderAimPoint;
			O.bHoldFire = bLedVolley && Sq.Stage != EVolleyCall::Fire;
			// His own file under his order does not dawdle: a man well behind his place runs to it.
			O.bHurry = bLed && bObey;
			O.HurryBeyondCm = PA250UnitClasses::Verbs().SquadRunWhenBehindM * 100.f;
			O.WalkWithinCm = PA250UnitClasses::Verbs().SquadWalkWithinM * 100.f;
			O.WalkSpeedCm = PA250UnitClasses::Verbs().SquadWalkSpeedCm;
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
		if (bOurs) ShoutEvent(TEXT("rout"), Sq.Faction, Sq.Anchor);
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

void UPA250VolleyCoordinator::SoundDrillCall(const FSquad& Sq, const TCHAR* Word)
{
	// A musician standing with the line beats (and fifes) the word: battle_look.json audio.orders.
	if (!GameMode.IsValid()) return;
	const ARFCharacter* Musician = nullptr;
	for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
	{
		const ARFCharacter* C = W.Get();
		if (C && !C->IsDead() && PA250UnitClasses::KitFor(C->FactionId, C->UnitClass).bRelaysOrders) { Musician = C; break; }
	}
	FString Drum, Fife;
	if (!Musician || !PA250BattleFX::OrderCue(Word, Drum, Fife)) return;
	const FVector At = Musician->GetActorLocation() + FVector(0.f, 0.f, 100.f);
	if (!Drum.IsEmpty()) FPA250BattleAudio::PlayAtOr(GameMode.Get(), Drum, TEXT("drum_roll"), At, EPA250SoundRange::Gunfire, 0.8f, 0.02f);
	if (!Fife.IsEmpty()) FPA250BattleAudio::PlayAt(GameMode.Get(), Fife, At + FVector(0.f, 0.f, 20.f), EPA250SoundRange::Gunfire, 0.6f, 0.02f);
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.DrillCall"), *Sq.Faction.ToString()))++;
}

void UPA250VolleyCoordinator::Shout(FName VoiceFaction, const TCHAR* Key, const TCHAR* Text, const FVector& Where, float Seconds)
{
	Callout = Text;
	CalloutTimer = Seconds;
	if (GameMode.IsValid())
	{
		FPA250BattleAudio::PlayVoice(GameMode.Get(), VoiceFaction, Key, Where + FVector(0.f, 0.f, 150.f));
		// The drummer rolls under "Make ready!"; the roll's closing accent lands as the muskets come up.
		if (FCString::Strcmp(Key, TEXT("make_ready")) == 0)
		{
			FPA250BattleAudio::PlayAt(GameMode.Get(), TEXT("drum_roll"), Where + FVector(0.f, 0.f, 100.f), EPA250SoundRange::Voice, 0.8f, 0.03f);
		}
	}
}

bool UPA250VolleyCoordinator::CallFileAfter(FSquad& Sq, const TCHAR* Key, const TCHAR* Text, const FVector& Where)
{
	const ARFCharacter* Leader = Sq.Leader.Get();
	if (!Leader || !Leader->IsPlayerControlled() || !GameMode.IsValid() || !GameMode->GetWorld()) return false;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	if (!PA250Intensity::MayShout(Now, Sq.LastFollowCallAt, PA250UnitClasses::Verbs().SquadCallCooldownSeconds)) return false;
	Sq.LastFollowCallAt = Now;
	FileCallCounts.FindOrAdd(FName(Key))++;
	Shout(Sq.Faction, Key, Text, Where, 1.5f);
	UE_LOG(LogTemp, Display, TEXT("PA250 squad: the leader calls \"%s\""), Text);
	return true;
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

UPA250VolleyCoordinator::FSquad* UPA250VolleyCoordinator::FindSquad(int32 SquadId)
{
	for (FSquad& Sq : Squads)
	{
		if (Sq.Id == SquadId) return &Sq;
	}
	return nullptr;
}

void UPA250VolleyCoordinator::StartCharge(FSquad& Sq, float Now)
{
	Sq.bCharging = true;
	Sq.bRoutCheckDone = false;
	Sq.ChargeStartTime = Now;
	CancelVolleyOrders(Sq, TEXT("cancelled_charge"));
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Charge"), *Sq.Faction.ToString()))++;
	if (IsNearViewer(Sq.Anchor)) ShoutEvent(TEXT("charge"), Sq.Faction, Sq.Anchor);
}

void UPA250VolleyCoordinator::ApplyVolleyShock(int32 TargetSquadId, FName ByFaction)
{
	FSquad* Target = FindSquad(TargetSquadId);
	if (!Target || Target->Faction == ByFaction || !GameMode.IsValid() || !GameMode->GetWorld()) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	Target->ShockUntil = GameMode->GetWorld()->GetTimeSeconds() + Verbs.VolleyShockSeconds;
	Target->ShockPressure = Verbs.VolleyShockPressure;
	Target->ShockSpan = Verbs.VolleyShockSeconds;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.VolleyShock"), *ByFaction.ToString()))++;
}

void UPA250VolleyCoordinator::ForceChargeRout(int32 TargetSquadId, FName ByFaction)
{
	FSquad* Target = FindSquad(TargetSquadId);
	if (!Target || Target->Faction == ByFaction) return;
	const EPA250Morale Before = Target->Morale.State;
	const EPA250Morale After = PA250ChargeRoutResult(Before);
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.ChargeRout"), *ByFaction.ToString()))++;
	if (After == Before) return;
	Target->Morale.Set(After);
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.%s"), *Target->Faction.ToString(), MoraleName(After)))++;
	if (After == EPA250Morale::Routing) CancelVolleyOrders(*Target, TEXT("cancelled_morale"));
	AnnounceMorale(*Target, Before);
}

bool UPA250VolleyCoordinator::ShakeSquadOf(const ARFCharacter* Officer)
{
	FSquad* Sq = Officer ? FindSquad(Officer->SquadId) : nullptr;
	if (!Sq || Sq->Morale.State != EPA250Morale::Steady) return false;
	const EPA250Morale Before = Sq->Morale.State;
	Sq->Morale.Set(EPA250Morale::Shaken);
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Shaken"), *Sq->Faction.ToString()))++;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.OfficerDown"), *Sq->Faction.ToString()))++;
	AnnounceMorale(*Sq, Before);
	return true;
}

bool UPA250VolleyCoordinator::RallyNearestRouting(FName Faction, const FVector& Where, float RadiusCm)
{
	FSquad* Best = nullptr;
	FVector BestCenter = FVector::ZeroVector;
	float BestD = RadiusCm;
	for (FSquad& Sq : Squads)
	{
		if (Sq.Faction != Faction || Sq.Morale.State != EPA250Morale::Routing) continue;
		// Routing men have left their ranks: judge the squad by where they are now.
		FVector Center = FVector::ZeroVector;
		int32 N = 0;
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			if (W.IsValid() && !W->IsDead()) { Center += W->GetActorLocation(); ++N; }
		}
		if (N == 0) continue;
		Center /= N;
		const float D = FVector::Dist2D(Center, Where);
		if (D <= BestD) { BestD = D; Best = &Sq; BestCenter = Center; }
	}
	if (!Best) return false;
	const EPA250Morale Before = Best->Morale.State;
	Best->Morale.Set(EPA250Morale::Rallying);
	Best->Anchor = BestCenter;
	Best->RallyPoint = BestCenter;
	Best->RecentLoss *= 0.5f; // the officer's presence: the losses weigh less on men who have been steadied
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Rallying"), *Faction.ToString()))++;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Rallied"), *Faction.ToString()))++;
	AnnounceMorale(*Best, Before);
	return true;
}

FString UPA250VolleyCoordinator::OrderNearestLine(FName Faction, const FVector& Where, float RadiusCm, bool bAllowVolley, bool bAllowCharge)
{
	if (!GameMode.IsValid() || !GameMode->GetWorld()) return FString();
	const UPA250BotTuning* BT = GameMode->GetBotTuning();
	const float ChargeRange = BT ? BT->Values.ChargeOrderRangeCm : 3500.f;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	FSquad* Best = nullptr;
	float BestD = RadiusCm;
	for (FSquad& Sq : Squads)
	{
		// A line that can obey: steady or shaken, in volley order, with an enemy in front of it.
		if (Sq.Faction != Faction || Sq.Role.bIndividualFire || !Sq.bAnchored || !Sq.Contact.IsValid()) continue;
		if (Sq.Morale.State != EPA250Morale::Steady && Sq.Morale.State != EPA250Morale::Shaken) continue;
		const float D = FVector::Dist2D(Sq.Anchor, Where);
		if (D <= BestD) { BestD = D; Best = &Sq; }
	}
	if (!Best) return FString();
	const float ContactDist = FVector::Dist2D(Best->Contact->GetActorLocation(), Best->Anchor);
	if (bAllowCharge && !Best->bCharging && Best->Morale.State == EPA250Morale::Steady && ContactDist <= ChargeRange)
	{
		StartCharge(*Best, Now);
		return TEXT("charge");
	}
	if (bAllowVolley && !Best->bCharging && Best->PendingShots.Num() == 0)
	{
		TArray<ARFCharacter*> Shooters;
		for (const TWeakObjectPtr<ARFCharacter>& W : Best->Members)
		{
			ARFCharacter* C = W.Get();
			if (C && !C->IsDead() && C->Musket && C->Musket->CanFire()) Shooters.Add(C);
		}
		if (Shooters.Num() > 0)
		{
			ScheduleVolley(*Best, Shooters);
			if (Best->PendingShots.Num() > 0)
			{
				Best->Stage = EVolleyCall::Fire;
				MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.OrderedVolley"), *Faction.ToString()))++;
				return TEXT("volley");
			}
		}
	}
	return FString();
}

bool UPA250VolleyCoordinator::SquadCentre(const FSquad& Sq, FVector& OutCentre)
{
	FVector Sum = FVector::ZeroVector;
	int32 N = 0;
	for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
	{
		if (W.IsValid() && !W->IsDead())
		{
			Sum += W->GetActorLocation();
			++N;
		}
	}
	if (N == 0) return false;
	OutCentre = Sum / N;
	return true;
}

int32 UPA250VolleyCoordinator::RallyAllRouting(FName Faction, const FVector& Where, float RadiusCm)
{
	int32 Rallied = 0;
	for (FSquad& Sq : Squads)
	{
		FVector Centre;
		if (Sq.Faction != Faction || Sq.Morale.State != EPA250Morale::Routing || !SquadCentre(Sq, Centre)) continue;
		if (FVector::Dist2D(Centre, Where) > RadiusCm) continue;
		const EPA250Morale Before = Sq.Morale.State;
		Sq.Morale.Set(EPA250Morale::Rallying);
		Sq.Anchor = Centre;
		Sq.RallyPoint = Centre;
		Sq.RecentLoss *= 0.5f; // as the officer's rally: the losses weigh less on men who have been steadied
		MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Rallying"), *Faction.ToString()))++;
		MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Rallied"), *Faction.ToString()))++;
		AnnounceMorale(Sq, Before);
		++Rallied;
	}
	if (Rallied > 0) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.GeneralRallied"), *Faction.ToString()))++;
	return Rallied;
}

int32 UPA250VolleyCoordinator::HeroHoldFast(FName Faction, const FVector& Where, float RadiusCm, float Seconds, float Scale)
{
	if (!GameMode.IsValid() || !GameMode->GetWorld()) return 0;
	const float Until = GameMode->GetWorld()->GetTimeSeconds() + Seconds;
	int32 Reached = 0;
	for (FSquad& Sq : Squads)
	{
		FVector Centre;
		if (Sq.Faction != Faction || !SquadCentre(Sq, Centre) || FVector::Dist2D(Centre, Where) > RadiusCm) continue;
		Sq.HeroPressureScale = FMath::Clamp(Scale, 0.f, 1.f);
		Sq.HeroPressureUntil = Until;
		++Reached;
	}
	if (Reached > 0) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.GeneralHoldFast"), *Faction.ToString()))++;
	return Reached;
}

int32 UPA250VolleyCoordinator::HeroCharge(FName Faction, const FVector& Where, float RadiusCm, float Seconds, float Scale, int32& OutCharging)
{
	OutCharging = 0;
	if (!GameMode.IsValid() || !GameMode->GetWorld()) return 0;
	const UPA250BotTuning* BT = GameMode->GetBotTuning();
	const float ChargeRange = BT ? BT->Values.ChargeOrderRangeCm : 3500.f;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	int32 Reached = 0;
	for (FSquad& Sq : Squads)
	{
		FVector Centre;
		if (Sq.Faction != Faction || !SquadCentre(Sq, Centre) || FVector::Dist2D(Centre, Where) > RadiusCm) continue;
		Sq.HeroBlowScale = FMath::Max(1.f, Scale);
		Sq.HeroBlowUntil = Now + Seconds;
		++Reached;
		// A line that can obey goes in: steady or shaken, in volley order, an enemy in front and within charge range.
		const bool bCanObey = !Sq.Role.bIndividualFire && Sq.bAnchored && Sq.Contact.IsValid() && !Sq.bCharging
			&& (Sq.Morale.State == EPA250Morale::Steady || Sq.Morale.State == EPA250Morale::Shaken)
			&& FVector::Dist2D(Sq.Contact->GetActorLocation(), Sq.Anchor) <= ChargeRange;
		if (bCanObey)
		{
			StartCharge(Sq, Now);
			++OutCharging;
		}
	}
	if (Reached > 0) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.GeneralCharge"), *Faction.ToString()))++;
	return Reached;
}

int32 UPA250VolleyCoordinator::HeroSteady(FName Faction, const FVector& Where, float RadiusCm, float Share)
{
	const float Keep = 1.f - FMath::Clamp(Share, 0.f, 1.f);
	int32 Reached = 0;
	for (FSquad& Sq : Squads)
	{
		FVector Centre;
		if (Sq.Faction != Faction || !SquadCentre(Sq, Centre) || FVector::Dist2D(Centre, Where) > RadiusCm) continue;
		Sq.RecentLoss *= Keep;
		Sq.ShockPressure *= Keep;
		if (Sq.Morale.State == EPA250Morale::Shaken)
		{
			const EPA250Morale Before = Sq.Morale.State;
			Sq.Morale.Set(EPA250Morale::Steady);
			MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Steady"), *Faction.ToString()))++;
			AnnounceMorale(Sq, Before);
		}
		++Reached;
	}
	if (Reached > 0) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.GeneralSteadied"), *Faction.ToString()))++;
	return Reached;
}

int32 UPA250VolleyCoordinator::HeroQuickStep(FName Faction, const FVector& Where, float RadiusCm, float Seconds, float Scale)
{
	if (!GameMode.IsValid() || !GameMode->GetWorld()) return 0;
	const float Until = GameMode->GetWorld()->GetTimeSeconds() + Seconds;
	int32 Reached = 0;
	for (FSquad& Sq : Squads)
	{
		FVector Centre;
		if (Sq.Faction != Faction || !SquadCentre(Sq, Centre) || FVector::Dist2D(Centre, Where) > RadiusCm) continue;
		Sq.HeroSpeedScale = FMath::Max(1.f, Scale);
		Sq.HeroSpeedUntil = Until;
		++Reached;
	}
	if (Reached > 0) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.GeneralQuickStep"), *Faction.ToString()))++;
	return Reached;
}

float UPA250VolleyCoordinator::SpeedScaleFor(const ARFCharacter* Soldier) const
{
	if (!Soldier || Soldier->SquadId < 0 || !GameMode.IsValid() || !GameMode->GetWorld()) return 1.f;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	for (const FSquad& Sq : Squads)
	{
		if (Sq.Id == Soldier->SquadId) return Now < Sq.HeroSpeedUntil ? Sq.HeroSpeedScale : 1.f;
	}
	return 1.f;
}

bool UPA250VolleyCoordinator::ShoutEvent(FName Event, FName Faction, const FVector& Where)
{
	const FPA250Shout* Words = PA250BattleFeel::Get().Shouts.Find(Event);
	if (!Words || !GameMode.IsValid() || !GameMode->GetWorld()) return false;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	float& Last = LastShoutEventAt.FindOrAdd(Event.ToString() + TEXT(".") + Faction.ToString(), -1000.f);
	if (!PA250Intensity::MayShout(Now, Last, Words->CooldownSeconds)) return false;
	Last = Now;
	ShoutEventCounts.FindOrAdd(Event)++;
	Shout(Faction, *Words->Key, *Words->Text, Where);
	return true;
}

float UPA250VolleyCoordinator::SteadyShare(FName Faction) const
{
	int32 All = 0, Steady = 0;
	for (const FSquad& Sq : Squads)
	{
		FVector Centre;
		if (Sq.Faction != Faction || !SquadCentre(Sq, Centre)) continue;
		++All;
		Steady += Sq.Morale.State == EPA250Morale::Steady ? 1 : 0;
	}
	return All > 0 ? float(Steady) / All : 1.f;
}

int32 UPA250VolleyCoordinator::CountRouting(FName Faction) const
{
	int32 N = 0;
	for (const FSquad& Sq : Squads)
	{
		FVector Centre;
		N += (Sq.Faction == Faction && Sq.Morale.State == EPA250Morale::Routing && SquadCentre(Sq, Centre)) ? 1 : 0;
	}
	return N;
}

bool UPA250VolleyCoordinator::IsRouting(const ARFCharacter* Soldier) const
{
	return Soldier && Soldier->SquadId >= 0 && GetSquadMorale(Soldier->SquadId) == EPA250Morale::Routing;
}

void UPA250VolleyCoordinator::CountSquads(FName Faction, int32& OutSquads, int32& OutBroken) const
{
	OutSquads = 0;
	OutBroken = 0;
	for (const FSquad& Sq : Squads)
	{
		if (Sq.Faction != Faction) continue;
		bool bAnyAlive = false;
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			if (W.IsValid() && !W->IsDead())
			{
				bAnyAlive = true;
				break;
			}
		}
		if (!bAnyAlive) continue;
		++OutSquads;
		if (PA250SquadIsBroken(Sq.Morale.State, Sq.bDrillWithdrawal)) ++OutBroken;
	}
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

// ---- A led file -----------------------------------------------------------

const TCHAR* PA250Squad::OrderWords(EPA250SquadOrder Order)
{
	switch (Order)
	{
	case EPA250SquadOrder::FollowMe: return TEXT("Follow me");
	case EPA250SquadOrder::FormLine: return TEXT("Form line here");
	case EPA250SquadOrder::Hold: return TEXT("Hold this ground");
	case EPA250SquadOrder::TakeCover: return TEXT("Take cover");
	case EPA250SquadOrder::Charge: return TEXT("Charge bayonets");
	case EPA250SquadOrder::FallBack: return TEXT("Fall back and fill your boxes");
	default: return TEXT("At your own judgement");
	}
}

const TCHAR* PA250Squad::OrderVoiceKey(EPA250SquadOrder Order)
{
	switch (Order)
	{
	case EPA250SquadOrder::FollowMe: return TEXT("follow_me");
	case EPA250SquadOrder::Hold: return TEXT("steady");
	case EPA250SquadOrder::Charge: return TEXT("charge_bayonets");
	case EPA250SquadOrder::FallBack: return TEXT("fall_back");
	default: return nullptr;
	}
}

const TCHAR* PA250Squad::OrderDrumCall(EPA250SquadOrder Order)
{
	switch (Order)
	{
	case EPA250SquadOrder::FollowMe: return TEXT("drum_call_follow");
	case EPA250SquadOrder::FormLine: return TEXT("drum_call_line");
	case EPA250SquadOrder::Hold: return TEXT("drum_call_hold");
	case EPA250SquadOrder::TakeCover: return TEXT("drum_call_cover");
	case EPA250SquadOrder::Charge: return TEXT("drum_call_charge");
	case EPA250SquadOrder::FallBack: return TEXT("drum_call_retire");
	default: return TEXT("drum_roll");
	}
}

FVector PA250Squad::LedAnchor(EPA250SquadOrder Order, const FVector& LeaderLocation, const FVector& LeaderFacing,
	const FVector& OrderPoint, const FVector& CurrentAnchor, float FollowBehindCm)
{
	switch (Order)
	{
	case EPA250SquadOrder::FollowMe: return LeaderLocation - LeaderFacing.GetSafeNormal2D() * FMath::Max(0.f, FollowBehindCm);
	case EPA250SquadOrder::FormLine:
	case EPA250SquadOrder::Hold:
	case EPA250SquadOrder::TakeCover:
	case EPA250SquadOrder::FallBack: return OrderPoint;
	default: return CurrentAnchor;
	}
}

EVolleyCall PA250Squad::LedVolleyNext(EVolleyCall Stage, bool bLeaderAiming, bool bLeaderFired, bool bShotsPending,
	bool bAnyLoaded, float StageTimer, bool& bOutFire)
{
	bOutFire = false;
	if (Stage == EVolleyCall::Fire) return bShotsPending ? EVolleyCall::Fire : EVolleyCall::None;
	if (!bLeaderAiming) return EVolleyCall::None;
	switch (Stage)
	{
	case EVolleyCall::None: return bAnyLoaded ? EVolleyCall::MakeReady : EVolleyCall::None;
	case EVolleyCall::MakeReady: return StageTimer <= 0.f ? EVolleyCall::Present : EVolleyCall::MakeReady;
	case EVolleyCall::Present:
		if (bLeaderFired)
		{
			bOutFire = true;
			return EVolleyCall::Fire;
		}
		return EVolleyCall::Present;
	default: return EVolleyCall::None;
	}
}

TArray<int32> PA250Squad::PickFile(const TArray<FCandidate>& Candidates, int32 MinMen, int32 MaxMen)
{
	TArray<FCandidate> Own, Others;
	for (const FCandidate& C : Candidates) (C.bSameClass ? Own : Others).Add(C);
	auto Nearer = [](const FCandidate& A, const FCandidate& B) { return A.DistCm != B.DistCm ? A.DistCm < B.DistCm : A.Index < B.Index; };
	Own.Sort(Nearer);
	Others.Sort(Nearer);
	TArray<int32> Out;
	for (const FCandidate& C : Own)
	{
		if (Out.Num() >= FMath::Max(0, MaxMen)) break;
		Out.Add(C.Index);
	}
	for (const FCandidate& C : Others)
	{
		if (Out.Num() >= FMath::Min(MinMen, MaxMen)) break;
		Out.Add(C.Index);
	}
	return Out;
}

UPA250VolleyCoordinator::FSquad* UPA250VolleyCoordinator::FindLedSquad(const ARFCharacter* Leader)
{
	if (!Leader) return nullptr;
	for (FSquad& Sq : Squads)
	{
		if (Sq.Leader.Get() == Leader) return &Sq;
	}
	return nullptr;
}

const UPA250VolleyCoordinator::FSquad* UPA250VolleyCoordinator::FindLedSquad(const ARFCharacter* Leader) const
{
	return const_cast<UPA250VolleyCoordinator*>(this)->FindLedSquad(Leader);
}

bool UPA250VolleyCoordinator::IsLeader(const ARFCharacter* C) const
{
	return FindLedSquad(C) != nullptr;
}

ARFCharacter* UPA250VolleyCoordinator::EnemyNear(FName Faction, const FVector& Point, float RadiusCm) const
{
	UWorld* World = GameMode.IsValid() ? GameMode->GetWorld() : nullptr;
	if (!World) return nullptr;
	ARFCharacter* Best = nullptr;
	float BestD = RadiusCm;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		if (It->IsDead() || It->FactionId == Faction || It->FactionId.IsNone()) continue;
		const float D = FVector::Dist(It->GetActorLocation(), Point);
		if (D <= BestD)
		{
			BestD = D;
			Best = *It;
		}
	}
	return Best;
}

int32 UPA250VolleyCoordinator::TryFallIn(ARFCharacter* Man)
{
	if (!Man || Man->IsDead() || !GameMode.IsValid()) return INDEX_NONE;
	if (FallenIn.Get() == Man)
	{
		FallOut(TEXT("fell out"));
		return INDEX_NONE;
	}
	const float RadiusCm = pa250::battle_rules().drill_fall_in_radius_m * 100.f;
	// The nearest friendly line with a man within reach: its own drill (no living leader but him),
	// in a state to stand in ranks.
	FSquad* Best = nullptr;
	float BestD = RadiusCm;
	for (FSquad& Sq : Squads)
	{
		if (Sq.Faction != Man->FactionId || Sq.Morale.State == EPA250Morale::Routing) continue;
		if (Sq.Leader.IsValid() && !Sq.Leader->IsDead() && Sq.Leader.Get() != Man) continue; // another leader's file
		int32 Standing = 0;
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			const ARFCharacter* C = W.Get();
			if (!C || C->IsDead() || C == Man) continue;
			++Standing;
			const float D = FVector::Dist2D(C->GetActorLocation(), Man->GetActorLocation());
			if (D < BestD) { BestD = D; Best = &Sq; }
		}
		if (Standing < 2 && Best == &Sq) Best = nullptr; // a line, not a man or two
	}
	if (!Best) return INDEX_NONE;
	// He stands in its ranks, not before a file of his own: a file he led keeps to its own drill.
	for (FSquad& Sq : Squads)
	{
		if (Sq.Leader.Get() == Man)
		{
			Sq.Leader = nullptr;
			Sq.Order = EPA250SquadOrder::None;
			Sq.OrderedBy = nullptr;
		}
		if (&Sq != Best)
		{
			Sq.Members.Remove(Man);
			Sq.FileOrder.Remove(Man);
		}
	}
	Best->Members.AddUnique(Man);
	Best->FileOrder.Reset(); // sorted again with him in it: he takes the place nearest where he stands
	Man->SquadId = Best->Id;
	Man->Tags.AddUnique(TEXT("PA250FallenIn"));
	FallenIn = Man;
	FallInSquadId = Best->Id;
	bFallInShotHeld = false;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.FallIn"), *Man->FactionId.ToString()))++;
	UE_LOG(LogTemp, Display, TEXT("PA250 fall-in: %s joins squad %d (%d men) %.0f cm from its nearest man"), *Man->GetName(), Best->Id, Best->Members.Num(), BestD);
	return Best->Id;
}

void UPA250VolleyCoordinator::FallOut(const TCHAR* Why)
{
	ARFCharacter* Man = FallenIn.Get();
	if (FSquad* Sq = FindSquad(FallInSquadId))
	{
		if (Man)
		{
			Sq->Members.Remove(Man);
			Sq->FileOrder.Remove(Man);
		}
		Sq->PendingShots.RemoveAll([](const FPendingVolleyShot& P) { return P.bHeldTrigger; });
	}
	if (Man)
	{
		Man->Tags.Remove(TEXT("PA250FallenIn"));
		Man->SquadId = -1;
		UE_LOG(LogTemp, Display, TEXT("PA250 fall-in: %s leaves squad %d (%s)"), *Man->GetName(), FallInSquadId, Why);
	}
	FallenIn = nullptr;
	FallInSquadId = INDEX_NONE;
	bFallInShotHeld = false;
}

bool UPA250VolleyCoordinator::HoldFallenInShot(ARFCharacter* Man)
{
	if (!Man || Man != FallenIn.Get() || !Man->Musket || !Man->Musket->CanFire()) return false;
	const FSquad* Sq = FindSquad(FallInSquadId);
	if (!Sq || (Sq->Stage != EVolleyCall::MakeReady && Sq->Stage != EVolleyCall::Present)) return false;
	bFallInShotHeld = true;
	UE_LOG(LogTemp, Display, TEXT("PA250 fall-in: shot held for squad %d's volley"), Sq->Id);
	return true;
}

void UPA250VolleyCoordinator::AddHeldTrigger(FSquad& Sq, float Now)
{
	ARFCharacter* Man = FallenIn.Get();
	if (!bFallInShotHeld || Sq.Id != FallInSquadId || !Man || Man->IsDead()) return;
	bFallInShotHeld = false;
	FPendingVolleyShot P;
	P.Shooter = Man;
	P.Epoch = Sq.VolleyEpoch;
	P.bHeldTrigger = true;
	P.bClipStarted = true; // the player's own animation fires his musket
	P.FireAtTime = Now + VolleyShotSeconds(Sq, Sq.PendingShots.Num(), 0.f, pa250::battle_rules().blast_volley_stage_jitter_ms);
	Sq.PendingShots.Add(P);
}

float UPA250VolleyCoordinator::VolleyShotSeconds(const FSquad& Sq, int32 Index, float RollingFileSeconds, float StageJitterMs) const
{
	const pa250::BattleRules& R = pa250::battle_rules();
	const bool bRagged = Sq.OfficerReplaceLeft > 0.f;
	const float Response = FMath::Max(0.f, R.drill_response_fire_s) * (bRagged ? R.drill_ragged_response_scale : 1.f);
	if (bRagged)
	{
		// No officer to give the word as one: each man fires when he is ready, over a wider spread.
		FRandomStream Rng = PA250BattleRng::Derived(HashCombine(HashCombine(GetTypeHash(Sq.Id), GetTypeHash(Sq.VolleyEpoch)), GetTypeHash(Index)));
		return Response + Rng.FRand() * FMath::Max(0.f, R.drill_volley_spread_s * R.drill_ragged_spread_scale);
	}
	const float Offset = VolleyTotalOffset(Sq.Id, Sq.VolleyEpoch, Index, RollingFileSeconds, StageJitterMs);
	// A steady line's volley falls inside volley_spread_s; rolling fire by files is meant to run longer.
	return Response + (RollingFileSeconds > 0.f ? Offset : FMath::Min(Offset, FMath::Max(0.f, R.drill_volley_spread_s)));
}

void UPA250VolleyCoordinator::NotifyOfficerDown(const ARFCharacter* Officer)
{
	if (!Officer) return;
	FSquad* Sq = FindLedSquad(Officer);
	if (!Sq)
	{
		Sq = FindSquad(Officer->SquadId);
		if (Sq && Sq->Faction != Officer->FactionId) Sq = nullptr;
	}
	if (!Sq || Sq->OfficerReplaceLeft > 0.f) return;
	Sq->OfficerReplaceLeft = FMath::Max(KINDA_SMALL_NUMBER, pa250::battle_rules().drill_officer_replace_s);
	Sq->bFallenOfficerWasPlayer = Officer->IsPlayerControlled() || Officer == FallenIn.Get()
		|| (GameMode.IsValid() && GameMode->GetWorld() && GameMode->GetWorld()->GetFirstPlayerController()
			&& GameMode->GetWorld()->GetFirstPlayerController()->GetPawn() == Officer);
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.OfficerlessFire"), *Sq->Faction.ToString()))++;
	UE_LOG(LogTemp, Display, TEXT("PA250 officer loss: squad %d fires raggedly for %.1f s (%s fell)"), Sq->Id, Sq->OfficerReplaceLeft, *Officer->GetName());
}

void UPA250VolleyCoordinator::ResumeAfterOfficer(FSquad& Sq)
{
	Sq.OfficerReplaceLeft = 0.f;
	const ARFCharacter* Fallen = Sq.Leader.Get();
	const TCHAR* How = TEXT("a sergeant gives the word");
	if (Fallen && Fallen->IsDead())
	{
		ARFCharacter* Sgt = Sq.bFallenOfficerWasPlayer ? nullptr : PickSuccessor(Fallen);
		if (Sgt)
		{
			TransferLeadership(Fallen, Sgt);
			How = TEXT("a sergeant leads the file");
		}
		else
		{
			// The player's file (or no man to take it): it keeps to its own drill, its men free for
			// the man he plays next to lead (EnsureLeaderFile takes no man of a living leader's file).
			Sq.Leader = nullptr;
			Sq.Order = EPA250SquadOrder::None;
			Sq.OrderedBy = nullptr;
			How = TEXT("the file keeps to its own drill");
		}
	}
	else if (!Sq.Leader.IsValid())
	{
		Sq.Leader = nullptr;
	}
	Sq.bFallenOfficerWasPlayer = false;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.SergeantResumed"), *Sq.Faction.ToString()))++;
	UE_LOG(LogTemp, Display, TEXT("PA250 officer loss: squad %d steady again (%s)"), Sq.Id, How);
}

float UPA250VolleyCoordinator::OrderResponseSeconds(EPA250SquadOrder Order, bool bRagged)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	const float D = Order == EPA250SquadOrder::Hold ? R.drill_response_halt_s
		: Order == EPA250SquadOrder::FormLine ? R.drill_response_advance_s : 0.f;
	return FMath::Max(0.f, D) * (bRagged ? R.drill_ragged_response_scale : 1.f);
}

void UPA250VolleyCoordinator::SetOrderAfterResponse(FSquad& Sq, EPA250SquadOrder Order, ARFCharacter* Giver, const FVector& AimPoint, float Until)
{
	const float Delay = OrderResponseSeconds(Order, Sq.OfficerReplaceLeft > 0.f);
	PendingOrders.RemoveAll([&Sq](const FPendingOrder& O) { return O.SquadId == Sq.Id; }); // a newer word replaces one not yet obeyed
	if (Delay <= 0.f || !GameMode.IsValid() || !GameMode->GetWorld())
	{
		SetOrder(Sq, Order, Giver, AimPoint, Until);
		return;
	}
	FPendingOrder O;
	O.SquadId = Sq.Id;
	O.Order = Order;
	O.Giver = Giver;
	O.AimPoint = AimPoint;
	O.Until = Until;
	O.ApplyAt = GameMode->GetWorld()->GetTimeSeconds() + Delay;
	PendingOrders.Add(O);
}

float UPA250VolleyCoordinator::GetOfficerReplaceLeft(int32 SquadId) const
{
	const FSquad* Sq = const_cast<UPA250VolleyCoordinator*>(this)->FindSquad(SquadId);
	return Sq ? Sq->OfficerReplaceLeft : 0.f;
}

EVolleyCall UPA250VolleyCoordinator::GetSquadStage(int32 SquadId) const
{
	const FSquad* Sq = const_cast<UPA250VolleyCoordinator*>(this)->FindSquad(SquadId);
	return Sq ? Sq->Stage : EVolleyCall::None;
}

FString UPA250VolleyCoordinator::GetSquadReason(int32 SquadId) const
{
	const FSquad* Sq = const_cast<UPA250VolleyCoordinator*>(this)->FindSquad(SquadId);
	return Sq ? FString(Sq->DebugReason) : FString();
}

EPA250SquadOrder UPA250VolleyCoordinator::GetSquadOrder(int32 SquadId) const
{
	const FSquad* Sq = const_cast<UPA250VolleyCoordinator*>(this)->FindSquad(SquadId);
	return Sq ? Sq->Order : EPA250SquadOrder::None;
}

TArray<TPair<ARFCharacter*, FVector>> UPA250VolleyCoordinator::GetSquadSpotsForTest(int32 SquadId) const
{
	TArray<TPair<ARFCharacter*, FVector>> Out;
	if (const FSquad* Sq = const_cast<UPA250VolleyCoordinator*>(this)->FindSquad(SquadId))
	{
		for (int32 i = 0; i < Sq->FileOrder.Num() && i < Sq->LastSpots.Num(); ++i) Out.Emplace(Sq->FileOrder[i].Get(), Sq->LastSpots[i]);
	}
	return Out;
}

TArray<float> UPA250VolleyCoordinator::GetPendingFireTimesForTest(int32 SquadId) const
{
	TArray<float> Out;
	if (const FSquad* Sq = const_cast<UPA250VolleyCoordinator*>(this)->FindSquad(SquadId))
	{
		for (const FPendingVolleyShot& P : Sq->PendingShots) Out.Add(P.FireAtTime);
	}
	return Out;
}

int32 UPA250VolleyCoordinator::EnsureLeaderFile(ARFCharacter* Leader)
{
	UWorld* World = GameMode.IsValid() ? GameMode->GetWorld() : nullptr;
	if (!World || !Leader || Leader->IsDead() || Leader->FactionId.IsNone()) return 0;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	if (FSquad* Mine = FindLedSquad(Leader))
	{
		int32 Live = 0;
		for (const TWeakObjectPtr<ARFCharacter>& W : Mine->Members) Live += (W.IsValid() && !W->IsDead()) ? 1 : 0;
		if (Mine->UnitClass == Leader->UnitClass && Live > 0) return Live;
		// He changed class, or the file is gone: what is left of it is an ordinary squad again.
		Mine->Leader = nullptr;
		Mine->Order = EPA250SquadOrder::None;
	}
	// A leader stands before his file, not in the ranks of the squad he came from (a scripted leader is a bot).
	for (FSquad& Sq : Squads)
	{
		Sq.Members.Remove(Leader);
		Sq.FileOrder.Remove(Leader);
	}
	TArray<ARFCharacter*> Men;
	TArray<PA250Squad::FCandidate> Candidates;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		ARFCharacter* C = *It;
		if (C == Leader || C->FactionId != Leader->FactionId || !ShouldJoinSquad(C) || IsLeader(C)) continue;
		if (const FSquad* Old = FindSquad(C->SquadId); Old && Old->Leader.IsValid() && !Old->Leader->IsDead()) continue; // another leader's man
		PA250Squad::FCandidate Cand;
		Cand.Index = Men.Num();
		Cand.bSameClass = C->UnitClass == Leader->UnitClass;
		Cand.DistCm = FVector::Dist2D(C->GetActorLocation(), Leader->GetActorLocation());
		Men.Add(C);
		Candidates.Add(Cand);
	}
	const TArray<int32> Picked = PA250Squad::PickFile(Candidates, Verbs.SquadFileMin, Verbs.SquadFileMax);
	if (Picked.Num() == 0) return 0;

	FSquad NewSq;
	NewSq.Id = NextSquadId++;
	NewSq.Faction = Leader->FactionId;
	NewSq.UnitClass = Leader->UnitClass;
	NewSq.Leader = Leader;
	NewSq.Order = EPA250SquadOrder::FollowMe;
	NewSq.OrderFacing = Leader->GetActorForwardVector().GetSafeNormal2D();
	{
		const FPA250UnitClassKit& K = PA250UnitClasses::KitFor(Leader->FactionId, Leader->UnitClass);
		NewSq.FireMaxCm = FMath::Max(500.f, K.PreferMaxM * 100.f);
		NewSq.PreferMinCm = FMath::Max(0.f, K.PreferMinM * 100.f);
	}
	bool bProfiled = false;
	for (const int32 Index : Picked)
	{
		ARFCharacter* C = Men[Index];
		for (FSquad& Sq : Squads)
		{
			Sq.Members.Remove(C);
			Sq.FileOrder.Remove(C);
		}
		// The file drills as its own class drills (the first man of the leader's class sets it).
		const APA250BotController* Bot = Cast<APA250BotController>(C->GetController());
		if (Bot && !bProfiled && (C->UnitClass == Leader->UnitClass || Index == Picked.Last()))
		{
			NewSq.Training = Bot->GetTraining();
			NewSq.Role = Bot->GetRole();
			bProfiled = true;
		}
		NewSq.Members.Add(C);
	}
	NewSq.Peak = NewSq.Members.Num();
	const int32 Id = NewSq.Id;
	Squads.Add(NewSq);
	for (const int32 Index : Picked) Men[Index]->SquadId = Id;
	Leader->SquadId = Id;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.FileFormed"), *Leader->FactionId.ToString()))++;
	UE_LOG(LogTemp, Display, TEXT("PA250 file: %s leads %d men (%s)"), *Leader->GetName(), Picked.Num(), *Leader->UnitClass.ToString());
	return Picked.Num();
}

void UPA250VolleyCoordinator::SetOrder(FSquad& Sq, EPA250SquadOrder Order, const ARFCharacter* Giver, const FVector& AimPoint, float Until)
{
	if (!Giver || !GameMode.IsValid() || !GameMode->GetWorld()) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	const bool bOwnFile = Sq.Leader.Get() == Giver;
	const FVector GiverLoc = Giver->GetActorLocation();
	FVector Facing = (AimPoint - GiverLoc).GetSafeNormal2D();
	if (Facing.IsNearlyZero()) Facing = Giver->GetActorForwardVector().GetSafeNormal2D();
	Sq.Order = Order;
	Sq.OrderUntil = Until;
	Sq.OrderedBy = bOwnFile ? nullptr : const_cast<ARFCharacter*>(Giver);
	Sq.OrderFacing = Facing;
	if (!Sq.bAnchored)
	{
		// Not yet drawn up: the order is where it starts from.
		Sq.bAnchored = true;
		Sq.Anchor = GiverLoc;
		Sq.Facing = Facing;
	}
	switch (Order)
	{
	case EPA250SquadOrder::FollowMe:
		Sq.OrderPoint = bOwnFile ? FVector::ZeroVector : (Sq.Anchor - GiverLoc) * FVector(1.f, 1.f, 0.f);
		break;
	case EPA250SquadOrder::FormLine:
		Sq.OrderPoint = bOwnFile ? GiverLoc + Facing * Verbs.SquadLineAheadM * 100.f : Sq.Anchor;
		break;
	case EPA250SquadOrder::Hold:
		Sq.OrderPoint = Sq.Anchor;
		break;
	case EPA250SquadOrder::TakeCover:
		Sq.OrderPoint = bOwnFile ? GiverLoc : Sq.Anchor;
		Sq.CoverQueryTimer = 0.f;
		break;
	case EPA250SquadOrder::Charge:
	{
		ARFCharacter* Foe = EnemyNear(Sq.Faction, AimPoint, Verbs.SquadAimSnapM * 100.f);
		if (!Foe) Foe = Sq.Contact.Get();
		if (Foe && !Foe->IsDead() && FPA250MoraleOrders::For(Sq.Morale.State).bMayCharge)
		{
			Sq.Contact = Foe;
			Sq.bEngaged = true;
			Sq.LastContactTime = Now;
			if (!Sq.bCharging) StartCharge(Sq, Now);
		}
		else
		{
			// Nobody to charge, or men in no state to: the order cannot be obeyed.
			Sq.Order = bOwnFile ? EPA250SquadOrder::FollowMe : EPA250SquadOrder::None;
		}
		return;
	}
	default:
		break;
	}
	// Any other order calls off a charge and a volley in hand.
	if (Sq.bCharging)
	{
		Sq.bCharging = false;
		Sq.LastChargeEndTime = Now;
	}
	if (Sq.Stage != EVolleyCall::None || Sq.PendingShots.Num() > 0) CancelVolleyOrders(Sq, TEXT("cancelled_order"));
}

int32 UPA250VolleyCoordinator::GiveOrder(ARFCharacter* Leader, EPA250SquadOrder Order, const FVector& AimPoint)
{
	if (!Leader || Leader->IsDead() || Order == EPA250SquadOrder::None || !GameMode.IsValid() || !GameMode->GetWorld()) return 0;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	int32 Took = 0;
	if (FSquad* Mine = FindLedSquad(Leader))
	{
		SetOrderAfterResponse(*Mine, Order, Leader, AimPoint, -1.f);
		++Took;
	}
	// An officer's word carries to every friendly line within his order radius. Anyone's order carries
	// on through a musician who hears it (the musician's verb): to the lines within his relay distance.
	{
		const bool bOfficer = PA250UnitClasses::KitFor(Leader->FactionId, Leader->UnitClass).bOfficer;
		TArray<FVector> Musicians;
		for (TActorIterator<ARFCharacter> It(GameMode->GetWorld()); It; ++It)
		{
			if (!It->IsDead() && *It != Leader && It->FactionId == Leader->FactionId && PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bRelaysOrders)
			{
				Musicians.Add(It->GetActorLocation());
			}
		}
		int32 Relayed = 0;
		for (FSquad& Sq : Squads)
		{
			if (Sq.Faction != Leader->FactionId || Sq.Leader.Get() == Leader || !Sq.bAnchored) continue;
			if (Sq.Leader.IsValid() && !Sq.Leader->IsDead()) continue; // another leader's file
			bool bRelayed = false;
			if (Sq.Morale.State == EPA250Morale::Routing
				|| !PA250Support::OrderReaches(Leader->GetActorLocation(), Sq.Anchor, bOfficer, Verbs.OrderRadiusM * 100.f, Musicians, Verbs.MusicianRelayM * 100.f, &bRelayed))
			{
				continue;
			}
			SetOrderAfterResponse(Sq, Order, Leader, AimPoint, Now + Verbs.SquadOrderSeconds);
			++Took;
			Relayed += bRelayed ? 1 : 0;
		}
		if (Relayed > 0)
		{
			MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.OrderRelayed"), *Leader->FactionId.ToString())) += Relayed;
		}
		// A musician who hears the order beats it: each order has its own call, so men (and the
		// player) know it by ear. From where the drummer stands, and it carries as gunfire does.
		const FVector* Drummer = nullptr;
		float Nearest = Verbs.MusicianRelayM * 100.f;
		for (const FVector& M : Musicians)
		{
			const float D = FVector::Dist2D(M, Leader->GetActorLocation());
			if (D <= Nearest)
			{
				Nearest = D;
				Drummer = &M;
			}
		}
		if (Drummer)
		{
			MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.DrumCall"), *Leader->FactionId.ToString()))++;
			// The word's own call from battle_look.json audio.orders (Form line = advance, Hold = halt),
			// else the call each order always had.
			FString Drum, Fife;
			const TCHAR* Word = Order == EPA250SquadOrder::FormLine ? TEXT("advance") : Order == EPA250SquadOrder::Hold ? TEXT("halt") : nullptr;
			if (Word && PA250BattleFX::OrderCue(Word, Drum, Fife))
			{
				FPA250BattleAudio::PlayAtOr(GameMode.Get(), Drum, PA250Squad::OrderDrumCall(Order), *Drummer + FVector(0.f, 0.f, 100.f), EPA250SoundRange::Gunfire, 0.9f, 0.02f);
				if (!Fife.IsEmpty()) FPA250BattleAudio::PlayAt(GameMode.Get(), Fife, *Drummer + FVector(0.f, 0.f, 120.f), EPA250SoundRange::Gunfire, 0.7f, 0.02f);
			}
			else
			{
				FPA250BattleAudio::PlayAtOr(GameMode.Get(), PA250Squad::OrderDrumCall(Order), TEXT("drum_roll"), *Drummer + FVector(0.f, 0.f, 100.f), EPA250SoundRange::Gunfire, 0.9f, 0.02f);
			}
		}
	}
	if (Took > 0)
	{
		MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.Order"), *Leader->FactionId.ToString()))++;
		const TCHAR* Key = PA250Squad::OrderVoiceKey(Order);
		const FString Words = FString(PA250Squad::OrderWords(Order)) + TEXT("!");
		Shout(Leader->FactionId, Key ? Key : TEXT("order"), *Words, Leader->GetActorLocation(), 2.5f);
	}
	return Took;
}

void UPA250VolleyCoordinator::SetColours(FName Faction, bool bFlying, const FVector& Where)
{
	if (bFlying) ColoursFlying.Add(Faction, Where);
	else ColoursFlying.Remove(Faction);
}

void UPA250VolleyCoordinator::PressSquad(int32 SquadId, float Pressure, float Seconds, FName ByFaction, const TCHAR* CountAs)
{
	FSquad* Sq = FindSquad(SquadId);
	if (!Sq || !GameMode.IsValid() || !GameMode->GetWorld() || Pressure <= 0.f || Seconds <= 0.f) return;
	const float Now = GameMode->GetWorld()->GetTimeSeconds();
	// Never less than a shock already on the squad.
	const float Standing = Now < Sq->ShockUntil ? Sq->ShockPressure * (Sq->ShockUntil - Now) / FMath::Max(0.1f, Sq->ShockSpan) : 0.f;
	if (Pressure < Standing) return;
	Sq->ShockUntil = Now + Seconds;
	Sq->ShockPressure = Pressure;
	Sq->ShockSpan = Seconds;
	if (CountAs && !ByFaction.IsNone()) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.%s"), *ByFaction.ToString(), CountAs))++;
}

void UPA250VolleyCoordinator::PressSide(FName Faction, float Pressure, float Seconds, const TCHAR* CountAs)
{
	TArray<int32> Ids;
	for (const FSquad& Sq : Squads)
	{
		if (Sq.Faction == Faction) Ids.Add(Sq.Id);
	}
	for (const int32 Id : Ids) PressSquad(Id, Pressure, Seconds, NAME_None, nullptr);
	if (CountAs) MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.%s"), *Faction.ToString(), CountAs))++;
}

void UPA250VolleyCoordinator::UpdateLeader(ARFCharacter* Leader, bool bAiming, const FVector& AimPoint)
{
	if (FSquad* Sq = FindLedSquad(Leader))
	{
		Sq->bLeaderAiming = bAiming;
		Sq->LeaderAimPoint = AimPoint;
	}
}

bool UPA250VolleyCoordinator::LeaderFired(ARFCharacter* Leader)
{
	FSquad* Sq = FindLedSquad(Leader);
	if (!Sq || Sq->Role.bIndividualFire || Sq->Stage != EVolleyCall::Present) return false;
	Sq->bLeaderFired = true;
	return true;
}

bool UPA250VolleyCoordinator::GetFileStatus(const ARFCharacter* Leader, int32& OutMen, int32& OutLoaded, int32& OutPresented, EPA250SquadOrder& OutOrder, int32* OutWithLeader) const
{
	OutMen = OutLoaded = OutPresented = 0;
	OutOrder = EPA250SquadOrder::None;
	if (OutWithLeader) *OutWithLeader = 0;
	const FSquad* Sq = FindLedSquad(Leader);
	if (!Sq) return false;
	const float WithCm = PA250UnitClasses::Verbs().SquadWithYouM * 100.f;
	for (const TWeakObjectPtr<ARFCharacter>& W : Sq->Members)
	{
		const ARFCharacter* C = W.Get();
		if (!C || C->IsDead()) continue;
		++OutMen;
		if (OutWithLeader && Leader && FVector::Dist2D(C->GetActorLocation(), Leader->GetActorLocation()) <= WithCm) ++*OutWithLeader;
		OutLoaded += (C->Musket && C->Musket->CanFire()) ? 1 : 0;
	}
	OutPresented = Sq->DebugPresented;
	OutOrder = Sq->Order;
	return true;
}

TArray<ARFCharacter*> UPA250VolleyCoordinator::GetFileMembers(const ARFCharacter* Leader) const
{
	TArray<ARFCharacter*> Out;
	if (const FSquad* Sq = FindLedSquad(Leader))
	{
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq->Members)
		{
			if (ARFCharacter* C = W.Get(); C && !C->IsDead()) Out.Add(C);
		}
	}
	return Out;
}

ARFCharacter* UPA250VolleyCoordinator::PickSuccessor(const ARFCharacter* FallenLeader) const
{
	const FSquad* Sq = FindLedSquad(FallenLeader);
	if (!Sq) return nullptr;
	ARFCharacter* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<ARFCharacter>& W : Sq->Members)
	{
		ARFCharacter* C = W.Get();
		if (!C || C->IsDead() || C->IsMounted() || !Cast<APA250BotController>(C->GetController()) || C->ActorHasTag(TEXT("PA250General"))) continue;
		const float D = FVector::DistSquared(C->GetActorLocation(), FallenLeader->GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = C;
		}
	}
	return Best;
}

void UPA250VolleyCoordinator::TransferLeadership(const ARFCharacter* OldLeader, ARFCharacter* Successor)
{
	FSquad* Sq = FindLedSquad(OldLeader);
	if (!Sq || !Successor) return;
	Sq->Members.Remove(Successor);
	Sq->FileOrder.Remove(Successor);
	Sq->Leader = Successor;
	Sq->bLeaderAiming = false;
	Sq->bLeaderFired = false;
	if (Sq->Order == EPA250SquadOrder::None) Sq->Order = EPA250SquadOrder::FollowMe;
	Successor->SquadId = Sq->Id;
}

void UPA250VolleyCoordinator::ScheduleLedVolley(FSquad& Sq, const TArray<ARFCharacter*>& Presented)
{
	++Sq.VolleyEpoch;
	Sq.PendingShots.Reset();
	UWorld* World = GameMode.IsValid() ? GameMode->GetWorld() : nullptr;
	if (!World) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float StageJitterMs = pa250::battle_rules().blast_volley_stage_jitter_ms;
	const float Now = World->GetTimeSeconds();
	ARFCharacter* Target = EnemyNear(Sq.Faction, Sq.LeaderAimPoint, Verbs.SquadAimSnapM * 100.f);
	const FVector Right(-Sq.Facing.Y, Sq.Facing.X, 0.f);
	TArray<ARFCharacter*> Shooters = Presented;
	Shooters.RemoveAll([](const ARFCharacter* C) { return C == nullptr; });
	Shooters.StableSort([&](const ARFCharacter& A, const ARFCharacter& B) {
		return FVector::DotProduct(A.GetActorLocation(), Right) > FVector::DotProduct(B.GetActorLocation(), Right);
	});
	for (ARFCharacter* C : Shooters)
	{
		if (C->IsDead() || !C->Musket || !C->Musket->CanFire()) continue;
		if (C == FallenIn.Get()) continue; // his trigger is his own (AddHeldTrigger)
		FPendingVolleyShot P;
		P.Shooter = C;
		P.Epoch = Sq.VolleyEpoch;
		const FVector AtMan = Target ? Target->GetActorLocation() + FVector(0.f, 0.f, 20.f) : FVector::ZeroVector;
		if (Target && HasLineOfSight(C, Target) && HasClearFiringLane(World, C, Target, AtMan))
		{
			P.Target = Target;
			P.AimPoint = AtMan;
		}
		else
		{
			// Nobody by the point (or this man cannot see him): at the point itself, unless a
			// friend stands in the way. The point lies on the ground or a wall, so stop short of it.
			const FVector Muzzle = C->Musket->GetMuzzleLocation() ? C->Musket->GetMuzzleLocation()->GetComponentLocation() : C->GetActorLocation();
			const FVector Short = Sq.LeaderAimPoint - (Sq.LeaderAimPoint - Muzzle).GetSafeNormal() * 60.f;
			if (!HasClearFiringLane(World, C, nullptr, Short)) continue;
			P.bAtPoint = true;
			P.AimPoint = Sq.LeaderAimPoint;
		}
		P.FireAtTime = Now + VolleyShotSeconds(Sq, Sq.PendingShots.Num(), 0.f, StageJitterMs);
		Sq.PendingShots.Add(P);
	}
	AddHeldTrigger(Sq, Now);
	if (Sq.PendingShots.Num() == 0) return;
	++Sq.VolleysThisFight;
	MoraleTransitions.FindOrAdd(FString::Printf(TEXT("%s.LedVolley"), *Sq.Faction.ToString()))++;
	UE_LOG(LogTemp, Display, TEXT("PA250 led volley: %d muskets fire with their leader (%s)"), Sq.PendingShots.Num(), Target ? TEXT("on his man") : TEXT("on his point"));
	// The same morale check a line's own volley forces: enough muskets shake the squad fired on.
	if (Target && Sq.PendingShots.Num() >= Verbs.VolleyMinShooters && PA250UnitClasses::KitFor(Sq.Faction, Sq.UnitClass).bVolleyShock)
	{
		ApplyVolleyShock(Target->SquadId, Sq.Faction);
	}
}

bool UPA250VolleyCoordinator::ShouldJoinSquad(const ARFCharacter* C)
{
	// Dragoons ride and fight on their own (APA250BotController::TickDragoon), never in a volley squad.
	// Nor is the general one of the ranks: he keeps his own post (APA250BotController::TickGeneral), and a
	// man of a file is a man the player can step into (PickSuccessor) or lead (EnsureLeaderFile).
	return C && !C->IsDead() && !C->FactionId.IsNone() && !C->IsPlayerControlled() && !Cast<APlayerController>(C->GetController())
		&& C->UnitClass != TEXT("dragoon") && !C->ActorHasTag(TEXT("PA250General"));
}

void UPA250VolleyCoordinator::Tick(float DeltaSeconds)
{
	ShotClock += DeltaSeconds;
	CalloutTimer = FMath::Max(0.f, CalloutTimer - DeltaSeconds);
	RefreshFormations();
	TickSquads(DeltaSeconds);

	// The drill: words of command take effect after the men's response; an officerless squad counts
	// down to its sergeant; the fallen-in man keeps his place or falls out.
	if (GameMode.IsValid() && GameMode->GetWorld())
	{
		const float Now = GameMode->GetWorld()->GetTimeSeconds();
		for (int32 i = PendingOrders.Num() - 1; i >= 0; --i)
		{
			if (PendingOrders[i].ApplyAt > Now) continue;
			const FPendingOrder O = PendingOrders[i];
			PendingOrders.RemoveAt(i);
			FSquad* Sq = FindSquad(O.SquadId);
			if (Sq && O.Giver.IsValid()) SetOrder(*Sq, O.Order, O.Giver.Get(), O.AimPoint, O.Until);
		}
	}
	for (FSquad& Sq : Squads)
	{
		// A led file whose leader fell without word reaching us (destroyed, or a path that skipped
		// the game mode's death handling) is caught here; NotifyOfficerDown ignores a second call.
		if (Sq.Leader.IsValid() && Sq.Leader->IsDead() && Sq.OfficerReplaceLeft <= 0.f) NotifyOfficerDown(Sq.Leader.Get());
		if (Sq.OfficerReplaceLeft <= 0.f) continue;
		Sq.OfficerReplaceLeft = FMath::Max(0.f, Sq.OfficerReplaceLeft - DeltaSeconds);
		if (Sq.OfficerReplaceLeft > 0.f) continue;
		// The full officer_replace_s has run. A volley in hand is finished raggedly first.
		if (Sq.Stage == EVolleyCall::Fire || Sq.PendingShots.Num() > 0)
		{
			Sq.OfficerReplaceLeft = KINDA_SMALL_NUMBER;
			continue;
		}
		ResumeAfterOfficer(Sq);
	}
	if (FallenIn.IsValid() || FallInSquadId != INDEX_NONE)
	{
		ARFCharacter* Man = FallenIn.Get();
		const FSquad* Sq = FindSquad(FallInSquadId);
		const int32 Place = (Sq && Man) ? Sq->FileOrder.IndexOfByKey(Man) : INDEX_NONE;
		const float BreakCm = pa250::battle_rules().drill_fall_in_break_m * 100.f;
		if (!Man || Man->IsDead()) FallOut(TEXT("fell"));
		else if (!Sq) FallOut(TEXT("the line is gone"));
		else if (Place != INDEX_NONE && Sq->LastSpots.IsValidIndex(Place) && FVector::Dist2D(Man->GetActorLocation(), Sq->LastSpots[Place]) > BreakCm)
		{
			FallOut(TEXT("strayed from his place"));
		}
	}

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
