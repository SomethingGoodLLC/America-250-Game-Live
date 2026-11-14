#include "PA250Subsystem.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <algorithm>

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
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/boston1775.json")));
}
FString UPA250Subsystem::CorrespondenceCatalogPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/opening_war_correspondence.json")));
}
FString UPA250Subsystem::LogisticsTuningPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/logistics_tuning.json")));
}
FString UPA250Subsystem::BuildingsCatalogPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/buildings_catalog.json")));
}
FString UPA250Subsystem::ProvinceSizesPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/province_sizes.json")));
}
FString UPA250Subsystem::SavePath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250/campaign.json")); }
FString UPA250Subsystem::TracePath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250/autoplay_trace.json")); }
FString UPA250Subsystem::AutosaveDir() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Autosaves")); }

void UPA250Subsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bPaused = true;
	SpeedIndex = FPA250CampaignSpeed::DefaultIndex;
	HourAccumulator = 0.f;
}

void UPA250Subsystem::Deinitialize()
{
	bCampaignActive = false;
	Super::Deinitialize();
}

void UPA250Subsystem::SetCampaignActive(bool bActive)
{
	bCampaignActive = bActive;
	if (bActive)
	{
		bPaused = true;
		HourAccumulator = 0.f;
	}
}

void UPA250Subsystem::SetPaused(bool bInPaused)
{
	bPaused = bInPaused;
	bPausedByLetter = false;  // the player's own pause or resume always wins
	if (bPaused)
	{
		HourAccumulator = 0.f;
	}
	BumpHudEpoch();
}

void UPA250Subsystem::SetSpeedIndex(int32 Idx)
{
	SpeedIndex = FPA250CampaignSpeed::Clamp(Idx);
	BumpHudEpoch();
}

bool UPA250Subsystem::IsTickable() const
{
	return bCampaignActive && Controller.has_campaign() && Controller.flow() == pa250::Flow::Campaign;
}

TStatId UPA250Subsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPA250Subsystem, STATGROUP_Tickables);
}

void UPA250Subsystem::Tick(float DeltaTime)
{
	if (!IsTickable() || bPaused || Controller.battle_pending())
	{
		return;
	}
	HourAccumulator += DeltaTime * FPA250CampaignSpeed::HoursAt(SpeedIndex);
	int32 Steps = 0;
	while (HourAccumulator >= 1.f && Steps < MaxHoursPerFrame)
	{
		HourAccumulator -= 1.f;
		++Steps;
		const pa250::CampaignState Before = Controller.state();
		pa250::TickOutput Out = Controller.advance_one_hour();
		BumpHudEpoch();
		MaybeAutosave(Before, Out.state);
		NoteDispatches(Before, Out.state);
		if (!Out.interrupts.empty())
		{
			// The sim interrupts only for contact and for a letter that needs an answer now.
			bPaused = true;
			bPausedByLetter = !Controller.battle_pending() &&
							  Out.interrupts.end() != std::find(Out.interrupts.begin(), Out.interrupts.end(),
																pa250::Interrupt::EventPopup);
			HourAccumulator = 0.f;
			RouteInterrupt(Out);
			break;
		}
	}
}

void UPA250Subsystem::RouteInterrupt(const pa250::TickOutput& Out)
{
	for (pa250::Interrupt I : Out.interrupts)
	{
		if (I == pa250::Interrupt::ArmiesInContact)
		{
			// Existing battle-launch flow: UI shows Fight; autoplay presses it.
			break;
		}
		if (I == pa250::Interrupt::EventPopup)
		{
			// A letter that needs an answer now: the panel opens, and answering it resumes the clock.
			break;
		}
	}
}

void UPA250Subsystem::NoteDispatches(const pa250::CampaignState& Before, const pa250::CampaignState& After)
{
	auto Terminal = [](const std::string& Status) {
		return Status == "delivered" || Status == "refused" || Status == "dismissed";
	};
	FString News;
	for (const auto& Now : After.staff.queue)
	{
		const pa250::CorrespondenceItem* Was = nullptr;
		for (const auto& Old : Before.staff.queue)
		{
			if (Old.id == Now.id)
			{
				Was = &Old;
				break;
			}
		}
		if (!Was)
		{
			if (Now.awaiting_choice && !pa250::letter_is_timely(Now))
			{
				News = FString::Printf(TEXT("A letter has reached the desk: %s. Open it from the desk button when you are ready."),
									   *ToFStr(Now.subject));
			}
			else if (!Now.awaiting_choice && Now.kind != "requisition_aftermath")
			{
				News = FString::Printf(TEXT("Filed: %s."), *ToFStr(Now.subject));
			}
		}
		else if (Was->status != "delivered" && Now.status == "delivered" &&
				 (Now.received_powder > 0 || Now.received_arms > 0 || Now.received_provisions > 0))
		{
			News = FString::Printf(TEXT("%s: %s — arrived at the depot: +%d powder, +%d muskets, +%d provisions."),
								   *ToFStr(After.staff.clerk_name), *ToFStr(Now.subject), Now.received_powder,
								   Now.received_arms, Now.received_provisions);
		}
		else if (!Terminal(Was->status) && Terminal(Now.status))
		{
			News = FString::Printf(TEXT("%s: %s — %s"), *ToFStr(After.staff.clerk_name), *ToFStr(Now.subject),
								   *ToFStr(Now.reason.empty() ? Now.status : Now.reason));
		}
	}
	if (!News.IsEmpty())
	{
		LastDispatch = News;
		++DispatchSerial;
	}
}

void UPA250Subsystem::AfterLetterAnswered()
{
	const pa250::CampaignState& St = Controller.state();
	bool bAwaiting = false;
	for (const auto& C : St.staff.queue)
	{
		bAwaiting = bAwaiting || C.awaiting_choice;
	}
	if (!bAwaiting)
	{
		bDeskOpen = false;
	}
	if (bPausedByLetter && St.pending_event_id.empty() && !Controller.battle_pending())
	{
		SetPaused(false);  // the letter stopped the clock, so its answer starts it again
	}
	BumpHudEpoch();
}

void UPA250Subsystem::MaybeAutosave(const pa250::CampaignState& Before, const pa250::CampaignState& After)
{
	const pa250::YMDH A = pa250::to_ymdh(Before.now);
	const pa250::YMDH B = pa250::to_ymdh(After.now);
	const bool Crossed = (B.d == 1 && B.h == 0) && !(A.d == 1 && A.h == 0 && A.m == B.m && A.y == B.y);
	if (!Crossed)
	{
		return;
	}
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*AutosaveDir());
	const FString Path = FPaths::Combine(AutosaveDir(), FString::Printf(TEXT("autosave_%04d_%02d.json"), B.y, B.m));
	std::string Err;
	Controller.save(ToStd(Path), Err);
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *FPaths::Combine(AutosaveDir(), TEXT("autosave_*.json")), true, false);
	Names.Sort();
	AutosavePaths.Reset();
	for (const FString& Name : Names)
	{
		AutosavePaths.Add(FPaths::Combine(AutosaveDir(), Name));
	}
	while (AutosavePaths.Num() > 3)
	{
		PF.DeleteFile(*AutosavePaths[0]);
		AutosavePaths.RemoveAt(0);
	}
}

FString UPA250Subsystem::FormatWhen(int64 HourIndex) const
{
	const pa250::YMDH Y = pa250::to_ymdh(pa250::GameTime{HourIndex});
	static const TCHAR* Months[] = {
		TEXT(""), TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"), TEXT("June"),
		TEXT("July"), TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")};
	const int32 Hour12 = (Y.h % 12 == 0) ? 12 : (Y.h % 12);
	const TCHAR* AmPm = Y.h < 12 ? TEXT("a.m.") : TEXT("p.m.");
	return FString::Printf(TEXT("%d %s %d · %d %s"), Y.d, Months[Y.m], Y.y, Hour12, AmPm);
}

FString UPA250Subsystem::FormatDate() const
{
	if (!Controller.has_campaign())
	{
		return TEXT("");
	}
	return FormatWhen(Controller.state().now.hour_index);
}

FString UPA250Subsystem::FormatHoursRemaining(int32 Hours)
{
	if (Hours <= 0)
	{
		return TEXT("0h");
	}
	const int32 Days = Hours / 24;
	const int32 Rem = Hours % 24;
	if (Days <= 0)
	{
		return FString::Printf(TEXT("%dh"), Rem);
	}
	if (Rem == 0)
	{
		return FString::Printf(TEXT("%dd"), Days);
	}
	return FString::Printf(TEXT("%dd %dh"), Days, Rem);
}

void UPA250Subsystem::ClearOrders()
{
	if (!Controller.has_campaign() || Controller.flow() != pa250::Flow::Campaign || Controller.battle_pending())
	{
		return;
	}
	const pa250::CampaignState& St = Controller.state();
	std::vector<std::string> ToHalt;
	for (const auto& Kv : St.armies)
	{
		if (Kv.second.faction != St.player_faction)
		{
			continue;
		}
		bool bBusy = !Kv.second.path.empty();
		if (!bBusy)
		{
			for (const auto& C : St.pending_commands)
			{
				if (C.payload.army_id == Kv.first && C.payload.kind == pa250::CommandKind::MoveArmy)
				{
					bBusy = true;
					break;
				}
			}
		}
		if (bBusy)
		{
			ToHalt.push_back(Kv.first);
		}
	}
	for (const std::string& Id : ToHalt)
	{
		Controller.enqueue_halt(Id);
	}
}

bool UPA250Subsystem::EnsureCorrespondenceCatalog(FString& OutError)
{
	if (pa250::correspondence_catalog_loaded())
	{
		return true;
	}
	try
	{
		pa250::load_correspondence_catalog(pa250::read_text_file(ToStd(CorrespondenceCatalogPath())));
		return true;
	}
	catch (const std::exception& E)
	{
		OutError = ToFStr(E.what());
		return false;
	}
}

void UPA250Subsystem::LoadSimDataFiles()
{
	// read_text_file throws when a file is missing; the sim then keeps its built-in defaults.
	auto Load = [](const FString& Path, void (*Fn)(const std::string&)) {
		try
		{
			Fn(pa250::read_text_file(ToStd(Path)));
		}
		catch (...)
		{
			UE_LOG(LogTemp, Warning, TEXT("PA250 sim data: %s not loaded; using built-in defaults"), *Path);
		}
	};
	Load(LogisticsTuningPath(), &pa250::load_logistics_tuning);
	Load(BuildingsCatalogPath(), &pa250::load_buildings_catalog);
	Load(ProvinceSizesPath(), &pa250::load_province_sizes);
}

bool UPA250Subsystem::NewCampaign(FString& OutError)
{
	try
	{
		if (!EnsureCorrespondenceCatalog(OutError))
		{
			return false;
		}
		LoadSimDataFiles();
		Controller.new_campaign(pa250::read_text_file(ToStd(ScenarioPath())));
		bPaused = true;
		HourAccumulator = 0.f;
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
	const auto Errs = Controller.enqueue_move(ToStd(ArmyId), ToStd(ToProvince));
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

bool UPA250Subsystem::AdvanceHours(int32 Hours, FString& OutError)
{
	if (!Controller.has_campaign())
	{
		OutError = TEXT("no campaign");
		return false;
	}
	if (Controller.battle_pending())
	{
		OutError = TEXT("resolve the pending battle first");
		return false;
	}
	Controller.advance_hours(Hours, true);
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::EndTurn(FString& OutError) { return AdvanceHours(24, OutError); }
bool UPA250Subsystem::BeginBattle(FString& OutError)
{
	SetPaused(true);
	PA250_FORWARD(Controller.begin_battle(Err))
}
bool UPA250Subsystem::PrepareDebugBattle(FString& OutError)
{
	if (!NewCampaign(OutError))
	{
		return false;
	}
	Controller.advance_hours(72, true);
	return BeginBattle(OutError);
}

bool UPA250Subsystem::ApplyBattleResult(const pa250::BattleResult& Result, FString& OutError)
{
	std::string Err;
	const bool bOk = Controller.finish_battle(Result, Err);
	if (!bOk)
	{
		OutError = ToFStr(Err);
		return false;
	}
	bUnseenBattleReport = true;  // the campaign map opens the after-battle report by itself
	// A question that arrives with the result holds the clock; answering it starts the clock again.
	bPausedByLetter = !Controller.state().pending_event_id.empty() && IsPendingLetterTimely();
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::ChooseLogistics(const FString& LetterId, const FString& Choice, FString& OutError)
{
	const auto Errs = Controller.choose_logistics_action(ToStd(LetterId), ToStd(Choice));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	AfterLetterAnswered();
	return true;
}

bool UPA250Subsystem::ResolveCommander(const FString& Action, FString& OutError)
{
	const auto Errs = Controller.resolve_commander_action(ToStd(Action));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	AfterLetterAnswered();
	return true;
}

bool UPA250Subsystem::ApprovePurchaseOrder(const FString& DestProvince, int32 Powder, int32 Cartridges,
										   int32 Muskets, int32 Bayonets, int32 Provisions, int32 Lead, int32 Paper,
										   FString& OutError)
{
	const auto Errs = Controller.approve_purchase_order(ToStd(DestProvince), Powder, Cartridges, Muskets, Bayonets,
														Provisions, Lead, Paper);
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::ApproveTransferOrder(const FString& SourceProvince, const FString& DestProvince, int32 Powder,
										   int32 Cartridges, int32 Muskets, int32 Bayonets, int32 Provisions,
										   int32 Lead, int32 Paper, FString& OutError)
{
	const auto Errs = Controller.approve_transfer_order(ToStd(SourceProvince), ToStd(DestProvince), Powder, Cartridges,
														Muskets, Bayonets, Provisions, Lead, Paper);
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::DevelopProvince(const FString& ProvinceId, const FString& Track, FString& OutError)
{
	return StartConstruction(ProvinceId, Track, OutError);
}

bool UPA250Subsystem::StartConstruction(const FString& ProvinceId, const FString& BuildingId, FString& OutError)
{
	const auto Errs = Controller.start_construction_action(ToStd(ProvinceId), ToStd(BuildingId));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::CancelConstruction(const FString& ProvinceId, const FString& BuildingId, FString& OutError)
{
	const auto Errs = Controller.cancel_construction_action(ToStd(ProvinceId), ToStd(BuildingId));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::DemolishBuilding(const FString& ProvinceId, const FString& BuildingId, FString& OutError)
{
	const auto Errs = Controller.demolish_building_action(ToStd(ProvinceId), ToStd(BuildingId));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::TakeLoan(FString& OutError)
{
	const auto Errs = Controller.take_loan_action();
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::RepayLoan(FString& OutError)
{
	const auto Errs = Controller.repay_loan_action();
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

FString UPA250Subsystem::GetTreasuryJson() const { return ToFStr(pa250::treasury_json(Controller.state())); }

int32 UPA250Subsystem::GetMonthlyNetGold() const
{
	return Controller.has_campaign() ? pa250::monthly_net_gold(Controller.state()) : 0;
}

bool UPA250Subsystem::StartRecruitment(const FString& ProvinceId, const FString& UnitId, FString& OutError)
{
	const auto Errs = Controller.start_recruitment_action(ToStd(ProvinceId), ToStd(UnitId));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

bool UPA250Subsystem::CancelRecruitment(const FString& ProvinceId, FString& OutError)
{
	const auto Errs = Controller.cancel_recruitment_action(ToStd(ProvinceId));
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	BumpHudEpoch();
	return true;
}

FString UPA250Subsystem::GetRecruitmentPreview(const FString& ProvinceId, const FString& UnitId) const
{
	return ToFStr(pa250::recruitment_preview(Controller.state(), ToStd(ProvinceId), ToStd(UnitId)));
}

FString UPA250Subsystem::GetCityViewJson(const FString& ProvinceId) const
{
	return ToFStr(pa250::city_view_json(Controller.state(), ToStd(ProvinceId)));
}

FString UPA250Subsystem::GetBuildingPreview(const FString& ProvinceId, const FString& BuildingId) const
{
	return ToFStr(pa250::building_preview(Controller.state(), ToStd(ProvinceId), ToStd(BuildingId)));
}

FString UPA250Subsystem::GetLastResourceChangeReason() const
{
	const auto& Changes = Controller.state().resource_changes;
	return Changes.empty() ? FString() : ToFStr(Changes.back().reason);
}

int32 UPA250Subsystem::GetProvinceSlotCount(const FString& ProvinceId) const
{
	return pa250::province_slot_count(ToStd(ProvinceId));
}

FString UPA250Subsystem::GetProvinceDevelopmentJson() const
{
	FString Out = TEXT("{");
	bool First = true;
	for (const auto& Kv : Controller.state().provinces)
	{
		if (!First) Out += TEXT(",");
		First = false;
		const pa250::Province& P = Kv.second;
		Out += FString::Printf(
			TEXT("\"%s\":{\"dev\":%d,\"farm\":%d,\"workshop\":%d,\"depot\":%d,\"fort\":%d,\"church\":%s,"
				 "\"militia_muster\":%s,\"slots\":%d,\"occupied\":%d,\"supply\":%d}"),
			UTF8_TO_TCHAR(P.id.c_str()), pa250::province_development(P), P.farm_level, P.workshop_level,
			P.depot_level, P.fort_level, P.church ? TEXT("true") : TEXT("false"),
			P.militia_muster ? TEXT("true") : TEXT("false"), pa250::province_slot_count(P.id),
			pa250::province_slots_occupied(Controller.state(), P.id), pa250::effective_supply_value(P));
	}
	Out += TEXT("}");
	return Out;
}

bool UPA250Subsystem::StartCartridgeCraft(const FString& ProvinceId, const FString& ArmyId, int32 Workers,
										  int32 Powder, int32 Lead, int32 Paper, FString& OutError)
{
	const auto Errs =
		Controller.start_cartridge_craft(ToStd(ProvinceId), ToStd(ArmyId), Workers, Powder, Lead, Paper);
	if (!Errs.empty())
	{
		OutError = ToFStr(Errs.front());
		return false;
	}
	return true;
}

FString UPA250Subsystem::GetLastBattleReportJson() const
{
	// The report itself, not a rebuilt BattleResult: fate, scavenged stores and the
	// attributed change lines only exist on BattleReport.
	return ToFStr(pa250::report_to_json(Controller.state().progress.last_battle_report));
}

FString UPA250Subsystem::GetShipmentsJson() const
{
	FString Out = TEXT("[");
	bool First = true;
	for (const auto& Sh : Controller.state().shipments)
	{
		if (!First) Out += TEXT(",");
		First = false;
		Out += FString::Printf(
			TEXT("{\"id\":\"%s\",\"status\":\"%s\",\"source\":\"%s\",\"dest\":\"%s\",\"deliver_on_hour\":%lld,"
				 "\"powder\":%d,\"cartridges\":%d,\"muskets\":%d}"),
			UTF8_TO_TCHAR(Sh.id.c_str()), UTF8_TO_TCHAR(Sh.status.c_str()),
			UTF8_TO_TCHAR(Sh.source_province.c_str()), UTF8_TO_TCHAR(Sh.dest_province.c_str()),
			static_cast<long long>(Sh.deliver_on_hour), Sh.powder, Sh.cartridges, Sh.muskets);
	}
	Out += TEXT("]");
	return Out;
}

bool UPA250Subsystem::HasStaffEvent() const { return Controller.has_staff_event(); }

FString UPA250Subsystem::GetPendingLetterId() const
{
	const pa250::CampaignState& St = Controller.state();
	if (!St.pending_event_id.empty())
	{
		return ToFStr(St.pending_event_id);
	}
	for (const auto& C : St.staff.queue)
	{
		if (C.awaiting_choice)
		{
			return ToFStr(C.id);
		}
	}
	return FString();
}

namespace
{
const pa250::CorrespondenceItem* FindAwaitingLetter(const pa250::CampaignState& St)
{
	const std::string Want = St.pending_event_id;
	for (const auto& C : St.staff.queue)
	{
		if (!Want.empty() && C.id == Want)
		{
			return &C;
		}
	}
	for (const auto& C : St.staff.queue)
	{
		if (C.awaiting_choice)
		{
			return &C;
		}
	}
	return nullptr;
}
}

bool UPA250Subsystem::IsStaffPanelOpen() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	if (!C || Controller.battle_pending())
	{
		return false;
	}
	if (!C->awaiting_choice)
	{
		return !Controller.state().pending_event_id.empty();  // a notice left open by an older save
	}
	return bDeskOpen || pa250::letter_is_timely(*C);
}

bool UPA250Subsystem::HasDeskLetter() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	return C && C->awaiting_choice && !Controller.battle_pending() && !IsStaffPanelOpen();
}

void UPA250Subsystem::OpenDesk()
{
	bDeskOpen = true;
	BumpHudEpoch();
}

void UPA250Subsystem::CloseDesk()
{
	bDeskOpen = false;
	BumpHudEpoch();
}

bool UPA250Subsystem::IsPendingLetterTimely() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	return C && pa250::letter_is_timely(*C);
}

int32 UPA250Subsystem::GetPendingLetterHoursLeft() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	const int64 Lapse = C ? pa250::letter_lapse_hour(*C) : 0;
	return Lapse > 0 ? static_cast<int32>(FMath::Max<int64>(0, Lapse - Controller.state().now.hour_index)) : -1;
}

FString UPA250Subsystem::GetDeskChipText() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	if (!C)
	{
		return FString();
	}
	FString Out = FString::Printf(TEXT("Letter on the desk: %s"), *ToFStr(C->subject));
	const int32 Left = GetPendingLetterHoursLeft();
	if (Left >= 0)
	{
		Out += Left >= 48 ? FString::Printf(TEXT("  ·  answer within %d days"), (Left + 12) / 24)
						  : FString::Printf(TEXT("  ·  answer within %d h"), Left);
	}
	return Out;
}

namespace
{
const pa250::CorrespondenceCatalogEntry* PendingSourceEntry(const pa250::CampaignState& St)
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(St);
	if (!C || C->kind != "source_decision" || C->source_id.empty())
	{
		return nullptr;
	}
	return pa250::find_correspondence_entry(C->source_id);
}
}

int32 UPA250Subsystem::GetPendingLetterChoiceCount() const
{
	const pa250::CorrespondenceCatalogEntry* E = PendingSourceEntry(Controller.state());
	return E ? static_cast<int32>(E->choices.size()) : 0;
}

FString UPA250Subsystem::GetPendingLetterChoiceId(int32 Index) const
{
	const pa250::CorrespondenceCatalogEntry* E = PendingSourceEntry(Controller.state());
	return (E && Index >= 0 && Index < static_cast<int32>(E->choices.size())) ? ToFStr(E->choices[Index].id) : FString();
}

FString UPA250Subsystem::GetPendingLetterChoiceLabel(int32 Index) const
{
	const pa250::CorrespondenceCatalogEntry* E = PendingSourceEntry(Controller.state());
	return (E && Index >= 0 && Index < static_cast<int32>(E->choices.size())) ? ToFStr(E->choices[Index].label) : FString();
}

FString UPA250Subsystem::GetPendingLetterChoiceSummary(int32 Index) const
{
	const pa250::CorrespondenceCatalogEntry* E = PendingSourceEntry(Controller.state());
	return (E && Index >= 0 && Index < static_cast<int32>(E->choices.size())) ? ToFStr(E->choices[Index].effects_summary)
																		   : FString();
}

bool UPA250Subsystem::IsPendingLetterAwaitingChoice() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	return C && C->awaiting_choice;
}

FString UPA250Subsystem::GetPendingLetterSubject() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	return C ? ToFStr(C->subject) : FString();
}

FString UPA250Subsystem::GetPendingLetterBody() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	if (!C)
	{
		return FString();
	}
	FString Text = ToFStr(C->body);
	if (!C->reason.empty())
	{
		Text += TEXT("\n\n");
		Text += ToFStr(C->reason);
	}
	return Text;
}

FString UPA250Subsystem::GetPendingLetterKind() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	return C ? ToFStr(C->kind) : FString();
}

FString UPA250Subsystem::GetPendingLetterSourceId() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	return C ? ToFStr(C->source_id) : FString();
}

FString UPA250Subsystem::GetPendingLetterSourceUrl() const
{
	const pa250::CorrespondenceItem* C = FindAwaitingLetter(Controller.state());
	if (!C || C->source_id.empty())
	{
		return FString();
	}
	const pa250::CorrespondenceCatalogEntry* E = pa250::find_correspondence_entry(C->source_id);
	return E ? ToFStr(E->institutional_url) : FString();
}

bool UPA250Subsystem::OpenPendingLetterSourceUrl() const
{
	const FString Url = GetPendingLetterSourceUrl();
	if (Url.IsEmpty())
	{
		return false;
	}
	// Display-only: never mutates campaign state. Offline failure is fine.
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	return true;
}
bool UPA250Subsystem::BeginDiplomacy(const FString& Faction, FString& OutError)
{
	SetPaused(true);
	PA250_FORWARD(Controller.begin_diplomacy(ToStd(Faction), Err))
}
bool UPA250Subsystem::LeaveDiplomacy(FString& OutError) { PA250_FORWARD(Controller.finish_diplomacy(nullptr, Err)) }
bool UPA250Subsystem::SaveCampaign(FString& OutError) { PA250_FORWARD(Controller.save(ToStd(SavePath()), Err)) }
bool UPA250Subsystem::LoadCampaign(FString& OutError)
{
	if (!EnsureCorrespondenceCatalog(OutError))
	{
		return false;
	}
	LoadSimDataFiles();
	std::string Err;
	const bool bOk = Controller.load(ToStd(SavePath()), Err);
	if (!bOk)
	{
		OutError = ToFStr(Err);
		return false;
	}
	BumpHudEpoch();
	return true;
}

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

bool UPA250Subsystem::ApplyEnvoyActJson(const FString& ProposalJson, FString& OutError)
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
	if (!pa250::validate_proposal(Controller.diplomacy_context(), Result.proposal).empty())
	{
		OutError = TEXT("refused by the sim");
		return false;
	}
	PA250_FORWARD(Controller.apply_envoy_act(Result, Err))
}

bool UPA250Subsystem::RegisterDiplomacyFailure(FString& OutError) { PA250_FORWARD(Controller.register_failure(Err)) }

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

FName UPA250Subsystem::BattleLevelForPreset(const FString& ScenarioPreset)
{
	static const TCHAR* Concord = TEXT("/Game/PA250/Maps/L_Concord");
	if (ScenarioPreset == TEXT("concord_bridge") && FPackageName::DoesPackageExist(Concord))
	{
		return FName(Concord);
	}
	return LevelNameForFlow(EPA250Flow::Battle);
}

void UPA250Subsystem::TravelToFlowLevel(UObject* WorldContext) const
{
	if (GetFlow() == EPA250Flow::Battle)
	{
		const FName Level = BattleLevelForPreset(UTF8_TO_TCHAR(GetBattleSetup().scenario_preset.c_str()));
		UE_LOG(LogTemp, Display, TEXT("PA250 battle level: %s"), *Level.ToString());
		UGameplayStatics::OpenLevel(WorldContext, Level);
		return;
	}
	UGameplayStatics::OpenLevel(WorldContext, LevelNameForFlow(GetFlow()));
}
