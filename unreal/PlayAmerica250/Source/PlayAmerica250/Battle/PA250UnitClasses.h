// PA250UnitClasses.h — what a soldier class is on the battlefield (plain C++, no UObject).
// GAMEPLAY SETTINGS (provisional balance values, not historical measurements). The class ids are the
// campaign's Army.units keys; the sim never reads these values. Bot drill and tactics stay in
// DA_CombatProfiles (training x role); this is the kit a class carries, for bots and the player alike.
//
// The numbers live in unreal/PA250Sim/data/unit_classes.json (user decision 2026-11-14) and are served
// only through this file. The C++ table in the .cpp is the FALLBACK: it is used when the file is
// missing or does not cover a class, every such use is counted and logged as an Error, and the
// PA250Battle commandlet fails if the file is missing or lacks a class either side can field.
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
	/** One line each for the class picker: what he is for on the field, and his one verb. */
	const TCHAR* Role = TEXT("");
	const TCHAR* Verb = TEXT("");
	// ---- Engagement band and firelock over distance (metres) ----
	/** His squads open fire inside PreferMaxM and close to PreferMinM (a grenadier's 0-25 is "hold fire and close"). */
	float PreferMinM = 30.f;
	float PreferMaxM = 60.f;
	/** Beyond this the aim cone widens by SpreadGrowth for every further AccurateM. */
	float AccurateM = 50.f;
	float SpreadGrowth = 1.5f;
	/** Reserve tickets a death costs (militia: half). */
	float TicketCost = 1.f;
	/** How much one man counts when taking a post (light infantry: more). */
	float CaptureWeight = 1.f;
	// ---- The class verb: which one this class takes part in (values in FPA250ClassVerbs) ----
	bool bVolleyShock = false;     // 3+ in rank firing within a second shake the squad they fire on
	bool bSteadyInCover = false;   // steadier and truer behind cover, quickly shaken in the open
	bool bOfficerHunter = false;   // an officer he kills leaves that officer's squad shaken
	bool bChargeRouts = false;     // his squad's bayonet charge forces a rout check on contact
	bool bForwardSpawn = false;    // may come on at a post close to the enemy
	bool bRallies = false;         // rallies a routing squad nearby; orders a line to volley or charge
	bool bTakesPrisoners = false;  // mounted, routing enemies he overtakes surrender
	bool bBuildsWorks = false;     // builds and repairs field works at the places marked for them
	bool bCarriesColours = false;  // ensign: carries the colours
	bool bRelaysOrders = false;    // musician: orders carry on through him; line infantry near him load in cadence
	bool bTendsWounded = false;    // surgeon: a fallen man he reaches in time is wounded, not killed
	bool bServesGuns = false;      // artilleryman: goes to his side's guns and works them at full drill
	/** How many of the class take the field at once (0 = as many as the army has). */
	int32 MaxOnField = 0;

	/** Pure: aim-cone multiplier for a shot at this distance (1 inside AccurateM). */
	float RangeSpreadScale(float DistCm) const
	{
		const float AccurateCm = FMath::Max(100.f, AccurateM * 100.f);
		return DistCm <= AccurateCm ? 1.f : 1.f + FMath::Max(0.f, SpreadGrowth) * (DistCm - AccurateCm) / AccurateCm;
	}
	// ---- Look (visual only) ----
	/** Body height multiplier: grenadiers were picked for size, light infantry for agility. */
	float HeightScale = 1.f;
	EPA250Hat Hat = EPA250Hat::None;
};

/** Values of the class verbs (unit_classes.json "verbs"). */
struct FPA250ClassVerbs
{
	int32 VolleyMinShooters = 3;
	float VolleyWindowSeconds = 1.f;
	float VolleyShockPressure = 0.5f;  // added to the target squad's morale pressure...
	float VolleyShockSeconds = 6.f;    // ...fading out over this long
	float VolleyReachM = 90.f;
	float CoverResolveBonus = 0.3f;    // militia resolve x (1 + this) while most of the squad is in cover
	float OpenPressure = 0.2f;         // added pressure while it is engaged in the open
	float CoveredFraction = 0.5f;
	float GrenadierContactM = 8.f;     // a charge "reaches" a squad inside this distance
	float ForwardSpawnEnemyM = 25.f;   // light infantry may come on at a post with the enemy this close
	float RallyRadiusM = 25.f;
	float RallyCooldownSeconds = 30.f;
	float OrderRadiusM = 30.f;
	float SurrenderRadiusM = 4.f;
	// Ensign and the colours (verbs.ensign).
	float EnsignRadiusM = 25.f;            // squads this near the flying colours rally sooner
	float EnsignRallyScale = 0.5f;         // ...in this share of the usual time
	float ColoursFallPressure = 0.4f;      // the morale check on the side whose colours fall or are taken
	float ColoursFallSeconds = 10.f;
	float ColoursPickupM = 2.5f;           // a man this close to fallen colours takes them up (or carries them off)
	float ColoursCaptureSeconds = 2.f;     // an enemy must stand over them this long
	float ColoursSeekM = 20.f;             // a bot this close to fallen colours goes for them
	// Musician (verbs.musician).
	float MusicianRelayM = 40.f;           // an order carries this far beyond a musician who heard it
	float MusicianCadenceM = 15.f;         // volley-role men this near him load in cadence
	float MusicianCadenceScale = 0.92f;    // ...in this share of the usual time
	// Surgeon (verbs.surgeon).
	float SurgeonWindowSeconds = 25.f;     // a fallen man can be saved for this long
	float SurgeonTreatSeconds = 4.f;       // the surgeon must stay with him this long
	float SurgeonReachM = 2.5f;
	float SurgeonSeekM = 50.f;
	// Sapper (unit_classes.json verbs.sapper).
	float SapperBuildSeconds = 30.f;   // one sapper raises a whole breastwork in this long
	float SapperReachM = 3.f;          // he works on a place this close to him
	float SapperSeekM = 60.f;          // a bot goes to a place that needs him within this distance
	float SapperBurstSeconds = 3.f;    // the player's T: this much labour at a time, feet planted
	// The leader's file (unit_classes.json verbs.squad).
	int32 SquadFileMin = 6;            // the player leads at least this many men when the side has them
	int32 SquadFileMax = 8;
	float SquadFollowBehindM = 3.f;    // "follow me": the line forms this far behind him
	float SquadLineAheadM = 4.f;       // "form line here": this far in front of him
	float SquadOrderSeconds = 45.f;    // how long another squad obeys an officer's order
	float SquadAimSnapM = 8.f;         // the enemy within this distance of his aim point is "his target"
	float SquadAimRayM = 2.f;          // ...or within this distance of the line he is looking along
	float SquadEnsureSeconds = 1.f;    // how often the file is made up again
};

/** What a grenadier charge does to the squad it reaches: a wavering squad breaks, a steady one is shaken. */
enum class EPA250Morale : uint8;
PLAYAMERICA250_API EPA250Morale PA250ChargeRoutResult(EPA250Morale Target);

namespace PA250UnitClasses
{
/** unreal/PA250Sim/data/unit_classes.json. */
PLAYAMERICA250_API FString DataPath();
/** Read the class data. False (and the built-in table stays in force) when the file is missing,
 *  unreadable, or lacks a class in RequiredClasses; OutErrors says what was wrong. KitFor loads
 *  DataPath() by itself on first use. */
PLAYAMERICA250_API bool LoadFromFile(const FString& Path, TArray<FString>* OutErrors = nullptr);
/** True while kits are served from the data file. */
PLAYAMERICA250_API bool LoadedFromData();
/** Times a kit has been served from the built-in fallback table since the last reset. */
PLAYAMERICA250_API int32 FallbackUses();
PLAYAMERICA250_API void ResetFallbackUses();
/** Classes the data file must cover for a side (every class it can field). */
PLAYAMERICA250_API const TArray<FName>& RequiredClasses(FName Faction);
PLAYAMERICA250_API const FPA250ClassVerbs& Verbs();
/** For a test that provokes the fallback on purpose: while one of these is alive the fallback is
 *  still used and counted, but logged as Display ("expected by a test") instead of Error. Unreal
 *  fails a commandlet that logged an error, and a real fallback is meant to fail it. */
struct PLAYAMERICA250_API FScopedExpectFallback
{
	FScopedExpectFallback();
	~FScopedExpectFallback();
};

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
