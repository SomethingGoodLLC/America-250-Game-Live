#include "PA250BattleHUD.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "MusketWeapon.h"
#include "PA250AimComponent.h"
#include "PA250BattleGameMode.h"
#include "PA250ControlPoint.h"
#include "PA250WeaponTuning.h"
#include "RFCharacter.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "PA250MeleeComponent.h"
#include "PA250VolleyCoordinator.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

void UPA250BattleHUD::ShowHitmarker(bool bKill)
{
	HitmarkerTimer = 0.35f;
	bHitmarkerKill = bKill;
}

bool UPA250BattleHUD::IsReticleHostile() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	return Me && Me->AimComponent && Me->AimComponent->LastResult.bHostileUnderReticle;
}

bool UPA250BattleHUD::IsReticleBlocked() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	return Me && Me->AimComponent && Me->AimComponent->LastResult.bBlocked;
}

float UPA250BattleHUD::GetReticleSpreadPx() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (!Me || !Me->WeaponTuning) return 8.f;
	// Same spread the next shot uses (stance, movement, line bonus), projected through the live FOV.
	const float Deg = Me->AimComponent && Me->AimComponent->LastResult.SpreadDegrees > 0.f
		? Me->AimComponent->LastResult.SpreadDegrees
		: Me->WeaponTuning->Values.CurrentSpreadDegrees(Me->IsAiming(), Me->GetVelocity().Size2D());
	const float Fov = Me->Camera ? Me->Camera->FieldOfView : 80.f;
	const FVector2D View = UWidgetLayoutLibrary::GetViewportSize(const_cast<UPA250BattleHUD*>(this));
	const float Scale = FMath::Max(0.1f, UWidgetLayoutLibrary::GetViewportScale(const_cast<UPA250BattleHUD*>(this)));
	const float HalfW = View.X > 0.f ? 0.5f * View.X / Scale : 640.f;
	const float Px = FMath::Tan(FMath::DegreesToRadians(Deg)) / FMath::Tan(FMath::DegreesToRadians(0.5f * Fov)) * HalfW;
	return FMath::Clamp(Px, 3.f, 120.f);
}

namespace
{
FLinearColor FactionColor(FName F, float Alpha = 1.f)
{
	if (F == TEXT("british")) return FLinearColor(0.78f, 0.08f, 0.06f, Alpha);
	if (F == TEXT("continental")) return FLinearColor(0.12f, 0.28f, 0.85f, Alpha);
	return FLinearColor(0.55f, 0.55f, 0.55f, Alpha);
}

void DrawRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, FVector2D Pos, FVector2D Size, const FLinearColor& C)
{
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
		FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, C);
}

void DrawCenteredText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Text, FVector2D Center,
	const FSlateFontInfo& Font, const FLinearColor& C)
{
	if (Text.IsEmpty()) return;
	const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
	const FVector2D Pos = Center - 0.5f * Size;
	// 1 px shadow so it reads over sky and grass alike.
	FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos + FVector2D(1, 1)))),
		Text, Font, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, C.A * 0.8f));
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
		Text, Font, ESlateDrawEffect::None, C);
}
} // namespace

FString UPA250BattleHUD::PostOrder(FName Me, FName Holder, FName Capturer, float Progress, bool bContested)
{
	if (Me.IsNone()) return FString();
	const bool bEnemyWorking = !Capturer.IsNone() && Capturer != Me && (Progress > 0.01f || bContested);
	if (Holder == Me)
	{
		return bEnemyWorking ? TEXT("DEFEND") : FString();
	}
	if (Capturer == Me && Progress > 0.01f)
	{
		return TEXT("CAPTURING");
	}
	return TEXT("ATTACK");
}

int32 UPA250BattleHUD::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, G, MyCullingRect, Out, LayerId, InWidgetStyle, bParentEnabled);
	const APA250BattleGameMode* GM = GameMode.Get();
	APlayerController* PC = GetOwningPlayer();
	if (!GM || !PC) return Layer;
	const ARFCharacter* Me = Cast<ARFCharacter>(PC->GetPawn());
	const FName MyFaction = Me && !Me->FactionId.IsNone() ? Me->FactionId : GM->GetPlayerFaction();
	const FVector2D View = G.GetLocalSize();
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	const FSlateFontInfo Letter = FCoreStyle::GetDefaultFontStyle("Bold", 15);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	++Layer;

	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	if (PC->PlayerCameraManager) { CamLoc = PC->PlayerCameraManager->GetCameraLocation(); CamRot = PC->PlayerCameraManager->GetCameraRotation(); }

	const auto& Posts = GM->GetPosts();
	const float Box = 30.f;
	const float TopGap = 8.f;
	const float TopW = Posts.Num() * (Box + TopGap) - TopGap;
	for (int32 i = 0; i < Posts.Num(); ++i)
	{
		const APA250ControlPoint* P = Posts[i].Get();
		if (!P) continue;
		const FString L = FString::Chr(PostLetter(i));
		const FName Capturer = P->GetCapturer();
		const float Progress = P->GetCaptureProgress();
		const bool bWorking = !Capturer.IsNone() && Progress > 0.01f;
		const float Pulse = P->IsContested() ? 0.65f + 0.35f * FMath::Sin(Now * 8.0) : 1.f;

		// Score-bar letter box: holder colour, capture fill along the bottom in the capturer's colour.
		const FVector2D TopPos(0.5f * View.X - 0.5f * TopW + i * (Box + TopGap), 46.f);
		DrawRect(Out, Layer, G, TopPos - FVector2D(2, 2), FVector2D(Box + 4, Box + 4), FLinearColor(0, 0, 0, 0.6f));
		DrawRect(Out, Layer + 1, G, TopPos, FVector2D(Box, Box), FactionColor(P->GetHolder(), 0.9f * Pulse));
		if (bWorking)
		{
			DrawRect(Out, Layer + 2, G, TopPos + FVector2D(0, Box - 6), FVector2D(Box * Progress, 6), FactionColor(Capturer));
		}
		DrawCenteredText(Out, Layer + 3, G, L, TopPos + FVector2D(0.5f * Box, 0.5f * Box - 1), Letter, FLinearColor::White);

		// World marker above the flagpole, pinned to the screen edge when off-screen.
		const FVector World = P->GetMarkerLocation();
		const FVector Local = CamRot.UnrotateVector(World - CamLoc);
		FVector2D S;
		bool bOnScreen = Local.X > 1.f && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, World, S, false);
		const FVector2D Margin(40.f, 70.f);
		if (!bOnScreen)
		{
			// Behind or beside the camera: point along the lateral direction, along the bottom/side edge.
			const FVector2D Dir = FVector2D(Local.Y, -Local.Z).GetSafeNormal();
			S = 0.5f * View + Dir * View.Size();
		}
		// Keep clear of the score bar, letter boxes and the callout line under them.
		const float MinY = 215.f;
		const bool bClamped = S.X < Margin.X || S.X > View.X - Margin.X || S.Y < MinY || S.Y > View.Y - Margin.Y;
		S.X = FMath::Clamp(S.X, Margin.X, View.X - Margin.X);
		S.Y = FMath::Clamp(S.Y, MinY, View.Y - Margin.Y);

		const FString Order = PostOrder(MyFaction, P->GetHolder(), Capturer, Progress, P->IsContested());
		const bool bUrgent = Order == TEXT("DEFEND") || Order == TEXT("CAPTURING");
		const float A = bClamped ? 0.85f : 1.f;
		const FVector2D BoxPos = S - FVector2D(0.5f * Box, 0.5f * Box);
		DrawRect(Out, Layer, G, BoxPos - FVector2D(2, 2), FVector2D(Box + 4, Box + 4), FLinearColor(0, 0, 0, 0.55f * A));
		DrawRect(Out, Layer + 1, G, BoxPos, FVector2D(Box, Box), FactionColor(P->GetHolder(), 0.85f * A * Pulse));
		DrawCenteredText(Out, Layer + 3, G, L, S - FVector2D(0, 1), Letter, FLinearColor(1, 1, 1, A));
		if (bWorking)
		{
			DrawRect(Out, Layer, G, BoxPos + FVector2D(-2, Box + 4), FVector2D(Box + 4, 7), FLinearColor(0, 0, 0, 0.55f * A));
			DrawRect(Out, Layer + 1, G, BoxPos + FVector2D(0, Box + 5), FVector2D(Box * Progress, 5), FactionColor(Capturer, A));
		}
		const FLinearColor OrderColor = Order == TEXT("DEFEND") ? FLinearColor(1.f, 0.55f, 0.2f, A)
		                              : bUrgent ? FLinearColor(1.f, 0.95f, 0.5f, A) : FLinearColor(1.f, 1.f, 1.f, 0.9f * A);
		DrawCenteredText(Out, Layer + 3, G, P->IsContested() ? FString(TEXT("CONTESTED")) : Order, S - FVector2D(0, 0.5f * Box + 11), Small, OrderColor);
		const float Meters = FVector::Dist(CamLoc, P->GetActorLocation()) / 100.f;
		const bool bInside = Me && FVector::Dist2D(Me->GetActorLocation(), P->GetActorLocation()) <= P->Radius;
		const FString Sub = bInside ? FString(TEXT("IN ZONE")) : FString::Printf(TEXT("%.0f m"), Meters);
		DrawCenteredText(Out, Layer + 3, G, Sub, S + FVector2D(0, 0.5f * Box + (bWorking ? 18.f : 11.f)), Small, FLinearColor(1, 1, 1, 0.8f * A));
	}
	return Layer + 4;
}

void UPA250BattleHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	HitmarkerTimer = FMath::Max(0.f, HitmarkerTimer - InDeltaTime);
}

TSharedRef<SWidget> UPA250BattleHUD::RebuildWidget()
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 34);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 14);
	TWeakObjectPtr<UPA250BattleHUD> WeakThis(this);

	auto Top = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		if (!GM) return FText::GetEmpty();
		// Post letters are painted under this bar (NativePaint).
		return FText::FromString(FString::Printf(TEXT("BRITISH %d          CONTINENTAL %d          Time %.0fs"),
			GM->TicketsRemaining(TEXT("british")), GM->TicketsRemaining(TEXT("continental")), GM->TimeRemaining()));
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
		const auto& Posts = GM->GetPosts();
		for (int32 i = 0; i < Posts.Num(); ++i)
		{
			APA250ControlPoint* P = Posts[i].Get();
			if (!P) continue;
			if (FVector::Dist2D(Me->GetActorLocation(), P->GetActorLocation()) > P->Radius) continue;
			const FString Name = FString::Printf(TEXT("%c  %s"), PostLetter(i), *APA250ControlPoint::DisplayName(P->PostId));
			const bool bLowering = !P->GetHolder().IsNone() && P->GetHolder() != Me->FactionId;
			if (P->IsContested())
			{
				return FText::FromString(FString::Printf(TEXT("%s contested - outnumber them to move the flag%s"), *Name, *Extra));
			}
			if (P->GetCaptureProgress() > 0.01f && P->GetCapturer() == Me->FactionId)
			{
				return FText::FromString(FString::Printf(TEXT("%s at %s  %.0f%%%s"), bLowering ? TEXT("Lowering the enemy flag") : TEXT("Raising our flag"), *Name, P->GetCaptureProgress() * 100.f, *Extra));
			}
			if (P->GetHolder() == Me->FactionId)
			{
				return FText::FromString(FString::Printf(TEXT("Holding %s%s"), *Name, *Extra));
			}
		}
		return Extra.IsEmpty() ? FText::GetEmpty() : FText::FromString(Extra.TrimStart());
	};

	auto MeP = [WeakThis]() -> const ARFCharacter* {
		const APlayerController* PC = WeakThis.IsValid() ? WeakThis->GetOwningPlayer() : nullptr;
		const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		return Me && !Me->IsDead() ? Me : nullptr;
	};
	auto IsReloadingBar = [MeP]() {
		const ARFCharacter* Me = MeP();
		return Me && Me->Musket && !Me->Musket->CanFire() && !Me->Musket->IsJammed();
	};

	auto Reload = [MeP]() {
		const ARFCharacter* Me = MeP();
		if (!Me || !Me->Musket) return FText::GetEmpty();
		if (Me->Musket->IsJammed()) return FText::FromString(TEXT("MUSKET JAMMED  -  hold R to clear"));
		if (Me->Musket->CanFire()) return FText::FromString(TEXT("Loaded"));
		return FText::FromString(FString::Printf(TEXT("Reloading   %s"), *Me->Musket->GetReloadPhaseLabel()));
	};
	auto ReloadColor = [MeP]() {
		const ARFCharacter* Me = MeP();
		if (Me && Me->Musket && Me->Musket->IsJammed()) return FSlateColor(FLinearColor(1.f, 0.3f, 0.2f));
		if (Me && Me->Musket && Me->Musket->CanFire()) return FSlateColor(FLinearColor(0.75f, 1.f, 0.75f));
		return FSlateColor(FLinearColor(1.f, 0.95f, 0.7f));
	};
	auto ReloadHint = [MeP]() {
		const ARFCharacter* Me = MeP();
		if (!Me || !Me->Musket || Me->Musket->CanFire() || Me->Musket->IsJammed()) return FText::GetEmpty();
		return FText::FromString(TEXT("Tap R as the marker crosses the gold zone to load faster  -  3 perfect taps = +15% damage  -  or just wait"));
	};
	auto ReloadFlash = [MeP]() {
		const ARFCharacter* Me = MeP();
		if (!Me) return FText::GetEmpty();
		switch (Me->GetReloadFeedback())
		{
		case EActiveReloadHit::Perfect: return FText::FromString(TEXT("PERFECT"));
		case EActiveReloadHit::Sweet: return FText::FromString(TEXT("Good"));
		case EActiveReloadHit::Miss: return FText::FromString(TEXT("Fumbled - step restarts"));
		default: return FText::GetEmpty();
		}
	};
	auto ReloadFlashColor = [MeP]() {
		const ARFCharacter* Me = MeP();
		const EActiveReloadHit H = Me ? Me->GetReloadFeedback() : EActiveReloadHit::None;
		return FSlateColor(H == EActiveReloadHit::Perfect ? FLinearColor(1.f, 0.85f, 0.2f)
			: H == EActiveReloadHit::Sweet ? FLinearColor(0.85f, 0.95f, 0.85f) : FLinearColor(1.f, 0.35f, 0.25f));
	};
	constexpr float BarW = 360.f;
	auto Zone = [MeP, BarW](bool bPerfect) {
		const ARFCharacter* Me = MeP();
		if (!Me || !Me->Musket) return FMargin(0.f);
		const float S = bPerfect ? Me->Musket->GetPerfectStart() : Me->Musket->GetSweetStart();
		const float E = bPerfect ? Me->Musket->GetPerfectEnd() : Me->Musket->GetSweetEnd();
		return FMargin(S * BarW, 0.f, (1.f - E) * BarW, 0.f);
	};
	auto Marker = [MeP, BarW]() {
		const ARFCharacter* Me = MeP();
		const float P = Me && Me->Musket ? FMath::Clamp(Me->Musket->GetReloadProgress(), 0.f, 1.f) : 0.f;
		const float L = FMath::Clamp(P * BarW - 2.f, 0.f, BarW - 4.f);
		return FMargin(L, 0.f, BarW - L - 4.f, 0.f);
	};
	auto Fill = [MeP, BarW]() {
		const ARFCharacter* Me = MeP();
		const float P = Me && Me->Musket ? FMath::Clamp(Me->Musket->GetReloadProgress(), 0.f, 1.f) : 0.f;
		return FMargin(0.f, 0.f, (1.f - P) * BarW, 0.f);
	};

	auto Melee = [MeP]() {
		const ARFCharacter* Me = MeP();
		if (!Me || !Me->MeleeComponent) return FText::GetEmpty();
		const FString Fb = Me->MeleeComponent->GetFeedbackText();
		if (!Fb.IsEmpty()) return FText::FromString(Fb);
		const float Cd = Me->MeleeComponent->GetChargeCooldownRemaining();
		if (Cd > 0.f) return FText::FromString(FString::Printf(TEXT("Bayonet charge ready in %.1fs"), Cd));
		if (Me->MeleeComponent->FindChargeTarget()) return FText::FromString(TEXT(">> Enemy in reach - press G to CHARGE! <<"));
		return FText::FromString(TEXT("Bayonet charge ready  (G)"));
	};
	auto MeleeColor = [MeP]() {
		const ARFCharacter* Me = MeP();
		if (Me && Me->MeleeComponent && !Me->MeleeComponent->GetFeedbackText().IsEmpty())
		{
			return FSlateColor(Me->MeleeComponent->WasFeedbackHit() ? FLinearColor(1.f, 0.4f, 0.3f) : FLinearColor(1.f, 0.9f, 0.5f));
		}
		if (Me && Me->MeleeComponent && Me->MeleeComponent->GetChargeCooldownRemaining() <= 0.f && Me->MeleeComponent->FindChargeTarget())
		{
			return FSlateColor(FLinearColor(1.f, 0.25f, 0.2f)); // charge prompt stands out
		}
		return FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 0.8f));
	};
	auto Sens = [MeP]() {
		const ARFCharacter* Me = MeP();
		return Me && Me->ShowSensitivityToast()
			? FText::FromString(FString::Printf(TEXT("Look sensitivity %.2f   ( [ slower   ] faster )"), ARFCharacter::GetLookSensitivity()))
			: FText::GetEmpty();
	};
	auto Planted = [MeP]() {
		const ARFCharacter* Me = MeP();
		const bool bShow = Me && Me->IsMovementLocked() && !(Me->MeleeComponent && Me->MeleeComponent->IsCharging());
		return bShow ? FText::FromString(TEXT("feet planted")) : FText::GetEmpty();
	};

	// Shouted orders and morale news near the camera ("Make ready!", "They're running!").
	auto Shout = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		const UPA250VolleyCoordinator* V = GM ? GM->GetVolleys() : nullptr;
		return V ? FText::FromString(V->GetCallout()) : FText::GetEmpty();
	};
	auto Banner = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		return GM ? FText::FromString(GM->GetBannerText()) : FText::GetEmpty();
	};

	auto Reticle = [WeakThis]() {
		if (!WeakThis.IsValid()) return FText::FromString(TEXT("+"));
		if (WeakThis->IsReticleBlocked()) return FText::FromString(TEXT("X"));
		return FText::FromString(TEXT("+"));
	};
	auto ReticleColor = [WeakThis]() {
		if (!WeakThis.IsValid()) return FSlateColor(FLinearColor::White);
		if (WeakThis->IsReticleBlocked()) return FSlateColor(FLinearColor(1.f, 0.85f, 0.2f));
		if (WeakThis->IsReticleHostile()) return FSlateColor(FLinearColor(1.f, 0.15f, 0.15f));
		return FSlateColor(FLinearColor::White);
	};
	auto Hitmarker = [WeakThis]() {
		if (!WeakThis.IsValid() || WeakThis->GetHitmarkerTimer() <= 0.f) return FText::GetEmpty();
		return FText::FromString(WeakThis->IsHitmarkerKill() ? TEXT("X") : TEXT("+"));
	};
	auto HitColor = [WeakThis]() {
		if (!WeakThis.IsValid()) return FSlateColor(FLinearColor::White);
		return WeakThis->IsHitmarkerKill() ? FSlateColor(FLinearColor(1.f, 0.1f, 0.1f)) : FSlateColor(FLinearColor::White);
	};
	auto KillFeed = [WeakThis]() {
		const APA250BattleGameMode* GM = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
		if (!GM) return FText::GetEmpty();
		FString S;
		for (const FString& L : GM->GetKillFeed())
		{
			S += L + TEXT("\n");
		}
		return FText::FromString(S);
	};
	auto SpreadPad = [WeakThis]() {
		const float Px = WeakThis.IsValid() ? WeakThis->GetReticleSpreadPx() : 8.f;
		return FMargin(Px, Px);
	};

	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center)
		[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f)).Padding(FMargin(18.f, 8.f))
			[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor::White).Text_Lambda(Top)]]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center).Padding(0, 84, 0, 0)
		[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.6f)).Text_Lambda(Capture)]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Center).Padding(0, 136, 0, 0)
		[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 22)).ColorAndOpacity(FLinearColor(1.f, 0.92f, 0.75f))
			.ShadowOffset(FVector2D(2, 2)).Text_Lambda(Shout)]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Right).Padding(0, 80, 24, 0)
		[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.8f)).Text_Lambda(KillFeed)]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center)
		[SNew(SBox).Padding_Lambda(SpreadPad)
			[SNew(STextBlock).Font(Big).ColorAndOpacity_Lambda(ReticleColor).Text_Lambda(Reticle)]]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(0, 40, 0, 0)
		[SNew(STextBlock).Font(Big).ColorAndOpacity_Lambda(HitColor).Text_Lambda(Hitmarker)]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(0, 90, 0, 0)
		[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.7f)).ShadowOffset(FVector2D(1, 1)).Text_Lambda(Planted)]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(0, 0, 0, 140)
		[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.9f)).ShadowOffset(FVector2D(1, 1)).Text_Lambda(Sens)]
		// Reload panel: state line, timing bar (gold = press zone, bright = perfect), hint, press result.
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0, 0, 0, 36)
		[SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
			[SNew(STextBlock).Font(Small).ShadowOffset(FVector2D(1, 1)).ColorAndOpacity_Lambda(MeleeColor).Text_Lambda(Melee)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[SNew(STextBlock).Font(Font).ShadowOffset(FVector2D(1, 1)).ColorAndOpacity_Lambda(ReloadColor).Text_Lambda(Reload)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
			[SNew(SBox).WidthOverride(BarW).HeightOverride(16.f)
				.Visibility_Lambda([IsReloadingBar]() { return IsReloadingBar() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				[SNew(SOverlay)
					+ SOverlay::Slot()
					[SNew(SImage).Image(FAppStyle::GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))]
					+ SOverlay::Slot().Padding(TAttribute<FMargin>::CreateLambda(Fill))
					[SNew(SImage).Image(FAppStyle::GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.18f))]
					+ SOverlay::Slot().Padding(TAttribute<FMargin>::CreateLambda([Zone]() { return Zone(false); }))
					[SNew(SImage).Image(FAppStyle::GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(0.85f, 0.65f, 0.15f, 0.75f))]
					+ SOverlay::Slot().Padding(TAttribute<FMargin>::CreateLambda([Zone]() { return Zone(true); }))
					[SNew(SImage).Image(FAppStyle::GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.45f, 1.f))]
					+ SOverlay::Slot().Padding(TAttribute<FMargin>::CreateLambda(Marker))
					[SNew(SImage).Image(FAppStyle::GetBrush("WhiteBrush")).ColorAndOpacity(FLinearColor::White)]]]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)
			[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 0.85f)).Text_Lambda(ReloadHint)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
			[SNew(STextBlock).Font(Font).ShadowOffset(FVector2D(1, 1)).ColorAndOpacity_Lambda(ReloadFlashColor).Text_Lambda(ReloadFlash)]]
		// Controls legend.
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Left).Padding(20, 0, 0, 20)
		[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.6f)).ShadowOffset(FVector2D(1, 1))
			.Text(FText::FromString(TEXT("LMB  fire (you stop to shoot)\nRMB  aim down sights     V  swap shoulder\nR  reload - tap in the gold zone, hold to clear a jam\nE  bayonet thrust     Q  butt-stroke (works mid-reload)\nG (or Shift + E)  bayonet charge - 6 m dash toward the enemy in front, first man hit takes 100\n[ / ]  look sensitivity slower / faster")))]
		+ SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center)
		[SNew(STextBlock).Font(Big).ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.3f)).ShadowOffset(FVector2D(2, 2)).Text_Lambda(Banner)];
}
