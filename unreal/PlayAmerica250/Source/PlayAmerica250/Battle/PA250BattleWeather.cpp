#include "PA250BattleWeather.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
bool GHasOverride = false;
EPA250Weather GOverride = EPA250Weather::Dry;
/** The command line is read once (every trigger pull asks). */
struct FCommandLineWeather
{
	bool bSet = false;
	EPA250Weather Weather = EPA250Weather::Dry;
	FCommandLineWeather()
	{
		FString Cmd;
		if (FParse::Value(FCommandLine::Get(), TEXT("-PA250Weather="), Cmd))
		{
			bSet = true;
			Weather = PA250BattleWeather::FromString(Cmd);
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 weather: %s (%s; BattleSetup has no weather field yet)"),
			PA250BattleWeather::Words(Weather), bSet ? TEXT("-PA250Weather") : TEXT("default"));
	}
};
}

namespace PA250BattleWeather
{
EPA250Weather Get(const UWorld* /*World*/)
{
	if (GHasOverride) return GOverride;
	static const FCommandLineWeather FromCommandLine;
	return FromCommandLine.Weather;
}

void SetOverride(EPA250Weather W)
{
	GHasOverride = true;
	GOverride = W;
}

void ClearOverride()
{
	GHasOverride = false;
	GOverride = EPA250Weather::Dry;
}

bool HasOverride()
{
	return GHasOverride;
}
}
