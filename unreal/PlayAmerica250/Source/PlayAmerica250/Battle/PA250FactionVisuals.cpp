#include "PA250FactionVisuals.h"

const FPA250FactionLook& UPA250FactionVisuals::LookFor(FName Faction, FName UnitClass) const
{
	if (Faction == TEXT("british"))
	{
		return British;
	}
	if (UnitClass == TEXT("militia") || UnitClass == TEXT("rifleman"))
	{
		return Militia;
	}
	return Continental;
}

UPA250FactionVisuals::UPA250FactionVisuals()
{
	// Madder red coat, blue facings, black tricorn.
	British.CoatColor = FLinearColor(0.55f, 0.08f, 0.08f);
	British.FacingColor = FLinearColor(0.08f, 0.15f, 0.55f);
	British.NameplateColor = FLinearColor(0.85f, 0.15f, 0.15f);
	British.HatStyle = TEXT("tricorn");
	// Dark blue coat, buff facings, tricorn.
	Continental.CoatColor = FLinearColor(0.08f, 0.14f, 0.42f);
	Continental.FacingColor = FLinearColor(0.78f, 0.68f, 0.42f);
	Continental.NameplateColor = FLinearColor(0.2f, 0.35f, 0.9f);
	Continental.HatStyle = TEXT("tricorn");
	// Brown/grey homespun, round hat.
	Militia.CoatColor = FLinearColor(0.35f, 0.30f, 0.22f);
	Militia.FacingColor = FLinearColor(0.45f, 0.45f, 0.42f);
	Militia.NameplateColor = FLinearColor(0.55f, 0.55f, 0.5f);
	Militia.HatStyle = TEXT("round");
}

UPA250FactionVisuals* UPA250FactionVisuals::MakeRuntimeDefaults()
{
	return NewObject<UPA250FactionVisuals>();
}

void UPA250FactionVisuals::HomespunFor(uint32 Seed, FLinearColor& OutCoat, FLinearColor& OutUnder)
{
	// Undyed and vegetable-dyed wool and linen: browns, drab, grey, butternut, dull green, rust.
	static const FLinearColor Coats[] = {
		FLinearColor(0.20f, 0.14f, 0.08f), // brown
		FLinearColor(0.34f, 0.28f, 0.18f), // drab
		FLinearColor(0.26f, 0.25f, 0.23f), // grey
		FLinearColor(0.38f, 0.26f, 0.11f), // butternut
		FLinearColor(0.12f, 0.16f, 0.10f), // dull green
		FLinearColor(0.28f, 0.26f, 0.14f), // olive drab (no reds: red means a regular at a glance)
		FLinearColor(0.14f, 0.11f, 0.09f), // dark brown
		FLinearColor(0.42f, 0.38f, 0.30f), // undyed light
	};
	static const FLinearColor Unders[] = {
		FLinearColor(0.52f, 0.47f, 0.36f), // buff
		FLinearColor(0.40f, 0.33f, 0.22f), // tan
		FLinearColor(0.58f, 0.56f, 0.50f), // off-white linen
		FLinearColor(0.22f, 0.16f, 0.10f), // brown
		FLinearColor(0.30f, 0.29f, 0.27f), // grey
	};
	const uint32 H = HashCombine(Seed, 0x9E3779B9u);
	const float Shade = 0.85f + 0.3f * ((H >> 16) & 0xFF) / 255.f; // no two coats quite alike
	OutCoat = Coats[H % UE_ARRAY_COUNT(Coats)] * Shade;
	OutUnder = Unders[(H >> 8) % UE_ARRAY_COUNT(Unders)];
	OutCoat.A = OutUnder.A = 1.f;
}
