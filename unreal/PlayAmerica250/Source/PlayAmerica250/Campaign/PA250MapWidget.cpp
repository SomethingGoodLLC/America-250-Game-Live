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

FText UPA250MapWidget::StatusText() const
{
	const FString Line = !LastMessage.IsEmpty() ? LastMessage : Message;
	return FText::FromString(Line.IsEmpty() ? TEXT("Select an army, then any province. Space pauses. − / + change speed.") : Line);
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
	if (Key == EKeys::RightMouseButton || Key == EKeys::Escape)
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

	// Transparent center — the 3D province map is the only map layer.
	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .BorderBackgroundColor(FLinearColor(0.92f, 0.86f, 0.72f, 0.92f))
				   .Padding(FMargin(14.f, 8.f))
					   [SNew(STextBlock)
							.Font(HeaderFont)
							.ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
							.AutoWrapText(true)
							.Text_Lambda([this]() { return HeaderText(); })]]
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
									 [SNew(SHorizontalBox).Visibility(NoticeVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Acknowledge"), [this]() { PressLogistics(TEXT("dismiss")); })]]]]]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				   .BorderBackgroundColor(FLinearColor(0.92f, 0.86f, 0.72f, 0.92f))
				   .Padding(FMargin(14.f, 8.f))
					   [SNew(SHorizontalBox) +
						SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(BodyFont)
								 .ColorAndOpacity(FLinearColor(0.15f, 0.1f, 0.05f))
								 .Text_Lambda([this]() { return StatusText(); })] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Zoom -"),
								[this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get())
									{
										A->ZoomBy(1.f / 1.4f);
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Zoom +"),
								[this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get())
									{
										A->ZoomBy(1.4f);
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Pause/Resume"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetPaused(!S2->IsPaused());
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Speed−"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetSpeedIndex(S2->GetSpeedIndex() - 1);
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Speed+"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetSpeedIndex(S2->GetSpeedIndex() + 1);
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(TEXT("Parley"), [this]() { PressParley(); }, NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeButton(TEXT("Save"), [this]() { PressSave(); })] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeButton(TEXT("Load"), [this]() { PressLoad(); })]]];
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
