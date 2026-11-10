#include "PA250MapWidget.h"
#include "Campaign/PA250Subsystem.h"
#include "PA250ProvinceMapActor.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Layout/SlateRect.h"
#include "pa250/sim.hpp"
#include <cstring>

namespace
{
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

int32 FitCount(const pa250::Army& A)
{
	int32 N = 0;
	for (const auto& U : A.units) N += U.second;
	return N;
}
FString ProvName(const pa250::CampaignState& St, const std::string& Id)
{
	auto It = St.provinces.find(Id);
	return It == St.provinces.end() ? ToFStr(Id) : ToFStr(It->second.name);
}
/** Route the army is on, as province names, following its remaining edge path. */
TArray<FString> RouteNames(const pa250::CampaignState& St, const pa250::Army& A)
{
	TArray<FString> Out;
	Out.Add(ProvName(St, A.province));
	std::string Cur = A.province;
	for (const std::string& Eid : A.path)
	{
		auto Eit = St.edges.find(Eid);
		if (Eit == St.edges.end()) break;
		Cur = (Eit->second.a == Cur) ? Eit->second.b : Eit->second.a;
		Out.Add(ProvName(St, Cur));
	}
	return Out;
}
FString OrdersText(const pa250::CampaignState& St, const pa250::Army& A)
{
	if (A.path.empty())
	{
		return TEXT("holding");
	}
	const TArray<FString> Route = RouteNames(St, A);
	return FString::Printf(TEXT("→ %s"), Route.Num() > 1 ? *Route.Last() : TEXT("?"));
}
} // namespace

UPA250Subsystem* UPA250MapWidget::Sub() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UPA250Subsystem>() : nullptr;
}

FText UPA250MapWidget::HeaderText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::FromString(TEXT("No campaign"));
	}
	const pa250::CampaignState& St = S->GetState();
	auto Get = [](const std::map<std::string, int>& M, const char* K) {
		auto It = M.find(K);
		return It == M.end() ? 0 : It->second;
	};
	FString Treaties;
	for (const auto& T : St.treaties)
	{
		Treaties += FString::Printf(TEXT("%s%s %s"), Treaties.IsEmpty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(T.type.c_str()),
		                            *UPA250Subsystem::FormatHoursRemaining(T.hours_remaining));
	}
	const FString Date = S->FormatDate();
	const FString Sp = FPA250CampaignSpeed::Format(S->GetHoursPerSecond());
	const TCHAR* Pause = S->IsPaused() ? TEXT("PAUSED") : TEXT("RUNNING");
	const pa250::FactionStores Roll = pa250::rollup_faction_stores(St, pa250::kContinental);
	int32 InTransit = 0;
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status == "in_transit" || Sh.status == "committed") ++InTransit;
	}
	return FText::FromString(FString::Printf(
		TEXT("%s   |   %s  speed %s   |   Gold %d   |   Powder %d  Cart %d  Muskets %d  Prov %d   |   In transit %d   |   Local support %d   |   Supplies Cont %d  Brit %d   |   Attitude %d   |   Treaties: %s"),
		*Date, Pause, *Sp, St.player_gold, Roll.powder,
		[&]() {
			int32 C = 0;
			for (const auto& KV : St.provinces)
			{
				if (KV.second.owner != pa250::kContinental) continue;
				C += pa250::province_ledger(St, KV.first).cartridges;
			}
			return C;
		}(),
		Roll.arms, Roll.provisions, InTransit, St.local_support, Get(St.supplies, "continental"),
		Get(St.supplies, "british"), Get(St.relations, "british"),
		Treaties.IsEmpty() ? TEXT("none") : *Treaties));
}

FText UPA250MapWidget::DateText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::FromString(TEXT("No campaign"));
	}
	return FText::FromString(FString::Printf(TEXT("%s  ·  %s  ·  %s"), *S->FormatDate(),
											 S->IsPaused() ? TEXT("Paused") : TEXT("Running"),
											 *FPA250CampaignSpeed::Format(S->GetHoursPerSecond())));
}

FText UPA250MapWidget::TreasuryText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("🪙 %d"), S->GetState().player_gold));
}

FText UPA250MapWidget::StoresText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::CampaignState& St = S->GetState();
	// Compact faction totals for the top bar (detail lives in the province window / ledger).
	int32 Powder = 0, Cart = 0, Muskets = 0, Prov = 0, InTransit = 0;
	for (const auto& Kv : St.provinces)
	{
		if (Kv.second.owner != St.player_faction) continue;
		const pa250::ProvinceStores& Ps = pa250::province_ledger(St, Kv.first);
		Powder += Ps.powder;
		Cart += Ps.cartridges;
		Muskets += Ps.muskets;
		Prov += Ps.provisions;
	}
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status == "in_transit" || Sh.status == "committed") ++InTransit;
	}
	return FText::FromString(FString::Printf(TEXT("Powder %d · Cart %d · Musk %d · Prov %d · ⛟ %d"), Powder, Cart,
											 Muskets, Prov, InTransit));
}

bool UPA250MapWidget::GetPlayerArmyId(int32 Index, FString& OutId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign() || Index < 0)
	{
		return false;
	}
	const pa250::CampaignState& St = S->GetState();
	int32 I = 0;
	for (const auto& Kv : St.armies)
	{
		if (Kv.second.faction != St.player_faction) continue;
		if (I == Index)
		{
			OutId = ToFStr(Kv.first);
			return true;
		}
		++I;
	}
	return false;
}

FText UPA250MapWidget::ArmyButtonText(int32 Index) const
{
	FString Id;
	if (!GetPlayerArmyId(Index, Id))
	{
		return FText::GetEmpty();
	}
	UPA250Subsystem* S = Sub();
	const pa250::CampaignState& St = S->GetState();
	const pa250::Army& A = St.armies.at(ToStd(Id));
	const bool bSel = Id == SelectedArmyId;
	return FText::FromString(FString::Printf(TEXT("%s %s — %s · %d fit · %s"), bSel ? TEXT("▶") : TEXT("⚔"),
											 *ToFStr(A.name), *ProvName(St, A.province), FitCount(A),
											 *OrdersText(St, A)));
}

FText UPA250MapWidget::SelectedArmyBannerText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedArmyId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	auto It = St.armies.find(ToStd(SelectedArmyId));
	if (It == St.armies.end())
	{
		return FText::GetEmpty();
	}
	const pa250::Army& A = It->second;
	FString Route;
	if (!A.path.empty())
	{
		Route = TEXT(" · Route: ");
		const TArray<FString> Names = RouteNames(St, A);
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			Route += (i ? TEXT(" → ") : TEXT("")) + Names[i];
		}
	}
	return FText::FromString(FString::Printf(
		TEXT("⚔ %s selected — at %s · %d fit · %s%s\nClick a town to march there · Esc / right-click cancels"),
		*ToFStr(A.name), *ProvName(St, A.province), FitCount(A), *OrdersText(St, A), *Route));
}

void UPA250MapWidget::PressSelectArmy(const FString& ArmyId)
{
	UPA250Subsystem* S = Sub();
	if (!S || ArmyId.IsEmpty() || !S->GetController().has_campaign())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	auto It = St.armies.find(ToStd(ArmyId));
	if (It == St.armies.end() || It->second.faction != St.player_faction)
	{
		return;
	}
	SelectedArmyId = ArmyId;
	SelectedProvinceId = ToFStr(It->second.province);
	SetStatusMessage(FString::Printf(TEXT("%s selected at %s. Click a destination town."),
									 *ToFStr(It->second.name), *ProvName(St, It->second.province)));
	NotifyMapSelectionChanged();
	if (APA250ProvinceMapActor* Map = MapActor.Get())
	{
		Map->FocusSelected();
	}
}

void UPA250MapWidget::PressSelectArmyAtSelection()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	for (int32 i = 0;; ++i)
	{
		FString Id;
		if (!GetPlayerArmyId(i, Id)) break;
		const pa250::CampaignState& St = S->GetState();
		if (ToFStr(St.armies.at(ToStd(Id)).province) == SelectedProvinceId)
		{
			PressSelectArmy(Id);
			return;
		}
	}
	SetStatusMessage(TEXT("No friendly army in this province. Use the Your forces panel (top-left)."));
}

FText UPA250MapWidget::ProvinceTitleText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	auto Pit = St.provinces.find(ToStd(SelectedProvinceId));
	if (Pit == St.provinces.end())
	{
		return FText::GetEmpty();
	}
	const pa250::Province& P = Pit->second;
	const TCHAR* Owner = P.owner == pa250::kBritish ? TEXT("♛ British Crown") : TEXT("⚜ Continentals — yours");
	FString Terrain;
	for (const std::string& T : P.terrain)
	{
		Terrain += Terrain.IsEmpty() ? ToFStr(T) : TEXT(", ") + ToFStr(T);
	}
	return FText::FromString(FString::Printf(TEXT("%s\n%s · %s · Development %d"), *ToFStr(P.name), Owner,
											 Terrain.IsEmpty() ? TEXT("—") : *Terrain,
											 pa250::province_development(P)));
}

FText UPA250MapWidget::DevelopmentText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	auto Pit = St.provinces.find(ToStd(SelectedProvinceId));
	if (Pit == St.provinces.end())
	{
		return FText::GetEmpty();
	}
	const pa250::Province& P = Pit->second;
	auto Pips = [](int32 Lvl) {
		FString Out;
		for (int32 i = 0; i < pa250::development_max_level(); ++i) Out += i < Lvl ? TEXT("●") : TEXT("○");
		return Out;
	};
	auto Cost = [](const char* Track, int32 Lvl) { return pa250::development_cost(Track, Lvl); };
	const bool bYours = P.owner == St.player_faction;
	return FText::FromString(FString::Printf(
		TEXT("Farms %s %d   ·   Workshop %s %d\nDepot %s %d   ·   Fort %s %d\nSupply value %d%s"),
		*Pips(P.farm_level), P.farm_level, *Pips(P.workshop_level), P.workshop_level, *Pips(P.depot_level),
		P.depot_level, *Pips(P.fort_level), P.fort_level, pa250::effective_supply_value(P),
		bYours ? *FString::Printf(TEXT("\nNext: Farm %dg · Workshop %dg · Depot %dg · Fort %dg"),
								  Cost("farm", P.farm_level), Cost("workshop", P.workshop_level),
								  Cost("depot", P.depot_level), Cost("fort", P.fort_level))
			   : TEXT("\nDevelop only provinces you hold.")));
}

FText UPA250MapWidget::BattleReportText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::BattleReport& Br = S->GetState().progress.last_battle_report;
	if (Br.province_id.empty())
	{
		return FText::GetEmpty();
	}
	auto Count = [](const std::map<std::string, std::map<std::string, int>>& M, const char* F) {
		int32 N = 0;
		auto It = M.find(F);
		if (It != M.end())
			for (const auto& Kv : It->second) N += Kv.second;
		return N;
	};
	auto Spent = [](const std::map<std::string, int>& M, const char* F) {
		auto It = M.find(F);
		return It == M.end() ? 0 : It->second;
	};
	int32 Morale = 0;
	for (const auto& Kv : Br.morale_delta) Morale += Kv.second;
	FString Reasons;
	for (const std::string& Line : Br.change_reasons)
	{
		Reasons += TEXT("\n  · ") + ToFStr(Line);
	}
	return FText::FromString(FString::Printf(
		TEXT("After-battle report — %s won\nGround: %s → %s (%s)\n"
			 "Support %d → %d · Gold %d → %d%s\n"
			 "Losses: yours %d killed / %d wounded · British %d / %d\n"
			 "Ammunition: cartridges yours %d · British %d · powder %d\nMorale: net %+d · Posts held by %s%s"),
		*ToFStr(Br.winner), *ToFStr(Br.province_owner_before), *ToFStr(Br.province_owner_after),
		Br.ownership_reason.empty() ? TEXT("unchanged") : *ToFStr(Br.ownership_reason),
		Br.local_support_before, Br.local_support_after, Br.gold_before, Br.gold_after,
		Br.defender_reward_support > 0
			? *FString::Printf(TEXT(" · defender reward +%d Support"), Br.defender_reward_support)
			: TEXT(""),
		Count(Br.losses, "continental"), Count(Br.wounded, "continental"), Count(Br.losses, "british"),
		Count(Br.wounded, "british"), Spent(Br.cartridges_spent, "continental"),
		Spent(Br.cartridges_spent, "british"),
		Spent(Br.powder_spent, "continental") + Spent(Br.powder_spent, "british"), Morale,
		Br.objective_holder.empty() ? TEXT("—") : *ToFStr(Br.objective_holder), *Reasons));
}

FText UPA250MapWidget::LedgerText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::CampaignState& St = S->GetState();
	FString Lines = FString::Printf(TEXT("Treasury 🪙 %d · Support %d\n"), St.player_gold, St.local_support);
	Lines += TEXT("Recent changes:\n");
	const int32 Start = FMath::Max(0, static_cast<int32>(St.resource_changes.size()) - 12);
	for (int32 i = Start; i < static_cast<int32>(St.resource_changes.size()); ++i)
	{
		Lines += TEXT("  ") + ToFStr(St.resource_changes[static_cast<size_t>(i)].reason) + TEXT("\n");
	}
	if (St.resource_changes.empty()) Lines += TEXT("  (none yet)\n");
	Lines += PendingText().ToString();
	Lines += TEXT("\nProvinces · dev · cart/musk/prov\n");
	for (const auto& Kv : St.provinces)
	{
		if (Kv.second.owner != St.player_faction) continue;
		const pa250::ProvinceStores& Ps = pa250::province_ledger(St, Kv.first);
		Lines += FString::Printf(TEXT("%s · %d · %d/%d/%d\n"), *ToFStr(Kv.second.name),
								 pa250::province_development(Kv.second), Ps.cartridges, Ps.muskets, Ps.provisions);
	}
	bool bAny = false;
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status != "in_transit" && Sh.status != "committed") continue;
		if (!bAny) Lines += TEXT("In transit:\n");
		bAny = true;
		Lines += FString::Printf(TEXT("  %s → %s · arrives %s\n"),
								 *ToFStr(Sh.source_province.empty() ? std::string("market") : Sh.source_province),
								 *ToFStr(Sh.dest_province),
								 *S->FormatWhen(Sh.deliver_on_hour));
	}
	for (const auto& Job : St.progress.cartridge_jobs)
	{
		if (Job.status != "underway") continue;
		Lines += FString::Printf(TEXT("Cartridges being made @ %s: +%d, ready %s\n"), *ToFStr(Job.province_id),
								 Job.cartridges_out, *S->FormatWhen(Job.complete_on_hour));
	}
	return FText::FromString(Lines);
}

FText UPA250MapWidget::PendingText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	FString Lines = TEXT("Pending:\n");
	bool bAny = false;
	for (const auto& Letter : St.staff.queue)
	{
		if (Letter.resolve_on_hour <= 0) continue;
		if (Letter.status == "delivered" || Letter.status == "refused" || Letter.status == "dismissed") continue;
		if (Letter.awaiting_choice || Letter.status == "needs_approval") continue;
		if (Letter.resolve_on_hour <= St.now.hour_index) continue;
		const int64 HoursLeft = Letter.resolve_on_hour - St.now.hour_index;
		Lines += FString::Printf(TEXT("  %s (%s) — %lldh · %s\n"), *ToFStr(Letter.subject), *ToFStr(Letter.kind),
								 static_cast<long long>(HoursLeft), *ToFStr(Letter.reason));
		bAny = true;
	}
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status != "in_transit" && Sh.status != "committed") continue;
		if (Sh.deliver_on_hour <= St.now.hour_index) continue;
		Lines += FString::Printf(TEXT("  Shipment %s → %s — %lldh\n"), *ToFStr(Sh.id), *ToFStr(Sh.dest_province),
								 static_cast<long long>(Sh.deliver_on_hour - St.now.hour_index));
		bAny = true;
	}
	if (!bAny) Lines += TEXT("  (none)\n");
	return FText::FromString(Lines);
}

FText UPA250MapWidget::DevelopTip(const FString& Track) const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FText::FromString(TEXT("Select a province you hold."));
	}
	return FText::FromString(ToFStr(pa250::development_preview(S->GetState(), ToStd(SelectedProvinceId), ToStd(Track))));
}

FText UPA250MapWidget::AftermathTip(const FString& Choice) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(ToFStr(pa250::aftermath_choice_preview(S->GetState(), ToStd(Choice))));
}

FText UPA250MapWidget::SupportText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::CampaignState& St = S->GetState();
	auto Get = [](const std::map<std::string, int>& M, const char* K) {
		auto It = M.find(K);
		return It == M.end() ? 0 : It->second;
	};
	FString Treaties;
	for (const auto& T : St.treaties)
	{
		Treaties += FString::Printf(TEXT("%s%s %s"), Treaties.IsEmpty() ? TEXT("") : TEXT(", "),
									UTF8_TO_TCHAR(T.type.c_str()), *UPA250Subsystem::FormatHoursRemaining(T.hours_remaining));
	}
	(void)Get;
	return FText::FromString(FString::Printf(TEXT("Support %d · Attitude %d%s"), St.local_support,
											 Get(St.relations, "british"),
											 Treaties.IsEmpty() ? TEXT("") : *FString(TEXT(" · ") + Treaties)));
}

FText UPA250MapWidget::AboutText() const
{
	return FText::FromString(TEXT(
		"About this map\n\n"
		"Source: “A plan of the town and harbour of Boston and the country adjacent with the road from Boston to "
		"Concord”, J. De Costa, engraved by Charles Hall, issued 29 July 1775. Working image: Library of Congress "
		"copy of the same plate; planned source NYPL Digital Collections item 46abf040-c5ed-012f-b389-58d385a7bc34.\n\n"
		"Historical-date caveat: this July 1775 engraving includes later siege information. Its printed soldiers, "
		"camps and battle annotations are historical artwork — not live units and not verified April 19 positions. "
		"A hollow diamond and a soft colour wash mark who holds a town: blue Continentals (yours), red British. The "
		"wash follows the period practice of hand-colouring and marks the town only; the 1775 plate shows no town "
		"boundaries, so none are drawn. An army is a round "
		"counter with a bar at wide zoom and, where artwork exists for it, a small illustration when zoomed in, "
		"labeled Yours or British — "
		"click it, then click a town to march.\n\n"
		"Army illustrations are modern artistic reconstructions in an engraved style, not period images and not a "
		"uniform reference for any particular unit; the number of figures drawn does not show strength. A British "
		"column is drawn marching only while it is on the march, and firing only while a battle is pending; a British "
		"force at rest keeps the counter. The provincial picture shows provincial regulars and is used only for a "
		"Continental force made up mostly of line infantry; a militia force keeps the counter (there is no militia "
		"illustration). "
		"The game date follows the campaign clock."));
}

FText UPA250MapWidget::DisclaimerText() const
{
	return AboutText();
}

FText UPA250MapWidget::StatusText() const
{
	const FString Line = !LastMessage.IsEmpty() ? LastMessage : Message;
	return FText::FromString(Line.IsEmpty()
								 ? TEXT("Select a town or army. Drag or arrow keys to pan, wheel to zoom, Space pauses, Esc clears.")
								 : Line);
}

FText UPA250MapWidget::LocationPanelText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	const std::string Pid = ToStd(SelectedProvinceId);
	auto Pit = St.provinces.find(Pid);
	if (Pit == St.provinces.end())
	{
		return FText::GetEmpty();
	}
	const pa250::Province& Prov = Pit->second;
	auto OwnerLabel = [](const std::string& Owner) -> FString {
		if (Owner == "british")
		{
			return TEXT("British");
		}
		if (Owner == "continental")
		{
			return TEXT("Continentals — yours");
		}
		return UTF8_TO_TCHAR(Owner.c_str());
	};
	FString Lines;
	Lines += FString::Printf(TEXT("Held by: %s\n"), *OwnerLabel(Prov.owner));
	{
		const pa250::ProvinceStores& Ps = pa250::province_ledger(St, Pid);
		Lines += FString::Printf(
			TEXT("Stores — powder %d · cartridges %d · muskets %d\nbayonets %d · provisions %d · lead %d · paper %d\n"),
			Ps.powder, Ps.cartridges, Ps.muskets, Ps.bayonets, Ps.provisions, Ps.lead, Ps.paper);
	}

	// Selected friendly army first, with the details the counter hides at rest.
	if (!SelectedArmyId.IsEmpty())
	{
		auto AIt = St.armies.find(ToStd(SelectedArmyId));
		if (AIt != St.armies.end() && AIt->second.province == Pid && AIt->second.faction == St.player_faction)
		{
			int32 Strength = 0;
			for (const auto& U : AIt->second.units)
			{
				Strength += U.second;
			}
			const FString Orders = AIt->second.path.empty() ? TEXT("Holding position") : TEXT("On the march");
			int32 Sick = 0, Wounded = 0;
			for (const auto& U : AIt->second.sick) Sick += U.second;
			for (const auto& U : AIt->second.wounded) Wounded += U.second;
			Lines += FString::Printf(
				TEXT("\nSelected force\n%s — %d fit · %d sick · %d wounded\nMorale %d · Readiness %d · Fatigue %d\nOrders: %s\n"),
				UTF8_TO_TCHAR(AIt->second.name.c_str()), Strength, Sick, Wounded, AIt->second.morale,
				AIt->second.readiness, AIt->second.fatigue, *Orders);
		}
	}

	bool bAnyArmy = false;
	Lines += TEXT("\nForces here\n");
	for (const auto& Kv : St.armies)
	{
		if (Kv.second.province != Pid)
		{
			continue;
		}
		bAnyArmy = true;
		const bool bPlayer = Kv.second.faction == St.player_faction;
		if (bPlayer)
		{
			int32 Strength = 0;
			for (const auto& U : Kv.second.units)
			{
				Strength += U.second;
			}
			Lines += FString::Printf(TEXT("• %s — %d men (yours)\n"), UTF8_TO_TCHAR(Kv.second.name.c_str()), Strength);
		}
		else
		{
			// Confirmed presence only; enemy strength stays unreported.
			Lines += FString::Printf(TEXT("• %s — British, strength not reported\n"),
									 UTF8_TO_TCHAR(Kv.second.name.c_str()));
		}
	}
	if (!bAnyArmy)
	{
		Lines += TEXT("No forces known to be present.\n");
		// Say where your forces actually are, so an empty town is not a dead end.
		Lines += TEXT("\nYour forces elsewhere\n");
		bool bElsewhere = false;
		for (const auto& Kv : St.armies)
		{
			if (Kv.second.faction != St.player_faction) continue;
			bElsewhere = true;
			Lines += FString::Printf(TEXT("• %s — at %s · %d fit · %s\n"), *ToFStr(Kv.second.name),
									 *ProvName(St, Kv.second.province), FitCount(Kv.second),
									 *OrdersText(St, Kv.second));
		}
		if (bElsewhere)
		{
			Lines += TEXT("Select one in Your forces (top-left), then click a town to march.\n");
		}
	}

	Lines += TEXT("\nReports\n");
	if (St.pending_battle.active && St.pending_battle.province == Pid)
	{
		Lines += TEXT("Armies in contact — battle pending.\n");
	}
	else if (St.ai_objective == Pid)
	{
		Lines += TEXT("Reported enemy interest in this town (unconfirmed).\n");
	}
	else
	{
		Lines += TEXT("No threats reported.\n");
	}

	if (bShowDev)
	{
		Lines += FString::Printf(TEXT("\n[Dev] province=%s selected_army=%s staff_queue=%d\n"), *SelectedProvinceId,
								 SelectedArmyId.IsEmpty() ? TEXT("none") : *SelectedArmyId,
								 static_cast<int32>(St.staff.queue.size()));
		if (APA250ProvinceMapActor* A = MapActor.Get())
		{
			Lines += FString::Printf(TEXT("[Dev] marker verified=%s zoom=%.2f centre=(%.3f, %.3f)\n"),
									 A->IsProvinceVerified(SelectedProvinceId) ? TEXT("true") : TEXT("false"),
									 A->GetZoomFactor(), A->GetZoomCenterUV().X, A->GetZoomCenterUV().Y);
		}
	}
	return FText::FromString(Lines);
}

bool UPA250MapWidget::CanEnterBattleAtSelection() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty() || !S->IsBattlePending())
	{
		return false;
	}
	return ToFStr(S->GetState().pending_battle.province) == SelectedProvinceId;
}

FText UPA250MapWidget::BattleText() const
{
	UPA250Subsystem* S = Sub();
	if (S && S->IsBattlePending())
	{
		const auto& St = S->GetState();
		// begin_battle fills the controller cache only when the player clicks To arms.
		// The broadside is on screen while the battle is still pending, so build the
		// read-only setup here or tickets and modifier notes stay empty.
		const pa250::BattleSetup Setup =
			pa250::build_battle_setup(St, St.pending_battle.province);
		auto It = St.provinces.find(St.pending_battle.province);
		const FString Name =
			It == St.provinces.end() ? TEXT("?") : UTF8_TO_TCHAR(It->second.name.c_str());
		FString Lines = FString::Printf(TEXT("Battle of %s\n"), *Name);
		auto SideBlock = [&](const char* Faction, const TCHAR* Label) {
			auto Sit = Setup.sides.find(Faction);
			if (Sit == Setup.sides.end()) return;
			int32 Total = 0;
			FString Classes;
			for (const auto& Kv : Sit->second.tickets)
			{
				if (Kv.second <= 0) continue;
				Total += Kv.second;
				Classes += FString::Printf(TEXT("%s %d · "), *ToFStr(Kv.first), Kv.second);
			}
			Lines += FString::Printf(TEXT("%s: %d tickets (%scart %d, morale %d, ready %d)\n"), Label, Total,
									 *Classes, Sit->second.cartridge_budget, Sit->second.morale,
									 Sit->second.readiness);
		};
		SideBlock(pa250::kContinental, TEXT("Continental"));
		SideBlock(pa250::kBritish, TEXT("British"));
		if (!Setup.modifier_notes.empty())
		{
			Lines += TEXT("Modifiers:\n");
			for (const std::string& Note : Setup.modifier_notes) Lines += TEXT("  · ") + ToFStr(Note) + TEXT("\n");
		}
		else
		{
			Lines += TEXT("No building/event modifiers.\n");
		}
		return FText::FromString(Lines);
	}
	return FText::GetEmpty();
}

FText UPA250MapWidget::StaffPanelText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->HasStaffEvent())
	{
		return FText::GetEmpty();
	}
	const FString Subject = S->GetPendingLetterSubject();
	const FString Body = S->GetPendingLetterBody();
	const FString Clerk = ToFStr(S->GetState().staff.clerk_name);
	return FText::FromString(FString::Printf(
		TEXT("%s — correspondence\n\n%s\n\n%s"), *Clerk, *Subject, *Body));
}

void UPA250MapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	const int32 Epoch = S->GetHudEpoch();
	if (Epoch == PaintedHudEpoch)
	{
		return;
	}
	PaintedHudEpoch = Epoch;
	InvalidateLayoutAndVolatility();
}

void UPA250MapWidget::NotifyMapSelectionChanged()
{
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		A->OnHudSelectionChanged(SelectedArmyId, SelectedProvinceId);
	}
}

void UPA250MapWidget::ClearSelection()
{
	SelectedArmyId.Reset();
	SelectedProvinceId.Reset();
	LastMessage = TEXT("Orders cleared.");
	NotifyMapSelectionChanged();
}

void UPA250MapWidget::SetStatusMessage(const FString& Msg)
{
	LastMessage = Msg;
	Message = Msg;
	InvalidateLayoutAndVolatility();
}

FString UPA250MapWidget::ClickProvince(const FString& ProvinceId)
{
	UPA250Subsystem* S = Sub();
	if (!S || ProvinceId.IsEmpty())
	{
		return FString();
	}
	const pa250::CampaignState& St = S->GetState();
	const std::string Pid = ToStd(ProvinceId);
	if (!St.provinces.count(Pid))
	{
		return FString();
	}
	if (S->GetFlow() != EPA250Flow::Campaign || S->IsBattlePending())
	{
		LastMessage = TEXT("Resolve the pending battle first.");
		return LastMessage;
	}

	FString OwnArmy;
	for (const auto& KV : St.armies)
	{
		if (KV.second.province == Pid && KV.second.faction == St.player_faction)
		{
			OwnArmy = ToFStr(KV.first);
			break;
		}
	}

	if (!SelectedArmyId.IsEmpty())
	{
		const pa250::Army& A = St.armies.at(ToStd(SelectedArmyId));
		FString Err;
		if (Pid == A.province)
		{
			SelectedProvinceId = ProvinceId;
			LastMessage = TEXT("Selected ") + ToFStr(A.name) + TEXT(" at ") + ToFStr(St.provinces.at(A.province).name) +
						  TEXT(". Click a province to march.");
		}
		else if (S->QueueMove(SelectedArmyId, ProvinceId, Err))
		{
			const pa250::PathPlan Plan = pa250::plan_path(St, ToStd(SelectedArmyId), Pid);
			const FString When = Plan.ok ? S->FormatWhen(Plan.arrival.hour_index) : FString();
			FString Route;
			for (int32 i = 0; i < Plan.provinces.size(); ++i)
			{
				Route += (i ? TEXT(" → ") : TEXT("")) + ProvName(St, Plan.provinces[i]);
			}
			LastMessage = ToFStr(A.name) + TEXT(" → ") + ToFStr(St.provinces.at(Pid).name) +
						  (When.IsEmpty() ? TEXT(".") : TEXT(". Arrives ") + When + TEXT(".")) +
						  (Route.IsEmpty() ? FString() : TEXT(" Route: ") + Route);
			SelectedProvinceId = ProvinceId;
		}
		else
		{
			LastMessage = TEXT("Illegal move: ") + Err;
		}
	}
	else if (!OwnArmy.IsEmpty())
	{
		SelectedArmyId = OwnArmy;
		SelectedProvinceId = ProvinceId;
		const pa250::Army& Own = St.armies.at(ToStd(OwnArmy));
		LastMessage = TEXT("Selected ") + ToFStr(Own.name) + TEXT(" at ") + ToFStr(St.provinces.at(Own.province).name) +
					  TEXT(". Click a province to march.");
	}
	else
	{
		SelectedProvinceId = ProvinceId;
		LastMessage = ToFStr(St.provinces.at(Pid).name) + TEXT(" (") + ToFStr(St.provinces.at(Pid).owner) + TEXT(")");
	}
	NotifyMapSelectionChanged();
	return LastMessage;
}

FReply UPA250MapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	const FKey Arrows[] = {EKeys::Left, EKeys::Right, EKeys::Up, EKeys::Down};
	for (int32 i = 0; i < 4; ++i)
	{
		if (Key == Arrows[i])
		{
			HeldArrows |= static_cast<uint8>(1 << i);
			return FReply::Handled();
		}
	}
	if (Key == EKeys::SpaceBar)
	{
		S->SetPaused(!S->IsPaused());
		return FReply::Handled();
	}
	const FKey SpeedKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven };
	for (int32 i = 0; i < UE_ARRAY_COUNT(SpeedKeys) && i < FPA250CampaignSpeed::Count; ++i)
	{
		if (Key == SpeedKeys[i])
		{
			S->SetSpeedIndex(i);
			return FReply::Handled();
		}
	}
	if (Key == EKeys::Equals || Key == EKeys::Add)
	{
		S->SetSpeedIndex(S->GetSpeedIndex() + 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
	{
		S->SetSpeedIndex(S->GetSpeedIndex() - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		// Layered: close transient panels first, then clear selection. Pause is
		// untouched (Space owns it), so Escape never changes the clock.
		if (bShowAbout)
		{
			bShowAbout = false;
			return FReply::Handled();
		}
		if (bShowLedger)
		{
			bShowLedger = false;
			return FReply::Handled();
		}
		ClearSelection();
		return FReply::Handled();
	}
	if (Key == EKeys::RightMouseButton)
	{
		ClearSelection();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UPA250MapWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const FKey Arrows[] = {EKeys::Left, EKeys::Right, EKeys::Up, EKeys::Down};
	for (int32 i = 0; i < 4; ++i)
	{
		if (Key == Arrows[i])
		{
			HeldArrows &= static_cast<uint8>(~(1 << i));
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void UPA250MapWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
	HeldArrows = 0; // a key released while unfocused must not leave the map scrolling
	Super::NativeOnFocusLost(InFocusEvent);
}

FSlateRect UPA250MapWidget::GetTopStripScreenRect() const
{
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return FSlateRect(0.f, 0.f, Size.X, TopStripHeightPx);
}

FSlateRect UPA250MapWidget::GetBottomStripScreenRect() const
{
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return FSlateRect(0.f, Size.Y - BottomStripHeightPx, Size.X, Size.Y);
}

TSharedRef<SWidget> UPA250MapWidget::RebuildWidget()
{
	if (UPA250Subsystem* S = Sub())
	{
		S->EnsureCampaign();
	}

	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 13);
	const FSlateFontInfo BroadsideFont = FCoreStyle::GetDefaultFontStyle("Bold", 26);

	auto MakeButton = [this, &BodyFont](const TCHAR* Label, TFunction<void()> Fn,
	                                    TAttribute<EVisibility> Vis = EVisibility::Visible,
	                                    TAttribute<FText> Tip = TAttribute<FText>()) {
		return SNew(SButton)
			.Visibility(Vis)
			.IsFocusable(false)
			.ToolTipText(Tip)
			.OnClicked_Lambda([Fn]() {
				Fn();
				return FReply::Handled();
			})
			.ContentPadding(FMargin(9.f, 4.f))[SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont)];
	};
	TAttribute<EVisibility> BattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsBattlePending()) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> NoBattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsBattlePending()) ? EVisibility::Collapsed : EVisibility::Visible;
	});
	TAttribute<EVisibility> StaffVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->HasStaffEvent() && !S2->IsBattlePending()) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> AftermathChoicesVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || S2->IsBattlePending() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		const FString Kind = S2->GetPendingLetterKind();
		return (Kind == TEXT("request_assistance") || Kind == TEXT("purchase") || Kind.IsEmpty())
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	// Commander orders belong only to the detached commander's own letter; a chapter
	// briefing shares the commander_report kind but is only ever acknowledged.
	auto IsDetachedCommanderLetter = [](UPA250Subsystem* S2) {
		return S2->GetPendingLetterKind() == TEXT("commander_report") &&
			   S2->GetPendingLetterSubject().Contains(TEXT("Detached"));
	};
	TAttribute<EVisibility> CommanderChoicesVis = TAttribute<EVisibility>::CreateLambda([this, IsDetachedCommanderLetter]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		return IsDetachedCommanderLetter(S2) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> ChapterChoicesVis = TAttribute<EVisibility>::CreateLambda([this, IsDetachedCommanderLetter]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		return (S2->GetPendingLetterKind() == TEXT("commander_report") && !IsDetachedCommanderLetter(S2))
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> KnoxChoicesVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		return S2->GetPendingLetterKind() == TEXT("knox_support") ? EVisibility::Visible : EVisibility::Collapsed;
	});
	// Delivered / refused letters are notices: one button that clears the popup.
	TAttribute<EVisibility> NoticeVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->HasStaffEvent() && !S2->IsBattlePending() && !S2->IsPendingLetterAwaitingChoice())
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	auto SourceKindAwaiting = [](UPA250Subsystem* S2, const TCHAR* Kind) {
		return S2 && S2->HasStaffEvent() && !S2->IsBattlePending() && S2->IsPendingLetterAwaitingChoice() &&
			   S2->GetPendingLetterKind() == Kind;
	};
	TAttribute<EVisibility> HistoricalNoticeVis = TAttribute<EVisibility>::CreateLambda([this, SourceKindAwaiting]() {
		return SourceKindAwaiting(Sub(), TEXT("historical_notice")) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> SourceL3Vis = TAttribute<EVisibility>::CreateLambda([this, SourceKindAwaiting]() {
		UPA250Subsystem* S2 = Sub();
		return (SourceKindAwaiting(S2, TEXT("source_decision")) && S2->GetPendingLetterSourceId() == TEXT("L3"))
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> SourceL5Vis = TAttribute<EVisibility>::CreateLambda([this, SourceKindAwaiting]() {
		UPA250Subsystem* S2 = Sub();
		return (SourceKindAwaiting(S2, TEXT("source_decision")) && S2->GetPendingLetterSourceId() == TEXT("L5"))
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> SourceL7Vis = TAttribute<EVisibility>::CreateLambda([this, SourceKindAwaiting]() {
		UPA250Subsystem* S2 = Sub();
		return (SourceKindAwaiting(S2, TEXT("source_decision")) && S2->GetPendingLetterSourceId() == TEXT("L7"))
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> ViewSourceVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->HasStaffEvent() && !S2->GetPendingLetterSourceUrl().IsEmpty()) ? EVisibility::Visible
																						: EVisibility::Collapsed;
	});

	TAttribute<EVisibility> LocationVis = TAttribute<EVisibility>::CreateLambda([this]() {
		// The battle broadside owns the right side while a battle is pending;
		// the province window must never sit on top of it.
		UPA250Subsystem* S2 = Sub();
		if (S2 && S2->IsBattlePending()) return EVisibility::Collapsed;
		return SelectedProvinceId.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	});
	TAttribute<EVisibility> ForcesVis = TAttribute<EVisibility>::CreateLambda([this]() {
		// Staff correspondence and battles take the screen; the forces list returns after.
		UPA250Subsystem* S2 = Sub();
		if (S2 && (S2->IsBattlePending() || S2->HasStaffEvent())) return EVisibility::Collapsed;
		return EVisibility::Visible;
	});
	TAttribute<EVisibility> ArmyBannerVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (!SelectedArmyId.IsEmpty() && S2 && !S2->IsBattlePending()) ? EVisibility::Visible
																		   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> SelectArmyHereVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || SelectedProvinceId.IsEmpty() || !S2->GetController().has_campaign()) return EVisibility::Collapsed;
		const pa250::CampaignState& St = S2->GetState();
		for (const auto& Kv : St.armies)
		{
			if (Kv.second.province == ToStd(SelectedProvinceId) && Kv.second.faction == St.player_faction &&
				ToFStr(Kv.first) != SelectedArmyId)
			{
				return EVisibility::Visible;
			}
		}
		return EVisibility::Collapsed;
	});
	TAttribute<EVisibility> EnterBattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return CanEnterBattleAtSelection() ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> AboutVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowAbout ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> LedgerVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowLedger ? EVisibility::Visible : EVisibility::Collapsed;
	});

	const FLinearColor Ink(0.12f, 0.08f, 0.04f);
	const FLinearColor Cream(0.93f, 0.87f, 0.70f);
	const FLinearColor Gold(0.85f, 0.65f, 0.25f);
	const FLinearColor DarkBar(0.10f, 0.08f, 0.06f, 0.97f);
	const FLinearColor DarkPanel(0.13f, 0.10f, 0.08f, 0.96f);
	auto MakeGroup = [&BodyFont, Cream](TFunction<FText()> Fn, const TCHAR* Tip) {
		return SNew(STextBlock)
			.Font(BodyFont)
			.ColorAndOpacity(Cream)
			.ToolTipText(FText::FromString(Tip))
			.Text_Lambda(MoveTemp(Fn));
	};
	auto PanelText = [&BodyFont, Cream](TFunction<FText()> Fn) {
		return SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).AutoWrapText(true).Text_Lambda(MoveTemp(Fn));
	};

	// Transparent centre — the 3D province map is the only map layer.
	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .BorderBackgroundColor(DarkBar)
				   .Padding(FMargin(14.f, 8.f))
					   [SNew(SHorizontalBox) +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(HeaderFont)
								 .ColorAndOpacity(Gold)
								 .Text(FText::FromString(TEXT("⚜ Play America 250")))] +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(HeaderFont)
								 .ColorAndOpacity(Cream)
								 .ToolTipText(FText::FromString(TEXT("Campaign date, pause state and clock speed. Space pauses; 1–7 set speed.")))
								 .Text_Lambda([this]() { return DateText(); })] +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0).VAlign(VAlign_Center)
							[MakeGroup([this]() { return TreasuryText(); }, TEXT("Gold: spend on purchases, development and diplomacy."))] +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0).VAlign(VAlign_Center)
							[MakeGroup([this]() { return StoresText(); }, TEXT("Selected province stores and shipments in transit."))] +
						SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[MakeGroup([this]() { return SupportText(); }, TEXT("Local support, British attitude, treaties."))] +
						SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[MakeButton(TEXT("📖 Ledger"), [this]() { PressLedgerToggle(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 0.f, 48.f, 0.f))
				  [SNew(SBorder)
					   .Visibility(BattleVis)
					   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					   .BorderBackgroundColor(FLinearColor(0.94f, 0.88f, 0.74f, 0.96f))
					   .Padding(FMargin(28.f, 18.f))
						   [SNew(SVerticalBox) +
							SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
								[SNew(STextBlock)
									 .Font(BroadsideFont)
									 .ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f))
									 .Text_Lambda([this]() { return BattleText(); })] +
							SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
								[MakeButton(TEXT("To arms!"), [this]() { PressFight(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 84.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(300.f)
					   [SNew(SBorder)
							.Visibility(ForcesVis)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(DarkPanel)
							.Padding(FMargin(10.f, 8.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
										  .ColorAndOpacity(Gold)
										  .Text(FText::FromString(TEXT("Your forces — click to select")))] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
									 [SNew(SButton)
										  .IsFocusable(false)
										  .ContentPadding(FMargin(9.f, 5.f))
										  .OnClicked_Lambda([this]() {
											  FString Id;
											  if (GetPlayerArmyId(0, Id)) PressSelectArmy(Id);
											  return FReply::Handled();
										  })
											  [SNew(STextBlock)
												   .Font(BodyFont)
												   .Text_Lambda([this]() { return ArmyButtonText(0); })]]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Center)
			  .Padding(FMargin(0.f, 84.f, 0.f, 0.f))
				  [SNew(SBorder)
					   .Visibility(ArmyBannerVis)
					   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					   .BorderBackgroundColor(FLinearColor(0.35f, 0.24f, 0.05f, 0.97f))
					   .Padding(FMargin(16.f, 8.f))
						   [SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
								.ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.80f))
								.AutoWrapText(true)
								.Text_Lambda([this]() { return SelectedArmyBannerText(); })]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 78.f, 24.f, 0.f))
				  [SNew(SBox).WidthOverride(336.f)
					   [SNew(SBorder)
							.Visibility(LocationVis)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(DarkPanel)
							.Padding(FMargin(14.f, 12.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
										  .ColorAndOpacity(Gold)
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return ProvinceTitleText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
									 [PanelText([this]() { return LocationPanelText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
									 [SNew(STextBlock)
										  .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
										  .ColorAndOpacity(Gold)
										  .Text(FText::FromString(TEXT("— Development —")))] +
								 SVerticalBox::Slot().AutoHeight()
									 [PanelText([this]() { return DevelopmentText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
									 [MakeButton(TEXT("⚔ Select the army here — then click a town to march"),
												 [this]() { PressSelectArmyAtSelection(); }, SelectArmyHereVis)] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("🌾 Farm"), [this]() { PressDevelop(TEXT("farm")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return DevelopTip(TEXT("farm")); }))] +
									  SHorizontalBox::Slot().FillWidth(0.5f)
										  [MakeButton(TEXT("⚒ Workshop"), [this]() { PressDevelop(TEXT("workshop")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return DevelopTip(TEXT("workshop")); }))]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("⛟ Depot"), [this]() { PressDevelop(TEXT("depot")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return DevelopTip(TEXT("depot")); }))] +
									  SHorizontalBox::Slot().FillWidth(0.5f)
										  [MakeButton(TEXT("🏰 Fort"), [this]() { PressDevelop(TEXT("fort")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return DevelopTip(TEXT("fort")); }))]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Buy cart."), [this]() { PressBuyCartridges(); })] +
									  SHorizontalBox::Slot().FillWidth(0.5f)
										  [MakeButton(TEXT("Make cart."), [this]() { PressMakeCartridges(); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Xfer → depot"), [this]() { PressTransferToDepot(); })] +
									  SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Clear"), [this]() { ClearSelection(); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("⚔ Enter battle"), [this]() { PressEnterBattleAtSelection(); },
													  EnterBattleVis)]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [PanelText([this]() { return BattleReportText(); })]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 196.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(300.f)
					   [SNew(SBorder)
							.Visibility(LedgerVis)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(DarkPanel)
							.Padding(FMargin(12.f, 10.f))
								[SNew(STextBlock)
									 .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
									 .ColorAndOpacity(Cream)
									 .Text_Lambda([this]() { return LedgerText(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(36.f, 0.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(420.f)
					   [SNew(SBorder)
							.Visibility(StaffVis)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.95f, 0.91f, 0.82f, 0.97f))
							.Padding(FMargin(18.f, 14.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(BodyFont)
										  .ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f))
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return StaffPanelText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(AftermathChoicesVis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Purchase"), [this]() { PressLogistics(TEXT("purchase")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return AftermathTip(TEXT("purchase")); }))] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Request aid"),
													  [this]() { PressLogistics(TEXT("request_assistance")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda([this]() {
														  return AftermathTip(TEXT("request_assistance"));
													  }))] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Requisition"),
													  [this]() { PressLogistics(TEXT("requisition")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return AftermathTip(TEXT("requisition")); }))] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Invest capacity"),
													  [this]() { PressLogistics(TEXT("capacity_investment")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda([this]() {
														  return AftermathTip(TEXT("capacity_investment"));
													  }))] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Dismiss"), [this]() { PressLogistics(TEXT("dismiss")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return AftermathTip(TEXT("dismiss")); }))]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(CommanderChoicesVis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Clarify: preserve"),
													  [this]() { PressCommander(TEXT("preserve_force")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Counterorder: contest"),
													  [this]() { PressCommander(TEXT("contest_crossing")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Escalate"), [this]() { PressCommander(TEXT("escalate")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Dismiss"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(KnoxChoicesVis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Support Knox"),
													  [this]() { PressLogistics(TEXT("knox_support")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Decline"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(ChapterChoicesVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Acknowledge"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(SourceL3Vis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Purchase provisions"),
													  [this]() { PressLogistics(TEXT("purchase_provisions")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Shared lodging"),
													  [this]() { PressLogistics(TEXT("shared_lodging")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Retain workers"),
													  [this]() { PressLogistics(TEXT("retain_workers")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(SourceL5Vis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Guarded removal"),
													  [this]() { PressLogistics(TEXT("guarded_removal")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Negotiate locally"),
													  [this]() { PressLogistics(TEXT("negotiate_local")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Evacuate / keep boats"),
													  [this]() { PressLogistics(TEXT("evacuate_preserve_boats")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(SourceL7Vis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Request transfer"),
													  [this]() { PressLogistics(TEXT("request_transfer")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Fund procurement"),
													  [this]() { PressLogistics(TEXT("fund_procurement")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Conserve ammunition"),
													  [this]() { PressLogistics(TEXT("conserve_ammunition")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(HistoricalNoticeVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Acknowledge"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(ViewSourceVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("View original source"), [this]() {
											  if (UPA250Subsystem* S2 = Sub())
											  {
												  S2->OpenPendingLetterSourceUrl();
											  }
										  })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(NoticeVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Acknowledge"), [this]() { PressLogistics(TEXT("dismiss")); })]]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(24.f, 96.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(400.f)
					   [SNew(SBorder)
							.Visibility(AboutVis)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.95f, 0.91f, 0.82f, 0.97f))
							.Padding(FMargin(16.f, 12.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(BodyFont)
										  .ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f))
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return AboutText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [MakeButton(TEXT("Close"), [this]() { PressAboutToggle(); })]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Bottom)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 0.f, 12.f, 56.f))
				  [SNew(SBorder)
					   .Visibility(NoBattleVis)
					   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					   .BorderBackgroundColor(DarkPanel)
					   .Padding(FMargin(6.f, 5.f))
						   [SNew(SHorizontalBox) +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("⌂ Overview"), [this]() { PressResetOverview(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("◎ Focus"), [this]() { PressFocusSelected(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("＋"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get()) A->ZoomBy(1.4f);
								})] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("－"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get()) A->ZoomBy(1.f / 1.4f);
								})] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Parley"), [this]() { PressParley(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Save"), [this]() { PressSave(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Load"), [this]() { PressLoad(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("About"), [this]() { PressAboutToggle(); })] +
							SHorizontalBox::Slot().AutoWidth()
								[MakeButton(TEXT("Dev"), [this]() { PressDevToggle(); })]]]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .BorderBackgroundColor(DarkBar)
				   .Padding(FMargin(14.f, 6.f))
					   [SNew(SHorizontalBox) +
						SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(BodyFont)
								 .ColorAndOpacity(Cream)
								 .Text_Lambda([this]() { return StatusText(); })] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Pause / Resume"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetPaused(!S2->IsPaused());
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Speed −"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetSpeedIndex(S2->GetSpeedIndex() - 1);
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Speed +"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetSpeedIndex(S2->GetSpeedIndex() + 1);
									}
								},
								NoBattleVis)]]];
}

void UPA250MapWidget::PressEndTurn()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->EndTurn(Err))
	{
		ClearSelection();
		LastMessage = S->IsBattlePending() ? TEXT("The armies have met! Prepare for battle.") : S->FormatDate();
		NotifyMapSelectionChanged();
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressParley()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->BeginDiplomacy(TEXT("british"), Err))
	{
		S->TravelToFlowLevel(this);
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressFight()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->BeginBattle(Err))
	{
		S->TravelToFlowLevel(this);
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressResetOverview()
{
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		A->ResetOverview();
		SetStatusMessage(TEXT("Campaign overview: Boston to Concord."));
	}
}

void UPA250MapWidget::PressFocusSelected()
{
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		if (SelectedProvinceId.IsEmpty())
		{
			SetStatusMessage(TEXT("Select a town or army first, then Focus selected."));
			return;
		}
		A->FocusSelected();
		SetStatusMessage(TEXT("Focused on the selection."));
	}
}

void UPA250MapWidget::PressLedgerToggle()
{
	bShowLedger = !bShowLedger;
	SetStatusMessage(bShowLedger ? TEXT("Ledger open — every province you hold, its development and stores.")
								 : TEXT("Ledger closed."));
}

void UPA250MapWidget::PressAboutToggle()
{
	bShowAbout = !bShowAbout;
}

void UPA250MapWidget::PressDevToggle()
{
	bShowDev = !bShowDev;
	SetStatusMessage(bShowDev ? TEXT("Developer diagnostics on (selection panel).") : TEXT("Developer diagnostics off."));
}

bool UPA250MapWidget::IsPointOverHud(FVector2D ScreenPos) const
{
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	if (ScreenPos.Y <= TopStripHeightPx || ScreenPos.Y >= Size.Y - BottomStripHeightPx)
	{
		return true;
	}
	// Selection panel (top-right), ledger (top-left, when open), toolbar (bottom-right).
	if (!SelectedProvinceId.IsEmpty() && ScreenPos.X >= Size.X - 384.f && ScreenPos.Y >= TopStripHeightPx &&
		ScreenPos.Y <= TopStripHeightPx + 620.f)
	{
		return true;
	}
	if (ScreenPos.X <= 332.f && ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 110.f)
	{
		return true; // Your forces panel
	}
	if (bShowLedger && ScreenPos.X <= 332.f && ScreenPos.Y >= TopStripHeightPx + 110.f &&
		ScreenPos.Y <= TopStripHeightPx + 480.f)
	{
		return true;
	}
	if (ScreenPos.X >= Size.X - 720.f && ScreenPos.Y >= Size.Y - BottomStripHeightPx - 64.f &&
		ScreenPos.Y <= Size.Y - BottomStripHeightPx)
	{
		return true;
	}
	if (bShowAbout && ScreenPos.X <= 448.f && ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 420.f)
	{
		return true;
	}
	return false;
}

void UPA250MapWidget::PressEnterBattleAtSelection()
{
	if (!CanEnterBattleAtSelection())
	{
		SetStatusMessage(TEXT("Enter battle only when contact is pending at the selected town."));
		return;
	}
	PressFight();
}

void UPA250MapWidget::PressSave()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	const bool bOk = S && S->SaveCampaign(Err);
	SetStatusMessage(bOk ? TEXT("Campaign saved.") : TEXT("Save failed: ") + Err);
}

void UPA250MapWidget::PressLoad()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	const bool bOk = S && S->LoadCampaign(Err);
	ClearSelection();
	SetStatusMessage(bOk ? TEXT("Campaign loaded.") : TEXT("Load failed: ") + Err);
	NotifyMapSelectionChanged();
}

void UPA250MapWidget::PressBuyCartridges()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	FString Err;
	if (S->ApprovePurchaseOrder(SelectedProvinceId, 0, 20, 0, 0, 0, 0, 0, Err))
	{
		SetStatusMessage(TEXT("Purchase approved — cartridges in transit (not delivered yet)."));
	}
	else
	{
		SetStatusMessage(TEXT("Purchase failed: ") + Err);
	}
}

void UPA250MapWidget::PressTransferToDepot()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	const FString Depot = ToFStr(pa250::default_depot_province(S->GetState(), pa250::kContinental));
	if (Depot == SelectedProvinceId)
	{
		SetStatusMessage(TEXT("Already at the depot province."));
		return;
	}
	FString Err;
	if (S->ApproveTransferOrder(SelectedProvinceId, Depot, 2, 0, 0, 0, 0, 0, 0, Err))
	{
		SetStatusMessage(TEXT("Transfer departed — source debited now; depot credits on arrival."));
	}
	else
	{
		SetStatusMessage(TEXT("Transfer failed: ") + Err);
	}
}

void UPA250MapWidget::PressDevelop(const FString& Track)
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	FString Err;
	if (S->DevelopProvince(SelectedProvinceId, Track, Err))
	{
		const pa250::CampaignState& St = S->GetState();
		auto Pit = St.provinces.find(ToStd(SelectedProvinceId));
		const int32 Dev = Pit == St.provinces.end() ? 0 : pa250::province_development(Pit->second);
		FString Reason = St.resource_changes.empty()
							 ? FString::Printf(TEXT("Developed %s (%s)"), *SelectedProvinceId, *Track)
							 : ToFStr(St.resource_changes.back().reason);
		SetStatusMessage(FString::Printf(TEXT("%s — development now %d. Gold %d."), *Reason, Dev, St.player_gold));
	}
	else if (Err == TEXT("not_your_province"))
	{
		SetStatusMessage(TEXT("You can only develop a province you hold."));
	}
	else if (Err == TEXT("max_level"))
	{
		SetStatusMessage(TEXT("That track is already at its maximum level."));
	}
	else if (Err == TEXT("insufficient_gold"))
	{
		SetStatusMessage(TEXT("Not enough gold to develop that track."));
	}
	else
	{
		SetStatusMessage(TEXT("Development failed: ") + Err);
	}
}

void UPA250MapWidget::PressMakeCartridges()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	FString ArmyId = SelectedArmyId;
	if (ArmyId.IsEmpty())
	{
		const std::string Pid = ToStd(SelectedProvinceId);
		for (const auto& KV : S->GetState().armies)
		{
			if (KV.second.province == Pid && KV.second.faction == S->GetState().player_faction)
			{
				ArmyId = ToFStr(KV.first);
				break;
			}
		}
	}
	if (ArmyId.IsEmpty())
	{
		SetStatusMessage(TEXT("Need a friendly army in this province to assign workers."));
		return;
	}
	FString Err;
	if (S->StartCartridgeCraft(SelectedProvinceId, ArmyId, 2, 1, 1, 1, Err))
	{
		SetStatusMessage(TEXT("Cartridge job underway — workers unavailable until it finishes."));
	}
	else
	{
		SetStatusMessage(TEXT("Cartridge job failed: ") + Err);
	}
}

void UPA250MapWidget::PressLogistics(const FString& Choice)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	FString Err;
	const FString LetterId = S->GetPendingLetterId();
	if (S->ChooseLogistics(LetterId, Choice, Err))
	{
		const pa250::CampaignState& St = S->GetState();
		FString Msg = St.resource_changes.empty()
						  ? FString::Printf(TEXT("Staff action: %s."), *Choice)
						  : ToFStr(St.resource_changes.back().reason);
		Msg += TEXT(" Check Pending in the Ledger for timed follow-ups.");
		SetStatusMessage(Msg);
	}
	else
	{
		SetStatusMessage(TEXT("Staff action failed: ") + Err);
	}
}

void UPA250MapWidget::PressCommander(const FString& Action)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	FString Err;
	if (S->ResolveCommander(Action, Err))
	{
		if (Action == TEXT("escalate"))
		{
			SetStatusMessage(TEXT("Escalated. If this company loses the field, the town is forfeit even without the post."));
		}
		else
		{
			SetStatusMessage(FString::Printf(TEXT("Orders to detached command: %s."), *Action));
		}
	}
	else
	{
		SetStatusMessage(TEXT("Commander order failed: ") + Err);
	}
}
