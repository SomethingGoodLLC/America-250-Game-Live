#include "PA250DiplomacyWidget.h"
#include "PA250DiplomacyGameMode.h"
#include "Campaign/PA250Subsystem.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void UPA250DiplomacyWidget::SubmitText(const FString& Text)
{
	if (APA250DiplomacyGameMode* GM = GameMode.Get())
	{
		GM->Send(Text);
	}
	if (Input.IsValid())
	{
		Input->SetText(FText::GetEmpty());
	}
}

void UPA250DiplomacyWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Input.Reset();
}

TSharedRef<SWidget> UPA250DiplomacyWidget::RebuildWidget()
{
	TWeakObjectPtr<UPA250DiplomacyWidget> Weak(this);
	const FSlateFontInfo Title = FCoreStyle::GetDefaultFontStyle("Bold", 20);
	const FSlateFontInfo Body = FCoreStyle::GetDefaultFontStyle("Regular", 13);
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FLinearColor Parchment(0.95f, 0.9f, 0.75f);

	auto GM = [Weak]() -> APA250DiplomacyGameMode* { return Weak.IsValid() ? Weak->GameMode.Get() : nullptr; };

	auto Header = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		if (!G) return FText::GetEmpty();
		FString Out = G->EnvoyName + (G->EnvoyTitle.IsEmpty() ? TEXT("") : TEXT(" - ") + G->EnvoyTitle);
		if (const UPA250Subsystem* Sub = UPA250Subsystem::Get(G))
		{
			const auto& St = Sub->GetState();
			auto Fit = St.factions.find("british");
			const bool bTurned = Fit != St.factions.end() && Fit->second.envoy_defected;
			Out += FString::Printf(TEXT("    |    Your treasury: %d gold%s"), St.player_gold, bTurned ? TEXT("    |    HE HAS CHANGED SIDES") : TEXT(""));
			const pa250::DiplomacyContext Ctx = pa250::build_diplomacy_context(St, "british");
			if (Ctx.mood != "calm")
			{
				Out += FString::Printf(TEXT("    |    Mood: %s"), UTF8_TO_TCHAR(Ctx.mood.c_str()));
				if (Ctx.snubbed_hours_left > 0) Out += FString::Printf(TEXT(" (refuses to treat for %dh)"), Ctx.snubbed_hours_left);
			}
			if (St.player_vassal) Out += TEXT("    |    Vassal of the Crown");
		}
		return FText::FromString(Out);
	};
	auto Status = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		if (!G) return FText::GetEmpty();
		switch (G->GetUiState())
		{
		case EPA250DiplomacyState::Connecting: return FText::FromString(TEXT("Connecting to the envoy..."));
		case EPA250DiplomacyState::Unavailable: return FText::FromString(TEXT("The representative is unavailable. You may Leave; nothing has changed."));
		case EPA250DiplomacyState::Talking:
			if (G->IsMicRecording()) return FText::FromString(FString::Printf(TEXT("Listening... %.1fs   (click Stop when you have finished)"), G->GetMicSeconds()));
			if (G->bTranscribing) return FText::FromString(TEXT("Writing down your words..."));
			return FText::FromString(G->bBusy ? TEXT("The envoy considers your words...") : (G->StatusLine.IsEmpty() ? TEXT("Type your words and press Send, or click Speak and talk.") : *G->StatusLine));
		default: return FText::GetEmpty();
		}
	};
	auto TranscriptText = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		if (!G) return FText::GetEmpty();
		const int32 Start = FMath::Max(0, G->Transcript.Num() - 14);
		FString Out;
		for (int32 i = Start; i < G->Transcript.Num(); ++i) Out += G->Transcript[i] + TEXT("\n\n");
		return FText::FromString(Out);
	};
	auto TermsText = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		if (!G || G->TermLines.Num() == 0) return FText::FromString(TEXT("No terms on the table.\n\nAsk for a ceasefire, an exchange of prisoners, or safe passage."));
		FString Out;
		for (const FString& L : G->TermLines) Out += TEXT("- ") + L + TEXT("\n");
		return FText::FromString(Out);
	};
	auto HasTerms = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		return (G && G->TermLines.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
	};
	auto CanTalk = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		return (G && G->GetUiState() == EPA250DiplomacyState::Talking && !G->bBusy);
	};

	auto CanMic = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		return G && (G->IsMicRecording() || (G->GetUiState() == EPA250DiplomacyState::Talking && !G->bBusy && !G->bTranscribing));
	};
	auto MicLabel = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		if (G && G->IsMicRecording())
		{
			const int32 Bars = FMath::Clamp(FMath::RoundToInt(G->GetMicLevel() * 12.f), 0, 6);
			return FText::FromString(FString::Printf(TEXT("Stop  %s%s"), *FString::ChrN(Bars, TEXT('|')), *FString::ChrN(6 - Bars, TEXT('.'))));
		}
		return FText::FromString(G && G->bTranscribing ? TEXT("...") : TEXT("Speak"));
	};
	auto MicColor = [GM]() {
		const APA250DiplomacyGameMode* G = GM();
		return FSlateColor(G && G->IsMicRecording() ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor(0.95f, 0.9f, 0.75f));
	};

	auto Btn = [&Body](const TCHAR* Label, TFunction<void()> Fn, TAttribute<EVisibility> Vis = EVisibility::Visible, TAttribute<bool> Enabled = true) {
		return SNew(SButton).Visibility(Vis).IsEnabled(Enabled).ContentPadding(FMargin(14.f, 6.f))
			.OnClicked_Lambda([Fn]() { Fn(); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(Body)];
	};

	TSharedRef<SEditableTextBox> InputBox = SNew(SEditableTextBox)
		.Font(Body).HintText(FText::FromString(TEXT("Address the envoy...")))
		.IsEnabled_Lambda(CanTalk)
		.OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type Type) {
			if (Type == ETextCommit::OnEnter && Weak.IsValid() && !T.IsEmptyOrWhitespace()) Weak->SubmitText(T.ToString());
		});
	Input = InputBox;

	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.05f, 0.03f, 0.02f, 0.7f)).Padding(FMargin(24.f, 10.f))
			[SNew(STextBlock).Font(Title).ColorAndOpacity(Parchment).Text_Lambda(Header)]
		]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Fill).Padding(20.f)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.07f, 0.04f, 0.025f, 0.78f)).Padding(14.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.62f).Padding(0, 0, 14, 0)
					[SNew(SBox).MinDesiredHeight(190.f).MaxDesiredHeight(190.f)
						[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Font(Body).ColorAndOpacity(Parchment).AutoWrapText(true).Text_Lambda(TranscriptText)]]]
					+ SHorizontalBox::Slot().FillWidth(0.38f)
					[
						SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.2f, 0.13f, 0.06f, 0.85f)).Padding(10.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Bold).ColorAndOpacity(Parchment).Text(FText::FromString(TEXT("Proposed terms")))]
							+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 6)[SNew(STextBlock).Font(Body).ColorAndOpacity(Parchment).AutoWrapText(true).Text_Lambda(TermsText)]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox).Visibility_Lambda(HasTerms)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Btn(TEXT("Accept"), [GM]() { if (auto* G = GM()) G->Accept(); })]
								+ SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("Reject"), [GM]() { if (auto* G = GM()) G->Reject(); })]
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)[InputBox]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton).IsEnabled_Lambda(CanMic).ContentPadding(FMargin(14.f, 6.f))
						.ToolTipText(FText::FromString(TEXT("Click, speak to the envoy, click again to send")))
						.OnClicked_Lambda([GM]() { if (auto* G = GM()) G->ToggleMic(); return FReply::Handled(); })
						[SNew(SBox).MinDesiredWidth(74.f)[SNew(STextBlock).Font(Bold).Justification(ETextJustify::Center).ColorAndOpacity_Lambda(MicColor).Text_Lambda(MicLabel)]]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Btn(TEXT("Send"), [Weak, InputBox]() { if (Weak.IsValid() && !InputBox->GetText().IsEmptyOrWhitespace()) Weak->SubmitText(InputBox->GetText().ToString()); }, EVisibility::Visible, TAttribute<bool>::CreateLambda(CanTalk))]
					+ SHorizontalBox::Slot().AutoWidth()[Btn(TEXT("Leave"), [GM]() { if (auto* G = GM()) G->Leave(); })]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[SNew(STextBlock).Font(Body).ColorAndOpacity(FLinearColor(0.85f, 0.8f, 0.55f)).Text_Lambda(Status)]
			]
		];
}
