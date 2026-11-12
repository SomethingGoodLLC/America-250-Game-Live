#include "PA250MapWidget.h"
#include "Campaign/PA250Subsystem.h"
#include "PA250ProvinceMapActor.h"
#include "UI/PA250CityViewWidget.h"
#include "UI/PA250HudStyle.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"
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
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
	const int32 Net = S->GetMonthlyNetGold();
	return FText::FromString(FString::Printf(TEXT("Treasury  %d gold   %s%d a month"), S->GetState().player_gold,
											 Net >= 0 ? TEXT("+") : TEXT("−"), FMath::Abs(Net)));
}

void UPA250MapWidget::RefreshTreasuryCache() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign() || S->GetHudEpoch() == TreasuryEpoch) return;
	TreasuryEpoch = S->GetHudEpoch();
	TreasuryLabels.Reset();
	TreasuryAmounts.Reset();
	TreasuryNote.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(S->GetTreasuryJson());
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;
	auto Row = [this](const FString& Label, const FString& Amount) {
		TreasuryLabels += Label + TEXT("\n");
		TreasuryAmounts += Amount + TEXT("\n");
	};
	auto Signed = [](int32 V) { return FString::Printf(TEXT("%s%d"), V >= 0 ? TEXT("+") : TEXT("−"), FMath::Abs(V)); };
	const int32 Debt = Root->GetIntegerField(TEXT("debt"));
	Row(TEXT("Balance"), FString::Printf(TEXT("%d gold"), Root->GetIntegerField(TEXT("gold"))));
	Row(FString::Printf(TEXT("Debt on bills of credit (limit %d)"), Root->GetIntegerField(TEXT("credit_limit"))),
		FString::Printf(TEXT("%d gold"), Debt));
	Row(TEXT(""), TEXT(""));
	Row(TEXT("INCOME, EACH MONTH"), TEXT(""));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("income")))
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		if (L.IsValid()) Row(TEXT("   ") + L->GetStringField(TEXT("label")), Signed(L->GetIntegerField(TEXT("gold"))));
	}
	Row(TEXT(""), TEXT(""));
	Row(TEXT("EXPENSES, EACH MONTH"), TEXT(""));
	const TArray<TSharedPtr<FJsonValue>>& Expenses = Root->GetArrayField(TEXT("expenses"));
	for (const TSharedPtr<FJsonValue>& V : Expenses)
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		if (L.IsValid()) Row(TEXT("   ") + L->GetStringField(TEXT("label")), Signed(L->GetIntegerField(TEXT("gold"))));
	}
	if (Expenses.Num() == 0) Row(TEXT("   None"), TEXT(""));
	Row(TEXT(""), TEXT(""));
	Row(TEXT("NET, EACH MONTH"), Signed(Root->GetIntegerField(TEXT("net"))) + TEXT(" gold"));
	LoanStep = Root->GetIntegerField(TEXT("loan_step"));
	LoanInterestPct = Root->GetIntegerField(TEXT("loan_interest_pct"));
	bCanBorrow = Root->GetBoolField(TEXT("can_borrow"));
	bCanRepay = Root->GetBoolField(TEXT("can_repay"));
	TreasuryNote = FString::Printf(TEXT("Also each month, into your towns' magazines: +%d muskets from workshops, +%d provisions from farms.\n"
										"Accounts are settled on the 1st. A shortfall is borrowed up to the limit; beyond it the army goes "
										"unpaid and its morale falls."),
								   Root->GetIntegerField(TEXT("muskets")), Root->GetIntegerField(TEXT("provisions")));
	const int32 Arrears = Root->GetIntegerField(TEXT("arrears_months"));
	if (Arrears > 0) TreasuryNote = FString::Printf(TEXT("THE ARMY IS UNPAID (%d month%s).\n"), Arrears, Arrears == 1 ? TEXT("") : TEXT("s")) + TreasuryNote;
}

void UPA250MapWidget::PressTreasuryToggle()
{
	bShowTreasury = !bShowTreasury;
	InvalidateLayoutAndVolatility();
}

void UPA250MapWidget::PressBorrow()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (!S) return;
	SetStatusMessage(S->TakeLoan(Err) ? S->GetLastResourceChangeReason()
					 : Err.StartsWith(TEXT("credit_exhausted")) ? FString(TEXT("The province's credit is exhausted: hold more towns or redeem bills first."))
					 : TEXT("Cannot borrow: ") + Err);
}

void UPA250MapWidget::PressRepay()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (!S) return;
	SetStatusMessage(S->RepayLoan(Err) ? S->GetLastResourceChangeReason()
					 : Err.StartsWith(TEXT("no_debt")) ? FString(TEXT("Nothing is owed."))
					 : Err.StartsWith(TEXT("insufficient_gold")) ? FString(TEXT("Not enough gold in the treasury to redeem bills."))
					 : TEXT("Cannot repay: ") + Err);
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
	return FText::FromString(FString::Printf(TEXT("Powder %d · Cart %d · Musk %d · Prov %d · in transit %d"), Powder, Cart,
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
	return FText::FromString(FString::Printf(TEXT("%s %s — %s · %d fit · %s"), bSel ? TEXT(">") : TEXT(""),
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
		TEXT("%s selected — at %s · %d fit · %s%s\nClick a town to march there · Esc / right-click cancels"),
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
	OpenCityView(SelectedProvinceId);
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
	const pa250::CampaignState& St = S->GetState();
	auto NameOf = [&St](const std::string& Id) {
		auto It = St.provinces.find(Id);
		return It == St.provinces.end() ? ToFStr(Id) : ToFStr(It->second.name);
	};
	const std::string& Mine = St.player_faction;
	const char* Enemy = Mine == "british" ? "continental" : "british";
	const FString EnemyName = Mine == "british" ? TEXT("Continentals") : TEXT("British");
	const bool bWon = Br.winner == Mine;
	FString Fate;
	if (Br.loser_fate == "retreated" && !Br.retreat_to.empty())
	{
		Fate = FString::Printf(TEXT("Enemy army: retreated to %s"), *NameOf(Br.retreat_to));
		if (!bWon) Fate = FString::Printf(TEXT("Your army: retreated to %s"), *NameOf(Br.retreat_to));
	}
	else if (Br.loser_fate == "shattered")
	{
		Fate = bWon ? TEXT("Enemy army: shattered and dispersed — it will not march again")
					: TEXT("Your army: shattered and dispersed — it will not march again");
	}
	else if (Br.loser_fate == "destroyed")
	{
		Fate = bWon ? TEXT("Enemy army: destroyed to a man") : TEXT("Your army: destroyed to a man");
	}
	else if (!Br.loser_fate.empty() && Br.loser_fate != "none")
	{
		Fate = FString::Printf(TEXT("Loser fate: %s"), *ToFStr(Br.loser_fate));
	}
	FString Reasons;
	for (const std::string& Line : Br.change_reasons)
	{
		Reasons += TEXT("\n  · ") + ToFStr(Line);
	}
	return FText::FromString(FString::Printf(
		TEXT("After-battle report — Battle of %s\n%s\n"
			 "Ground: %s → %s (%s) · Posts held by %s\n"
			 "Support %d → %d · Gold %d → %d%s\n"
			 "— Casualties —\n"
			 "Yours: %d killed · %d wounded\n"
			 "%s: %d killed · %d wounded\n"
			 "By your own hand: %d killed\n"
			 "— Ammunition —\n"
			 "Cartridges (balls): yours %d · enemy %d · Powder: yours %d · enemy %d\n"
			 "Scavenged from the field: +%d cartridges · +%d powder · +%d muskets\n"
			 "Morale: net %+d%s%s"),
		*NameOf(Br.province_id), bWon ? TEXT("Victory") : TEXT("Defeat"), *ToFStr(Br.province_owner_before),
		*ToFStr(Br.province_owner_after),
		Br.ownership_reason.empty() ? TEXT("unchanged") : *ToFStr(Br.ownership_reason),
		Br.objective_holder.empty() ? TEXT("—") : *ToFStr(Br.objective_holder), Br.local_support_before,
		Br.local_support_after, Br.gold_before, Br.gold_after,
		Br.defender_reward_support > 0
			? *FString::Printf(TEXT(" · defender reward +%d Support"), Br.defender_reward_support)
			: TEXT(""),
		Count(Br.losses, Mine.c_str()), Count(Br.wounded, Mine.c_str()), *EnemyName, Count(Br.losses, Enemy),
		Count(Br.wounded, Enemy), Br.personal_kills, Spent(Br.cartridges_spent, Mine.c_str()),
		Spent(Br.cartridges_spent, Enemy), Spent(Br.powder_spent, Mine.c_str()), Spent(Br.powder_spent, Enemy),
		Br.scavenged_cartridges, Br.scavenged_powder, Br.scavenged_muskets, Morale,
		Fate.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\n%s"), *Fate), *Reasons));
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
		"Who holds a town is shown by its diamond and a soft colour wash: a hollow blue diamond for Continentals "
		"(yours), a solid red diamond for the British. The "
		"wash follows the period practice of hand-colouring and marks the town only; the 1775 plate shows no town "
		"boundaries, so none are drawn. An army is a round "
		"counter with a bar at wide zoom and, where artwork exists for it, a small illustration when zoomed in, "
		"labeled Yours or British — "
		"click it, then click a town to march.\n\n"
		"Army illustrations are modern artistic reconstructions in an engraved style, not period images and not a "
		"uniform reference for any particular unit; the number of figures drawn does not show strength. A British "
		"column is drawn marching only while it is on the march, and firing only while a battle is pending; a British "
		"force at rest keeps the counter. The provincial picture shows provincial regulars and is used only for a "
		"Continental force made up mostly of line infantry; any other Continental force is drawn as a painted "
		"militia company (a modern illustration, not in the engraved style). "
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

FString UPA250MapWidget::DevText() const
{
	UPA250Subsystem* S = Sub();
	if (!bShowDev || !S || !S->GetController().has_campaign()) return FString();
	FString Out = FString::Printf(TEXT("[Dev] province=%s army=%s staff_queue=%d"), *SelectedProvinceId,
								  SelectedArmyId.IsEmpty() ? TEXT("none") : *SelectedArmyId,
								  static_cast<int32>(S->GetState().staff.queue.size()));
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		Out += FString::Printf(TEXT(" verified=%s zoom=%.2f centre=(%.3f, %.3f)"),
							   A->IsProvinceVerified(SelectedProvinceId) ? TEXT("true") : TEXT("false"), A->GetZoomFactor(),
							   A->GetZoomCenterUV().X, A->GetZoomCenterUV().Y);
	}
	return Out;
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
	// An army can cease to exist between ticks (absorbed into another, or destroyed in battle).
	if (!SelectedArmyId.IsEmpty() && S->GetController().has_campaign() &&
		!S->GetState().armies.count(ToStd(SelectedArmyId)))
	{
		SelectedArmyId.Reset();
		NotifyMapSelectionChanged();
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
	if (CityView) CityView->CloseCityView();
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
		if (IsCityViewOpen())
		{
			CloseCityView();
			return FReply::Handled();
		}
		if (bShowAbout)
		{
			bShowAbout = false;
			return FReply::Handled();
		}
		if (bShowTreasury)
		{
			bShowTreasury = false;
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

	const FSlateFontInfo HeaderFont = FPA250HudStyle::Font(TEXT("Bold"), 15);
	const FSlateFontInfo BodyFont = FPA250HudStyle::Font(TEXT("Regular"), 13);
	const FSlateFontInfo BroadsideFont = FPA250HudStyle::Font(TEXT("Bold"), 26);

	const FPA250HudStyle& Hud = FPA250HudStyle::Get();
	auto MakeButton = [this, &BodyFont, &Hud](const TCHAR* Label, TFunction<void()> Fn,
	                                    TAttribute<EVisibility> Vis = EVisibility::Visible,
	                                    TAttribute<FText> Tip = TAttribute<FText>()) {
		return SNew(SButton)
			.ButtonStyle(&Hud.Button)
			.Visibility(Vis)
			.IsFocusable(false)
			.ToolTipText(Tip)
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([Fn]() {
				Fn();
				return FReply::Handled();
			})
			.ContentPadding(FMargin(11.f, 4.f))
				[SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont).ColorAndOpacity(FPA250HudStyle::Cream)];
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
	TAttribute<EVisibility> AboutVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowAbout ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> LedgerVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowLedger ? EVisibility::Visible : EVisibility::Collapsed;
	});

	const FLinearColor Cream = FPA250HudStyle::Cream;
	const FLinearColor Gold = FPA250HudStyle::Brass;
	// A value in the top strip sits in its own inset box, as on a ledger rule.
	auto MakeGroup = [&BodyFont, &Hud, Cream](TFunction<FText()> Fn, const TCHAR* Tip) {
		return SNew(SBorder)
			.BorderImage(&Hud.Chip)
			.Padding(FMargin(9.f, 2.f))
			.ToolTipText(FText::FromString(Tip))
			[SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda(MoveTemp(Fn))];
	};
	auto SectionHeader = [&Hud](const TCHAR* Label) {
		return SNew(SBorder)
			.BorderImage(&Hud.Header)
			.Padding(FMargin(9.f, 1.f))
			[SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 12)).ColorAndOpacity(FPA250HudStyle::Brass).Text(FText::FromString(Label))];
	};

	// One row per army the player has (companies raised in towns appear here as they form).
	TSharedRef<SVerticalBox> ForcesList = SNew(SVerticalBox);
	for (int32 Row = 0; Row < MaxForceRows; ++Row)
	{
		ForcesList->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
			[SNew(SButton)
				 .ButtonStyle(&Hud.Button)
				 .IsFocusable(false)
				 .ContentPadding(FMargin(9.f, 5.f))
				 .Visibility_Lambda([this, Row]() {
					 FString Id;
					 return GetPlayerArmyId(Row, Id) ? EVisibility::Visible : EVisibility::Collapsed;
				 })
				 .OnClicked_Lambda([this, Row]() {
					 FString Id;
					 if (GetPlayerArmyId(Row, Id)) PressSelectArmy(Id);
					 return FReply::Handled();
				 })
				 [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(FPA250HudStyle::Cream).Text_Lambda([this, Row]() {
					 return ArmyButtonText(Row);
				 })]];
	}

	// Towns the player holds, with what is under way in each: the quick way back to a town.
	TSharedRef<SVerticalBox> TownsList = SNew(SVerticalBox);
	if (UPA250Subsystem* S3 = Sub(); S3 && S3->GetController().has_campaign())
	{
		TArray<TPair<FString, FString>> Towns; // name, id — by name, so the list reads alphabetically
		for (const auto& Kv : S3->GetState().provinces) Towns.Emplace(ToFStr(Kv.second.name), ToFStr(Kv.first));
		Towns.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });
		for (const TPair<FString, FString>& Town : Towns)
		{
			const FString Id = Town.Value;
			TownsList->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
				[SNew(SButton)
					 .ButtonStyle(&Hud.Button)
					 .IsFocusable(false)
					 .ContentPadding(FMargin(9.f, 3.f))
					 .ToolTipText(FText::FromString(TEXT("Open this town.")))
					 .Visibility_Lambda([this, Id]() { return IsTownHeld(Id) ? EVisibility::Visible : EVisibility::Collapsed; })
					 .OnClicked_Lambda([this, Id]() {
						 SelectedArmyId.Reset();
						 OpenCityView(Id);
						 if (APA250ProvinceMapActor* A = MapActor.Get()) A->OnHudSelectionChanged(SelectedArmyId, SelectedProvinceId);
						 return FReply::Handled();
					 })
					 [SNew(SHorizontalBox) +
					  SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						  [SNew(STextBlock).Font(BodyFont).Text(FText::FromString(Town.Key)).ColorAndOpacity_Lambda([this, Id]() {
							  return FSlateColor(SelectedProvinceId == Id ? FPA250HudStyle::Brass : FPA250HudStyle::Cream);
						  })] +
					  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Regular"), 11)).ColorAndOpacity(FPA250HudStyle::CreamDim).Text_Lambda([this, Id]() {
							  return TownRowStatus(Id);
						  })]]];
		}
	}

	// Date plaque: the date, one pip per clock speed (lit up to the current one), and the state.
	TSharedRef<SHorizontalBox> SpeedPips = SNew(SHorizontalBox);
	for (int32 i = 0; i < FPA250CampaignSpeed::Count; ++i)
	{
		SpeedPips->AddSlot().AutoWidth().Padding(1.5f, 0.f).VAlign(VAlign_Center)
			[SNew(SBox).WidthOverride(6.f).HeightOverride(15.f)
			 [SNew(SImage).Image(&Hud.White).ColorAndOpacity_Lambda([this, i]() {
				 UPA250Subsystem* S2 = Sub();
				 const bool bLit = S2 && i <= S2->GetSpeedIndex();
				 return FSlateColor(!bLit ? FLinearColor(0.f, 0.f, 0.f, 0.55f)
										  : S2->IsPaused() ? FPA250HudStyle::BrassDim : FPA250HudStyle::Brass);
			 })]];
	}
	TSharedRef<SWidget> DatePlaque =
		SNew(SBorder)
			.BorderImage(&Hud.Header)
			.Padding(FMargin(12.f, 2.f))
			.ToolTipText(FText::FromString(TEXT("Campaign date, clock speed and pause state. Space pauses; 1–7 set speed.")))
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				 [SNew(STextBlock).Font(HeaderFont).ColorAndOpacity(Cream).Text_Lambda([this]() {
					 UPA250Subsystem* S2 = Sub();
					 return FText::FromString(S2 && S2->GetController().has_campaign() ? S2->FormatDate() : FString(TEXT("No campaign")));
				 })] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[SpeedPips] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				 [SNew(SBox).MinDesiredWidth(64.f)
				  [SNew(STextBlock)
					   .Font(FPA250HudStyle::Font(TEXT("Bold"), 12))
					   .ColorAndOpacity_Lambda([this]() {
						   UPA250Subsystem* S2 = Sub();
						   return FSlateColor(S2 && S2->IsPaused() ? FPA250HudStyle::Red : FPA250HudStyle::Cream);
					   })
					   .Text_Lambda([this]() {
						   UPA250Subsystem* S2 = Sub();
						   if (!S2) return FText::GetEmpty();
						   return FText::FromString(S2->IsPaused() ? FString(TEXT("PAUSED")) : FPA250CampaignSpeed::Format(S2->GetHoursPerSecond()));
					   })]]];

	// Soft dark edge over the map, under every panel: the sheet reads as lit from above on a desk.
	if (!VignetteTexture)
	{
		const int32 N = 256;
		VignetteTexture = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (VignetteTexture)
		{
			VignetteTexture->SRGB = true;
			VignetteTexture->Filter = TF_Bilinear;
			VignetteTexture->AddressX = TA_Clamp;
			VignetteTexture->AddressY = TA_Clamp;
			FTexture2DMipMap& Mip = VignetteTexture->GetPlatformData()->Mips[0];
			FColor* Px = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const float DX = (X + 0.5f) / N * 2.f - 1.f;
					const float DY = (Y + 0.5f) / N * 2.f - 1.f;
					const float A = FMath::SmoothStep(0.62f, 1.38f, FMath::Sqrt(DX * DX + DY * DY)) * 0.62f;
					Px[Y * N + X] = FColor(14, 9, 5, static_cast<uint8>(A * 255.f));
				}
			}
			Mip.BulkData.Unlock();
			VignetteTexture->UpdateResource();
			VignetteBrush.SetResourceObject(VignetteTexture);
			VignetteBrush.ImageSize = FVector2D(N, N);
			VignetteBrush.DrawAs = ESlateBrushDrawType::Image;
		}
	}
	auto PanelText = [&BodyFont, Cream](TFunction<FText()> Fn) {
		return SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).AutoWrapText(true).Text_Lambda(MoveTemp(Fn));
	};

	// Transparent centre — the 3D province map is the only map layer.
	return SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SImage).Image(&VignetteBrush).Visibility(EVisibility::HitTestInvisible)]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Fill)
			  [SNew(SVerticalBox) +
			   SVerticalBox::Slot().AutoHeight()
				   [SNew(SBorder)
						.BorderImage(&Hud.Bar)
						.Padding(FMargin(12.f, 6.f))
							[SNew(SHorizontalBox) +
							 SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0).VAlign(VAlign_Center)
								 [SNew(SBorder).BorderImage(&Hud.Header).Padding(FMargin(12.f, 2.f))
								  [SNew(STextBlock).Font(HeaderFont).ColorAndOpacity(Gold).Text(FText::FromString(TEXT("PLAY AMERICA 250")))]] +
							 SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
								 [SNew(SButton)
									  .ButtonStyle(&Hud.Button)
									  .IsFocusable(false)
									  .ContentPadding(FMargin(10.f, 2.f))
									  .ToolTipText(FText::FromString(TEXT("The treasury: balance, monthly income and expenses, and borrowing. Click to open.")))
									  .OnClicked_Lambda([this]() {
										  PressTreasuryToggle();
										  return FReply::Handled();
									  })
									  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda([this]() { return TreasuryText(); })]] +
							 SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
								 [MakeGroup([this]() { return StoresText(); }, TEXT("Selected province stores and shipments in transit."))] +
							 SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Center)
								 [MakeGroup([this]() { return SupportText(); }, TEXT("Local support, British attitude, treaties."))] +
							 SHorizontalBox::Slot().AutoWidth().Padding(8, 0).VAlign(VAlign_Center)
								 [MakeButton(TEXT("Ledger"), [this]() { PressLedgerToggle(); })] +
							 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[DatePlaque]]] +
			   SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(2.f)[SNew(SImage).Image(&Hud.Rule)]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 0.f, 48.f, 0.f))
				  [SNew(SBorder)
					   .Visibility(BattleVis)
					   .BorderImage(&Hud.Parchment)
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
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(10.f, 8.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SectionHeader(TEXT("YOUR FORCES"))] +
								 SVerticalBox::Slot().AutoHeight()[ForcesList]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 84.f, 16.f, 0.f))
				  [SNew(SBox).WidthOverride(TownsPanelWidth)
					   [SNew(SBorder)
							.Visibility(ForcesVis)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(10.f, 8.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("YOUR TOWNS"))] +
								 SVerticalBox::Slot().AutoHeight()[TownsList]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Center)
			  .Padding(FMargin(0.f, 84.f, 0.f, 0.f))
				  [SNew(SBorder)
					   .Visibility(ArmyBannerVis)
					   .BorderImage(&Hud.Banner)
					   .Padding(FMargin(16.f, 8.f))
						   [SNew(STextBlock)
								.Font(FPA250HudStyle::Font(TEXT("Bold"), 14))
								.ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.80f))
								.AutoWrapText(true)
								.Text_Lambda([this]() { return SelectedArmyBannerText(); })]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Center)
			  .Padding(FMargin(0.f, 84.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(TreasuryPanelWidth).Visibility_Lambda([this]() {
					  UPA250Subsystem* S2 = Sub();
					  return bShowTreasury && S2 && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
				  })
					   [SNew(SBorder)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(14.f, 10.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("THE PROVINCE TREASURY"))] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(1.f)
										  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda([this]() {
											  RefreshTreasuryCache();
											  return FText::FromString(TreasuryLabels);
										  })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Gold).Justification(ETextJustify::Right).Text_Lambda([this]() {
											  RefreshTreasuryCache();
											  return FText::FromString(TreasuryAmounts);
										  })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Italic"), 11))
										  .ColorAndOpacity(FPA250HudStyle::CreamDim)
										  .WrapTextAt(TreasuryPanelWidth - 30.f)
										  .Text_Lambda([this]() {
											  RefreshTreasuryCache();
											  return FText::FromString(TreasuryNote);
										  })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 6, 0)
										  [SNew(SButton)
											   .ButtonStyle(&Hud.Button)
											   .IsFocusable(false)
											   .HAlign(HAlign_Center)
											   .ContentPadding(FMargin(8.f, 4.f))
											   .IsEnabled_Lambda([this]() { RefreshTreasuryCache(); return bCanBorrow; })
											   .ToolTipText_Lambda([this]() {
												   return FText::FromString(FString::Printf(
													   TEXT("Issue bills of credit: gold now, %d%% interest every month until redeemed."), LoanInterestPct));
											   })
											   .OnClicked_Lambda([this]() { PressBorrow(); return FReply::Handled(); })
											   [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda([this]() {
												   return FText::FromString(FString::Printf(TEXT("Borrow %d gold"), LoanStep));
											   })]] +
									  SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 6, 0)
										  [SNew(SButton)
											   .ButtonStyle(&Hud.Button)
											   .IsFocusable(false)
											   .HAlign(HAlign_Center)
											   .ContentPadding(FMargin(8.f, 4.f))
											   .IsEnabled_Lambda([this]() { RefreshTreasuryCache(); return bCanRepay; })
											   .ToolTipText(FText::FromString(TEXT("Redeem bills of credit and stop paying interest on them.")))
											   .OnClicked_Lambda([this]() { PressRepay(); return FReply::Handled(); })
											   [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text(FText::FromString(TEXT("Repay")))]] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Close"), [this]() { PressTreasuryToggle(); })]]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 196.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(300.f)
					   [SNew(SBorder)
							.Visibility(LedgerVis)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(12.f, 10.f))
								[SNew(STextBlock)
									 .Font(FPA250HudStyle::Font(TEXT("Regular"), 12))
									 .ColorAndOpacity(Cream)
									 .Text_Lambda([this]() { return LedgerText(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(36.f, 0.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(420.f)
					   [SNew(SBorder)
							.Visibility(StaffVis)
							.BorderImage(&Hud.Parchment)
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
							.BorderImage(&Hud.Parchment)
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
					   .BorderImage(&Hud.Panel)
					   .Padding(FMargin(6.f, 5.f))
						   [SNew(SHorizontalBox) +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Overview"), [this]() { PressResetOverview(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Focus"), [this]() { PressFocusSelected(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Zoom +"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get()) A->ZoomBy(1.4f);
								})] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Zoom −"), [this]() {
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
				   .BorderImage(&Hud.Bar)
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
	SetStatusMessage(bShowDev ? TEXT("Developer diagnostics on (town panel footer).") : TEXT("Developer diagnostics off."));
}

bool UPA250MapWidget::IsCityViewOpen() const
{
	return CityView && CityView->GetVisibility() != ESlateVisibility::Collapsed &&
		   CityView->GetVisibility() != ESlateVisibility::Hidden;
}

bool UPA250MapWidget::IsPointOverHud(FVector2D ScreenPos) const
{
	// The town panel is docked, not modal: only its own rectangle blocks the map.
	if (CityView && IsCityViewOpen() && CityView->IsViewportPointOverPanel(ScreenPos))
	{
		return true;
	}
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	if (ScreenPos.Y <= TopStripHeightPx || ScreenPos.Y >= Size.Y - BottomStripHeightPx)
	{
		return true;
	}
	// Your-forces list (top-left, one row per army), ledger (when open), toolbar (bottom-right).
	int32 ArmyRows = 0;
	for (FString Id; ArmyRows < MaxForceRows && GetPlayerArmyId(ArmyRows, Id);) ++ArmyRows;
	if (ScreenPos.X <= 332.f && ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 76.f + 34.f * FMath::Max(1, ArmyRows))
	{
		return true;
	}
	if (bShowTreasury && FMath::Abs(ScreenPos.X - Size.X * 0.5f) <= TreasuryPanelWidth * 0.5f + 16.f &&
		ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 430.f)
	{
		return true;
	}
	// Your-towns list (top-right, one row per town held).
	if (ScreenPos.X >= Size.X - (TownsPanelWidth + 32.f) && ScreenPos.Y >= TopStripHeightPx &&
		ScreenPos.Y <= TopStripHeightPx + 50.f + 32.f * HeldTownCount())
	{
		return true;
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

namespace
{
// One purchase order for the selected town; the status line quotes the sim's own ledger entry.
void BuyForTown(UPA250MapWidget* W, UPA250Subsystem* S, const FString& Town, int32 Cartridges, int32 Muskets, int32 Provisions)
{
	if (!S || Town.IsEmpty()) return;
	FString Err;
	if (S->ApprovePurchaseOrder(Town, 0, Cartridges, Muskets, 0, Provisions, 0, 0, Err))
	{
		const FString Entry = S->GetLastResourceChangeReason();
		W->SetStatusMessage((Entry.IsEmpty() ? FString(TEXT("Purchase approved")) : Entry) + TEXT(" — in transit, not delivered yet."));
	}
	else
	{
		W->SetStatusMessage(TEXT("Purchase failed: ") + Err);
	}
}
} // namespace

void UPA250MapWidget::PressBuyCartridges() { BuyForTown(this, Sub(), SelectedProvinceId, BuyCartridgesQty, 0, 0); }
void UPA250MapWidget::PressBuyMuskets() { BuyForTown(this, Sub(), SelectedProvinceId, 0, BuyMusketsQty, 0); }
void UPA250MapWidget::PressBuyProvisions() { BuyForTown(this, Sub(), SelectedProvinceId, 0, 0, BuyProvisionsQty); }

bool UPA250MapWidget::IsTownHeld(const FString& ProvinceId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return false;
	const auto It = S->GetState().provinces.find(ToStd(ProvinceId));
	return It != S->GetState().provinces.end() && It->second.owner == S->GetState().player_faction;
}

int32 UPA250MapWidget::HeldTownCount() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return 0;
	int32 N = 0;
	for (const auto& Kv : S->GetState().provinces) N += Kv.second.owner == S->GetState().player_faction ? 1 : 0;
	return N;
}

FText UPA250MapWidget::TownRowStatus(const FString& ProvinceId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return FText::GetEmpty();
	const pa250::CampaignState& St = S->GetState();
	const std::string Pid = ToStd(ProvinceId);
	// What is under way wins the line; otherwise how many plots are still free.
	TArray<FString> Parts;
	for (const auto& Job : St.progress.construction_jobs)
	{
		if (Job.status == "underway" && Job.province_id == Pid)
			Parts.Add(FString::Printf(TEXT("building %dd"), (Job.hours_remaining + 23) / 24));
	}
	for (const auto& Job : St.progress.recruit_jobs)
	{
		if (Job.province_id == Pid) Parts.Add(FString::Printf(TEXT("muster %dd"), (Job.hours_remaining + 23) / 24));
	}
	if (St.pending_battle.active && St.pending_battle.province == Pid) Parts.Insert(TEXT("BATTLE"), 0);
	if (Parts.Num() > 0) return FText::FromString(FString::Join(Parts, TEXT(" · ")));
	const int32 Free = pa250::province_slot_count(Pid) - pa250::province_slots_occupied(St, Pid);
	return FText::FromString(Free > 0 ? FString::Printf(TEXT("%d free plot%s"), Free, Free == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("full")));
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

FString UPA250MapWidget::HandleTownClicked(const FString& ProvinceId)
{
	// With an army selected, a click on another town is its march order and nothing else. Any
	// other click on a town is "look at this town": select it and show its panel, as a province
	// view opens on a province click in a grand-strategy game.
	bool bMarchOrder = false;
	if (!SelectedArmyId.IsEmpty())
	{
		if (UPA250Subsystem* S = Sub())
		{
			const auto It = S->GetState().armies.find(ToStd(SelectedArmyId));
			bMarchOrder = It != S->GetState().armies.end() && ToFStr(It->second.province) != ProvinceId;
		}
	}
	const FString Result = ClickProvince(ProvinceId);
	if (!bMarchOrder && SelectedProvinceId == ProvinceId)
	{
		OpenCityView(ProvinceId);
	}
	return Result;
}

void UPA250MapWidget::OpenCityView(const FString& ProvinceId)
{
	if (ProvinceId.IsEmpty()) return;
	if (SelectedProvinceId != ProvinceId)
	{
		SelectedProvinceId = ProvinceId;
		NotifyMapSelectionChanged();
	}
	if (!CityView)
	{
		// WBP_CityView (optional) only overrides the header/plot pictures; the layout is Slate.
		UClass* Class = CityViewClass
							? CityViewClass.Get()
							: StaticLoadClass(UPA250CityViewWidget::StaticClass(), nullptr,
											  TEXT("/Game/PA250/UI/CityView/WBP_CityView.WBP_CityView_C"));
		if (!Class) Class = UPA250CityViewWidget::StaticClass();
		CityView = CreateWidget<UPA250CityViewWidget>(GetOwningPlayer(), Class);
		if (CityView)
		{
			CityView->SetHud(this);
			// Closing the panel (X or Esc) ends the selection: the panel is the selection's window.
			CityView->OnClosed.AddUniqueDynamic(this, &UPA250MapWidget::ClearSelection);
			CityView->AddToViewport(40); // below the hover tooltip (50)
		}
	}
	if (!CityView)
	{
		SetStatusMessage(TEXT("Town panel failed to open."));
		return;
	}
	CityView->OpenForProvince(ProvinceId);
}

void UPA250MapWidget::CloseCityView()
{
	if (CityView) CityView->CloseCityView();
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
