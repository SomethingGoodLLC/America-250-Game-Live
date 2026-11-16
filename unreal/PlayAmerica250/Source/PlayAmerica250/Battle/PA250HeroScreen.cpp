#include "PA250HeroScreen.h"
#include "PA250BattleGameMode.h"
#include "PA250HudStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void UPA250HeroScreen::OpenFor(APlayerController* PC, APA250BattleGameMode* GM)
{
	OwnerPC = PC;
	GameMode = GM;
	// Built before the game mode was known: drop the slate and build it again with the general's words.
	if (IsInViewport())
	{
		RemoveFromParent();
	}
	ReleaseSlateResources(true);
	AddToViewport(110);
	if (PC)
	{
		PC->SetInputMode(FInputModeUIOnly());
		PC->bShowMouseCursor = true;
	}
}

void UPA250HeroScreen::Accept()
{
	APA250BattleGameMode* GM = GameMode.Get();
	APlayerController* PC = OwnerPC.Get();
	if (!GM) return;
	GM->CloseHeroScreen(PC);
	GM->BeginHero(PC);
}

void UPA250HeroScreen::Decline()
{
	if (APA250BattleGameMode* GM = GameMode.Get()) GM->CloseHeroScreen(OwnerPC.Get());
}

TSharedRef<SWidget> UPA250HeroScreen::RebuildWidget()
{
	const FPA250HudStyle& St = FPA250HudStyle::Get();
	const FSlateFontInfo Title = FPA250HudStyle::Font(TEXT("Bold"), 22);
	const FSlateFontInfo Head = FPA250HudStyle::Font(TEXT("Bold"), 15);
	const FSlateFontInfo Body = FPA250HudStyle::Font(TEXT("Regular"), 13);
	const FSlateFontInfo Italic = FPA250HudStyle::Font(TEXT("Italic"), 12);
	TWeakObjectPtr<UPA250HeroScreen> WeakThis(this);
	const APA250BattleGameMode* GM = GameMode.Get();
	const FString TitleText = GM ? GM->GetHeroScreenTitle() : FString();
	const FString KitText = GM ? GM->GetHeroScreenKit() : FString();
	const FString CostText = GM ? GM->GetHeroScreenCost() : FString();

	auto Button = [&St, &Head](const TCHAR* Label, TFunction<void()> OnClick) {
		return SNew(SButton).ButtonStyle(&St.Button).IsFocusable(false).ContentPadding(FMargin(30.f, 9.f))
			.OnClicked_Lambda([OnClick]() {
				OnClick();
				return FReply::Handled();
			})
			[SNew(STextBlock).Font(Head).ColorAndOpacity(FPA250HudStyle::Cream).Text(FText::FromString(Label))];
	};

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(STextBlock).Font(Title).ColorAndOpacity(FPA250HudStyle::Cream).Text(FText::FromString(TitleText))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 12.f)
		[SNew(STextBlock).Font(Italic).ColorAndOpacity(FPA250HudStyle::CreamDim).AutoWrapText(true)
			.Text(FText::FromString((GM && !GM->GetHeroScreenTraits().IsEmpty() ? FString::Printf(TEXT("A %s commander.  "), *GM->GetHeroScreenTraits()) : FString())
				+ TEXT("You have earned the right to fight as him for a time. Your own man fights on in the ranks until you come back to him.")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 11)).ColorAndOpacity(FPA250HudStyle::BrassDim).Text(FText::FromString(TEXT("WHAT HE FIGHTS WITH")))]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(STextBlock).Font(Body).ColorAndOpacity(FPA250HudStyle::Cream).AutoWrapText(true).Text(FText::FromString(KitText))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 4.f)
		[SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 11)).ColorAndOpacity(FPA250HudStyle::BrassDim).Text(FText::FromString(TEXT("WHAT HIS FALL COSTS")))]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(STextBlock).Font(Body).ColorAndOpacity(FPA250HudStyle::Red).AutoWrapText(true).Text(FText::FromString(CostText))]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
			[Button(TEXT("NOT NOW"), [WeakThis]() { if (WeakThis.IsValid()) WeakThis->Decline(); })]
			+ SHorizontalBox::Slot().AutoWidth()
			[Button(TEXT("TAKE COMMAND"), [WeakThis]() { if (WeakThis.IsValid()) WeakThis->Accept(); })]
		];

	return SNew(SBorder)
		.BorderImage(&St.White)
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.62f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(860.f)
			[
				SNew(SBorder).BorderImage(&St.Panel).Padding(FMargin(26.f, 20.f))[Column]
			]
		];
}
