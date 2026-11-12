// PA250HudStyle.h — one look for the campaign HUD: a 1775 writing-desk palette (Continental navy,
// walnut, brass, parchment), a period serif, framed panels and struck-brass buttons. Slate only.
#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"

struct PLAYAMERICA250_API FPA250HudStyle
{
	/** Shared instance; lives for the process (Slate widgets keep raw pointers to its brushes). */
	static const FPA250HudStyle& Get();

	/**
	 * Face is "Regular", "Bold" or "Italic". Uses Crimson Text (SIL OFL, Content/PA250/Fonts) with
	 * the engine font as fallback for glyphs it lacks, or the engine font alone if the files are
	 * missing. Size is in the same points the HUD used with the engine font.
	 */
	static FSlateFontInfo Font(const TCHAR* Face, int32 Size);

	/** A colour as it should look on screen. Slate treats brush and text colours as linear, so a
	 *  value typed in as "what I see in a paint program" comes out washed unless converted. */
	static FLinearColor SRGB(float R, float G, float B, float A = 1.f);

	// Palette (already converted)
	static const FLinearColor Navy;
	static const FLinearColor Walnut;
	static const FLinearColor Brass;
	static const FLinearColor BrassDim;
	static const FLinearColor Cream;
	static const FLinearColor CreamDim;
	static const FLinearColor Ink;
	static const FLinearColor Red;
	static const FLinearColor Green;

	FSlateColorBrush Bar;             // top and bottom strips
	FSlateColorBrush Rule;            // brass hairline
	FSlateColorBrush White;           // tint with ColorAndOpacity
	FSlateRoundedBoxBrush Panel;      // dark framed panel
	FSlateRoundedBoxBrush Header;     // navy section header strip / plaque
	FSlateRoundedBoxBrush Chip;       // inset value box in the top strip
	FSlateRoundedBoxBrush Parchment;  // letters, broadsides, notices
	FSlateRoundedBoxBrush Banner;     // selected-army banner
	FButtonStyle Button;

private:
	FPA250HudStyle();
	FSlateRoundedBoxBrush Btn, BtnHover, BtnPressed, BtnDisabled;
};
