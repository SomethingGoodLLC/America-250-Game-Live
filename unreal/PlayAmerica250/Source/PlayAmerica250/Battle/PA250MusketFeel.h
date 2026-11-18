// PA250MusketFeel.h — Battle Feel M4: whether a trigger pull fires, flashes in the pan or misfires.
// Pure, so the 10k-shot test drives the very roll ARFCharacter::FireMusketAt uses.
// Chances: battle_feel.json "musket". A man with a training profile (every bot) misfires at the
// profile's MisfireChance (DA_CombatProfiles, stress-scaled); a man without one (the player) at
// musket.misfire_chance. Every man, the player too, can flash in the pan (musket.flash_in_pan_chance).
// Rain (PA250BattleWeather) multiplies both. Before 2026-11-16 the player never misfired.
#pragma once

#include "CoreMinimal.h"
#include "PA250BattleFeel.h"
#include "PA250BattleWeather.h"
#include "PA250CombatProfiles.h"

enum class EPA250TriggerResult : uint8
{
	Fires,
	FlashInPan, // the priming burns, the charge does not catch: a flash, no shot
	Misfire     // the flint gives no spark: a click, no shot
};

namespace PA250MusketFeel
{
struct FChances
{
	float Misfire = 0.f;
	float Flash = 0.f;
};

inline FChances ChancesFor(const FPA250BattleFeel& Feel, float ProfileMisfire, float Stress, EPA250Weather Weather)
{
	FChances C;
	C.Misfire = ProfileMisfire > 0.f ? ProfileMisfire : Feel.MusketMisfireChance;
	C.Flash = Feel.MusketFlashInPanChance;
	if (Weather == EPA250Weather::Rain)
	{
		C.Misfire *= Feel.MusketRainMisfireScale;
		C.Flash *= Feel.MusketRainFlashScale;
	}
	C.Misfire = UPA250CombatProfiles::MisfireChanceAt(C.Misfire, Stress);
	C.Flash = FMath::Clamp(C.Flash, 0.f, 1.f);
	return C;
}

/** One roll in [0, 1) decides the pull: flash first, then misfire, else it fires. */
inline EPA250TriggerResult Resolve(float Roll01, const FChances& C)
{
	if (Roll01 < C.Flash) return EPA250TriggerResult::FlashInPan;
	if (Roll01 < C.Flash + C.Misfire) return EPA250TriggerResult::Misfire;
	return EPA250TriggerResult::Fires;
}

/** The roll for a man's Nth trigger pull: his own stream from the battle seed, so it depends on
 *  neither the wall clock nor what other men drew this frame. */
inline float RollFor(uint64 BattleSeed, uint32 ManSalt, uint32 PullIndex)
{
	FRandomStream Rng(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(BattleSeed), ManSalt), GetTypeHash(PullIndex))));
	return Rng.FRand();
}
}
