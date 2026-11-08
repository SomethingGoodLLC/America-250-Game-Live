#include "PA250FactionVisuals.h"

const FPA250FactionLook& UPA250FactionVisuals::LookFor(FName Faction, FName UnitClass) const
{
	if (Faction == TEXT("british"))
	{
		return British;
	}
	if (UnitClass == TEXT("militia"))
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
