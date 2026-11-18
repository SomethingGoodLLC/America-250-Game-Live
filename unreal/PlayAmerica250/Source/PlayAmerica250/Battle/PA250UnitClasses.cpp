#include "PA250UnitClasses.h"
#include "PA250Morale.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

namespace
{
/** Kits read from unit_classes.json. Text is kept for the life of the process: kits hand out raw
 *  pointers into it, and a reload must never leave an old kit's label dangling. */
struct FLoadedClasses
{
	TMap<FName, TMap<FName, FPA250UnitClassKit*>> ByFaction;
	FPA250ClassVerbs Verbs;
	bool bLoaded = false;
	bool bTried = false;
};
FLoadedClasses& Loaded()
{
	static FLoadedClasses L;
	return L;
}
int32 GFallbackUses = 0;
TSet<FString> GFallbackLogged;
int32 GExpectFallback = 0; // > 0: a test is provoking the fallback on purpose

const TCHAR* Keep(const FString& Text)
{
	static TArray<TUniquePtr<FString>> Store;
	Store.Add(MakeUnique<FString>(Text));
	return **Store.Last();
}

const FPA250UnitClassKit& BuiltIn(FName Faction, FName UnitClass)
{
	const TArray<FPA250UnitClassKit>& Kits = Faction == TEXT("continental") ? ContinentalKits() : BritishKits();
	for (const FPA250UnitClassKit& K : Kits)
	{
		if (K.Id == UnitClass) return K;
	}
	return Kits[0];
}

bool KnownBuiltIn(FName Faction, FName UnitClass)
{
	const TArray<FPA250UnitClassKit>& Kits = Faction == TEXT("continental") ? ContinentalKits() : BritishKits();
	for (const FPA250UnitClassKit& K : Kits)
	{
		if (K.Id == UnitClass) return true;
	}
	return false;
}

const FPA250UnitClassKit& Fallback(FName Faction, FName UnitClass, const TCHAR* Why)
{
	++GFallbackUses;
	const FString Key = Faction.ToString() + TEXT("/") + UnitClass.ToString();
	if (!GFallbackLogged.Contains(Key))
	{
		// Loud, once for each class that falls back (KitFor is asked several times a second per soldier).
		GFallbackLogged.Add(Key);
		if (GExpectFallback > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 unit classes (expected by a test): fallback to the built-in kit for %s %s (%s)"), *Faction.ToString(), *UnitClass.ToString(), Why);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 unit classes: FALLBACK to the built-in kit for %s %s (%s). Fix %s; the built-in table has no engagement band or verb for it."),
				*Faction.ToString(), *UnitClass.ToString(), Why, *PA250UnitClasses::DataPath());
		}
	}
	return BuiltIn(Faction, UnitClass);
}
}

FString PA250UnitClasses::DataPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/unit_classes.json")));
}

const TArray<FName>& PA250UnitClasses::RequiredClasses(FName Faction)
{
	static const TArray<FName> British = {TEXT("line_infantry"), TEXT("grenadier"), TEXT("light_infantry"), TEXT("officer"), TEXT("dragoon"), TEXT("sapper"),
		TEXT("ensign"), TEXT("musician"), TEXT("surgeon"), TEXT("artilleryman")};
	static const TArray<FName> Continental = {TEXT("line_infantry"), TEXT("militia"), TEXT("rifleman"), TEXT("officer"), TEXT("dragoon"), TEXT("sapper"),
		TEXT("ensign"), TEXT("musician"), TEXT("surgeon"), TEXT("artilleryman")};
	return Faction == TEXT("continental") ? Continental : British;
}

bool PA250UnitClasses::LoadFromFile(const FString& Path, TArray<FString>* OutErrors)
{
	FLoadedClasses& L = Loaded();
	L.bTried = true;
	L.bLoaded = false;
	L.ByFaction.Reset();
	L.Verbs = FPA250ClassVerbs();
	TArray<FString> Errors;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Errors.Add(FString::Printf(TEXT("cannot read %s"), *Path));
	}
	else if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Errors.Add(FString::Printf(TEXT("%s is not valid JSON"), *Path));
	}
	const TSharedPtr<FJsonObject>* Factions = nullptr;
	if (Root.IsValid() && !Root->TryGetObjectField(TEXT("factions"), Factions))
	{
		Errors.Add(TEXT("no \"factions\" object"));
	}
	TMap<FName, TMap<FName, FPA250UnitClassKit*>> Read;
	if (Factions)
	{
		for (const auto& FactionPair : (*Factions)->Values)
		{
			const TSharedPtr<FJsonObject>* Classes = nullptr;
			if (!FactionPair.Value->TryGetObject(Classes)) continue;
			for (const auto& ClassPair : (*Classes)->Values)
			{
				const TSharedPtr<FJsonObject>* J = nullptr;
				if (!ClassPair.Value->TryGetObject(J)) continue;
				const FName Faction(*FactionPair.Key), Class(*ClassPair.Key);
				// Start from the built-in kit so a field the file leaves out keeps its old value.
				FPA250UnitClassKit* K = new FPA250UnitClassKit(KnownBuiltIn(Faction, Class) ? BuiltIn(Faction, Class) : FPA250UnitClassKit());
				K->Id = Class;
				FString S;
				if ((*J)->TryGetStringField(TEXT("label"), S)) K->Label = Keep(S);
				if ((*J)->TryGetStringField(TEXT("weapon"), S)) K->Weapon = Keep(S);
				if ((*J)->TryGetStringField(TEXT("blurb"), S)) K->Blurb = Keep(S);
				if ((*J)->TryGetStringField(TEXT("role"), S)) K->Role = Keep(S);
				if ((*J)->TryGetStringField(TEXT("verb"), S)) K->Verb = Keep(S);
				auto Num = [&J](const TCHAR* Name, float& Out) { double D = 0.0; if ((*J)->TryGetNumberField(Name, D)) Out = static_cast<float>(D); };
				auto Flag = [&J](const TCHAR* Name, bool& Out) { bool B = false; if ((*J)->TryGetBoolField(Name, B)) Out = B; };
				Num(TEXT("health"), K->MaxHealth);
				Num(TEXT("speed"), K->SpeedScale);
				Num(TEXT("spread"), K->SpreadScale);
				Num(TEXT("range"), K->RangeScale);
				Num(TEXT("reload"), K->ReloadScale);
				Num(TEXT("melee"), K->MeleeScale);
				Num(TEXT("height"), K->HeightScale);
				Num(TEXT("prefer_min_m"), K->PreferMinM);
				Num(TEXT("prefer_max_m"), K->PreferMaxM);
				Num(TEXT("accurate_m"), K->AccurateM);
				Num(TEXT("spread_growth"), K->SpreadGrowth);
				Num(TEXT("ticket_cost"), K->TicketCost);
				Num(TEXT("capture_weight"), K->CaptureWeight);
				Flag(TEXT("officer"), K->bOfficer);
				Flag(TEXT("volley_shock"), K->bVolleyShock);
				Flag(TEXT("steady_in_cover"), K->bSteadyInCover);
				Flag(TEXT("officer_hunter"), K->bOfficerHunter);
				Flag(TEXT("charge_routs"), K->bChargeRouts);
				Flag(TEXT("forward_spawn"), K->bForwardSpawn);
				Flag(TEXT("rallies"), K->bRallies);
				Flag(TEXT("takes_prisoners"), K->bTakesPrisoners);
				Flag(TEXT("builds_works"), K->bBuildsWorks);
				Flag(TEXT("carries_colours"), K->bCarriesColours);
				Flag(TEXT("relays_orders"), K->bRelaysOrders);
				Flag(TEXT("tends_wounded"), K->bTendsWounded);
				Flag(TEXT("serves_guns"), K->bServesGuns);
				{
					float Cap = static_cast<float>(K->MaxOnField);
					Num(TEXT("max_on_field"), Cap);
					K->MaxOnField = FMath::Max(0, FMath::RoundToInt(Cap));
				}
				if ((*J)->TryGetStringField(TEXT("bayonet"), S))
				{
					if (S == TEXT("all")) K->Bayonet = EPA250Bayonet::All;
					else if (S == TEXT("some")) K->Bayonet = EPA250Bayonet::Some;
					else if (S == TEXT("none")) K->Bayonet = EPA250Bayonet::None;
					else Errors.Add(FString::Printf(TEXT("%s %s: unknown bayonet '%s'"), *FactionPair.Key, *ClassPair.Key, *S));
				}
				if ((*J)->TryGetStringField(TEXT("hat"), S))
				{
					if (S == TEXT("none")) K->Hat = EPA250Hat::None;
					else if (S == TEXT("bearskin")) K->Hat = EPA250Hat::Bearskin;
					else if (S == TEXT("cap")) K->Hat = EPA250Hat::Cap;
					else if (S == TEXT("round_hat")) K->Hat = EPA250Hat::RoundHat;
					else if (S == TEXT("cocked_hat")) K->Hat = EPA250Hat::CockedHat;
					else Errors.Add(FString::Printf(TEXT("%s %s: unknown hat '%s'"), *FactionPair.Key, *ClassPair.Key, *S));
				}
				if (K->PreferMaxM < K->PreferMinM || K->AccurateM <= 0.f || K->TicketCost <= 0.f)
				{
					Errors.Add(FString::Printf(TEXT("%s %s: band %.0f-%.0f m, accurate %.0f m, ticket cost %.2f"), *FactionPair.Key, *ClassPair.Key, K->PreferMinM, K->PreferMaxM, K->AccurateM, K->TicketCost));
				}
				Read.FindOrAdd(Faction).Add(Class, K); // never freed: old kits may still be referenced
			}
		}
		for (const TCHAR* F : {TEXT("british"), TEXT("continental")})
		{
			for (const FName& Class : RequiredClasses(F))
			{
				const TMap<FName, FPA250UnitClassKit*>* Side = Read.Find(F);
				if (!Side || !Side->Contains(Class)) Errors.Add(FString::Printf(TEXT("%s has no kit for %s"), F, *Class.ToString()));
			}
		}
	}
	const TSharedPtr<FJsonObject>* V = nullptr;
	FPA250ClassVerbs Verbs;
	if (Root.IsValid() && Root->TryGetObjectField(TEXT("verbs"), V))
	{
		auto Get = [V](const TCHAR* Group, const TCHAR* Name, float& Out) {
			const TSharedPtr<FJsonObject>* G = nullptr;
			double D = 0.0;
			if ((*V)->TryGetObjectField(Group, G) && (*G)->TryGetNumberField(Name, D)) Out = static_cast<float>(D);
		};
		float MinShooters = static_cast<float>(Verbs.VolleyMinShooters);
		Get(TEXT("volley"), TEXT("min_shooters"), MinShooters);
		Verbs.VolleyMinShooters = FMath::Max(1, FMath::RoundToInt(MinShooters));
		Get(TEXT("volley"), TEXT("window_seconds"), Verbs.VolleyWindowSeconds);
		Get(TEXT("volley"), TEXT("shock_pressure"), Verbs.VolleyShockPressure);
		Get(TEXT("volley"), TEXT("shock_seconds"), Verbs.VolleyShockSeconds);
		Get(TEXT("volley"), TEXT("reach_m"), Verbs.VolleyReachM);
		Get(TEXT("militia"), TEXT("cover_resolve_bonus"), Verbs.CoverResolveBonus);
		Get(TEXT("militia"), TEXT("open_pressure"), Verbs.OpenPressure);
		Get(TEXT("militia"), TEXT("covered_fraction"), Verbs.CoveredFraction);
		Get(TEXT("grenadier"), TEXT("contact_m"), Verbs.GrenadierContactM);
		Get(TEXT("light_infantry"), TEXT("forward_spawn_enemy_m"), Verbs.ForwardSpawnEnemyM);
		Get(TEXT("officer"), TEXT("rally_radius_m"), Verbs.RallyRadiusM);
		Get(TEXT("officer"), TEXT("rally_cooldown_seconds"), Verbs.RallyCooldownSeconds);
		Get(TEXT("officer"), TEXT("order_radius_m"), Verbs.OrderRadiusM);
		Get(TEXT("dragoon"), TEXT("surrender_radius_m"), Verbs.SurrenderRadiusM);
		Get(TEXT("ensign"), TEXT("radius_m"), Verbs.EnsignRadiusM);
		Get(TEXT("ensign"), TEXT("rally_scale"), Verbs.EnsignRallyScale);
		Get(TEXT("ensign"), TEXT("colours_fall_pressure"), Verbs.ColoursFallPressure);
		Get(TEXT("ensign"), TEXT("colours_fall_seconds"), Verbs.ColoursFallSeconds);
		Get(TEXT("ensign"), TEXT("pickup_m"), Verbs.ColoursPickupM);
		Get(TEXT("ensign"), TEXT("capture_seconds"), Verbs.ColoursCaptureSeconds);
		Get(TEXT("ensign"), TEXT("seek_m"), Verbs.ColoursSeekM);
		Get(TEXT("musician"), TEXT("relay_m"), Verbs.MusicianRelayM);
		Get(TEXT("musician"), TEXT("cadence_m"), Verbs.MusicianCadenceM);
		Get(TEXT("musician"), TEXT("cadence_reload_scale"), Verbs.MusicianCadenceScale);
		Get(TEXT("surgeon"), TEXT("window_seconds"), Verbs.SurgeonWindowSeconds);
		Get(TEXT("surgeon"), TEXT("treat_seconds"), Verbs.SurgeonTreatSeconds);
		Get(TEXT("surgeon"), TEXT("reach_m"), Verbs.SurgeonReachM);
		Get(TEXT("surgeon"), TEXT("seek_m"), Verbs.SurgeonSeekM);
		Get(TEXT("sapper"), TEXT("build_seconds"), Verbs.SapperBuildSeconds);
		Get(TEXT("sapper"), TEXT("reach_m"), Verbs.SapperReachM);
		Get(TEXT("sapper"), TEXT("seek_m"), Verbs.SapperSeekM);
		Get(TEXT("sapper"), TEXT("burst_seconds"), Verbs.SapperBurstSeconds);
		float FileMin = static_cast<float>(Verbs.SquadFileMin), FileMax = static_cast<float>(Verbs.SquadFileMax);
		Get(TEXT("squad"), TEXT("file_min"), FileMin);
		Get(TEXT("squad"), TEXT("file_max"), FileMax);
		Verbs.SquadFileMin = FMath::Max(1, FMath::RoundToInt(FileMin));
		Verbs.SquadFileMax = FMath::Max(Verbs.SquadFileMin, FMath::RoundToInt(FileMax));
		Get(TEXT("squad"), TEXT("follow_behind_m"), Verbs.SquadFollowBehindM);
		Get(TEXT("squad"), TEXT("follow_spacing_m"), Verbs.SquadFollowSpacingM);
		Get(TEXT("squad"), TEXT("follow_turn_degrees"), Verbs.SquadFollowTurnDegrees);
		Get(TEXT("squad"), TEXT("run_when_behind_m"), Verbs.SquadRunWhenBehindM);
		Get(TEXT("squad"), TEXT("with_you_m"), Verbs.SquadWithYouM);
		Get(TEXT("squad"), TEXT("line_ahead_m"), Verbs.SquadLineAheadM);
		Get(TEXT("squad"), TEXT("order_seconds"), Verbs.SquadOrderSeconds);
		Get(TEXT("squad"), TEXT("aim_snap_m"), Verbs.SquadAimSnapM);
		Get(TEXT("squad"), TEXT("aim_ray_m"), Verbs.SquadAimRayM);
		Get(TEXT("squad"), TEXT("ensure_seconds"), Verbs.SquadEnsureSeconds);
		const TSharedPtr<FJsonObject>* M = nullptr;
		if ((*V)->TryGetObjectField(TEXT("morale"), M))
		{
			// Closed: the four thresholds the squads use, each required, nothing else.
			static const TCHAR* Keys[] = {TEXT("shaken_enter"), TEXT("shaken_exit"), TEXT("rout_enter"), TEXT("min_state_seconds")};
			float* Out[] = {&Verbs.Morale.ShakenEnter, &Verbs.Morale.ShakenExit, &Verbs.Morale.RoutEnter, &Verbs.Morale.MinStateSeconds};
			for (int32 k = 0; k < 4; ++k)
			{
				double D = 0.0;
				if ((*M)->TryGetNumberField(Keys[k], D)) *Out[k] = static_cast<float>(D);
				else Errors.Add(FString::Printf(TEXT("verbs.morale has no %s"), Keys[k]));
			}
			for (const auto& Pair : (*M)->Values)
			{
				if (Pair.Key == TEXT("note")) continue;
				bool bKnown = false;
				for (const TCHAR* K : Keys) bKnown |= Pair.Key == K;
				if (!bKnown) Errors.Add(FString::Printf(TEXT("verbs.morale: unknown key %s"), *Pair.Key));
			}
			if (!(Verbs.Morale.ShakenExit < Verbs.Morale.ShakenEnter && Verbs.Morale.ShakenEnter < Verbs.Morale.RoutEnter))
			{
				Errors.Add(TEXT("verbs.morale: needs shaken_exit < shaken_enter < rout_enter"));
			}
		}
		else
		{
			Errors.Add(TEXT("no verbs.morale object"));
		}
	}
	else if (Root.IsValid())
	{
		Errors.Add(TEXT("no \"verbs\" object"));
	}
	if (OutErrors) *OutErrors = Errors;
	if (Errors.Num() > 0)
	{
		if (GExpectFallback > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 unit classes (expected by a test): %s was refused: %s"), *Path, *FString::Join(Errors, TEXT("; ")));
		}
		else
		{
			for (const FString& E : Errors)
			{
				UE_LOG(LogTemp, Error, TEXT("PA250 unit classes: %s"), *E);
			}
			UE_LOG(LogTemp, Error, TEXT("PA250 unit classes: FALLBACK to the built-in table: %s was not loaded (%d problem(s))"), *Path, Errors.Num());
		}
		return false;
	}
	L.ByFaction = MoveTemp(Read);
	L.Verbs = Verbs;
	L.bLoaded = true;
	UE_LOG(LogTemp, Display, TEXT("PA250 unit classes: loaded from %s"), *Path);
	return true;
}

PA250UnitClasses::FScopedExpectFallback::FScopedExpectFallback() { ++GExpectFallback; }
PA250UnitClasses::FScopedExpectFallback::~FScopedExpectFallback() { --GExpectFallback; }

bool PA250UnitClasses::LoadedFromData() { return Loaded().bLoaded; }
int32 PA250UnitClasses::FallbackUses() { return GFallbackUses; }
void PA250UnitClasses::ResetFallbackUses()
{
	GFallbackUses = 0;
	GFallbackLogged.Reset();
}

const FPA250ClassVerbs& PA250UnitClasses::Verbs()
{
	if (!Loaded().bTried) LoadFromFile(DataPath());
	return Loaded().Verbs;
}

EPA250Morale PA250ChargeRoutResult(EPA250Morale Target)
{
	switch (Target)
	{
	case EPA250Morale::Steady: return EPA250Morale::Shaken;
	case EPA250Morale::Shaken:
	case EPA250Morale::Withdrawing:
	case EPA250Morale::Rallying: return EPA250Morale::Routing;
	default: return Target; // already running
	}
}

const FPA250UnitClassKit& PA250UnitClasses::KitFor(FName Faction, FName UnitClass)
{
	FLoadedClasses& L = Loaded();
	if (!L.bTried) LoadFromFile(DataPath());
	if (!L.bLoaded)
	{
		return Fallback(Faction, UnitClass, TEXT("the data file is not loaded"));
	}
	const FName Side = Faction == TEXT("continental") ? FName(TEXT("continental")) : FName(TEXT("british"));
	const TMap<FName, FPA250UnitClassKit*>* Kits = L.ByFaction.Find(Side);
	if (Kits)
	{
		if (FPA250UnitClassKit* const* K = Kits->Find(UnitClass)) return **K;
		// A class the built-in table knows but the file does not: the file is incomplete.
		if (KnownBuiltIn(Side, UnitClass)) return Fallback(Side, UnitClass, TEXT("the data file has no entry for this class"));
		// A class nobody knows gets the side's plain musket man, as it always did.
		if (FPA250UnitClassKit* const* Line = Kits->Find(TEXT("line_infantry"))) return **Line;
	}
	return Fallback(Side, UnitClass, TEXT("the data file has no entry for this side"));
}

const TArray<FName>& PA250UnitClasses::DisplayOrder()
{
	static const TArray<FName> Order = {TEXT("line_infantry"), TEXT("grenadier"), TEXT("light_infantry"), TEXT("militia"),
		TEXT("rifleman"), TEXT("sapper"), TEXT("artilleryman"), TEXT("ensign"), TEXT("musician"), TEXT("surgeon"), TEXT("officer"), TEXT("dragoon")};
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
