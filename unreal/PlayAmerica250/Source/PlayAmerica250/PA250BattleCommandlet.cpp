#include "PA250BattleCommandlet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PA250BattleCases.h"
#include "PA250ControlPoint.h"

int32 UPA250BattleCommandlet::Main(const FString& Params)
{
	// Same assertions as the PA250.Battle.* automation tests (PA250BattleCases.cpp).
	PA250BattleCases::FLog Log;
	Log.Check(GetDefault<APA250ControlPoint>() && GetDefault<APA250ControlPoint>()->CaptureSeconds > 0.f, TEXT("control point CDO"));
	PA250BattleCases::RunAim(Log);
	PA250BattleCases::RunWeapon(Log);
	PA250BattleCases::RunFaction(Log);
	PA250BattleCases::RunMelee(Log);
	PA250BattleCases::RunConquest(Log);
	PA250BattleCases::RunSpacing(Log);
	PA250BattleCases::RunTraining(Log);
	PA250BattleCases::RunMorale(Log);
	PA250BattleCases::RunTactics(Log);
	PA250BattleCases::RunAudio(Log);
	PA250BattleCases::RunBattleRoad(Log);
	PA250BattleCases::RunScenario(Log);
	PA250BattleCases::RunCampaignRead(Log);
	PA250BattleCases::RunCampaignArc(Log);
	PA250BattleCases::RunClasses(Log);
	PA250BattleCases::RunEconomy(Log);
	PA250BattleCases::RunSquad(Log);
	PA250BattleCases::RunWorks(Log);
	PA250BattleCases::RunSupport(Log);
	PA250BattleCases::RunGunnery(Log);
	PA250BattleCases::RunModes(Log);
	PA250BattleCases::RunFeel(Log);
	PA250BattleCases::RunPlaytest(Log);
	PA250BattleCases::RunMarch(Log);
	PA250BattleCases::RunPeople(Log);
	PA250BattleCases::RunStory(Log);

	const int32 Fails = Log.Failures.Num();
	FString Failed;
	for (const FString& F : Log.Failures)
	{
		Failed += (Failed.IsEmpty() ? TEXT("\"") : TEXT(",\"")) + F.ReplaceCharWithEscapedChar() + TEXT("\"");
	}
	FString Json = FString::Printf(TEXT("{\"reason\":\"commandlet_smoke\",\"passed\":%s,\"checks\":%d,\"fails\":%d,\"failures\":["),
		Fails == 0 ? TEXT("true") : TEXT("false"), Log.Passed + Fails, Fails);
	Json += Failed;
	Json += TEXT("]}\n");
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("battle_commandlet.json")));
	UE_LOG(LogTemp, Display, TEXT("PA250 battle commandlet done checks=%d fails=%d"), Log.Passed + Fails, Fails);
	return Fails == 0 ? 0 : 1;
}
