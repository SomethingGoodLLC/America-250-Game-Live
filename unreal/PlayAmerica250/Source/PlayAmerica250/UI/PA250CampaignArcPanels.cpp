#include "PA250CampaignArcPanels.h"
#include "pa250/sim.hpp"

namespace
{
FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

FString TownName(const pa250::CampaignState& State, const std::string& Id)
{
	const auto It = State.provinces.find(Id);
	return It == State.provinces.end() ? ToF(Id) : ToF(It->second.name);
}

FString DateWords(pa250::YMDH V)
{
	static const TCHAR* Months[] = {TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"), TEXT("June"),
		TEXT("July"), TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")};
	return FString::Printf(TEXT("%d %s %d"), static_cast<int32>(V.d), Months[FMath::Clamp(static_cast<int32>(V.m), 1, 12) - 1], V.y);
}

std::string StatusOf(const pa250::CampaignState& State, const std::string& Id)
{
	const auto It = State.progress.missions.find(Id);
	return It == State.progress.missions.end() ? std::string("locked") : It->second.status;
}
}

FString PA250ArcPanels::TimeLeft(int64 Hours)
{
	if (Hours >= 48) return FString::Printf(TEXT("%lld days"), static_cast<long long>(Hours / 24));
	if (Hours >= 24) return TEXT("1 day");
	return FString::Printf(TEXT("%lld hours"), static_cast<long long>(FMath::Max<int64>(0, Hours)));
}

FString PA250ArcPanels::LiveScoreboard(const pa250::CampaignState& State)
{
	if (!pa250::campaign_rules_loaded() || State.progress.outcome.decided) return FString();
	const pa250::WindowScore W = pa250::window_score_snapshot(State);
	const bool bYouPatriot = State.player_faction == pa250::kContinental;
	const int32 YouTowns = bYouPatriot ? W.patriot_towns : W.british_towns;
	const int32 CrownTowns = bYouPatriot ? W.british_towns : W.patriot_towns;
	const int32 YouPoints = bYouPatriot ? W.patriot_points : W.british_points;
	const int32 CrownPoints = bYouPatriot ? W.british_points : W.patriot_points;
	return FString::Printf(TEXT("You %d · Crown %d · %d–%d · %s left"), YouTowns, CrownTowns, YouPoints, CrownPoints,
						   *TimeLeft(W.hours_remaining));
}

TArray<FString> PA250ArcPanels::SiegeNotices(const pa250::CampaignState& State)
{
	TArray<FString> Out;
	bool bAny = false;
	for (const auto& Ak : State.armies)
	{
		const pa250::Army& A = Ak.second;
		if (A.faction != State.player_faction) continue;
		// Where it stands, and where its march ends.
		std::string Dest = A.province;
		for (const std::string& EdgeId : A.path)
		{
			const auto E = State.edges.find(EdgeId);
			if (E == State.edges.end()) break;
			Dest = E->second.a == Dest ? E->second.b : E->second.a;
		}
		for (const std::string& Town : {A.province, Dest})
		{
			const std::string Why = pa250::why_cannot_besiege(State, Ak.first, Town);
			if (Why.empty()) continue;
			Out.AddUnique(ToF(Why));
			bAny = true;
		}
	}
	if (bAny)
	{
		const std::string How = pa250::how_to_form_army(State, State.player_faction);
		if (!How.empty()) Out.Add(ToF(How));
	}
	return Out;
}

PA250ArcPanels::FMissionCard PA250ArcPanels::MissionCard(const pa250::MissionResult& Result, const pa250::CampaignState& State)
{
	FMissionCard C;
	C.bDone = Result.outcome == "complete";
	C.Heading = C.bDone ? TEXT("MISSION DONE") : TEXT("MISSION FAILED");
	C.Title = ToF(Result.title);
	C.Summary = ToF(Result.summary);
	auto Signed = [](int32 V) { return FString::Printf(TEXT("%s%d"), V > 0 ? TEXT("+") : TEXT(""), V); };
	if (Result.gold != 0) C.Lines.Add(FString::Printf(TEXT("Treasury %s gold (now %d)"), *Signed(Result.gold), State.player_gold));
	if (Result.local_support != 0) C.Lines.Add(FString::Printf(TEXT("Support %s (now %d)"), *Signed(Result.local_support), State.local_support));
	if (Result.army_morale != 0) C.Lines.Add(FString::Printf(TEXT("Morale of your armies %s"), *Signed(Result.army_morale)));
	if (Result.army_readiness != 0) C.Lines.Add(FString::Printf(TEXT("Readiness of your armies %s"), *Signed(Result.army_readiness)));
	if (Result.british_attitude != 0) C.Lines.Add(FString::Printf(TEXT("The British envoy's attitude %s"), *Signed(Result.british_attitude)));
	if (!Result.stores_province.empty())
	{
		TArray<FString> Stores;
		if (Result.powder != 0) Stores.Add(FString::Printf(TEXT("%s powder"), *Signed(Result.powder)));
		if (Result.cartridges != 0) Stores.Add(FString::Printf(TEXT("%s cartridges"), *Signed(Result.cartridges)));
		if (Result.muskets != 0) Stores.Add(FString::Printf(TEXT("%s muskets"), *Signed(Result.muskets)));
		if (Result.provisions != 0) Stores.Add(FString::Printf(TEXT("%s provisions"), *Signed(Result.provisions)));
		if (Stores.Num() > 0) C.Lines.Add(FString::Printf(TEXT("Magazine at %s: %s"), *TownName(State, Result.stores_province), *FString::Join(Stores, TEXT(", "))));
	}
	if (Result.start_evacuation) C.Lines.Add(TEXT("The British begin to embark: hold the heights and Boston is yours"));
	if (C.Lines.Num() == 0) C.Lines.Add(C.bDone ? TEXT("Nothing else changed") : TEXT("It cost you nothing but the chance"));
	// What is open now, or opens next.
	const pa250::MissionDef* Current = nullptr;
	const pa250::MissionDef* Upcoming = nullptr;
	for (const pa250::MissionDef& M : pa250::mission_defs())
	{
		if (M.faction != State.player_faction) continue;
		const std::string Status = StatusOf(State, M.id);
		if (Status == "active" && !Current) Current = &M;
		if (Status == "locked" && !Upcoming) Upcoming = &M;
	}
	if (Current) C.Next = FString::Printf(TEXT("Now: %s. %s"), *ToF(Current->title), *ToF(Current->goal));
	else if (Upcoming) C.Next = FString::Printf(TEXT("Next: %s. %s"), *ToF(Upcoming->title), *ToF(Upcoming->goal));
	else C.Next = TEXT("No mission is waiting: the campaign is yours to finish.");
	return C;
}

PA250ArcPanels::FTracker PA250ArcPanels::Tracker(const pa250::CampaignState& State, const FString& SelectedArmyId)
{
	FTracker T;
	const std::vector<pa250::MissionDef>& Defs = pa250::mission_defs();
	const pa250::CampaignRules& R = pa250::campaign_rules();
	const std::string Mine = State.player_faction;
	const int64 Now = State.now.hour_index;

	const pa250::MissionDef* Current = nullptr;
	const pa250::MissionDef* Next = nullptr;
	int32 Total = 0, Done = 0;
	for (const pa250::MissionDef& M : Defs)
	{
		if (M.faction != Mine) continue;
		++Total;
		const std::string Status = StatusOf(State, M.id);
		if (Status == "complete") ++Done;
		if (Status == "active" && !Current) Current = &M;
		if (Status == "locked" && !Next) Next = &M;
	}
	T.bVisible = Total > 0 || pa250::campaign_rules_loaded();
	if (Current)
	{
		T.Heading = FString::Printf(TEXT("MISSION %d OF %d"), Done + 1, Total);
		T.Title = ToF(Current->title);
		T.Flavor = ToF(Current->flavor);
		T.Goal = ToF(Current->goal);
		T.Progress = ToF(pa250::mission_progress_text(State, Mine, Current->complete));
		const auto It = State.progress.missions.find(Current->id);
		if (It != State.progress.missions.end() && It->second.deadline_hour > 0)
		{
			T.Deadline = FString::Printf(TEXT("%s left"), *TimeLeft(It->second.deadline_hour - Now));
		}
	}
	else if (Next)
	{
		T.Heading = TEXT("NEXT MISSION");
		T.Title = ToF(Next->title);
		T.Goal = ToF(Next->goal);
		const bool bWaitsForDate = Next->has_available_from && Now < pa250::from_ymdh(Next->available_from).hour_index;
		T.Progress = bWaitsForDate ? FString::Printf(TEXT("Not before %s"), *DateWords(Next->available_from))
			: (Next->prerequisite_must_succeed ? FString(TEXT("Needs the mission before it done")) : FString(TEXT("Opens shortly")));
	}
	else if (Total > 0)
	{
		T.Heading = TEXT("MISSIONS");
		T.Title = FString::Printf(TEXT("%d of %d done"), Done, Total);
	}

	// ---- Standing notices ----
	const pa250::CampaignProgress& P = State.progress;
	if (P.evacuation_hours_remaining > 0)
	{
		T.Notices.Add(FString::Printf(TEXT("The British sail from %s in %s unless they retake %s."), *TownName(State, R.british_base),
			*TimeLeft(P.evacuation_hours_remaining), *TownName(State, R.heights)));
	}
	if (P.hq_lost_hours > 0)
	{
		T.Notices.Add(FString::Printf(TEXT("%s, your headquarters, is in British hands: day %d of %d. Retake it or the campaign is lost."),
			*TownName(State, R.patriot_hq), P.hq_lost_hours / 24 + 1, R.hq_held_days));
	}
	if (P.support_low_hours > 0)
	{
		T.Notices.Add(FString::Printf(TEXT("Support is under %d: day %d of %d before the country gives up the cause."), R.support_floor,
			P.support_low_hours / 24 + 1, R.support_floor_days));
	}
	const auto Selected = State.armies.find(TCHAR_TO_UTF8(*SelectedArmyId));
	const bool bMineSelected = Selected != State.armies.end() && Selected->second.faction == Mine;
	if (bMineSelected) T.SelectedArmyName = ToF(Selected->second.name);
	if (P.enlistment_warned && !P.enlistments_expired && Mine == pa250::kContinental)
	{
		int32 Owed = 0;
		for (const auto& Ak : State.armies)
		{
			if (Ak.second.faction != Mine) continue;
			for (const auto& U : Ak.second.units) Owed += pa250::reenlist_cost(State, Ak.first, U.first);
		}
		const int64 Left = pa250::from_ymdh(R.enlist_expiry).hour_index - Now;
		T.Notices.Add(Owed > 0
			? FString::Printf(TEXT("Enlistments run out in %s. %d gold re-enlists every company; the rest go home. Select an army to re-enlist it."), *TimeLeft(Left), Owed)
			: FString::Printf(TEXT("Enlistments run out in %s. Every company has re-enlisted."), *TimeLeft(Left)));
		if (bMineSelected)
		{
			for (const auto& U : Selected->second.units) T.ReenlistCost += pa250::reenlist_cost(State, Selected->first, U.first);
			T.bCanReenlist = T.ReenlistCost > 0;
		}
	}
	// An army that cannot take the fort it stands before, or is marching on.
	T.Notices.Append(SiegeNotices(State));
	// The siege guns.
	const pa250::CampaignProject& K = P.knox;
	for (const pa250::MissionDef& M : Defs)
	{
		if (M.faction == Mine && M.complete.type == "project_status" && StatusOf(State, M.id) == "active" && K.status != "underway" && K.status != "delivered")
		{
			T.bCanSendForGuns = true;
		}
	}
	T.GunsCost = R.siege_train_gold;
	if (K.status == "underway")
	{
		T.Notices.Add(FString::Printf(TEXT("The siege guns are on the road: %s to %s."), *TimeLeft(K.ready_hour - Now), *TownName(State, R.patriot_hq)));
	}
	else if (K.status == "delivered" && !K.province.empty())
	{
		const auto Hauler = State.armies.find(K.army_id);
		T.Notices.Add(Hauler != State.armies.end()
			? FString::Printf(TEXT("The siege guns are with %s at %s."), *ToF(Hauler->second.name), *TownName(State, K.province))
			: FString::Printf(TEXT("The siege guns lie at %s. Select an army there to haul them."), *TownName(State, K.province)));
		if (bMineSelected)
		{
			T.bCanLeaveGuns = K.army_id == Selected->first;
			T.bCanHaulGuns = !T.bCanLeaveGuns && Selected->second.province == K.province;
		}
	}
	// Sieges touching the player.
	for (const auto& Pk : State.provinces)
	{
		const pa250::Province& Town = Pk.second;
		if (Town.siege_by.empty()) continue;
		if (Town.owner == Mine)
		{
			T.Notices.Add(FString::Printf(TEXT("%s is besieged: it falls in %s unless the besiegers leave or are beaten."), *ToF(Town.name), *TimeLeft(Town.siege_hours_remaining)));
		}
		else if (Town.siege_by == Mine)
		{
			T.Notices.Add(FString::Printf(TEXT("You besiege %s: it falls in %s if your army stays."), *ToF(Town.name), *TimeLeft(Town.siege_hours_remaining)));
		}
	}
	return T;
}

PA250ArcPanels::FEnd PA250ArcPanels::EndScreen(const pa250::CampaignState& State)
{
	FEnd E;
	const pa250::CampaignOutcome& O = State.progress.outcome;
	E.bDecided = O.decided;
	if (!O.decided) return E;
	E.bPlayerWon = O.winner == State.player_faction;
	E.Headline = E.bPlayerWon ? TEXT("VICTORY") : TEXT("DEFEAT");
	const bool bPatriots = O.winner == pa250::kContinental;
	E.Subline = O.reason == "window_score"
		? FString::Printf(TEXT("The campaign is decided on points for the %s."), bPatriots ? TEXT("Patriots") : TEXT("Crown"))
		: FString::Printf(TEXT("The %s have won the campaign for Boston."), bPatriots ? TEXT("Patriots") : TEXT("British"));
	for (const std::string& Line : O.summary) E.Lines.Add(ToF(Line));
	return E;
}
