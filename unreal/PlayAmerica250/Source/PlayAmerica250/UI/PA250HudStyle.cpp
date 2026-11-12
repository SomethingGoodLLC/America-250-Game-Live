#include "UI/PA250HudStyle.h"

#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

FLinearColor FPA250HudStyle::SRGB(float R, float G, float B, float A)
{
	return FLinearColor(FMath::Pow(R, 2.2f), FMath::Pow(G, 2.2f), FMath::Pow(B, 2.2f), A);
}

const FLinearColor FPA250HudStyle::Navy = FPA250HudStyle::SRGB(0.11f, 0.17f, 0.29f);
const FLinearColor FPA250HudStyle::Walnut = FPA250HudStyle::SRGB(0.09f, 0.07f, 0.05f);
const FLinearColor FPA250HudStyle::Brass = FPA250HudStyle::SRGB(0.85f, 0.68f, 0.30f);
const FLinearColor FPA250HudStyle::BrassDim = FPA250HudStyle::SRGB(0.50f, 0.39f, 0.18f);
const FLinearColor FPA250HudStyle::Cream = FPA250HudStyle::SRGB(0.94f, 0.89f, 0.74f);
const FLinearColor FPA250HudStyle::CreamDim = FPA250HudStyle::SRGB(0.72f, 0.66f, 0.53f);
const FLinearColor FPA250HudStyle::Ink = FPA250HudStyle::SRGB(0.16f, 0.11f, 0.06f);
const FLinearColor FPA250HudStyle::Red = FPA250HudStyle::SRGB(0.84f, 0.40f, 0.31f);
const FLinearColor FPA250HudStyle::Green = FPA250HudStyle::SRGB(0.50f, 0.66f, 0.36f);

FPA250HudStyle::FPA250HudStyle()
	: Bar(SRGB(0.060f, 0.080f, 0.125f, 0.98f))
	, Rule(BrassDim)
	, White(FLinearColor::White)
	, Panel(SRGB(0.085f, 0.068f, 0.050f, 0.95f), 5.f, BrassDim, 1.5f)
	, Header(SRGB(0.105f, 0.165f, 0.290f), 3.f, BrassDim, 1.f)
	, Chip(FLinearColor(0.f, 0.f, 0.f, 0.45f), 3.f, FLinearColor(1.f, 0.9f, 0.6f, 0.10f), 1.f)
	, Parchment(SRGB(0.93f, 0.88f, 0.74f, 0.98f), 4.f, SRGB(0.30f, 0.22f, 0.12f), 2.f)
	, Banner(SRGB(0.40f, 0.29f, 0.10f, 0.97f), 4.f, Brass, 1.5f)
	, Btn(SRGB(0.135f, 0.210f, 0.360f), 3.f, BrassDim, 1.f)
	, BtnHover(SRGB(0.185f, 0.285f, 0.480f), 3.f, Brass, 1.f)
	, BtnPressed(SRGB(0.095f, 0.150f, 0.270f), 3.f, Brass, 1.f)
	, BtnDisabled(SRGB(0.13f, 0.13f, 0.14f), 3.f, SRGB(0.24f, 0.23f, 0.21f), 1.f)
{
	const FMargin None(0.f);
	Button.SetNormal(Btn).SetHovered(BtnHover).SetPressed(BtnPressed).SetDisabled(BtnDisabled).SetNormalPadding(None).SetPressedPadding(None);
}

const FPA250HudStyle& FPA250HudStyle::Get()
{
	static const FPA250HudStyle* Instance = new FPA250HudStyle();
	return *Instance;
}

FSlateFontInfo FPA250HudStyle::Font(const TCHAR* Face, int32 Size)
{
	// Built once. A typeface entry is added only for a file that exists, so a missing font folder
	// falls back to the engine font instead of drawing nothing.
	static TSharedPtr<const FCompositeFont> Serif;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("PA250/Fonts"));
		TSharedRef<FStandaloneCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>();
		const TCHAR* Faces[] = {TEXT("Regular"), TEXT("Bold"), TEXT("Italic")};
		for (const TCHAR* Name : Faces)
		{
			const FString Path = Dir / FString::Printf(TEXT("CrimsonText-%s.ttf"), Name);
			if (IFileManager::Get().FileExists(*Path))
			{
				Composite->DefaultTypeface.Fonts.Emplace(FTypefaceEntry(FName(Name), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad));
			}
		}
		// Crimson has no geometric shapes (the level pips) or arrows; the engine sans supplies them.
		const FString EngineFonts = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir() / TEXT("Slate/Fonts"));
		for (const TCHAR* Name : Faces)
		{
			const FString Path = EngineFonts / FString::Printf(TEXT("Roboto-%s.ttf"), Name);
			if (IFileManager::Get().FileExists(*Path))
			{
				Composite->FallbackTypeface.Typeface.Fonts.Emplace(FTypefaceEntry(FName(Name), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad));
			}
		}
		if (Composite->DefaultTypeface.Fonts.Num() == 3)
		{
			Serif = Composite;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PA250 HUD: serif font files not found in %s; using the engine font"), *Dir);
		}
	}
	if (!Serif.IsValid())
	{
		return FCoreStyle::GetDefaultFontStyle(Face, Size);
	}
	// Crimson Text runs small next to the engine sans at the same point size.
	return FSlateFontInfo(Serif, FMath::RoundToFloat(Size * 1.08f), FName(Face));
}
