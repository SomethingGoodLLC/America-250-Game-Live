// PA250BattleStory.h — the history card shown when the player goes to battle ("To arms!"): a
// full-screen picture with a slow pan, the battle's title and date, three or four sentences of
// documented history, one line on the player's own situation from the pre-battle card, and the
// picture's credit. It stays up while the battle level loads, offers "Continue" when the field is
// ready, and the field then fades in. Data: unreal/PA250Sim/data/battle_stories.json (display only).
// The matching and the text are pure, so the PA250Battle commandlet checks them without a window.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "pa250/types.hpp"

namespace PA250BattleStory
{
struct FStory
{
	bool bFound = false;
	bool bFallback = false;  // matched by terrain, not by town or scenario
	FString Key;
	FString Title;
	FString Date;
	FString Image;      // key of the picture (PA250StoryArt)
	FString Credit;     // "Amos Doolittle, after Ralph Earl, 1775. Connecticut Historical Society."
	FString Narration;  // name of the narration asset, played if it has been imported
	TArray<FString> Sentences;
};

struct FPan
{
	float Seconds = 40.f;
	float Zoom = 1.12f;
	float Travel = 0.06f;
};

/** Reads battle_stories.json once (again when bForce). False, with the reason, when it cannot be read. */
PLAYAMERICA250_API bool Load(FString& OutError, bool bForce = false);
PLAYAMERICA250_API int32 NumStories();
PLAYAMERICA250_API int32 NumFallbacks();
PLAYAMERICA250_API FPan Pan();
/** Pure once loaded: the story for a battle. By town, then by scenario, then by the first of the
 *  town's terrain tags that has a fallback (the tags in the order the setup gives them). */
PLAYAMERICA250_API FStory StoryFor(const pa250::BattleSetup& Setup);
/** Pure: one line on the player's own situation, from the numbers of the pre-battle card. */
PLAYAMERICA250_API FString SituationLine(const pa250::BattleSetup& Setup);
/** Pure: the picture's offset (in shares of its own size) at a time: a slow drift there and back. */
PLAYAMERICA250_API FVector2D PanOffset(float Seconds, const FPan& Pan);
/** Content path of a story's narration ("" when the story has none). */
PLAYAMERICA250_API FString NarrationAssetPath(const FStory& Story);
}

/** The story screen. Plain Slate with no UObject access after construction, so the engine's
 *  loading screen (which paints on its own thread while the level loads) can show it too. */
class SPA250BattleStoryScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPA250BattleStoryScreen) {}
	SLATE_ARGUMENT(PA250BattleStory::FStory, Story)
	SLATE_ARGUMENT(FString, Situation)
	SLATE_ARGUMENT(FSlateBrush, Picture)
	SLATE_ARGUMENT(bool, bPlaceholder)
	/** Seconds (FPlatformTime) the pan started at, so a second screen carries on where the first was. */
	SLATE_ARGUMENT(double, StartSeconds)
	/** True once the field is ready: "Continue" replaces the waiting line. Null: never (the loading screen). */
	SLATE_ARGUMENT(TFunction<bool()>, IsReady)
	SLATE_EVENT(FSimpleDelegate, OnContinue)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FSlateBrush Picture;
	PA250BattleStory::FPan Pan;
	double StartSeconds = 0.0;
	TFunction<bool()> IsReady;
	FSimpleDelegate OnContinue;
};
