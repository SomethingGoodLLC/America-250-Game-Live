// PA250CampaignArcPanels.h — the mission tracker and the end screen of the campaign map, as text.
// Pure functions over the sim's state (pa250::mission_defs, CampaignState): the map widget lays the
// text out in Slate, and the PA250Battle commandlet checks it without a window. Display only.
#pragma once

#include "CoreMinimal.h"
#include "pa250/types.hpp"

namespace PA250ArcPanels
{
/** What the tracker shows right now. */
struct FTracker
{
	/** False when the mission file is not loaded: the panel hides. */
	bool bVisible = false;
	FString Heading;   // "MISSION" / "NEXT MISSION" / "MISSIONS DONE"
	FString Title;
	FString Flavor;
	FString Goal;
	FString Progress;  // where the goal stands, from the sim
	FString Deadline;  // empty when the mission has none
	/** Standing warnings and news: enlistments, evacuation, headquarters lost, the guns, sieges. */
	TArray<FString> Notices;
	// Actions the tracker offers (the widget shows a button for each that is true).
	bool bCanSendForGuns = false;
	int32 GunsCost = 0;
	bool bCanHaulGuns = false;   // the selected army stands where the guns lie
	bool bCanLeaveGuns = false;  // the selected army is hauling them
	bool bCanReenlist = false;   // the selected army has companies not yet signed for the new year
	int32 ReenlistCost = 0;
	FString SelectedArmyName;
};
/** SelectedArmyId may be empty. */
PLAYAMERICA250_API FTracker Tracker(const pa250::CampaignState& State, const FString& SelectedArmyId);

/** The card shown once when a mission of the player's ends: what happened and what it paid or cost. */
struct FMissionCard
{
	bool bDone = false;   // complete (true) or failed
	FString Heading;      // "MISSION DONE" / "MISSION FAILED"
	FString Title;
	FString Summary;      // the mission file's own line
	TArray<FString> Lines;  // each thing that changed, with where it stands now
	FString Next;         // the mission that is open now or opens next
};
PLAYAMERICA250_API FMissionCard MissionCard(const pa250::MissionResult& Result, const pa250::CampaignState& State);

/** Standing notices for an army that cannot besiege where it stands or where it is marching (it has
 *  no general), with how to get one. Empty when nothing of the kind applies. */
PLAYAMERICA250_API TArray<FString> SiegeNotices(const pa250::CampaignState& State);

/** The end screen. */
struct FEnd
{
	bool bDecided = false;
	bool bPlayerWon = false;
	FString Headline;  // "VICTORY" / "DEFEAT"
	FString Subline;   // who won, in a line
	TArray<FString> Lines;
};
PLAYAMERICA250_API FEnd EndScreen(const pa250::CampaignState& State);

/** "3 days", "1 day", "5 hours". */
PLAYAMERICA250_API FString TimeLeft(int64 Hours);

/** Top-bar line while the campaign runs: towns you hold against the Crown, both scores, time left.
 *  Empty once the campaign is decided or the rules are not loaded. */
PLAYAMERICA250_API FString LiveScoreboard(const pa250::CampaignState& State);
}
