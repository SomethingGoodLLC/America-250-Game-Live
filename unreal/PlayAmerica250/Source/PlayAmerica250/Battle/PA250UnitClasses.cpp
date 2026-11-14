#include "PA250UnitClasses.h"

namespace
{
FPA250UnitClassKit Kit(const TCHAR* Id, const TCHAR* Label, const TCHAR* Weapon, const TCHAR* Blurb, float Health, float Speed,
	float Spread, float Range, float Reload, float Melee, EPA250Bayonet Bayonet, bool bOfficer = false)
{
	FPA250UnitClassKit K;
	K.Id = FName(Id);
	K.Label = Label;
	K.Weapon = Weapon;
	K.Blurb = Blurb;
	K.MaxHealth = Health;
	K.SpeedScale = Speed;
	K.SpreadScale = Spread;
	K.RangeScale = Range;
	K.ReloadScale = Reload;
	K.MeleeScale = Melee;
	K.Bayonet = Bayonet;
	K.bOfficer = bOfficer;
	return K;
}

FPA250UnitClassKit Look(FPA250UnitClassKit K, float Height, EPA250Hat Hat)
{
	K.HeightScale = Height;
	K.Hat = Hat;
	return K;
}

// GAMEPLAY SETTINGS.                                                          health speed spread range reload melee
const TArray<FPA250UnitClassKit>& BritishKits()
{
	static const TArray<FPA250UnitClassKit> Kits = {
		Kit(TEXT("line_infantry"), TEXT("Regular"), TEXT("Brown Bess and bayonet"),
			TEXT("The battalion man. Steady in the volley and in the charge."), 100.f, 1.f, 1.f, 1.f, 1.f, 1.f, EPA250Bayonet::All),
		Look(Kit(TEXT("grenadier"), TEXT("Grenadier"), TEXT("Brown Bess and bayonet"),
			TEXT("Shock troops: the biggest men of the regiment. Deadliest with the bayonet; a ball stops them like any man."), 100.f, 0.95f, 1.f, 1.f, 1.f, 1.3f, EPA250Bayonet::All),
			1.07f, EPA250Hat::Bearskin),
		Look(Kit(TEXT("light_infantry"), TEXT("Light Infantry"), TEXT("Short musket and bayonet"),
			TEXT("Skirmishers in loose order. Fast and a better shot; lighter in a bayonet fight."), 90.f, 1.15f, 0.88f, 1.f, 1.f, 0.9f, EPA250Bayonet::All),
			0.96f, EPA250Hat::Cap),
		Look(Kit(TEXT("officer"), TEXT("Officer"), TEXT("Fusil and bayonet"),
			TEXT("Leads from the line: men within 15 m of him aim steadier and load faster."), 100.f, 1.05f, 0.95f, 1.f, 0.95f, 1.f, EPA250Bayonet::All, true),
			1.02f, EPA250Hat::CockedHat),
		// GAME INVENTION for 1775: dragoons ride to the fight (see Mounted/), dismount and fight on foot.
		Look(Kit(TEXT("dragoon"), TEXT("Dragoon"), TEXT("Carbine, pistol and sabre"),
			TEXT("Mounted infantry. Rides to the fight, dismounts out of range and fights on foot; remounts to move. H mounts and dismounts."),
			100.f, 1.f, 1.1f, 0.9f, 1.f, 1.f, EPA250Bayonet::None), 1.f, EPA250Hat::Cap),
		// No such troops in the slice; kept so a British militia entry behaves as it always did.
		Kit(TEXT("militia"), TEXT("Regular"), TEXT("Musket and bayonet"), TEXT(""), 80.f, 1.f, 1.f, 1.f, 1.f, 1.f, EPA250Bayonet::All),
	};
	return Kits;
}

const TArray<FPA250UnitClassKit>& ContinentalKits()
{
	static const TArray<FPA250UnitClassKit> Kits = {
		Kit(TEXT("line_infantry"), TEXT("Provincial"), TEXT("Musket, a few bayonets"),
			TEXT("Enlisted men who stand in ranks and volley. Steadier than militia in the open."), 100.f, 1.f, 1.f, 1.f, 1.f, 1.f, EPA250Bayonet::Some),
		Kit(TEXT("militia"), TEXT("Militia"), TEXT("Own firelock, no bayonet"),
			TEXT("Minutemen and militia. Best from walls and fences; fragile in the open and hand to hand."), 80.f, 1.f, 1.f, 1.f, 1.f, 1.f, EPA250Bayonet::None),
		Look(Kit(TEXT("rifleman"), TEXT("Rifleman"), TEXT("Long rifle, no bayonet"),
			TEXT("Marksman. Kills at twice a musket's range, but loads slowly and is weak hand to hand."), 85.f, 1.05f, 0.45f, 2.f, 1.6f, 0.75f, EPA250Bayonet::None),
			0.98f, EPA250Hat::RoundHat),
		Look(Kit(TEXT("officer"), TEXT("Officer"), TEXT("Fusil and bayonet"),
			TEXT("Leads from the line: men within 15 m of him aim steadier and load faster."), 100.f, 1.05f, 0.95f, 1.f, 0.95f, 1.f, EPA250Bayonet::All, true),
			1.02f, EPA250Hat::CockedHat),
		Look(Kit(TEXT("dragoon"), TEXT("Dragoon"), TEXT("Carbine, pistol and sabre"),
			TEXT("Mounted infantry. Rides to the fight, dismounts out of range and fights on foot; remounts to move. H mounts and dismounts."),
			100.f, 1.f, 1.1f, 0.9f, 1.f, 1.f, EPA250Bayonet::None), 1.f, EPA250Hat::RoundHat),
	};
	return Kits;
}
}

const FPA250UnitClassKit& PA250UnitClasses::KitFor(FName Faction, FName UnitClass)
{
	const TArray<FPA250UnitClassKit>& Kits = Faction == TEXT("continental") ? ContinentalKits() : BritishKits();
	for (const FPA250UnitClassKit& K : Kits)
	{
		if (K.Id == UnitClass) return K;
	}
	return Kits[0];
}

const TArray<FName>& PA250UnitClasses::DisplayOrder()
{
	static const TArray<FName> Order = {TEXT("line_infantry"), TEXT("grenadier"), TEXT("light_infantry"), TEXT("militia"),
		TEXT("rifleman"), TEXT("officer"), TEXT("dragoon")};
	return Order;
}

FString PA250UnitClasses::TraitLine(const FPA250UnitClassKit& Kit)
{
	TArray<FString> Parts;
	auto Pct = [&Parts](float Scale, const TCHAR* Better, const TCHAR* Worse, bool bLowerIsBetter) {
		const int32 P = FMath::RoundToInt((Scale - 1.f) * 100.f);
		if (P == 0) return;
		const bool bGood = bLowerIsBetter ? P < 0 : P > 0;
		Parts.Add(FString::Printf(TEXT("%s %d%%"), bGood ? Better : Worse, FMath::Abs(P)));
	};
	const int32 Health = FMath::RoundToInt(Kit.MaxHealth - 100.f);
	if (Health != 0) Parts.Add(FString::Printf(TEXT("health %+d"), Health));
	Pct(Kit.SpeedScale, TEXT("faster"), TEXT("slower"), false);
	Pct(Kit.SpreadScale, TEXT("tighter aim"), TEXT("looser aim"), true);
	if (Kit.RangeScale > 1.01f) Parts.Add(FString::Printf(TEXT("range x%.3g"), Kit.RangeScale));
	Pct(Kit.ReloadScale, TEXT("quicker reload"), TEXT("slower reload"), true);
	Pct(Kit.MeleeScale, TEXT("stronger hand to hand"), TEXT("weaker hand to hand"), false);
	if (Kit.Bayonet == EPA250Bayonet::None) Parts.Add(TEXT("no bayonet"));
	if (Kit.bOfficer) Parts.Add(TEXT("steadies men nearby"));
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT("  ·  ")) : FString(TEXT("the standard musket man"));
}

bool PA250UnitClasses::InOfficerRange(const FVector& Soldier, const FVector& Officer)
{
	return FVector::DistSquared(Soldier, Officer) <= FMath::Square(OfficerRadiusCm);
}
