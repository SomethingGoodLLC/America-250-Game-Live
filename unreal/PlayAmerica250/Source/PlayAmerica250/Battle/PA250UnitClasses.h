// PA250UnitClasses.h — what a soldier class is on the battlefield (plain C++, no UObject).
// GAMEPLAY SETTINGS (provisional balance values, not historical measurements). The class ids are the
// campaign's Army.units keys; the sim never reads these values. Bot drill and tactics stay in
// DA_CombatProfiles (training x role); this table is the kit a class carries, for bots and the player alike.
#pragma once

#include "CoreMinimal.h"

enum class EPA250Bayonet : uint8
{
	None,   // clubbed musket only
	Some,   // one man in four (provincials armed from mixed stores)
	All
};

/** Headgear that tells the classes apart at a glance (visual only). The soldier model wears a cocked
 *  hat; classes with their own headgear are drawn without it (see ARFCharacter::ApplyClassLook). */
enum class EPA250Hat : uint8
{
	None,      // the plain cocked hat
	Bearskin,  // grenadier: tall fur cap
	Cap,       // light infantry: low leather cap, green feather
	RoundHat,  // rifleman: broad-brimmed round hat
	CockedHat  // officer: cocked hat with a gold feather
};

struct FPA250UnitClassKit
{
	FName Id;
	/** One or two words: the nameplate, the kill feed and the class card title. */
	const TCHAR* Label = TEXT("Soldier");
	const TCHAR* Weapon = TEXT("Musket");
	/** What he is for, in one line (class card). */
	const TCHAR* Blurb = TEXT("");
	float MaxHealth = 100.f;
	/** Walk and sprint speed multiplier. */
	float SpeedScale = 1.f;
	/** Aim cone multiplier (< 1 = straighter shooting). */
	float SpreadScale = 1.f;
	/** Stretches damage falloff and maximum range (2 = a rifle that still kills at 200 m). */
	float RangeScale = 1.f;
	/** Reload time multiplier (> 1 = slower): a patched rifle ball is forced down the bore. */
	float ReloadScale = 1.f;
	/** Bayonet, butt and club damage multiplier. */
	float MeleeScale = 1.f;
	EPA250Bayonet Bayonet = EPA250Bayonet::All;
	/** Leader: friendly soldiers near him aim steadier and load faster. */
	bool bOfficer = false;
	// ---- Look (visual only) ----
	/** Body height multiplier: grenadiers were picked for size, light infantry for agility. */
	float HeightScale = 1.f;
	EPA250Hat Hat = EPA250Hat::None;
};

namespace PA250UnitClasses
{
/** Friendly soldiers within this distance of a living officer of their side get the bonuses below. */
inline constexpr float OfficerRadiusCm = 1500.f;
inline constexpr float OfficerSpreadScale = 0.8f;  // aim cone
inline constexpr float OfficerReloadScale = 0.88f; // reload time

/** Never fails: an unknown class gets the faction's plain musket man. */
PLAYAMERICA250_API const FPA250UnitClassKit& KitFor(FName Faction, FName UnitClass);
/** Classes of a side in the order the class picker shows them (line first, leader last). */
PLAYAMERICA250_API const TArray<FName>& DisplayOrder();
/** "+" / "-" traits for the class card, e.g. "Bayonet  ·  +20% health  ·  slower". */
PLAYAMERICA250_API FString TraitLine(const FPA250UnitClassKit& Kit);
/** Pure: is this man near enough to an officer (not himself) to be steadied by him? */
PLAYAMERICA250_API bool InOfficerRange(const FVector& Soldier, const FVector& Officer);
}
