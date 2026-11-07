#include "PA250MapWidget.h"
#include "Campaign/PA250Subsystem.h"
#include "SPA250Map.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
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
	auto Get = [](const std::map<std::string, int>& M, const char* K) { auto It = M.find(K); return It == M.end() ? 0 : It->second; };
	FString Treaties;
	for (const auto& T : St.treaties)
	{
		Treaties += FString::Printf(TEXT("%s%s(%d)"), Treaties.IsEmpty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(T.type.c_str()), T.turns_remaining);
	}
	return FText::FromString(FString::Printf(TEXT("Turn %d   |   Supplies  Continental %d  British %d   |   British attitude %d   |   Treaties: %s"),
		St.turn, Get(St.supplies, "continental"), Get(St.supplies, "british"), Get(St.relations, "british"),
		Treaties.IsEmpty() ? TEXT("none") : *Treaties));
}

FText UPA250MapWidget::StatusText() const
{
	FString Line = Message;
	if (MapWidget.IsValid() && !MapWidget->LastMessage.IsEmpty())
	{
		Line = MapWidget->LastMessage;
	}
	return FText::FromString(Line.IsEmpty() ? TEXT("Click your army, then an adjacent province, then End Turn.") : Line);
}

FText UPA250MapWidget::BattleText() const
{
	UPA250Subsystem* S = Sub();
	if (S && S->IsBattlePending())
	{
		const auto& St = S->GetState();
		auto It = St.provinces.find(St.pending_battle.province);
		return FText::FromString(FString::Printf(TEXT("Battle of %s"), It == St.provinces.end() ? TEXT("?") : UTF8_TO_TCHAR(It->second.name.c_str())));
	}
	return FText::GetEmpty();
}

TSharedRef<SWidget> UPA250MapWidget::RebuildWidget()
{
	UPA250Subsystem* S = Sub();
	if (S)
	{
		S->EnsureCampaign();
	}
	MapWidget = SNew(SPA250Map).Subsystem(S);

	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 13);
	const FSlateFontInfo BigFont = FCoreStyle::GetDefaultFontStyle("Bold", 28);

	auto MakeButton = [this, &BodyFont](const TCHAR* Label, TFunction<void()> Fn, TAttribute<EVisibility> Vis = EVisibility::Visible) {
		return SNew(SButton)
			.Visibility(Vis)
			.OnClicked_Lambda([Fn]() { Fn(); return FReply::Handled(); })
			.ContentPadding(FMargin(14.f, 6.f))
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont)];
	};
	TAttribute<EVisibility> BattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsBattlePending()) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> NoBattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsBattlePending()) ? EVisibility::Collapsed : EVisibility::Visible;
	});

	return SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.05f, 0.04f, 0.03f)).Padding(0)[MapWidget.ToSharedRef()]]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Fill)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.05f, 0.04f, 0.03f, 0.82f)).Padding(FMargin(14.f, 8.f))
			[SNew(STextBlock).Font(HeaderFont).ColorAndOpacity(FLinearColor(0.95f, 0.9f, 0.75f)).Text_Lambda([this]() { return HeaderText(); })]
		]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center)
		[
			SNew(SBorder).Visibility(BattleVis).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.35f, 0.02f, 0.02f, 0.9f)).Padding(FMargin(40.f, 20.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(BigFont).ColorAndOpacity(FLinearColor::White).Text_Lambda([this]() { return BattleText(); })]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 0)[MakeButton(TEXT("To arms!"), [this]() { PressFight(); })]
			]
		]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Fill)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.05f, 0.04f, 0.03f, 0.85f)).Padding(FMargin(14.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Font(BodyFont).ColorAndOpacity(FLinearColor(0.95f, 0.9f, 0.75f)).Text_Lambda([this]() { return StatusText(); })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeButton(TEXT("End Turn"), [this]() { PressEndTurn(); }, NoBattleVis)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeButton(TEXT("Parley with the British envoy"), [this]() { PressParley(); }, NoBattleVis)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeButton(TEXT("Save"), [this]() { PressSave(); })]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)[MakeButton(TEXT("Load"), [this]() { PressLoad(); })]
			]
		];
}

void UPA250MapWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MapWidget.Reset();
}

FString UPA250MapWidget::ClickProvince(const FString& ProvinceId)
{
	return MapWidget.IsValid() ? MapWidget->HandleProvinceClick(ProvinceId) : FString();
}

void UPA250MapWidget::PressEndTurn()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->EndTurn(Err))
	{
		if (MapWidget.IsValid())
		{
			MapWidget->ClearSelection();
			MapWidget->InvalidateOverlay();
			MapWidget->LastMessage = S->IsBattlePending() ? TEXT("The armies have met! Prepare for battle.") : FString::Printf(TEXT("Turn %d."), S->GetTurn());
		}
	}
	else
	{
		SetMessage(Err);
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
		SetMessage(Err);
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
		SetMessage(Err);
	}
}

void UPA250MapWidget::PressSave()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	const bool bOk = S && S->SaveCampaign(Err);
	SetMessage(bOk ? TEXT("Campaign saved.") : TEXT("Save failed: ") + Err);
	if (MapWidget.IsValid()) MapWidget->LastMessage.Reset();
}

void UPA250MapWidget::PressLoad()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	const bool bOk = S && S->LoadCampaign(Err);
	SetMessage(bOk ? TEXT("Campaign loaded.") : TEXT("Load failed: ") + Err);
	if (MapWidget.IsValid())
	{
		MapWidget->LastMessage.Reset();
		MapWidget->ClearSelection();
		MapWidget->InvalidateOverlay();
	}
}
