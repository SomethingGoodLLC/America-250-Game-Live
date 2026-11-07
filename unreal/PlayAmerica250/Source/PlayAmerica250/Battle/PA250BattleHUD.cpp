#include "PA250BattleHUD.h"
#include "MusketWeapon.h"
#include "PA250BattleGameMode.h"
#include "RFCharacter.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UPA250BattleHUD::RebuildWidget()
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 34);
	TWeakObjectPtr<UPA250BattleHUD> WeakThis(this);
	auto Top = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		if (!GM) return FText::GetEmpty();
		const FName Holder = GM->GetObjectiveHolder();
		return FText::FromString(FString::Printf(TEXT("BRITISH %d     CONTINENTAL %d     Green: %s (%.0fs)     Time %.0fs"),
			GM->TicketsRemaining(TEXT("british")), GM->TicketsRemaining(TEXT("continental")),
			Holder.IsNone() ? TEXT("contested") : *Holder.ToString(), GM->GetObjectiveHold(), GM->TimeRemaining()));
	};
	auto Reload = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		const APlayerController* PC = WeakThis.IsValid() ? WeakThis->GetOwningPlayer() : nullptr;
		const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (!GM || !Me || !Me->Musket) return FText::GetEmpty();
		return FText::FromString(Me->Musket->CanFire() ? TEXT("Musket loaded - fire!") : FString::Printf(TEXT("Reloading %.0f%%"), Me->Musket->GetReloadProgress() * 100.f));
	};
	auto Banner = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		return GM ? FText::FromString(GM->GetBannerText()) : FText::GetEmpty();
	};
	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center)
		[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f)).Padding(FMargin(18.f, 8.f))
			[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor::White).Text_Lambda(Top)]]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0, 0, 0, 40)
		[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.7f)).ShadowOffset(FVector2D(1, 1)).Text_Lambda(Reload)]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center)
		[SNew(STextBlock).Font(Big).ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.3f)).ShadowOffset(FVector2D(2, 2)).Text_Lambda(Banner)];
}
