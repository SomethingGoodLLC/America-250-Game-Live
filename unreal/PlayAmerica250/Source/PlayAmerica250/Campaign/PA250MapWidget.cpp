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
}

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
	auto Store = [&](const char* Faction, const char* Field) -> int32 {
		auto It = St.stores.find(Faction);
		if (It == St.stores.end())
		{
			return 0;
		}
		if (std::strcmp(Field, "powder") == 0)
		{
			return It->second.powder;
		}
		if (std::strcmp(Field, "arms") == 0)
		{
			return It->second.arms;
		}
		return It->second.provisions;
	};
	return FText::FromString(FString::Printf(
		TEXT("%s   |   %s  speed %s   |   Gold %d   |   Powder %d  Arms %d  Prov %d   |   Local support %d   |   Supplies Cont %d  Brit %d   |   Attitude %d   |   Treaties: %s"),
		*Date, Pause, *Sp, St.player_gold, Store("continental", "powder"), Store("continental", "arms"),
		Store("continental", "provisions"), St.local_support, Get(St.supplies, "continental"), Get(St.supplies, "british"),
		Get(St.relations, "british"), Treaties.IsEmpty() ? TEXT("none") : *Treaties));
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
	return FText::FromString(FString::Printf(TEXT("Treasury: Gold %d"), S->GetState().player_gold));
}

FText UPA250MapWidget::StoresText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	auto It = St.stores.find("continental");
	if (It == St.stores.end())
	{
		return FText::FromString(TEXT("Stores: —"));
	}
	return FText::FromString(FString::Printf(TEXT("Stores: Powder %d · Arms %d · Provisions %d"), It->second.powder,
											 It->second.arms, It->second.provisions));
}

FText UPA250MapWidget::SupportText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
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
	return FText::FromString(FString::Printf(TEXT("Support: Local %d · Supplies C %d / B %d · British attitude %d · Treaties: %s"),
											 St.local_support, Get(St.supplies, "continental"), Get(St.supplies, "british"),
											 Get(St.relations, "british"), Treaties.IsEmpty() ? TEXT("none") : *Treaties));
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
		"A hollow ring marks who holds a town: blue Continentals (yours), red British. A solid flag is an army, "
		"labeled Yours or British. The game date follows the campaign clock."));
}

FText UPA250MapWidget::DisclaimerText() const
{
	return AboutText();
}

FText UPA250MapWidget::StatusText() const
{
	const FString Line = !LastMessage.IsEmpty() ? LastMessage : Message;
	return FText::FromString(Line.IsEmpty()
								 ? TEXT("Select a town or army. Drag to pan, wheel to zoom, Space pauses, Esc clears.")
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
	Lines += FString::Printf(TEXT("%s\nHeld by: %s\n"), UTF8_TO_TCHAR(Prov.name.c_str()), *OwnerLabel(Prov.owner));

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
			Lines += FString::Printf(TEXT("\nSelected force\n%s — %d men\nMorale %d · Readiness %d · Fatigue %d\nOrders: %s\n"),
									 UTF8_TO_TCHAR(AIt->second.name.c_str()), Strength, AIt->second.morale,
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
		auto It = St.provinces.find(St.pending_battle.province);
		const FString Preset = ToFStr(pa250::scenario_preset_for_province(St.pending_battle.province));
		FString Rule = Preset;
		if (Preset == TEXT("concord_bridge"))
		{
			Rule = TEXT("North Bridge — one crossing");
		}
		else if (Preset == TEXT("battle_road"))
		{
			Rule = TEXT("Battle Road — preserve the force");
		}
		else if (Preset == TEXT("lexington_common"))
		{
			Rule = TEXT("Lexington Green");
		}
		return FText::FromString(FString::Printf(TEXT("Battle of %s\n%s"),
		                                         It == St.provinces.end() ? TEXT("?") : UTF8_TO_TCHAR(It->second.name.c_str()),
		                                         *Rule));
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
			LastMessage = ToFStr(A.name) + TEXT(" ordered to ") + ToFStr(St.provinces.at(Pid).name) +
			              (When.IsEmpty() ? TEXT(".") : TEXT(". Arrives ") + When + TEXT("."));
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
	                                    TAttribute<EVisibility> Vis = EVisibility::Visible) {
		return SNew(SButton)
			.Visibility(Vis)
			.IsFocusable(false)
			.OnClicked_Lambda([Fn]() {
				Fn();
				return FReply::Handled();
			})
			.ContentPadding(FMargin(14.f, 6.f))[SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont)];
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
		return SelectedProvinceId.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	});
	TAttribute<EVisibility> EnterBattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return CanEnterBattleAtSelection() ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> AboutVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowAbout ? EVisibility::Visible : EVisibility::Collapsed;
	});

	auto MakeGroup = [&BodyFont](TFunction<FText()> Fn, const TCHAR* Tip) {
		return SNew(STextBlock)
			.Font(BodyFont)
			.ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
			.ToolTipText(FText::FromString(Tip))
			.Text_Lambda(MoveTemp(Fn));
	};

	// Transparent centre — the 3D province map is the only map layer.
	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .BorderBackgroundColor(FLinearColor(0.93f, 0.88f, 0.74f, 0.94f))
				   .Padding(FMargin(14.f, 8.f))
					   [SNew(SHorizontalBox) +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(HeaderFont)
								 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
								 .ToolTipText(FText::FromString(TEXT("Campaign date, pause state and clock speed. Space pauses; 1–7 set speed.")))
								 .Text_Lambda([this]() { return DateText(); })] +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0).VAlign(VAlign_Center)
							[MakeGroup([this]() { return TreasuryText(); }, TEXT("Gold available to the Continental cause. Spent through staff purchases and diplomacy."))] +
						SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0).VAlign(VAlign_Center)
							[MakeGroup([this]() { return StoresText(); }, TEXT("Usable war materials held by the Continental forces: powder, arms and provisions."))] +
						SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[MakeGroup([this]() { return SupportText(); }, TEXT("Local support, supply accrual for each side, British attitude, and treaties currently in force."))]]]
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
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 78.f, 24.f, 0.f))
				  [SNew(SBox).WidthOverride(360.f)
					   [SNew(SBorder)
							.Visibility(LocationVis)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.95f, 0.91f, 0.82f, 0.96f))
							.Padding(FMargin(14.f, 12.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(BodyFont)
										  .ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f))
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return LocationPanelText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Clear"), [this]() { ClearSelection(); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Enter battle"), [this]() { PressEnterBattleAtSelection(); },
													  EnterBattleVis)]]]]]
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
										  [MakeButton(TEXT("Purchase"), [this]() { PressLogistics(TEXT("purchase")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Request aid"),
													  [this]() { PressLogistics(TEXT("request_assistance")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Requisition"),
													  [this]() { PressLogistics(TEXT("requisition")); })] +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Invest capacity"),
													  [this]() { PressLogistics(TEXT("capacity_investment")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Dismiss"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
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
			  .Padding(FMargin(0.f, 0.f, 16.f, 64.f))
				  [SNew(SBorder)
					   .Visibility(NoBattleVis)
					   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					   .BorderBackgroundColor(FLinearColor(0.93f, 0.88f, 0.74f, 0.88f))
					   .Padding(FMargin(8.f, 8.f))
						   [SNew(SVerticalBox) +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Campaign overview"), [this]() { PressResetOverview(); })] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Focus selected"), [this]() { PressFocusSelected(); })] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Zoom +"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get())
									{
										A->ZoomBy(1.4f);
									}
								})] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Zoom −"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get())
									{
										A->ZoomBy(1.f / 1.4f);
									}
								})] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Parley"), [this]() { PressParley(); })] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Save"), [this]() { PressSave(); })] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("Load"), [this]() { PressLoad(); })] +
							SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[MakeButton(TEXT("About this map"), [this]() { PressAboutToggle(); })] +
							SVerticalBox::Slot().AutoHeight()
								[MakeButton(TEXT("Dev"), [this]() { PressDevToggle(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Bottom)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 0.f, 0.f, 56.f))
				  [SNew(SBorder)
					   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					   .BorderBackgroundColor(FLinearColor(0.93f, 0.88f, 0.74f, 0.94f))
					   .Padding(FMargin(10.f, 6.f))
						   [SNew(SHorizontalBox) +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
								[SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
									 [SNew(SBorder)
										  .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
										  .BorderBackgroundColor(FLinearColor(0.10f, 0.28f, 0.72f))]] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
								[SNew(STextBlock)
									 .Font(BodyFont)
									 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
									 .Text(FText::FromString(TEXT("Your towns")))] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
								[SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
									 [SNew(SBorder)
										  .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
										  .BorderBackgroundColor(FLinearColor(0.78f, 0.12f, 0.10f))]] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
								[SNew(STextBlock)
									 .Font(BodyFont)
									 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
									 .Text(FText::FromString(TEXT("British towns")))] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
								[SNew(SBox).WidthOverride(16.f).HeightOverride(10.f)
									 [SNew(SBorder)
										  .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
										  .BorderBackgroundColor(FLinearColor(0.12f, 0.34f, 0.82f))]] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
								[SNew(STextBlock)
									 .Font(BodyFont)
									 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
									 .Text(FText::FromString(TEXT("Your army")))] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
								[SNew(SBox).WidthOverride(16.f).HeightOverride(10.f)
									 [SNew(SBorder)
										  .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
										  .BorderBackgroundColor(FLinearColor(0.82f, 0.14f, 0.11f))]] +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[SNew(STextBlock)
									 .Font(BodyFont)
									 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
									 .Text(FText::FromString(TEXT("British army")))]]]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .BorderBackgroundColor(FLinearColor(0.93f, 0.88f, 0.74f, 0.94f))
				   .Padding(FMargin(14.f, 6.f))
					   [SNew(SHorizontalBox) +
						SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(BodyFont)
								 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
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
	// Selection panel (top-right) and utility stack (bottom-right).
	if (!SelectedProvinceId.IsEmpty() && ScreenPos.X >= Size.X - 408.f && ScreenPos.Y >= TopStripHeightPx &&
		ScreenPos.Y <= TopStripHeightPx + 340.f)
	{
		return true;
	}
	if (ScreenPos.X >= Size.X - 220.f && ScreenPos.Y >= Size.Y - BottomStripHeightPx - 380.f)
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
		SetStatusMessage(FString::Printf(TEXT("Staff action: %s. Advance time for replies and deliveries."), *Choice));
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
