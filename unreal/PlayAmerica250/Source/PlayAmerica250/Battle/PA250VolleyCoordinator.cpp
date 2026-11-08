#include "PA250VolleyCoordinator.h"
#include "EngineUtils.h"
#include "MusketTuning.h"
#include "MusketWeapon.h"
#include "PA250BattleGameMode.h"
#include "PA250ControlPoint.h"
#include "RFCharacter.h"

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

void UPA250VolleyCoordinator::FireVolley(const TArray<ARFCharacter*>& Squad)
{
	for (ARFCharacter* C : Squad)
	{
		if (!C || C->IsDead() || !C->Musket || !C->Musket->CanFire()) continue;
		float Dist = 0.f;
		ARFCharacter* Enemy = nullptr;
		float Best = 6000.f;
		for (TActorIterator<ARFCharacter> It(C->GetWorld()); It; ++It)
		{
			if (It->IsDead() || It->FactionId == C->FactionId) continue;
			const float D = FVector::Dist(It->GetActorLocation(), C->GetActorLocation());
			if (D < Best) { Best = D; Enemy = *It; }
		}
		if (!Enemy) continue;
		Dist = Best;
		++Engagements;
		if (C->bInLine) ++LineEngagements;
		const bool bHit = C->FireMusketAt(Enemy->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		NotifyShot(C, (Enemy->GetActorLocation() - C->GetActorLocation()).GetSafeNormal(), bHit);
	}
}

void UPA250VolleyCoordinator::TickSquads(float DeltaSeconds)
{
	if (!GameMode.IsValid() || !GameMode->GetTuning()) return;
	const FVolleyTuning& Tun = GameMode->GetTuning()->Values.Volley;

	// Rebuild squads periodically from clustered bots.
	static float Rebuild = 0.f;
	Rebuild -= DeltaSeconds;
	if (Rebuild <= 0.f || Squads.Num() == 0)
	{
		Rebuild = 2.f;
		Squads.Reset();
		TArray<ARFCharacter*> Bots;
		for (TActorIterator<ARFCharacter> It(GameMode->GetWorld()); It; ++It)
		{
			if (!It->IsDead() && !It->IsPlayerControlled()) Bots.Add(*It);
		}
		TSet<ARFCharacter*> Used;
		for (ARFCharacter* Seed : Bots)
		{
			if (Used.Contains(Seed)) continue;
			FSquad Sq;
			Sq.Faction = Seed->FactionId;
			for (ARFCharacter* O : Bots)
			{
				if (Used.Contains(O) || O->FactionId != Seed->FactionId) continue;
				if (FVector::Dist2D(Seed->GetActorLocation(), O->GetActorLocation()) > 1600.f) continue;
				Sq.Members.Add(O);
				Used.Add(O);
				if (Sq.Members.Num() >= Tun.SquadMaxSize) break;
			}
			if (Sq.Members.Num() >= Tun.SquadMinSize)
			{
				Sq.Stage = EVolleyCall::MakeReady;
				Sq.StageTimer = Tun.MakeReadySeconds;
				Squads.Add(Sq);
			}
		}
	}

	for (FSquad& Sq : Squads)
	{
		Sq.StageTimer -= DeltaSeconds;
		TArray<ARFCharacter*> Live;
		for (const TWeakObjectPtr<ARFCharacter>& W : Sq.Members)
		{
			if (W.IsValid() && !W->IsDead()) Live.Add(W.Get());
		}
		if (Live.Num() < Tun.SquadMinSize)
		{
			Sq.Stage = EVolleyCall::None;
			continue;
		}
		if (Sq.Stage == EVolleyCall::MakeReady)
		{
			Callout = TEXT("Make Ready");
			CalloutTimer = 0.8f;
			if (Sq.StageTimer <= 0.f)
			{
				Sq.Stage = EVolleyCall::Present;
				Sq.StageTimer = Tun.PresentSeconds;
			}
		}
		else if (Sq.Stage == EVolleyCall::Present)
		{
			Callout = TEXT("Present");
			CalloutTimer = 0.8f;
			if (Sq.StageTimer <= 0.f)
			{
				Sq.Stage = EVolleyCall::Fire;
				Sq.StageTimer = 0.2f;
			}
		}
		else if (Sq.Stage == EVolleyCall::Fire)
		{
			Callout = TEXT("Fire");
			CalloutTimer = 0.8f;
			FireVolley(Live);
			Sq.Stage = EVolleyCall::MakeReady;
			Sq.StageTimer = Tun.MakeReadySeconds + 4.f + FMath::FRandRange(0.f, 3.f);
		}
	}
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
