// Automation tests for active reload, formation lines, and volley windows.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "ActiveReloadSequencer.h"
#include "FormationQuery.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPA250ActiveReloadPerfect, "PA250.Combat.ActiveReload.Perfect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPA250ActiveReloadPerfect::RunTest(const FString& Parameters)
{
	FActiveReloadSequencer S;
	FActiveReloadTuningPlain T;
	T.JamChanceOnMiss = 0.f;
	S.Configure(T);
	S.BeginReload();

	auto HitAt = [&](float ProgressTarget) {
		while (S.GetPhaseProgress() < ProgressTarget && S.IsReloading() && !S.IsJammed())
		{
			S.Tick(0.01f);
		}
		return S.TryCommit();
	};

	TestEqual(TEXT("phase1 perfect"), (int32)HitAt(0.5f), (int32)EActiveReloadHit::Perfect);
	TestEqual(TEXT("phase2 perfect"), (int32)HitAt(0.5f), (int32)EActiveReloadHit::Perfect);
	TestEqual(TEXT("phase3 perfect"), (int32)HitAt(0.5f), (int32)EActiveReloadHit::Perfect);
	TestTrue(TEXT("complete"), S.IsComplete());
	TestTrue(TEXT("damage bonus pending"), S.PeekDamageBonus() > 0.f);
	const float B1 = S.ConsumeDamageBonus();
	TestTrue(TEXT("bonus consumed"), B1 > 0.f);
	TestEqual(TEXT("bonus only once"), S.ConsumeDamageBonus(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPA250ActiveReloadSweetMissJam, "PA250.Combat.ActiveReload.SweetMissJam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPA250ActiveReloadSweetMissJam::RunTest(const FString& Parameters)
{
	FActiveReloadSequencer S;
	FActiveReloadTuningPlain T;
	T.JamChanceOnMiss = 1.f;
	S.Configure(T);
	S.BeginReload();
	S.Tick(0.001f);
	TestEqual(TEXT("miss"), (int32)S.TryCommit(), (int32)EActiveReloadHit::Miss);
	TestTrue(TEXT("jammed"), S.IsJammed());
	float Acc = 0.f;
	while (S.IsJammed() && Acc < 5.f)
	{
		S.TryClearJam(true, 0.1f);
		Acc += 0.1f;
	}
	TestTrue(TEXT("cleared"), !S.IsJammed());
	TestTrue(TEXT("resumed"), S.IsReloading());

	S.Reset();
	T.JamChanceOnMiss = 0.f;
	S.Configure(T);
	S.BeginReload();
	while (S.GetPhaseProgress() < S.GetSweetStart() + 0.02f)
	{
		S.Tick(0.02f);
	}
	const EActiveReloadHit H = S.TryCommit();
	TestTrue(TEXT("sweet or perfect"), H == EActiveReloadHit::Sweet || H == EActiveReloadHit::Perfect);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPA250FormationLine, "PA250.Combat.Formation.Line",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPA250FormationLine::RunTest(const FString& Parameters)
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
	TestEqual(TEXT("line of 3"), CountLinemates(Members, 0, T), 3);
	TestTrue(TEXT("bonus applies"), LineSpreadScale(3, T) < 1.f);

	Members[2].Location = FVector(0.f, 2000.f, 0.f);
	TestEqual(TEXT("too far"), CountLinemates(Members, 0, T), 2);
	TestEqual(TEXT("no bonus under min"), LineSpreadScale(2, T), 1.f);

	Members[2].Location = FVector(200.f, 0.f, 0.f);
	Members[2].Forward = FVector(-1.f, 0.f, 0.f);
	TestEqual(TEXT("facing reject"), CountLinemates(Members, 0, T), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPA250VolleyWindow, "PA250.Combat.Volley.Window",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPA250VolleyWindow::RunTest(const FString& Parameters)
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
	TestTrue(TEXT("volley valid"), R.bValid);
	TestEqual(TEXT("shooters"), R.Shooters, 3);
	TestEqual(TEXT("hits"), R.Hits, 1);

	R = EvaluateVolley(Shots, 5.f, T);
	TestFalse(TEXT("window expired"), R.bValid);
	return true;
}

#endif
