#include "PA250Subsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
}

UPA250Subsystem* UPA250Subsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UPA250Subsystem>() : nullptr;
}

FString UPA250Subsystem::ScenarioPath()
{
	// Project dir is <repo>/unreal/PlayAmerica250. Development layout; see PROGRESS.md for packaging notes.
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/boston1775.json")));
}
FString UPA250Subsystem::SavePath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250/campaign.json")); }
FString UPA250Subsystem::TracePath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250/autoplay_trace.json")); }

bool UPA250Subsystem::NewCampaign(FString& OutError)
{
	try
	{
		Controller.new_campaign(pa250::read_text_file(ToStd(ScenarioPath())));
		return true;
	}
	catch (const std::exception& E)
	{
		OutError = ToFStr(E.what());
		return false;
	}
}

bool UPA250Subsystem::EnsureCampaign()
{
	FString Err;
	return Controller.has_campaign() || NewCampaign(Err);
}

bool UPA250Subsystem::QueueMove(const FString& ArmyId, const FString& ToProvince, FString& OutError)
{
	const auto Errs = Controller.queue_move(ToStd(ArmyId), ToStd(ToProvince));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	return true;
}

#define PA250_FORWARD(Call)                    \
	std::string Err;                           \
	const bool bOk = (Call);                   \
	if (!bOk) { OutError = ToFStr(Err); }      \
	return bOk;

bool UPA250Subsystem::EndTurn(FString& OutError) { PA250_FORWARD(Controller.end_turn(Err)) }
bool UPA250Subsystem::BeginBattle(FString& OutError) { PA250_FORWARD(Controller.begin_battle(Err)) }
bool UPA250Subsystem::PrepareDebugBattle(FString& OutError)
{
	if (!NewCampaign(OutError))
	{
		return false;
	}
	for (int32 i = 0; i < 12 && !Controller.battle_pending(); ++i)
	{
		if (!EndTurn(OutError))
		{
			return false;
		}
	}
	return BeginBattle(OutError);
}

bool UPA250Subsystem::ApplyBattleResult(const pa250::BattleResult& Result, FString& OutError)
{
	PA250_FORWARD(Controller.finish_battle(Result, Err))
}
bool UPA250Subsystem::BeginDiplomacy(const FString& Faction, FString& OutError)
{
	PA250_FORWARD(Controller.begin_diplomacy(ToStd(Faction), Err))
}
bool UPA250Subsystem::LeaveDiplomacy(FString& OutError) { PA250_FORWARD(Controller.finish_diplomacy(nullptr, Err)) }
bool UPA250Subsystem::SaveCampaign(FString& OutError) { PA250_FORWARD(Controller.save(ToStd(SavePath()), Err)) }
bool UPA250Subsystem::LoadCampaign(FString& OutError) { PA250_FORWARD(Controller.load(ToStd(SavePath()), Err)) }

FString UPA250Subsystem::GetBattleSetupJson() const { return ToFStr(pa250::setup_to_json(Controller.battle_setup())); }

bool UPA250Subsystem::ApplyBattleResultJson(const FString& ResultJson, FString& OutError)
{
	try
	{
		return ApplyBattleResult(pa250::result_from_json(ToStd(ResultJson)), OutError);
	}
	catch (const std::exception& E)
	{
		OutError = ToFStr(E.what());
		return false;
	}
}

FString UPA250Subsystem::GetDiplomacyContextJson() const
{
	return ToFStr(pa250::context_to_json(Controller.diplomacy_context()));
}

TArray<FString> UPA250Subsystem::ValidateProposalJson(const FString& ProposalJson) const
{
	pa250::DiplomacyProposal Proposal;
	std::vector<std::string> Errors = pa250::proposal_from_json(ToStd(ProposalJson), Proposal);
	const auto More = pa250::validate_proposal(Controller.diplomacy_context(), Proposal);
	Errors.insert(Errors.end(), More.begin(), More.end());
	TArray<FString> Out;
	for (const auto& E : Errors)
	{
		Out.Add(ToFStr(E));
	}
	return Out;
}

bool UPA250Subsystem::AcceptProposalJson(const FString& ProposalJson, FString& OutError)
{
	if (Controller.flow() != pa250::Flow::Diplomacy)
	{
		OutError = TEXT("not in diplomacy");
		return false;
	}
	pa250::DiplomacyResult Result;
	Result.faction = Controller.diplomacy_context().faction;
	Result.accepted = true;
	const auto ParseErrors = pa250::proposal_from_json(ToStd(ProposalJson), Result.proposal);
	if (!ParseErrors.empty())
	{
		OutError = ToFStr(ParseErrors.front());
		return false;
	}
	PA250_FORWARD(Controller.finish_diplomacy(&Result, Err))
}

FString UPA250Subsystem::GetStateHash() const { return Controller.has_campaign() ? ToFStr(Controller.hash_hex()) : FString(); }

EPA250Flow UPA250Subsystem::GetFlow() const
{
	switch (Controller.flow())
	{
	case pa250::Flow::Battle: return EPA250Flow::Battle;
	case pa250::Flow::Diplomacy: return EPA250Flow::Diplomacy;
	default: return EPA250Flow::Campaign;
	}
}

bool UPA250Subsystem::WriteTrace(const FString& Extra) const
{
	FString Json = ToFStr(Controller.trace_json());
	return FFileHelper::SaveStringToFile(Json, *TracePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FName UPA250Subsystem::LevelNameForFlow(EPA250Flow Flow)
{
	switch (Flow)
	{
	case EPA250Flow::Battle: return FName(TEXT("/Game/PA250/Maps/L_Lexington"));
	case EPA250Flow::Diplomacy: return FName(TEXT("/Game/PA250/Maps/L_DiplomacyRoom"));
	default: return FName(TEXT("/Game/PA250/Maps/L_Campaign"));
	}
}

void UPA250Subsystem::TravelToFlowLevel(UObject* WorldContext) const
{
	UGameplayStatics::OpenLevel(WorldContext, LevelNameForFlow(GetFlow()));
}
