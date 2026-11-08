#include "PA250CombatCommandlet.h"
#include "ActiveReloadSequencer.h"
#include "FormationQuery.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
int32 GFails = 0;
void Check(bool Cond, const TCHAR* Msg)
{
	const TCHAR* Status = Cond ? TEXT("PASS") : TEXT("FAIL");
	UE_LOG(LogTemp, Display, TEXT("PA250.Combat %s: %s"), Status, Msg);
	if (!Cond) ++GFails;
}
}

int32 UPA250CombatCommandlet::Main(const FString& Params)
{
	GFails = 0;

	{
		FActiveReloadSequencer S;
		FActiveReloadTuningPlain T;
		T.JamChanceOnMiss = 0.f;
		S.Configure(T);
		S.BeginReload();
		auto HitAt = [&](float Target) {
			while (S.GetPhaseProgress() < Target && S.IsReloading() && !S.IsJammed()) S.Tick(0.01f);
			return S.TryCommit();
		};
		Check(HitAt(0.5f) == EActiveReloadHit::Perfect, TEXT("perfect phase1"));
		Check(HitAt(0.5f) == EActiveReloadHit::Perfect, TEXT("perfect phase2"));
		Check(HitAt(0.5f) == EActiveReloadHit::Perfect, TEXT("perfect phase3"));
		Check(S.IsComplete(), TEXT("reload complete"));
		Check(S.PeekDamageBonus() > 0.f, TEXT("damage bonus pending"));
		Check(S.ConsumeDamageBonus() > 0.f, TEXT("damage bonus consumed"));
		Check(S.ConsumeDamageBonus() == 0.f, TEXT("damage bonus once"));
	}

	{
		FActiveReloadSequencer S;
		FActiveReloadTuningPlain T;
		T.JamChanceOnMiss = 1.f;
		S.Configure(T);
		S.BeginReload();
		S.Tick(0.001f);
		Check(S.TryCommit() == EActiveReloadHit::Miss, TEXT("miss"));
		Check(S.IsJammed(), TEXT("jammed"));
		float Acc = 0.f;
		while (S.IsJammed() && Acc < 5.f) { S.TryClearJam(true, 0.1f); Acc += 0.1f; }
		Check(!S.IsJammed(), TEXT("jam cleared"));
		Check(S.IsReloading(), TEXT("reload resumed"));
	}

	{
		FFormationTuningPlain T;
		T.LineRadius = 500.f;
		T.LineFacingDegrees = 30.f;
		T.LineMinSize = 3;
		T.LineAccuracyBonus = 0.2f;
		TArray<FFormationMember> Members;
		for (int32 i = 0; i < 3; ++i)
		{
			FFormationMember M;
			M.Location = FVector(i * 100.f, 0.f, 0.f);
			M.Forward = FVector(1.f, 0.f, 0.f);
			M.Index = i;
			Members.Add(M);
		}
		Check(CountLinemates(Members, 0, T) == 3, TEXT("line size 3"));
		Check(LineSpreadScale(3, T) < 1.f, TEXT("line accuracy bonus"));
		Members[2].Location = FVector(0.f, 2000.f, 0.f);
		Check(CountLinemates(Members, 0, T) == 2, TEXT("distance reject"));
		Members[2].Location = FVector(200.f, 0.f, 0.f);
		Members[2].Forward = FVector(-1.f, 0.f, 0.f);
		Check(CountLinemates(Members, 0, T) == 2, TEXT("facing reject"));
	}

	{
		FVolleyTuningPlain T;
		T.VolleyWindowSeconds = 1.f;
		T.VolleyMinShooters = 3;
		TArray<FVolleyShot> Shots;
		for (int32 i = 0; i < 3; ++i)
		{
			FVolleyShot S;
			S.Time = 0.1f * i;
			S.Direction = FVector(1.f, 0.f, 0.f);
			S.bHit = (i == 0);
			Shots.Add(S);
		}
		FVolleyResult R = EvaluateVolley(Shots, 0.5f, T);
		Check(R.bValid && R.Shooters == 3 && R.Hits == 1, TEXT("volley window"));
		R = EvaluateVolley(Shots, 5.f, T);
		Check(!R.bValid, TEXT("volley expired"));
	}

	const TCHAR* Passed = GFails == 0 ? TEXT("true") : TEXT("false");
	const FString Out = FString::Printf(TEXT("{\"passed\":%s,\"fails\":%d}\n"), Passed, GFails);
	FFileHelper::SaveStringToFile(Out, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("combat_tests.json")));
	UE_LOG(LogTemp, Display, TEXT("PA250 combat commandlet done fails=%d"), GFails);
	return GFails == 0 ? 0 : 1;
}
