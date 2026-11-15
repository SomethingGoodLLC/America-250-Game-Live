// PA250CampaignPassCases.cpp — commandlet checks for the campaign pass (marches, people, lobbying,
// decisions, letters, battle stories). Same FLog as PA250BattleCases.cpp; run by -run=PA250Battle.
#include "PA250BattleCases.h"
#include "Campaign/PA250MapWidget.h"
#include "Campaign/PA250MarchRoute.h"
#include "Campaign/PA250Subsystem.h"
#include "UI/PA250BattleCards.h"
#include "UI/PA250CampaignPassPanels.h"
#include "UI/PA250StoryArt.h"
#include "UI/PA250BattleStory.h"
#include "pa250/sim.hpp"

namespace PA250BattleCases
{
void RunMarch(FLog& Log)
{
	using namespace PA250MarchRoute;
	// A road with a bend: 100 east, then 100 south.
	TArray<FVector2D> Road = {FVector2D(0.f, 0.f), FVector2D(100.f, 0.f), FVector2D(100.f, 100.f)};
	Log.Check(FMath::IsNearlyEqual(PathLength(Road), 200.f), TEXT("march: the route's length is the road's, bend and all"));
	TArray<FStroke> Dashes;
	LayDashes(Road, 12.f, 8.f, 70.f, 0.f, Dashes);
	Log.Check(Dashes.Num() == 10, FString::Printf(TEXT("march: 12-on 8-off along 200 lays 10 dashes (%d)"), Dashes.Num()));
	int32 Faded = 0, OnRoad = 0, East = 0, South = 0;
	for (const FStroke& D : Dashes)
	{
		Faded += D.bMarched ? 1 : 0;
		// Every dash lies on the road, not on the straight line between the towns.
		const bool bOnFirst = FMath::Abs(D.Center.Y) < 1.f && D.Center.X >= 0.f && D.Center.X <= 100.f;
		const bool bOnSecond = FMath::Abs(D.Center.X - 100.f) < 1.f && D.Center.Y >= 0.f && D.Center.Y <= 100.f;
		OnRoad += (bOnFirst || bOnSecond) ? 1 : 0;
		East += FMath::IsNearlyEqual(D.YawDegrees, 0.f, 0.5f) ? 1 : 0;
		South += FMath::IsNearlyEqual(D.YawDegrees, 90.f, 0.5f) ? 1 : 0;
	}
	Log.Check(OnRoad == Dashes.Num() && East == 5 && South == 5,
		FString::Printf(TEXT("march: the dashes follow the road round the bend (%d of %d on it, %d east, %d south)"), OnRoad, Dashes.Num(), East, South));
	Log.Check(Faded == 3, FString::Printf(TEXT("march: 70 of 200 marched fades the three dashes behind the army (%d)"), Faded));
	{
		TArray<FStroke> None;
		LayDashes(Road, 12.f, 8.f, 0.f, 0.f, None);
		int32 F = 0;
		for (const FStroke& D : None) F += D.bMarched ? 1 : 0;
		Log.Check(F == 0, TEXT("march: nothing is faded before the first hour is marched"));
	}
	TArray<FStroke> Head;
	LayArrowhead(Road, 15.f, 28.f, Head);
	Log.Check(Head.Num() == 2, TEXT("march: the arrowhead is two strokes"));
	if (Head.Num() == 2)
	{
		// Both strokes end at the tip (the destination) and sweep back up the road, one to each side.
		auto Tip = [](const FStroke& S) {
			const float R = FMath::DegreesToRadians(S.YawDegrees);
			return S.Center - FVector2D(FMath::Cos(R), FMath::Sin(R)) * (S.Length * 0.5f);
		};
		const bool bAtEnd = Tip(Head[0]).Equals(FVector2D(100.f, 100.f), 0.5f) && Tip(Head[1]).Equals(FVector2D(100.f, 100.f), 0.5f);
		const bool bBack = Head[0].Center.Y < 100.f && Head[1].Center.Y < 100.f && (Head[0].Center.X - 100.f) * (Head[1].Center.X - 100.f) < 0.f;
		Log.Check(bAtEnd && bBack, TEXT("march: the arrowhead's point is on the destination and its barbs sweep back, one each side"));
	}
	Log.Check(ArrivalLabel(22, 4, 6) == TEXT("arrives 22 April, 6 a.m."), FString::Printf(TEXT("march: the arrival label reads as asked (%s)"), *ArrivalLabel(22, 4, 6)));
	Log.Check(ArrivalLabel(1, 12, 15) == TEXT("arrives 1 December, 3 p.m.") && ArrivalLabel(3, 7, 0) == TEXT("arrives 3 July, midnight") &&
				  ArrivalLabel(3, 7, 12) == TEXT("arrives 3 July, noon"),
		TEXT("march: afternoon, midnight and noon read as a clock does"));

	// The sim's march as the map will label it, and the halt order.
	pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
	St.now = pa250::from_ymdh(pa250::YMDH{1775, 4, 21, 23});
	St.ai_objective.clear();
	St.treaties.push_back({"ceasefire", 24 * 30, "", ""});
	pa250::CommandPayload Move;
	Move.kind = pa250::CommandKind::MoveArmy;
	Move.army_id = "continental_militia";
	Move.dest_province = "lexington";
	St = pa250::enqueue_command(St, Move);
	for (int32 i = 0; i < 3; ++i) St = pa250::advance_hour(St).state;
	const pa250::PathPlan Plan = pa250::march_plan(St, "continental_militia");
	const pa250::YMDH When = pa250::to_ymdh(Plan.arrival);
	Log.Check(Plan.ok && Plan.provinces.size() == 2 && Plan.provinces.back() == "lexington" && Plan.total_march_hours == 1,
		FString::Printf(TEXT("march: three hours out of Concord the plan is one hour short of Lexington (%d towns, %d h)"), static_cast<int32>(Plan.provinces.size()),
			Plan.total_march_hours));
	Log.Check(ArrivalLabel(When.d, When.m, When.h) == TEXT("arrives 22 April, 3 a.m."),
		FString::Printf(TEXT("march: its label (%s)"), *ArrivalLabel(When.d, When.m, When.h)));
	std::string Err;
	const pa250::CampaignState Halted = pa250::halt_army(St, "continental_militia", Err);
	const pa250::Army& A = Halted.armies.at("continental_militia");
	Log.Check(Err.empty() && A.path.empty() && A.province == "lincoln" && A.returning_hours == 1 && A.returning_edge == "lincoln-lexington",
		FString::Printf(TEXT("march: halted on the road it turns about for Lincoln (%s, %d h back)"), UTF8_TO_TCHAR(A.province.c_str()), A.returning_hours));
	// That one hour of turn-about is spent by the next march hour, so a probe that advances once
	// never sees it. Two hours into a three-hour edge still owes an hour after that tick.
	{
		pa250::CampaignState Live = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		Live.now = pa250::from_ymdh(pa250::YMDH{1775, 4, 19, 8});
		Live.ai_objective.clear();
		Live.treaties.push_back({"ceasefire", 24 * 30, "", ""});
		pa250::Army& Col = Live.armies.at("british_regulars");
		Col.province = "boston";
		Col.previous_province = "boston";
		Col.path = {"boston-cambridge"};
		Col.hours_into_edge = 2;
		const pa250::CampaignState Turned = pa250::halt_army(Live, "british_regulars", Err);
		const pa250::Army& Back = Turned.armies.at("british_regulars");
		Log.Check(Err.empty() && Back.path.empty() && Back.province == "boston" && Back.returning_hours == 2,
			FString::Printf(TEXT("march: two hours into a three-hour edge turns about for 2 h (%d)"), Back.returning_hours));
		const pa250::CampaignState Hour = pa250::advance_hour(Turned).state;
		const pa250::Army& Still = Hour.armies.at("british_regulars");
		Log.Check(Still.returning_hours == 1 && Still.province == "boston" && Still.path.empty(),
			FString::Printf(TEXT("march: after one march hour the turn-about is still live (%d h back to Boston)"), Still.returning_hours));
	}
	Log.Check(UPA250MapWidget::HaltMessage(TEXT("Massachusetts Militia"), TEXT("Lincoln"), 1) == TEXT("Massachusetts Militia turns about: 1 march hour back to Lincoln.") &&
				  UPA250MapWidget::HaltMessage(TEXT("Massachusetts Militia"), TEXT("Concord"), 0) == TEXT("Massachusetts Militia halts at Concord."),
		TEXT("march: the status line says which kind of halt it was"));
}

namespace
{
/** The game's own data files, loaded as UPA250Subsystem::LoadSimDataFiles loads them. */
bool LoadPassData(PA250BattleCases::FLog& Log)
{
	auto Read = [](const TCHAR* File) { return pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::SimDataPath(File))); };
	pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
	pa250::load_campaign_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::CampaignRulesPath())));
	pa250::load_missions(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::MissionsPath())));
	const auto Pe = pa250::load_persons(Read(TEXT("persons_boston_1775.json")));
	const auto Po = pa250::load_politics(Read(TEXT("bodies_boston_1775.json")), Read(TEXT("measures_boston_1775.json")));
	const auto De = pa250::load_decisions(Read(TEXT("decisions_boston_1775.json")));
	Log.Check(Pe.empty() && Po.empty() && De.empty() && pa250::persons_loaded() && pa250::politics_loaded() && pa250::decisions_loaded(),
		FString::Printf(TEXT("people: persons, bodies, measures and decisions load (%d, %d, %d errors)"), static_cast<int32>(Pe.size()), static_cast<int32>(Po.size()),
			static_cast<int32>(De.size())));
	return Pe.empty() && Po.empty() && De.empty();
}

pa250::CampaignState PassStateAt(pa250::YMDH At, bool bDecided)
{
	pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
	St.ai_objective.clear();
	St.treaties.push_back({"ceasefire", 24 * 500, "", ""});
	if (bDecided)
	{
		for (const char* Id : {"which_heights", "canada_expedition", "reenlist_how", "assault_or_guns", "take_the_heights_how", "burn_charlestown", "hold_or_sail"}) St.pass.decisions[Id] = "";
	}
	St.now = pa250::GameTime{pa250::from_ymdh(At).hour_index - 1};
	return pa250::advance_hour(St).state;
}
} // namespace

void RunPeople(FLog& Log)
{
	if (!LoadPassData(Log)) return;
	using namespace PA250PassPanels;
	pa250::CampaignState St = PassStateAt(pa250::YMDH{1775, 4, 21, 6}, true);

	// The forces list: who commands, and the men. The militia opens under Colonel Barrett (user
	// decision 2026-11-15), before the clock has run an hour.
	{
		const pa250::CampaignState Opening = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		Log.Check(ForceLine(Opening, Opening.armies.at("continental_militia")) == TEXT("Colonel James Barrett · 88 men"),
			FString::Printf(TEXT("people: at the first hour the militia reads as Colonel Barrett's (%s)"), *ForceLine(Opening, Opening.armies.at("continental_militia"))));
		bool bAllLed = true;
		for (const auto& Kv : Opening.armies) bAllLed &= !pa250::general_of(Opening, Kv.first).empty();
		Log.Check(bAllLed && pa250::army_unit_cap(Opening, "continental_militia") == 40, TEXT("people: no army opens as a detachment, and the militia may take in 40 units"));
	}
	Log.Check(ForceLine(St, St.armies.at("continental_militia")) == TEXT("Colonel James Barrett · 88 men"),
		FString::Printf(TEXT("people: the militia under its colonel names him and its men (%s)"), *ForceLine(St, St.armies.at("continental_militia"))));
	// Relieved through the panel's own button, the militia reads as a detachment.
	pa250::CampaignState Leaderless = St;
	{
		const TArray<FPersonRow> Start = People(St, FString(), FString());
		const FPersonRow* Barrett = Start.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("james_barrett"); });
		Log.Check(Barrett && Barrett->bPresent && Barrett->Name.Contains(TEXT("James Barrett")) && Barrett->Where == TEXT("Commands Massachusetts Militia") &&
					  Barrett->Traits == TEXT("Defensive, Popular (traits invented)") && Barrett->Note.EndsWith(TEXT("(documented)")) && Barrett->Actions.Num() == 1 &&
					  Barrett->Actions[0].Label == TEXT("Relieve"),
			FString::Printf(TEXT("people: Colonel Barrett commands the militia, with his traits marked invented and his note documented, and can be relieved (%s)"),
				Barrett ? *Barrett->Where : TEXT("missing")));
		if (Barrett && Barrett->Actions.Num() == 1)
		{
			std::string Err;
			Leaderless = pa250::apply_campaign_order(St, TCHAR_TO_UTF8(*Barrett->Actions[0].OrderJson), Err);
			Log.Check(Err.empty() && pa250::general_of(Leaderless, "continental_militia").empty(), TEXT("people: the Relieve button's order is the sim's relieve_person"));
		}
	}
	Log.Check(ForceLine(Leaderless, Leaderless.armies.at("continental_militia")) == TEXT("No general: a detachment · 88 men"),
		FString::Printf(TEXT("people: an army with no general reads as a detachment with its men (%s)"), *ForceLine(Leaderless, Leaderless.armies.at("continental_militia"))));
	Log.Check(ForceLine(St, St.armies.at("british_regulars")) == TEXT("Lieutenant Colonel Francis Smith · 96 men"),
		FString::Printf(TEXT("people: an army under a general names him and its men (%s)"), *ForceLine(St, St.armies.at("british_regulars"))));

	// The people panel: 22 Patriots in three groups; what a general can do depends on the town selected.
	TArray<FPersonRow> Rows = People(St, FString(), FString());
	int32 Generals = 0, Agents = 0;
	const FPersonRow* Heath = nullptr;
	for (const FPersonRow& R : Rows)
	{
		Generals += R.Group == TEXT("Generals") ? 1 : 0;
		Agents += R.Group == TEXT("Agents") ? 1 : 0;
		if (R.Id == TEXT("william_heath")) Heath = &R;
	}
	Log.Check(Rows.Num() == 22 && Generals == 10 && Agents == 7, FString::Printf(TEXT("people: the panel lists 22 people, 10 generals and 7 agents (%d, %d, %d)"), Rows.Num(), Generals, Agents));
	Log.Check(Heath && Heath->bPresent && Heath->Where == TEXT("At hand") && Heath->Traits == TEXT("Defensive, Adroit (traits invented)") && Heath->Actions.Num() == 0 &&
				  Heath->Hint.Contains(TEXT("Select a town")) && Heath->Note.EndsWith(TEXT("(documented)")),
		TEXT("people: Heath is at hand with his two traits marked invented, his note marked documented, and a hint until a town is selected"));
	bool bNoIds = true;
	for (const FPersonRow& R : Rows) bNoIds &= !R.Where.Contains(TEXT("_")) && !R.Name.Contains(TEXT("_")) && !R.Ability.Contains(TEXT("_"));
	Log.Check(bNoIds, TEXT("people: no raw id in any name, place or ability line"));
	{
		// Concord has no muster: the panel says why. Charlestown has a fort: the order is offered and the sim accepts it.
		const TArray<FPersonRow> AtConcord = People(St, TEXT("continental_militia"), TEXT("concord"));
		const FPersonRow* H = AtConcord.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("william_heath"); });
		Log.Check(H && H->Actions.Num() == 0 && H->Hint.Contains(TEXT("Militia Muster or a Fort")), TEXT("people: no army can be formed at Concord, and the panel says a muster is wanted"));
		// While Barrett has the militia a second general is not offered it, and the panel says why.
		{
			pa250::CampaignState Led = St;
			Led.armies.at("continental_militia").province = "charlestown";
			const TArray<FPersonRow> Taken = People(Led, TEXT("continental_militia"), TEXT("charlestown"));
			const FPersonRow* T = Taken.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("william_heath"); });
			Log.Check(T && T->Actions.Num() == 0 && T->Hint == TEXT("The army there already has a general."),
				FString::Printf(TEXT("people: Heath is not offered an army that has its colonel (%s)"), T ? *T->Hint : TEXT("missing")));
		}
		pa250::CampaignState Moved = Leaderless;
		Moved.armies.at("continental_militia").province = "charlestown";
		const TArray<FPersonRow> AtFort = People(Moved, TEXT("continental_militia"), TEXT("charlestown"));
		const FPersonRow* F = AtFort.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("william_heath"); });
		Log.Check(F && F->Actions.Num() == 1 && F->Actions[0].Label == TEXT("Form army at Charlestown"), TEXT("people: at Charlestown's fort the panel offers 'Form army at Charlestown'"));
		if (F && F->Actions.Num() == 1)
		{
			std::string Err;
			const pa250::CampaignState Formed = pa250::apply_campaign_order(Moved, TCHAR_TO_UTF8(*F->Actions[0].OrderJson), Err);
			Log.Check(Err.empty() && pa250::general_of(Formed, "continental_militia") == "william_heath" &&
						  ForceLine(Formed, Formed.armies.at("continental_militia")) == TEXT("General William Heath · 88 men"),
				TEXT("people: the button's order is the sim's form_army, and the forces line then names General William Heath"));
			const TArray<FPersonRow> After = People(Formed, TEXT("continental_militia"), TEXT("charlestown"));
			const FPersonRow* G = After.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("william_heath"); });
			Log.Check(G && G->Where == TEXT("Commands Massachusetts Militia") && G->Actions.Num() == 1 && G->Actions[0].Label == TEXT("Relieve"),
				TEXT("people: a general in command shows his army and can be relieved"));
		}
		// Agents: Gridley's works where a town is selected; Revere's alarm waits for the card.
		const FPersonRow* Gridley = AtFort.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("richard_gridley"); });
		const FPersonRow* Revere = AtFort.FindByPredicate([](const FPersonRow& R) { return R.Id == TEXT("paul_revere"); });
		Log.Check(Gridley && !Gridley->bPresent && Gridley->Where == TEXT("Expected 23 April 1775"), TEXT("people: a man not yet at hand shows the day he is expected"));
		Log.Check(Revere && Revere->Actions.Num() == 0 && Revere->Hint.Contains(TEXT("before a battle")) && Revere->Ability.StartsWith(TEXT("Alarm (2 uses left)")),
			TEXT("people: the alarm shows its uses and where it is spent"));
	}
	Log.Check(OrderRefusal(TEXT("no_muster")).Contains(TEXT("Militia Muster")) && !OrderRefusal(TEXT("some_new_code")).Contains(TEXT("_")),
		TEXT("people: a refusal is told in words, an unknown code without its underscores"));

	// The floor.
	Log.Check(FloorButton(St) == TEXT("Congress") && UnityChip(St) == TEXT("Unity 70 · 1 favor") && FloorChip(St) == TEXT("Congress · Unity 70 · 1 favor"),
		FString::Printf(TEXT("floor: the top bar has Congress and '%s'"), *FloorChip(St)));
	// The top bar itself (user decision 2026-11-15): Congress and People are in it; what gives way is
	// the title plaque on a narrow screen and the count of shipments when none is on the road.
	Log.Check(!TopBarShowsTitle(1660.f) && TopBarShowsTitle(1920.f) && TopBarShowsTitle(0.f),
		TEXT("top bar: the title plaque shows at 1920 units (16:9) and gives way at 1660 (1710x1112); an unmeasured bar keeps it"));
	Log.Check(StoresChip(8, 60, 10, 45, 0) == TEXT("Powder 8 · Cart 60 · Musk 10 · Prov 45") && StoresChip(8, 60, 10, 45, 2) == TEXT("Powder 8 · Cart 60 · Musk 10 · Prov 45 · in transit 2"),
		FString::Printf(TEXT("top bar: shipments are counted only when some are on the road (%s)"), *StoresChip(8, 60, 10, 45, 0)));
	Log.Check(FMath::IsNearlyEqual(FloorPanelWidth(1920.f, true), 700.f) && FloorPanelWidth(1660.f, true) < 700.f && FloorPanelWidth(1660.f, true) >= 560.f &&
				  1660.f - 16.f - FloorPanelWidth(1660.f, true) >= 1660.f * 0.5f + 175.f && FMath::IsNearlyEqual(FloorPanelWidth(1660.f, false), 700.f),
		FString::Printf(TEXT("top bar: the floor panel narrows to clear the mission tracker at 1660 units (%.0f), and is 700 when the tracker is folded"), FloorPanelWidth(1660.f, true)));
	Log.Check(ChanceBar(62) == TEXT("######....  62 in 100") && ChanceBar(5) == TEXT("#.........  5 in 100") && LeanWords(20) == TEXT("warm (+20)") && LeanWords(-10) == TEXT("cool (-10)"),
		TEXT("floor: a bloc's chance is ten marks and the figure; its lean is a word and the number"));
	const FFloor Fl = Floor(St);
	Log.Check(Fl.bLoaded && Fl.Heading == TEXT("Massachusetts Provincial Congress") && Fl.Blocs.Num() == 3 && Fl.Open.Num() == 3 && Fl.AtSea.Num() == 0,
		FString::Printf(TEXT("floor: the Provincial Congress, three blocs, three measures on the floor (%d, %d)"), Fl.Blocs.Num(), Fl.Open.Num()));
	if (Fl.Blocs.Num() == 3 && Fl.Open.Num() > 0)
	{
		const FBloc& B = Fl.Blocs[0];
		Log.Check(B.Name == TEXT("Committee men") && B.Lean == TEXT("warm (+20)") && !B.Moves.IsEmpty() && B.Actions.Num() == 3 && B.Actions[0].Label == TEXT("Write (+5)") &&
					  B.Actions[1].Label == TEXT("Call in a favor (+15)") && B.Actions[2].Label.StartsWith(TEXT("Seat ")),
			FString::Printf(TEXT("floor: each bloc shows its lean, what moves it, and Write, Call in a favor and Seat a delegate (%d actions)"), B.Actions.Num()));
		const FMeasure& M = Fl.Open[0];
		Log.Check(M.Title == TEXT("Raise the army") && M.When == TEXT("Vote on 23 April 1775") && M.Chances.Contains(TEXT("85 in 100")) && M.Chances.Contains(TEXT("Committee men")) &&
					  M.IfPassed.StartsWith(TEXT("If it passes: ")) && M.IfFailed.StartsWith(TEXT("If it fails: ")) && M.Actions.Num() == 1 && M.Actions[0].Label == TEXT("Vote now") &&
					  M.Note.EndsWith(TEXT("(documented)")),
			TEXT("floor: a measure shows its vote date, each bloc's bar, both outcomes, its note with its basis, and Vote now"));
	}
	{
		// The British floor: dispatches at sea with their dates, and the ministry's confidence.
		pa250::CampaignState Br = PassStateAt(pa250::YMDH{1775, 5, 1, 6}, true);
		Br.player_faction = "british";
		const FFloor Wh = Floor(Br);
		Log.Check(FloorButton(Br) == TEXT("Whitehall") && Wh.Heading == TEXT("The King's ministers") && Wh.Summary.StartsWith(TEXT("The ministry's confidence 60")) &&
					  UnityChip(Br).EndsWith(TEXT("Confidence 60")) && FloorChip(Br).StartsWith(TEXT("Whitehall · Confidence 60 · ")) && !FloorChip(Br).Contains(TEXT("Unity")),
			FString::Printf(TEXT("floor: the British player has Whitehall and the ministry's confidence, and no Unity in the bar (%s)"), *FloorChip(Br)));
		bool bReinforcement = false, bSend = false;
		for (const FString& L : Wh.AtSea) bReinforcement |= L.StartsWith(TEXT("Reinforcement with three generals: ")) && L.Contains(TEXT("25 May 1775"));
		for (const FMeasure& M : Wh.Open) bSend |= M.Id == TEXT("provision_convoy") && M.Actions.Num() == 1 && M.Actions[0].Label == TEXT("Send the dispatch") && M.When.Contains(TEXT("76 days"));
		Log.Check(bReinforcement && bSend, TEXT("floor: the reinforcement is at sea with its arrival date, and a convoy can be asked for (answer in 76 days)"));
	}

	// The decision.
	{
		const pa250::CampaignState June = PassStateAt(pa250::YMDH{1775, 6, 13, 0}, false);
		const FDecision D = Decision(June);
		Log.Check(D.bPending && D.Title == TEXT("Which heights first?") && D.Painting == TEXT("heights_1775") && D.Options.Num() == 2 && D.Note.Contains(TEXT("documented")),
			TEXT("decision: on 13 June the heights are put, with a painting key and two options"));
		if (D.Options.Num() == 2)
		{
			const FDecisionOption& O = D.Options[1];
			Log.Check(O.Label == TEXT("Fortify Dorchester") && O.Cost == TEXT("Cost: -15 gold for tools and timber") && O.Gain.StartsWith(TEXT("Gain: Dorchester gains")) &&
						  O.Later.StartsWith(TEXT("After 5 days: ")) && O.Commit.IsEmpty() && O.Actions.Num() == 1 && O.Actions[0].Label == TEXT("Choose"),
				TEXT("decision: an option prints its cost, its gain and its delayed consequence, and one Choose button"));
		}
		pa250::CampaignState Sept = PassStateAt(pa250::YMDH{1775, 9, 5, 0}, true);
		Sept.pass.decisions.erase("canada_expedition");
		Sept = pa250::advance_hour(Sept).state;
		const FDecision Canada = Decision(Sept);
		bool bArnold = false;
		if (Canada.Options.Num() == 2)
		{
			for (const FAction& A : Canada.Options[0].Actions) bArnold |= A.Label == TEXT("Send Captain Benedict Arnold");
		}
		Log.Check(Canada.bPending && Canada.Options.Num() == 2 && Canada.Options[0].Commit.StartsWith(TEXT("Needs a general, who leaves the map for 120 days")) && bArnold,
			TEXT("decision: an option that commits a person offers one button for each who can go"));
		bool bPlaceholder = false;
		Log.Check(PA250StoryArt::AssetPathFor(TEXT("heights_1775")) == TEXT("/Game/PA250/Stories/T_Story_heights_1775.T_Story_heights_1775") &&
					  PA250StoryArt::PlaceholderPixel(TEXT("heights_1775"), 0.5f, 0.2f) != PA250StoryArt::PlaceholderPixel(TEXT("heights_1775"), 0.5f, 0.9f),
			TEXT("decision: a painting key names the asset wanted, and the placeholder has a sky and a ground"));
		(void)bPlaceholder;
	}

	// The pre-battle card: the commander beside his plate, and an enemy column not yet counted.
	{
		pa250::CampaignState B = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		B.ai_objective.clear();
		B.armies.at("continental_militia").province = "lexington";
		B.armies.at("british_regulars").province = "lexington";
		B.armies.at("british_regulars").previous_province = "menotomy";
		B.pending_battle = {true, "lexington", "british_regulars", "continental_militia"};
		B.pass.seeded = true;
		B.pass.persons["artemas_ward"].role = "general";
		B.pass.persons["artemas_ward"].army_id = "continental_militia";
		B.pass.persons["francis_smith"].role = "general";
		B.pass.persons["francis_smith"].army_id = "british_regulars";
		const pa250::BattleSetup Setup = pa250::build_battle_setup(B, "lexington");
		const PA250BattleCards::FCommander Mine = PA250BattleCards::Commander(Setup.sides.at("continental"));
		Log.Check(Mine.bGeneral && Mine.Name == TEXT("General Artemas Ward") && Mine.Initials == TEXT("AW") && Mine.Traits == TEXT("Defensive, Steadfast") &&
					  Mine.Line == TEXT("General Artemas Ward commands Massachusetts Militia"),
			FString::Printf(TEXT("card: the commander's block names him, his plate and his traits (%s / %s / %s)"), *Mine.Name, *Mine.Initials, *Mine.Traits));
		bool bTraitRow = false, bCommanderAmongRows = false;
		for (const PA250BattleCards::FRow& R : PA250BattleCards::SideRows(Setup.sides.at("continental")))
		{
			bTraitRow |= R.bChange && R.Value == TEXT("+8") && R.Source == TEXT("General Artemas Ward: Defensive");
			bCommanderAmongRows |= R.Label == TEXT("Commander");
		}
		Log.Check(bTraitRow && !bCommanderAmongRows, TEXT("card: his trait is a signed row under Morale with him as its source"));
		const pa250::SideSetup& Enemy = Setup.sides.at("british");
		Log.Check(Enemy.intel == "estimate", TEXT("card: the enemy column has not been counted"));
		const TArray<PA250BattleCards::FRow> Est = PA250BattleCards::EstimateRows(Enemy);
		Log.Check(Est.Num() >= 5 && Est[0].Label == TEXT("Men") && Est[0].Value == FString::Printf(TEXT("%d-%d"), Enemy.intel_men_low, Enemy.intel_men_high) &&
					  Est[3].Label == TEXT("Morale") && Est[3].Value == TEXT("?") && Est[3].Source.Contains(TEXT("scout")),
			FString::Printf(TEXT("card: uncounted, the enemy shows a rough count of men (%s) and question marks"), Est.Num() > 0 ? *Est[0].Value : TEXT("")));
		// Concord, counted: an organizer (and a scout) make the British Men row the bridge company,
		// not the whole column and not the old cap of 24.
		auto MenHeadline = [](const pa250::SideSetup& Side) {
			for (const PA250BattleCards::FRow& R : PA250BattleCards::SideRows(Side))
			{
				if (R.Label == TEXT("Men") && !R.bChange) return R.Value;
			}
			return FString(TEXT("none"));
		};
		auto FieldedMen = [](const pa250::SideSetup& Side) {
			int32 N = 0;
			for (const pa250::SideFactor& F : Side.factors)
			{
				if (F.label == "men") N += F.value;
			}
			return N;
		};
		auto Concord = [](bool bScout) {
			pa250::CampaignState C = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
			C.ai_objective.clear();
			C.armies.at("continental_militia").province = "concord";
			C.armies.at("british_regulars").province = "concord";
			C.armies.at("british_regulars").previous_province = "lincoln";
			C.pending_battle = {true, "concord", "british_regulars", "continental_militia"};
			C.pass.seeded = true;
			if (bScout)
			{
				C.pass.scouted_until[std::string("continental") + ":british_regulars"] = C.now.hour_index + 24;
			}
			else
			{
				C.pass.persons["samuel_adams"].role = "organizer";
				C.pass.persons["samuel_adams"].province = "concord";
			}
			return pa250::build_battle_setup(C, "concord");
		};
		for (const bool bScout : {false, true})
		{
			const pa250::BattleSetup ConcordSetup = Concord(bScout);
			const pa250::SideSetup& Brit = ConcordSetup.sides.at("british");
			const int32 Fielded = FieldedMen(Brit);
			const FString Headline = MenHeadline(Brit);
			const FString How = bScout ? TEXT("scout") : TEXT("organizer");
			Log.Check(Brit.intel == "exact" && Headline == FString::FromInt(Fielded) && Fielded == 88 && Headline != TEXT("96") && Headline != TEXT("24"),
				FString::Printf(TEXT("card: Concord %s shows British Men %s (fielded %d, not the column 96, not the old company 24)"), *How, *Headline, Fielded));
			const FString Sit = PA250BattleStory::SituationLine(ConcordSetup);
			Log.Check(Sit.Contains(TEXT("The enemy has 88")) && !Sit.Contains(TEXT("thought to be")),
				FString::Printf(TEXT("card: Concord %s situation names 88 men (%s)"), *How, *Sit));
		}
		// The agents offered under the card: Revere's alarm (the player defends).
		const TArray<FAction> Offered = CardAgents(B);
		bool bAlarm = false;
		for (const FAction& A : Offered) bAlarm |= A.Label == TEXT("Paul Revere: raise the alarm (2 uses left)");
		Log.Check(bAlarm, FString::Printf(TEXT("card: the alarm is offered before a battle the player defends (%d agents)"), Offered.Num()));
		if (bAlarm)
		{
			std::string Err;
			const pa250::CampaignState Alarmed = pa250::apply_campaign_order(B, "{\"order\":\"use_agent\",\"person\":\"paul_revere\"}", Err);
			const pa250::BattleSetup After = pa250::build_battle_setup(Alarmed, "lexington");
			bool bRow = false;
			for (const PA250BattleCards::FRow& R : PA250BattleCards::SideRows(After.sides.at("continental"))) bRow |= R.Label == TEXT("Alarm") && R.Value == TEXT("yes") && R.Source.Contains(TEXT("Paul Revere's alarm"));
			Log.Check(Err.empty() && After.sides.at("continental").alarmed && bRow, TEXT("card: once spent, the alarm is a row on the card with Paul Revere as its source"));
		}
	}
	pa250::unload_persons();
	pa250::unload_politics();
	pa250::unload_decisions();
}

void RunStory(FLog& Log)
{
	pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
	pa250::load_correspondence_catalog(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::CorrespondenceCatalogPath())));
	const pa250::CampaignState Opening = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
	auto BattleAt = [&Opening](const std::string& Town, const std::string& From) {
		pa250::CampaignState B = Opening;
		B.ai_objective.clear();
		B.armies.at("continental_militia").province = Town;
		B.armies.at("british_regulars").province = Town;
		B.armies.at("british_regulars").previous_province = From;
		B.pending_battle = {true, Town, "british_regulars", "continental_militia"};
		return B;
	};

	// M5: the detached commander's letter prints its cost and gain, and its orders are rows on the
	// next Concord card with the letter as their source.
	const std::vector<pa250::CommanderLetterChoice> Choices = pa250::commander_letter_choices();
	Log.Check(Choices.size() == 3 && Choices[0].label == "Preserve the company" && Choices[0].effects_summary.find("Cost: ") == 0 &&
				  Choices[0].effects_summary.find("Gain: ") != std::string::npos,
		TEXT("letters: the commander's letter has three answers, each with its cost and gain in words"));
	{
		pa250::CampaignState B = BattleAt("concord", "lincoln");
		for (const pa250::CommanderLetterFactor& F : Choices.empty() ? std::vector<pa250::CommanderLetterFactor>{} : Choices[0].factors)
		{
			pa250::TownFactor T;
			T.province = "concord";
			T.faction = "continental";
			T.label = F.label;
			T.value = F.value;
			T.source = "Letter, Detached command: timing at Concord: Preserve the company";
			T.one_battle = true;
			B.pass.town_factors.push_back(T);
		}
		const pa250::BattleSetup Setup = pa250::build_battle_setup(B, "concord");
		bool bEarly = false, bSaved = false;
		for (const PA250BattleCards::FRow& R : PA250BattleCards::SideRows(Setup.sides.at("continental")))
		{
			bEarly |= R.Label == TEXT("Withdraws early") && R.Value == TEXT("50%") && R.Source.StartsWith(TEXT("Letter, Detached command: timing at Concord"));
			bSaved |= R.Label == TEXT("Magazine saved") && R.Value == TEXT("50%");
		}
		Log.Check(bEarly && bSaved, TEXT("letters: 'Preserve the company' shows on the Concord card as Withdraws early 50% and Magazine saved 50%, with the letter as their source"));
	}

	// M6: the stories.
	FString Why;
	Log.Check(PA250BattleStory::Load(Why, true) && PA250BattleStory::NumStories() == 7 && PA250BattleStory::NumFallbacks() == 9,
		FString::Printf(TEXT("story: battle_stories.json loads (%d stories, %d terrain fallbacks) %s"), PA250BattleStory::NumStories(), PA250BattleStory::NumFallbacks(), *Why));
	int32 Found = 0, Keyed = 0, Short = 0, NoCredit = 0;
	FString Fallbacks;
	for (const auto& Pk : Opening.provinces)
	{
		const pa250::BattleSetup Setup = pa250::build_battle_setup(BattleAt(Pk.first, Pk.second.adjacent.empty() ? Pk.first : Pk.second.adjacent.front()), Pk.first);
		const PA250BattleStory::FStory St = PA250BattleStory::StoryFor(Setup);
		Found += St.bFound ? 1 : 0;
		Keyed += St.bFound && !St.bFallback ? 1 : 0;
		Short += St.Sentences.Num() < 3 || St.Sentences.Num() > 4 ? 1 : 0;
		NoCredit += St.Credit.IsEmpty() || St.Title.IsEmpty() || St.Date.IsEmpty() ? 1 : 0;
		if (St.bFallback) Fallbacks += FString::Printf(TEXT("%s%s:%s"), Fallbacks.IsEmpty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(Pk.first.c_str()), *St.Key);
	}
	Log.Check(Found == 13 && Short == 0 && NoCredit == 0, FString::Printf(TEXT("story: all 13 towns get a story of three or four sentences with a title, a date and a credit (%d found)"), Found));
	Log.Check(Keyed >= 6, FString::Printf(TEXT("story: %d towns have their own story or their scenario's; the rest fall back by terrain (%s)"), Keyed, *Fallbacks));
	{
		const pa250::BattleSetup Lex = pa250::build_battle_setup(BattleAt("lexington", "menotomy"), "lexington");
		const PA250BattleStory::FStory St = PA250BattleStory::StoryFor(Lex);
		Log.Check(St.Key == TEXT("lexington") && St.Title == TEXT("Lexington Common") && St.Date == TEXT("19 April 1775") && St.Image == TEXT("lexington_common") &&
					  St.Credit.Contains(TEXT("Amos Doolittle")) && St.Credit.Contains(TEXT("1775")) && St.Sentences.Num() == 4,
			TEXT("story: Lexington has its own story, dated, with Doolittle's plate credited"));
		const FString Line = PA250BattleStory::SituationLine(Lex);
		Log.Check(Line.StartsWith(TEXT("Today you defend Lexington with 88 men, ")) && Line.Contains(TEXT("rounds a man")) && Line.Contains(TEXT("no general")) && !Line.Contains(TEXT("_")),
			FString::Printf(TEXT("story: one line gives the player's own situation from the card (%s)"), *Line));
		Log.Check(PA250BattleStory::NarrationAssetPath(St) == TEXT("/Game/PA250/Audio/Story/story_lexington.story_lexington"), TEXT("story: the narration asset is named from the data"));
		const pa250::BattleSetup Road = pa250::build_battle_setup(BattleAt("menotomy", "lexington"), "menotomy");
		Log.Check(PA250BattleStory::StoryFor(Road).Key == (Road.scenario_preset == "battle_road" ? TEXT("battle_road") : PA250BattleStory::StoryFor(Road).Key),
			TEXT("story: a Battle Road fight away from Lexington takes the Battle Road story"));
	}
	{
		const PA250BattleStory::FPan P = PA250BattleStory::Pan();
		const FVector2D A = PA250BattleStory::PanOffset(0.f, P), B = PA250BattleStory::PanOffset(P.Seconds * 0.5f, P), C = PA250BattleStory::PanOffset(P.Seconds, P);
		const FVector2D Step = PA250BattleStory::PanOffset(10.f, P) - PA250BattleStory::PanOffset(9.f, P);
		Log.Check(P.Seconds >= 20.f && P.Zoom > 1.f && A.X < B.X && B.X < C.X && FMath::Abs(C.X - A.X - P.Travel) < 0.001f && FMath::Abs(Step.X) < 0.01f &&
					  (P.Zoom - 1.f) * 0.5f >= P.Travel * 0.5f,
			FString::Printf(TEXT("story: the picture drifts slowly (%.0f s a pass, %.3f of its width in a second) and never shows its edge"), P.Seconds, Step.X));
	}
}
} // namespace PA250BattleCases
