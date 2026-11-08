// Automation test: drives the subsystem's flow state machine and save/load hash without loading levels.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Campaign/PA250Subsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPA250FlowTest, "PA250.Flow.StateMachineAndSaveLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPA250FlowTest::RunTest(const FString& Parameters)
{
	// ClassWithin = UGameInstance, so host the subsystem in a bare game instance (no world, no levels).
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	UPA250Subsystem* Sub = NewObject<UPA250Subsystem>(GI);
	FString Err;
	TestTrue(TEXT("new campaign"), Sub->NewCampaign(Err));
	TestEqual(TEXT("starts in campaign"), Sub->GetFlow(), EPA250Flow::Campaign);
	TestFalse(TEXT("cannot begin battle without one"), Sub->BeginBattle(Err));

	for (int i = 0; i < 3; ++i)
	{
		TestTrue(TEXT("end turn"), Sub->EndTurn(Err));
	}
	TestTrue(TEXT("armies met at Lexington"), Sub->IsBattlePending());
	TestFalse(TEXT("turn blocked by pending battle"), Sub->EndTurn(Err));
	TestTrue(TEXT("begin battle"), Sub->BeginBattle(Err));
	TestEqual(TEXT("battle flow"), Sub->GetFlow(), EPA250Flow::Battle);
	TestEqual(TEXT("level for battle"), Sub->LevelNameForFlow(EPA250Flow::Battle).ToString(), FString(TEXT("/Game/PA250/Maps/L_Lexington")));

	TestTrue(TEXT("apply result json"), Sub->ApplyBattleResultJson(
		TEXT("{\"province_id\":\"lexington\",\"winner\":\"continental\",\"objective_holder\":\"continental\","
		     "\"losses\":{\"british\":{\"line_infantry\":10}},\"retreat_province\":\"\"}"), Err));
	TestEqual(TEXT("back to campaign"), Sub->GetFlow(), EPA250Flow::Campaign);

	TestTrue(TEXT("begin diplomacy"), Sub->BeginDiplomacy(TEXT("british"), Err));
	TestEqual(TEXT("diplomacy flow"), Sub->GetFlow(), EPA250Flow::Diplomacy);
	const FString Bad = TEXT("{\"spoken_line\":\"x\",\"clauses\":[{\"type\":\"cede_province\",\"province_id\":\"boston\"}]}");
	TestTrue(TEXT("sim rejects Boston cession"), Sub->ValidateProposalJson(Bad).Num() > 0);
	const FString HashBefore = Sub->GetStateHash();
	TestTrue(TEXT("rejected accept is a no-op"), Sub->AcceptProposalJson(Bad, Err));
	TestEqual(TEXT("hash unchanged"), Sub->GetStateHash(), HashBefore);
	TestTrue(TEXT("back in campaign"), Sub->GetFlow() == EPA250Flow::Campaign);

	TestTrue(TEXT("begin diplomacy again"), Sub->BeginDiplomacy(TEXT("british"), Err));
	const FString Good = TEXT("{\"spoken_line\":\"Agreed.\",\"clauses\":[{\"type\":\"ceasefire\",\"days\":3}]}");
	TestEqual(TEXT("valid proposal"), Sub->ValidateProposalJson(Good).Num(), 0);
	TestTrue(TEXT("accept"), Sub->AcceptProposalJson(Good, Err));
	TestNotEqual(TEXT("ceasefire changed the state"), Sub->GetStateHash(), HashBefore);
	TestEqual(TEXT("one treaty"), (int32)Sub->GetState().treaties.size(), 1);

	TestTrue(TEXT("end turn under ceasefire"), Sub->EndTurn(Err));
	const FString Hash = Sub->GetStateHash();
	TestTrue(TEXT("save"), Sub->SaveCampaign(Err));
	TestTrue(TEXT("end turn after save"), Sub->EndTurn(Err));
	TestNotEqual(TEXT("state advanced"), Sub->GetStateHash(), Hash);
	TestTrue(TEXT("load"), Sub->LoadCampaign(Err));
	TestEqual(TEXT("load reproduces turn-boundary hash"), Sub->GetStateHash(), Hash);
	return true;
}

#endif
