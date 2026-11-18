// PA250BattleWeather.h — Battle Feel M4 weather seam.
// BattleSetup has no weather field yet (proposed in PROGRESS.md, "Proposed contract changes").
// One accessor: a test's SetOverride, else -PA250Weather=rain|dry on the command line (debug and
// probe runs), else dry. When the contract gains weather, only Get changes.
#pragma once

#include "CoreMinimal.h"

enum class EPA250Weather : uint8
{
	Dry,
	Rain
};

namespace PA250BattleWeather
{
/** Pure display of the enum. */
inline const TCHAR* Words(EPA250Weather W)
{
	return W == EPA250Weather::Rain ? TEXT("rain") : TEXT("dry");
}

/** Parse "rain" / "dry" (case-insensitive). Unknown → dry. */
inline EPA250Weather FromString(const FString& S)
{
	return S.Equals(TEXT("rain"), ESearchCase::IgnoreCase) ? EPA250Weather::Rain : EPA250Weather::Dry;
}

/**
 * The field's weather for musket feel / wet powder.
 * Order: SetOverride (tests) → -PA250Weather=… (read once) → Dry.
 * Logs once which it used.
 */
PLAYAMERICA250_API EPA250Weather Get(const UWorld* World = nullptr);

/** Debug / tests: pin weather until ClearOverride. */
PLAYAMERICA250_API void SetOverride(EPA250Weather W);
PLAYAMERICA250_API void ClearOverride();
PLAYAMERICA250_API bool HasOverride();
}
