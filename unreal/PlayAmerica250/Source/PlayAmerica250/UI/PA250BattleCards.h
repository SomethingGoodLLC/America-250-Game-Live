// PA250BattleCards.h — the two cards the campaign map shows around a battle (Slate, FPA250HudStyle).
// Before: both sides' men, on-field strength, rounds per man, morale and fortification, each line
// with the building or condition that produced it (BattleSetup.sides.*.factors).
// After: why the fight ended in plain words, losses, prisoners, and what changed on the map
// (BattleReport). Display only: nothing here changes campaign state.
// The text is built by pure functions so the PA250Battle commandlet can check it without a window.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "pa250/types.hpp"

namespace PA250BattleCards
{
/** One line of the pre-battle card. A headline row carries the side's final number and what it rests
 *  on; a change row under it carries a signed amount and the building or condition behind it. */
struct FRow
{
	FString Label;
	FString Value;
	FString Source;
	bool bChange = false;
};

/** Pure: one side's rows in reading order (men, on the field, rounds a man, morale, readiness,
 *  fortification, then anything else the sim reported). */
PLAYAMERICA250_API TArray<FRow> SideRows(const pa250::SideSetup& Side);

/** Pure: the line under the pre-battle card's title: who attacks, the kind of fight and its rule. */
PLAYAMERICA250_API FString ModeLine(const pa250::BattleSetup& Setup);

/** Pure: why the fight ended, as the player would say it. */
PLAYAMERICA250_API FString EndReasonSentence(const std::string& Reason, bool bPlayerWon);

/** The after-battle card as text. */
struct FAfter
{
	FString Title;
	FString Verdict;
	FString Reason;
	/** "Losses (men)", or "Losses (units)" for a report saved before battles counted men. */
	FString LossHeading;
	TArray<FString> Losses;
	TArray<FString> Prisoners;
	TArray<FString> MapChanges;
	TArray<FString> Notes;
};
/** Pure. PlayerFaction decides "yours" and "theirs"; province names come from State. */
PLAYAMERICA250_API FAfter AfterBattle(const pa250::BattleReport& Report, const pa250::CampaignState& State);

TSharedRef<SWidget> BuildPreBattle(const pa250::BattleSetup& Setup);
TSharedRef<SWidget> BuildAfterBattle(const pa250::BattleReport& Report, const pa250::CampaignState& State);
}
