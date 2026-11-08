#include "PA250ClockCommandlet.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "pa250/sim.hpp"

int32 UPA250ClockCommandlet::Main(const FString& Params)
{
	using namespace pa250;
	const FString Scenario = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/boston1775.json")));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Scenario))
	{
		UE_LOG(LogTemp, Error, TEXT("cannot read scenario"));
		return 1;
	}
	CampaignState Base = load_scenario(TCHAR_TO_UTF8(*Text));

	// Speed independence: 240 hours via advance_hour twice → same hash
	CampaignState A = Base, B = Base;
	for (int i = 0; i < 240; ++i)
	{
		A = advance_hour(A).state;
		B = advance_hour(B).state;
	}
	if (state_hash(A) != state_hash(B))
	{
		UE_LOG(LogTemp, Error, TEXT("speed independence failed"));
		return 2;
	}

	// Contact: British boat toward cambridge with militia there
	CampaignState C = Base;
	C.armies["continental_militia"].province = "cambridge";
	CommandPayload P;
	P.kind = CommandKind::MoveArmy;
	P.army_id = "british_regulars";
	P.dest_province = "cambridge";
	auto Plan = plan_path(C, P.army_id, P.dest_province);
	P.edge_path = Plan.edges;
	C = enqueue_command(C, P);
	int Battles = 0;
	for (int i = 0; i < 48; ++i)
	{
		TickOutput Out = advance_hour(C);
		C = Out.state;
		Battles += static_cast<int>(Out.battles.size());
		if (C.pending_battle.active)
		{
			break;
		}
	}
	if (!C.pending_battle.active || Battles != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("contact test failed battles=%d pending=%d"), Battles, C.pending_battle.active);
		return 3;
	}

	// Monthly autosave boundary: crossing May 1 00:00
	CampaignState D = Base;
	D.now = from_ymdh({1775, 4, 30, 23});
	const int Before = D.supplies["british"];
	D = advance_hour(D).state;
	YMDH Y = to_ymdh(D.now);
	if (!(Y.m == 5 && Y.d == 1 && Y.h == 0) || D.supplies["british"] <= Before)
	{
		UE_LOG(LogTemp, Error, TEXT("monthly boundary failed"));
		return 4;
	}

	UE_LOG(LogTemp, Display, TEXT("PA250.Campaign.Clock passed"));
	return 0;
}
