#include "PA250MapTooltip.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UPA250MapTooltip::RebuildWidget()
{
	// Parchment card with dark ink text: matches the map's plates, readable over the engraving.
	SAssignNew(Panel, SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor(240, 230, 204, 245)))
		.Padding(FMargin(9.f, 6.f))
		.Visibility(EVisibility::HitTestInvisible)
		[
			SAssignNew(Label, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.ColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(FColor(40, 30, 18))))
				.Text(FText::GetEmpty())
		];
	return Panel.ToSharedRef();
}

void UPA250MapTooltip::ShowAt(const FString& Text, FVector2D CursorPx, FVector2D ViewportPx)
{
	if (!Panel.IsValid() || !Label.IsValid())
	{
		return;
	}
	if (Text != Current)
	{
		Current = Text;
		Label->SetText(FText::FromString(Text));
	}
	// Measure, then place below-right of the cursor; flip left / up where it would leave the view.
	Panel->SlatePrepass(1.f);
	const FVector2D Size = Panel->GetDesiredSize();
	FVector2D Pos = CursorPx + FVector2D(CursorOffsetPx, CursorOffsetPx);
	if (Pos.X + Size.X > ViewportPx.X - 4.f)
	{
		Pos.X = CursorPx.X - CursorOffsetPx - Size.X;
	}
	if (Pos.Y + Size.Y > ViewportPx.Y - 4.f)
	{
		Pos.Y = CursorPx.Y - CursorOffsetPx - Size.Y;
	}
	Pos.X = FMath::Max(4.f, Pos.X);
	Pos.Y = FMath::Max(4.f, Pos.Y);
	SetPositionInViewport(Pos, /*bRemoveDPIScale=*/true);
	if (!bShowing)
	{
		bShowing = true;
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UPA250MapTooltip::ShowBeside(const FString& Text, const FBox2D& AvoidPx, FVector2D CursorPx, FVector2D ViewportPx)
{
	if (!AvoidPx.bIsValid || !Panel.IsValid() || !Label.IsValid())
	{
		ShowAt(Text, CursorPx, ViewportPx);
		return;
	}
	if (Text != Current)
	{
		Current = Text;
		Label->SetText(FText::FromString(Text));
	}
	Panel->SlatePrepass(1.f);
	const FVector2D Size = Panel->GetDesiredSize();
	constexpr float Gap = 10.f;
	// Right of the hovered thing, top-aligned with it; else left; else below; else above.
	const FVector2D Options[] = {
		FVector2D(AvoidPx.Max.X + Gap, AvoidPx.Min.Y),
		FVector2D(AvoidPx.Min.X - Gap - Size.X, AvoidPx.Min.Y),
		FVector2D(AvoidPx.Min.X, AvoidPx.Max.Y + Gap),
		FVector2D(AvoidPx.Min.X, AvoidPx.Min.Y - Gap - Size.Y),
	};
	FVector2D Pos = Options[0];
	for (const FVector2D& O : Options)
	{
		if (O.X >= 4.f && O.Y >= 4.f && O.X + Size.X <= ViewportPx.X - 4.f && O.Y + Size.Y <= ViewportPx.Y - 4.f)
		{
			Pos = O;
			break;
		}
	}
	Pos.X = FMath::Clamp(Pos.X, 4.f, FMath::Max(4.f, ViewportPx.X - Size.X - 4.f));
	Pos.Y = FMath::Clamp(Pos.Y, 4.f, FMath::Max(4.f, ViewportPx.Y - Size.Y - 4.f));
	SetPositionInViewport(Pos, /*bRemoveDPIScale=*/true);
	if (!bShowing)
	{
		bShowing = true;
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UPA250MapTooltip::HideTip()
{
	// Always collapse: the first call comes right after AddToViewport, while bShowing is still false and
	// the widget is Visible at full-viewport size (no position yet), which veiled the whole map in cream.
	bShowing = false;
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}
