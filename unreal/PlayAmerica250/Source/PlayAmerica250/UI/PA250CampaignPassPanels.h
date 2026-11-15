// PA250CampaignPassPanels.h — the campaign pass on the map HUD (Slate, FPA250HudStyle): the people
// panel (generals, delegates, organizers, envoys, agents and what each can do with what is selected),
// the floor of the body the player answers to (blocs, measures, dispatches at sea), the full-screen
// decision, and the agents offered under the pre-battle card. Everything shown comes from the sim's
// read-only snapshots (pa250::persons_json, politics_json, decision_json); every button issues a
// campaign order through the callback and changes nothing itself. The text is built by pure
// functions so the PA250Battle commandlet checks it without a window.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "pa250/types.hpp"

struct FSlateBrush;

namespace PA250PassPanels
{
/** A button: what it says, the order it issues, and why it is there. */
struct FAction
{
	FString Label;
	FString OrderJson;
	FString Tip;
};

struct FPersonRow
{
	FString Id;
	FString Name;     // with rank
	FString Group;    // "Generals" | "Delegates and organizers" | "Agents"
	FString Where;    // "At hand", "Commands ...", "Expected 6 August 1775", ...
	FString Traits;   // "Defensive, Steadfast" (generals)
	FString Ability;  // "Alarm (2 left): Rides ahead ..." (agents)
	FString Note;     // the one-line note, with "(documented)" or "(invented)"
	bool bPresent = false;
	TArray<FAction> Actions;
	/** Why nothing can be done with him and what is selected, when that is so. */
	FString Hint;
};
/** Pure. The player's people in reading order, with the orders each can be given now for the
 *  selected army and town (either may be empty). Only orders the sim would accept are offered. */
PLAYAMERICA250_API TArray<FPersonRow> People(const pa250::CampaignState& State, const FString& SelectedArmyId, const FString& SelectedProvinceId);

/** Pure: "General William Heath · 88 men" or "No general: a detachment · 88 men". */
PLAYAMERICA250_API FString ForceLine(const pa250::CampaignState& State, const pa250::Army& Army);
/** Pure: the top-bar chip, "Unity 70 · 1 favor" (empty when no body is loaded). */
PLAYAMERICA250_API FString UnityChip(const pa250::CampaignState& State);
/** Pure: the top-bar button that opens the floor: "Congress" or "Whitehall" (empty: no body). */
PLAYAMERICA250_API FString FloorButton(const pa250::CampaignState& State);
/** Pure: a bloc's chance of voting for a measure as ten marks and the figure: "######....  62 in 100". */
PLAYAMERICA250_API FString ChanceBar(int32 Pct);
/** Pure: a bloc's lean in words: "warm (+20)", "cool (-10)", "divided (0)". */
PLAYAMERICA250_API FString LeanWords(int32 Lean);
/** Pure: a refused order's code in words for the status line. */
PLAYAMERICA250_API FString OrderRefusal(const FString& Code);

struct FBloc
{
	FString Id, Name, Lean, Moves, Delegate;
	TArray<FAction> Actions;
};
struct FMeasure
{
	FString Id, Title, Text, When, Chances, IfPassed, IfFailed, Note, Status;
	TArray<FAction> Actions;
};
struct FFloor
{
	bool bLoaded = false;
	FString Heading;   // the body's name
	FString Summary;   // favors, Unity and its floor, clerk slots; the ministry's confidence for the British
	TArray<FBloc> Blocs;
	TArray<FMeasure> Open;      // on the floor now
	TArray<FMeasure> Upcoming;  // not yet, or waiting to return
	TArray<FMeasure> Settled;   // passed or failed
	TArray<FString> AtSea;      // dispatches in transit, with their dates
};
PLAYAMERICA250_API FFloor Floor(const pa250::CampaignState& State);

struct FDecisionOption
{
	FString Id, Label, Cost, Gain, Later, Commit;  // Commit: "Needs a general for 120 days" or empty
	TArray<FAction> Actions;                       // one "Choose", or one per person who can go
};
struct FDecision
{
	bool bPending = false;
	FString Id, Title, Text, Painting, Note;
	TArray<FDecisionOption> Options;
};
PLAYAMERICA250_API FDecision Decision(const pa250::CampaignState& State);

/** Pure: the agents a pending battle offers on its card (alarm when the player defends; a scout for
 *  the enemy column not yet counted; an engineer when the player defends a town he holds). */
PLAYAMERICA250_API TArray<FAction> CardAgents(const pa250::CampaignState& State);

using FIssue = TFunction<void(const FString& OrderJson)>;
TSharedRef<SWidget> BuildPeople(const pa250::CampaignState& State, const FString& SelectedArmyId, const FString& SelectedProvinceId, FIssue Issue, TFunction<void()> Close);
TSharedRef<SWidget> BuildFloor(const pa250::CampaignState& State, FIssue Issue, TFunction<void()> Close);
TSharedRef<SWidget> BuildDecision(const pa250::CampaignState& State, FIssue Issue, const FSlateBrush* Painting);
TSharedRef<SWidget> BuildCardAgents(const pa250::CampaignState& State, FIssue Issue);
}
