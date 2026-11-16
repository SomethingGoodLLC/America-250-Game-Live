// PA250BattleLook.h — how the battle looks (M7): each level's light for each time band, the
// rendering tiers, and the tier this platform runs. All from unreal/PA250Sim/data/battle_look.json
// (protocol/schemas/battle_look.yaml). The band comes from the battle setup's hour_of_day. The
// level's own sun, sky light, fog and post-process volume are set when the battle begins; no level
// is regenerated. Display only.
#pragma once

#include "CoreMinimal.h"

/** One level's look in one band. */
struct FPA250LookSettings
{
	FRotator SunRot = FRotator::ZeroRotator;
	float SunLux = 0.f;
	FColor SunColor = FColor::White;
	float SkyIntensity = 1.f;
	float FogDensity = 0.f;
	float FogHeightFalloff = 0.f;
	bool bVolumetricFog = false;
	float ExposureEv = 0.f;
	float WhiteTemp = 6500.f;
	FVector Gain = FVector::OneVector;
	float Bloom = 0.f;
	float Vignette = 0.f;
};

struct FPA250LookTier
{
	FString Name;
	TMap<FString, float> Cvars;
};

struct FPA250BattleLookData
{
	bool bLoaded = false;
	float FrameRateBar = 0.f;
	/** [dawn, day, dusk]: hour ranges, both ends in the band. */
	TArray<FIntPoint> Bands[3];
	int32 DefaultBand = 1;
	/** Level name (L_Lexington...) to its three looks. */
	TMap<FString, TArray<FPA250LookSettings>> Levels;
	/** Lowest first. */
	TArray<FPA250LookTier> Tiers;
	/** Platform key (win64, mac) to the tier it runs. */
	TMap<FName, FString> Chosen;
	float ShadowCastM = 0.f;
	bool bUpdateRateOptimisation = false;
};

namespace PA250BattleLook
{
PLAYAMERICA250_API FString DataPath();
/** Reads the file. False, with the reasons, when it is missing or malformed; the data is then unloaded
 *  and the battle leaves every level as it was built. */
PLAYAMERICA250_API bool LoadFromFile(const FString& Path, TArray<FString>* OutErrors = nullptr);
/** The loaded data (DataPath() on first use; an Error line when it does not load). */
PLAYAMERICA250_API const FPA250BattleLookData& Get();

/** Pure: 0 dawn, 1 day, 2 dusk. An hour in no band (or -1: the setup did not say) takes the default. */
PLAYAMERICA250_API int32 BandForHour(int32 Hour, const TArray<FIntPoint> Bands[3], int32 DefaultBand);
PLAYAMERICA250_API const TCHAR* BandName(int32 Band);
/** Pure: the tier a platform runs: its chosen one when the file names the platform and the tier
 *  exists, else the lowest (bOutFallback). Empty when there is no tier at all. */
PLAYAMERICA250_API FString TierFor(FName Platform, const FPA250BattleLookData& Data, bool& bOutFallback);

/** Sets the level's lights for the band of HourOfDay and the platform's tier. -PA250Hour=<h> and
 *  -PA250Look=<tier> override (debug battles and measurement). bRelight false leaves the lights as
 *  the level was built and sets the tier only (a debug battle with no hour given: its campaign
 *  clock is not a battle's hour). Returns false when nothing was applied. */
PLAYAMERICA250_API bool Apply(UWorld* World, int32 HourOfDay, bool bRelight = true);
/** What was applied last (for the trace and the cases): "L_Concord/day/low", or empty. */
PLAYAMERICA250_API FString Applied();
}
