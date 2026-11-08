#include "PA250BattleHUD.h"
#include "MusketWeapon.h"
#include "PA250BattleGameMode.h"
#include "PA250ControlPoint.h"
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
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 14);
	TWeakObjectPtr<UPA250BattleHUD> WeakThis(this);

	auto Top = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		if (!GM) return FText::GetEmpty();
		FString Posts;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : GM->GetPosts())
		{
			const APA250ControlPoint* P = WP.Get();
			if (!P) continue;
			const FName H = P->GetHolder();
			const TCHAR* Mark = H == TEXT("british") ? TEXT("B") : H == TEXT("continental") ? TEXT("C") : TEXT("-");
			Posts += FString::Printf(TEXT(" %s[%s]"), *P->PostId.ToString(), Mark);
		}
		return FText::FromString(FString::Printf(TEXT("BRITISH %d     CONTINENTAL %d    %s     Time %.0fs"),
			GM->TicketsRemaining(TEXT("british")), GM->TicketsRemaining(TEXT("continental")),
			*Posts, GM->TimeRemaining()));
	};

	auto Capture = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		const APlayerController* PC = WeakThis.IsValid() ? WeakThis->GetOwningPlayer() : nullptr;
		const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (!GM || !Me) return FText::GetEmpty();
		FString Extra;
		if (Me->IsInLine())
		{
			Extra = FString::Printf(TEXT("  In Line (%d)"), Me->GetLineSize());
		}
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : GM->GetPosts())
		{
			APA250ControlPoint* P = WP.Get();
			if (!P) continue;
			if (FVector::Dist2D(Me->GetActorLocation(), P->GetActorLocation()) > P->Radius) continue;
			if (P->IsContested())
			{
				return FText::FromString(FString::Printf(TEXT("%s contested%s"), *P->PostId.ToString(), *Extra));
			}
			if (P->GetCaptureProgress() > 0.01f && P->GetCapturer() == Me->FactionId)
			{
				return FText::FromString(FString::Printf(TEXT("Capturing %s %.0f%%%s"), *P->PostId.ToString(), P->GetCaptureProgress() * 100.f, *Extra));
			}
			if (P->GetHolder() == Me->FactionId)
			{
				return FText::FromString(FString::Printf(TEXT("Holding %s%s"), *P->PostId.ToString(), *Extra));
			}
		}
		return Extra.IsEmpty() ? FText::GetEmpty() : FText::FromString(Extra.TrimStart());
	};

	auto Reload = [WeakThis]() {
		const APlayerController* PC = WeakThis.IsValid() ? WeakThis->GetOwningPlayer() : nullptr;
		const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (!Me || !Me->Musket) return FText::GetEmpty();
		if (Me->Musket->IsJammed()) return FText::FromString(TEXT("JAMMED — hold R to clear"));
		if (Me->Musket->CanFire()) return FText::FromString(TEXT("Musket loaded - fire!"));
		const float P = Me->Musket->GetReloadProgress();
		const float Ss = Me->Musket->GetSweetStart();
		const float Se = Me->Musket->GetSweetEnd();
		const float Ps = Me->Musket->GetPerfectStart();
		const float Pe = Me->Musket->GetPerfectEnd();
		const TCHAR* Zone = (P >= Ps && P <= Pe) ? TEXT("PERFECT") : (P >= Ss && P <= Se) ? TEXT("SWEET") : TEXT("...");
		return FText::FromString(FString::Printf(TEXT("%s  %.0f%%  [%s]  (tap R)"), *Me->Musket->GetReloadPhaseLabel(), P * 100.f, Zone));
	};

	auto Banner = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		return GM ? FText::FromString(GM->GetBannerText()) : FText::GetEmpty();
	};

	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center)
		[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f)).Padding(FMargin(18.f, 8.f))
			[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor::White).Text_Lambda(Top)]]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center).Padding(0, 48, 0, 0)
		[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.6f)).Text_Lambda(Capture)]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0, 0, 0, 40)
		[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.7f)).ShadowOffset(FVector2D(1, 1)).Text_Lambda(Reload)]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center)
		[SNew(STextBlock).Font(Big).ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.3f)).ShadowOffset(FVector2D(2, 2)).Text_Lambda(Banner)];
}