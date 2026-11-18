// PA250BattleFX.h — Battle Feel M5: one router from gameplay events to look/sound/shake.
// Data: battle_look.json "fx" (Niagara path, sound bank and its fallback, shake and its falloff).
// The only path for these events: the gun's report (CannonFired / CanisterFired), a round shot's
// strike (its shake and the earth over the view stay battle_feel.intensity.round_shot, through
// PA250Intensity::RoundShotStrike), a shell's burst (from PA250Blast) and a dud's landing.
// Tracker "grapeshot" maps to CanisterFired (no GrapeshotFired symbol).
#pragma once

#include "CoreMinimal.h"

class UWorld;

enum class EPA250BattleFXEvent : uint8
{
	CannonFired,
	RoundShotGroundStrike,
	CanisterFired,
	ShellBurst,
	ShellDud
};

struct FPA250BattleFXEntry
{
	FString NiagaraPath; // empty / null → no system
	FString SoundBank;
	FString FallbackSound; // played when SoundBank has no assets imported yet
	float ShakeScale = 0.f;
	float InnerM = 0.f;
	float OuterM = 0.f;
	bool bLoaded = false;
};

namespace PA250BattleFX
{
inline const TCHAR* EventName(EPA250BattleFXEvent E)
{
	switch (E)
	{
	case EPA250BattleFXEvent::CannonFired: return TEXT("CannonFired");
	case EPA250BattleFXEvent::RoundShotGroundStrike: return TEXT("RoundShotGroundStrike");
	case EPA250BattleFXEvent::CanisterFired: return TEXT("CanisterFired");
	case EPA250BattleFXEvent::ShellBurst: return TEXT("ShellBurst");
	case EPA250BattleFXEvent::ShellDud: return TEXT("ShellDud");
	}
	return TEXT("?");
}

/** Look up battle_look.fx.<event> (lazy-load look file). */
PLAYAMERICA250_API FPA250BattleFXEntry Lookup(EPA250BattleFXEvent Event);

/**
 * Fire the event at World location. ShellDud: sound only (no Niagara, no shake).
 * Shake uses PA250Intensity::ShakeScaleAt + Shake at the listener's distance (ListenerDistM when
 * given: tests). Counts for tests.
 */
PLAYAMERICA250_API void Fire(UWorld* World, EPA250BattleFXEvent Event, const FVector& At, float Volume = 1.f, float ListenerDistM = -1.f);
/** Tests: how many Niagara paths named in the data did not load (placeholders not yet authored). */
PLAYAMERICA250_API int32 MissingNiagaraCount();
/** Tests: re-read battle_look.json. */
PLAYAMERICA250_API void ReloadForTest();

/** A word of command's calls (battle_look.json audio.orders.<Key>: advance, halt, make_ready,
 *  present, fire, rally). False when the data has no such word. */
PLAYAMERICA250_API bool OrderCue(const FString& Key, FString& OutDrum, FString& OutFife);

PLAYAMERICA250_API int32 FireCount(EPA250BattleFXEvent Event);
PLAYAMERICA250_API float LastShakeScale(EPA250BattleFXEvent Event);
PLAYAMERICA250_API void ResetCounts();
}
