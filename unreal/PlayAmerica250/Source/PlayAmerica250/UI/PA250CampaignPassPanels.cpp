#include "PA250CampaignPassPanels.h"
#include "PA250HudStyle.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "pa250/sim.hpp"

namespace
{
FString PassF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
std::string PassStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

TSharedPtr<FJsonObject> PassParse(const std::string& Text)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PassF(Text));
	FJsonSerializer::Deserialize(Reader, Root);
	return Root;
}

/** {"order":"...","k":"v",...} from pairs; values are ids, which need no escaping. */
FString PassOrder(std::initializer_list<TPair<const TCHAR*, FString>> Fields)
{
	FString Out = TEXT("{");
	for (const auto& F : Fields)
	{
		if (F.Value.IsEmpty()) continue;
		Out += FString::Printf(TEXT("%s\"%s\":\"%s\""), Out.Len() > 1 ? TEXT(",") : TEXT(""), F.Key, *F.Value);
	}
	return Out + TEXT("}");
}

bool PassAccepted(const pa250::CampaignState& State, const FString& Order) { return pa250::validate_campaign_order(State, PassStd(Order)).empty(); }

FString PassTown(const pa250::CampaignState& State, const FString& Id)
{
	const auto It = State.provinces.find(PassStd(Id));
	return It == State.provinces.end() ? Id : PassF(It->second.name);
}

FString Uses(int32 N) { return FString::Printf(TEXT("%d %s left"), N, N == 1 ? TEXT("use") : TEXT("uses")); }
}

FString PA250PassPanels::OrderRefusal(const FString& Code)
{
	static const TMap<FString, FString> Words = {
		{TEXT("no_muster"), TEXT("An army is formed where men can be mustered: the town needs a Militia Muster or a Fort.")},
		{TEXT("no_army_here"), TEXT("None of your armies stands in that town.")},
		{TEXT("army_has_general"), TEXT("The army there already has a general.")},
		{TEXT("not_a_general"), TEXT("He is no general.")},
		{TEXT("person_busy"), TEXT("He holds a role already: relieve him first.")},
		{TEXT("person_not_available"), TEXT("He is not at hand.")},
		{TEXT("not_your_province"), TEXT("You do not hold that town.")},
		{TEXT("battle_pending"), TEXT("Not while a battle is waiting.")},
		{TEXT("wrong_role"), TEXT("He cannot serve in that role.")},
		{TEXT("town_has_organizer"), TEXT("That town has an organizer already.")},
		{TEXT("unknown_bloc"), TEXT("That bloc does not sit in the body now meeting.")},
		{TEXT("already_have_envoy"), TEXT("You have an envoy already.")},
		{TEXT("no_uses_left"), TEXT("He has done all he can.")},
		{TEXT("no_battle_pending"), TEXT("The alarm is raised when the enemy is upon a town you defend.")},
		{TEXT("not_defending"), TEXT("The alarm is for defenders: here you attack.")},
		{TEXT("already_alarmed"), TEXT("The alarm has been raised here already.")},
		{TEXT("not_an_enemy_army"), TEXT("That army is your own.")},
		{TEXT("no_clerk_slot"), TEXT("Your clerk has written all he can today.")},
		{TEXT("no_favors"), TEXT("You are owed no favors.")},
		{TEXT("too_early"), TEXT("It cannot be put yet.")},
		{TEXT("already_passed"), TEXT("It has passed already.")},
		{TEXT("already_sent"), TEXT("That dispatch is at sea already.")},
		{TEXT("not_before_it_returns"), TEXT("It failed: it cannot be put again until it returns.")},
		{TEXT("needs_person"), TEXT("Someone must be named to go.")},
		{TEXT("not_pending"), TEXT("That decision is not before you.")},
	};
	const FString* Found = Words.Find(Code);
	return Found ? *Found : FString::Printf(TEXT("The order was refused (%s)."), *Code.Replace(TEXT("_"), TEXT(" ")));
}

FString PA250PassPanels::ChanceBar(int32 Pct)
{
	const int32 P = FMath::Clamp(Pct, 0, 100);
	const int32 Marks = FMath::Clamp(FMath::RoundToInt(P / 10.f), 0, 10);
	FString Bar;
	for (int32 i = 0; i < 10; ++i) Bar += i < Marks ? TEXT("#") : TEXT(".");
	return FString::Printf(TEXT("%s  %d in 100"), *Bar, P);
}

FString PA250PassPanels::LeanWords(int32 Lean)
{
	const TCHAR* Word = Lean >= 40 ? TEXT("with you") : Lean >= 10 ? TEXT("warm") : Lean > -10 ? TEXT("divided") : Lean > -40 ? TEXT("cool") : TEXT("against you");
	return FString::Printf(TEXT("%s (%+d)"), Word, Lean);
}

FString PA250PassPanels::ForceLine(const pa250::CampaignState& State, const pa250::Army& Army)
{
	int32 Units = 0;
	for (const auto& U : Army.units) Units += U.second;
	const int32 Men = Units * 4;
	const std::string Gen = pa250::general_of(State, Army.id);
	if (!pa250::persons_loaded()) return FString::Printf(TEXT("%d men"), Men);
	if (Gen.empty())
	{
		return Army.role == "allied_company" ? FString::Printf(TEXT("Under its own captain · %d men"), Men)
											 : FString::Printf(TEXT("No general: a detachment · %d men"), Men);
	}
	return FString::Printf(TEXT("%s · %d men"), *PassF(pa250::person_name(Gen)), Men);
}

FString PA250PassPanels::UnityChip(const pa250::CampaignState& State)
{
	const TSharedPtr<FJsonObject> Root = PassParse(pa250::politics_json(State));
	if (!Root.IsValid() || !Root->GetBoolField(TEXT("loaded"))) return FString();
	const int32 Favors = Root->GetIntegerField(TEXT("favors"));
	FString Out = FString::Printf(TEXT("Unity %d · %d %s"), Root->GetIntegerField(TEXT("unity")), Favors, Favors == 1 ? TEXT("favor") : TEXT("favors"));
	if (Root->HasField(TEXT("ministry_confidence"))) Out += FString::Printf(TEXT(" · Confidence %d"), Root->GetIntegerField(TEXT("ministry_confidence")));
	return Out;
}

FString PA250PassPanels::FloorButton(const pa250::CampaignState& State)
{
	const TSharedPtr<FJsonObject> Root = PassParse(pa250::politics_json(State));
	if (!Root.IsValid() || !Root->GetBoolField(TEXT("loaded"))) return FString();
	return Root->GetObjectField(TEXT("body"))->GetStringField(TEXT("button"));
}

TArray<PA250PassPanels::FPersonRow> PA250PassPanels::People(const pa250::CampaignState& State, const FString& SelArmy, const FString& SelProvince)
{
	TArray<FPersonRow> Rows;
	const TSharedPtr<FJsonObject> Root = PassParse(pa250::persons_json(State));
	if (!Root.IsValid() || !Root->HasField(TEXT("persons"))) return Rows;
	// The enemy army standing in the selected town (a scout's mark), and the town of the selected army.
	FString EnemyHere, EnemyName;
	for (const auto& Kv : State.armies)
	{
		if (Kv.second.faction != State.player_faction && PassF(Kv.second.province) == SelProvince && !SelProvince.IsEmpty())
		{
			EnemyHere = PassF(Kv.first);
			EnemyName = PassF(Kv.second.name);
		}
	}
	const FString Town = PassTown(State, SelProvince);
	FString ArmyName;
	if (const auto It = State.armies.find(PassStd(SelArmy)); It != State.armies.end()) ArmyName = PassF(It->second.name);

	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("persons")))
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			if (!P.IsValid()) continue;
			TArray<FString> Roles;
			for (const TSharedPtr<FJsonValue>& R : P->GetArrayField(TEXT("roles"))) Roles.Add(R->AsString());
			const int32 Group = Roles.Contains(TEXT("general")) ? 0 : Roles.Contains(TEXT("agent")) && !Roles.Contains(TEXT("delegate")) ? 2 : 1;
			if (Group != Pass) continue;
			FPersonRow Row;
			Row.Id = P->GetStringField(TEXT("id"));
			const FString Rank = P->GetStringField(TEXT("rank"));
			Row.Name = (Rank.IsEmpty() ? FString() : Rank + TEXT(" ")) + P->GetStringField(TEXT("name"));
			Row.Group = Group == 0 ? TEXT("Generals") : Group == 1 ? TEXT("Delegates and organizers") : TEXT("Agents");
			Row.Where = P->GetStringField(TEXT("where"));
			Row.bPresent = P->GetBoolField(TEXT("present"));
			Row.Note = FString::Printf(TEXT("%s (%s)"), *P->GetStringField(TEXT("note")), *P->GetStringField(TEXT("basis")));
			for (const TSharedPtr<FJsonValue>& T : P->GetArrayField(TEXT("traits")))
			{
				Row.Traits += (Row.Traits.IsEmpty() ? TEXT("") : TEXT(", ")) + T->AsObject()->GetStringField(TEXT("label"));
			}
			if (!Row.Traits.IsEmpty()) Row.Traits += TEXT(" (traits invented)");
			const FString Role = P->GetStringField(TEXT("role"));
			FString AbilityId;
			int32 UsesLeft = 0;
			if (const TSharedPtr<FJsonObject>* Ab = nullptr; P->TryGetObjectField(TEXT("ability"), Ab))
			{
				AbilityId = (*Ab)->GetStringField(TEXT("id"));
				UsesLeft = (*Ab)->GetIntegerField(TEXT("uses_left"));
				Row.Ability = FString::Printf(TEXT("%s (%s): %s"), *(*Ab)->GetStringField(TEXT("label")), *Uses(UsesLeft), *(*Ab)->GetStringField(TEXT("text")));
			}
			if (Row.bPresent)
			{
				auto Offer = [&](const FString& Label, const FString& Order, const FString& Tip) {
					if (PassAccepted(State, Order)) Row.Actions.Add({Label, Order, Tip});
				};
				if (!Role.IsEmpty())
				{
					Offer(TEXT("Relieve"), PassOrder({{TEXT("order"), TEXT("relieve_person")}, {TEXT("person"), Row.Id}}), TEXT("He gives up his role and is at hand again."));
				}
				else
				{
					if (Roles.Contains(TEXT("general")))
					{
						if (SelProvince.IsEmpty()) Row.Hint = TEXT("Select a town with a Militia Muster or a Fort where your army stands, to form an army under him.");
						else
						{
							const FString Order = PassOrder({{TEXT("order"), TEXT("form_army")}, {TEXT("person"), Row.Id}, {TEXT("province"), SelProvince}});
							Offer(FString::Printf(TEXT("Form army at %s"), *Town), Order, TEXT("He takes command of your army there: it may hold more units and may besiege."));
							if (Row.Actions.Num() == 0) Row.Hint = OrderRefusal(PassF(pa250::validate_campaign_order(State, PassStd(Order))));
						}
					}
					if (Roles.Contains(TEXT("organizer")) && !SelProvince.IsEmpty())
					{
						Offer(FString::Printf(TEXT("Organizer at %s"), *Town),
							PassOrder({{TEXT("order"), TEXT("assign_person")}, {TEXT("person"), Row.Id}, {TEXT("role"), TEXT("organizer")}, {TEXT("province"), SelProvince}}),
							TEXT("Support each month, faster musters in that town, and the enemy's numbers counted when he comes there."));
					}
					if (Roles.Contains(TEXT("envoy")))
					{
						Offer(TEXT("Make envoy"), PassOrder({{TEXT("order"), TEXT("assign_person")}, {TEXT("person"), Row.Id}, {TEXT("role"), TEXT("envoy")}}),
							TEXT("He speaks for you at a parley: the British envoy's Attitude rises once."));
					}
					if (Roles.Contains(TEXT("delegate")) && Row.Actions.Num() == 0 && Row.Hint.IsEmpty()) Row.Hint = TEXT("Seat him with a bloc from the floor (Congress button).");
				}
				if (!AbilityId.IsEmpty() && UsesLeft > 0)
				{
					if (AbilityId == TEXT("scout"))
					{
						if (EnemyHere.IsEmpty()) Row.Hint = TEXT("Select a town where an enemy army stands, to have it counted.");
						else Offer(FString::Printf(TEXT("Scout %s"), *EnemyName), PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Row.Id}, {TEXT("army_id"), EnemyHere}}), TEXT("Its true strength is known for a week."));
					}
					else if (AbilityId == TEXT("boats"))
					{
						if (SelArmy.IsEmpty()) Row.Hint = TEXT("Select one of your armies, to find it boats.");
						else Offer(FString::Printf(TEXT("Boats for %s"), *ArmyName), PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Row.Id}, {TEXT("army_id"), SelArmy}}), TEXT("It may cross by the ferries and boat routes for three days."));
					}
					else if (AbilityId == TEXT("engineer"))
					{
						if (SelProvince.IsEmpty()) Row.Hint = TEXT("Select a town you hold, to have works laid out there.");
						else Offer(FString::Printf(TEXT("Works at %s"), *Town), PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Row.Id}, {TEXT("province"), SelProvince}}), TEXT("One more level of fortification for its defenders, for two weeks."));
					}
					else if (AbilityId == TEXT("pen"))
					{
						if (SelProvince.IsEmpty()) Row.Hint = TEXT("Select a town you hold, to have her write for it.");
						else Offer(FString::Printf(TEXT("Write for %s"), *Town), PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Row.Id}, {TEXT("province"), SelProvince}}), TEXT("Support rises."));
					}
					else if (AbilityId == TEXT("alarm"))
					{
						Row.Hint = TEXT("Spent on the card before a battle you defend.");
					}
				}
			}
			Rows.Add(MoveTemp(Row));
		}
	}
	return Rows;
}

PA250PassPanels::FFloor PA250PassPanels::Floor(const pa250::CampaignState& State)
{
	FFloor F;
	const TSharedPtr<FJsonObject> Root = PassParse(pa250::politics_json(State));
	if (!Root.IsValid() || !Root->GetBoolField(TEXT("loaded"))) return F;
	F.bLoaded = true;
	const TSharedPtr<FJsonObject> Body = Root->GetObjectField(TEXT("body"));
	const bool bDispatch = Body->GetBoolField(TEXT("dispatch"));
	F.Heading = Body->GetStringField(TEXT("name"));
	const int32 Favors = Root->GetIntegerField(TEXT("favors"));
	const int32 Slots = Root->GetIntegerField(TEXT("clerk_slots_left"));
	F.Summary = FString::Printf(TEXT("Unity %d (under %d the troops of other colonies begin to go home) · %d %s owed you · your clerk can write %d more %s today"),
		Root->GetIntegerField(TEXT("unity")), Root->GetIntegerField(TEXT("unity_floor")), Favors, Favors == 1 ? TEXT("favor") : TEXT("favors"), Slots,
		Slots == 1 ? TEXT("letter") : TEXT("letters"));
	if (Root->HasField(TEXT("ministry_confidence")))
	{
		F.Summary = FString::Printf(TEXT("The ministry's confidence %d (under %d you are recalled) · %d %s owed you · your clerk can write %d more %s today"),
			Root->GetIntegerField(TEXT("ministry_confidence")), Root->GetIntegerField(TEXT("recall_floor")), Favors, Favors == 1 ? TEXT("favor") : TEXT("favors"), Slots,
			Slots == 1 ? TEXT("letter") : TEXT("letters"));
	}
	// A delegate at hand with no role, to be seated.
	FString FreeDelegate, FreeDelegateName;
	if (const TSharedPtr<FJsonObject> People = PassParse(pa250::persons_json(State)); People.IsValid() && People->HasField(TEXT("persons")))
	{
		for (const TSharedPtr<FJsonValue>& V : People->GetArrayField(TEXT("persons")))
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			bool bDelegate = false;
			for (const TSharedPtr<FJsonValue>& R : P->GetArrayField(TEXT("roles"))) bDelegate |= R->AsString() == TEXT("delegate");
			if (!bDelegate || !P->GetBoolField(TEXT("present")) || !P->GetStringField(TEXT("role")).IsEmpty() || !FreeDelegate.IsEmpty()) continue;
			FreeDelegate = P->GetStringField(TEXT("id"));
			FreeDelegateName = P->GetStringField(TEXT("name"));
		}
	}
	TMap<FString, FString> BlocNames;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("blocs")))
	{
		const TSharedPtr<FJsonObject> B = V->AsObject();
		FBloc Bloc;
		Bloc.Id = B->GetStringField(TEXT("id"));
		Bloc.Name = B->GetStringField(TEXT("name"));
		Bloc.Lean = LeanWords(B->GetIntegerField(TEXT("lean")));
		Bloc.Moves = B->GetStringField(TEXT("moves"));
		Bloc.Delegate = B->GetStringField(TEXT("delegate"));
		BlocNames.Add(Bloc.Id, Bloc.Name);
		auto Offer = [&](const FString& Label, const FString& Order, const FString& Tip) {
			if (PassAccepted(State, Order)) Bloc.Actions.Add({Label, Order, Tip});
		};
		Offer(FString::Printf(TEXT("Write (%+d)"), Root->GetIntegerField(TEXT("write_shift"))),
			PassOrder({{TEXT("order"), TEXT("lobby")}, {TEXT("action"), TEXT("write")}, {TEXT("bloc"), Bloc.Id}}), TEXT("One clerk slot today for a small shift."));
		Offer(FString::Printf(TEXT("Call in a favor (%+d)"), Root->GetIntegerField(TEXT("favor_shift"))),
			PassOrder({{TEXT("order"), TEXT("lobby")}, {TEXT("action"), TEXT("favor")}, {TEXT("bloc"), Bloc.Id}}), TEXT("Spends one favor for a large shift."));
		if (!FreeDelegate.IsEmpty() && Bloc.Delegate.IsEmpty())
		{
			Offer(FString::Printf(TEXT("Seat %s (%+d a week)"), *FreeDelegateName, Root->GetIntegerField(TEXT("delegate_shift_per_week"))),
				PassOrder({{TEXT("order"), TEXT("assign_person")}, {TEXT("person"), FreeDelegate}, {TEXT("role"), TEXT("delegate")}, {TEXT("bloc"), Bloc.Id}}),
				TEXT("A steady shift while he stays with this bloc."));
		}
		F.Blocs.Add(MoveTemp(Bloc));
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("measures")))
	{
		const TSharedPtr<FJsonObject> M = V->AsObject();
		FMeasure Me;
		Me.Id = M->GetStringField(TEXT("id"));
		Me.Title = M->GetStringField(TEXT("title"));
		Me.Text = M->GetStringField(TEXT("text"));
		Me.When = M->GetStringField(TEXT("when"));
		Me.Status = M->GetStringField(TEXT("status"));
		Me.IfPassed = (bDispatch ? TEXT("If granted: ") : TEXT("If it passes: ")) + M->GetStringField(TEXT("if_passed"));
		Me.IfFailed = (bDispatch ? TEXT("If refused: ") : TEXT("If it fails: ")) + M->GetStringField(TEXT("if_failed"));
		Me.Note = FString::Printf(TEXT("%s (%s)"), *M->GetStringField(TEXT("note")), *M->GetStringField(TEXT("basis")));
		const TSharedPtr<FJsonObject> Chances = M->GetObjectField(TEXT("chances"));
		for (const FBloc& B : F.Blocs)
		{
			Me.Chances += FString::Printf(TEXT("%s%s  %s"), Me.Chances.IsEmpty() ? TEXT("") : TEXT("\n"), *ChanceBar(Chances->GetIntegerField(B.Id)), *B.Name);
		}
		if (M->GetBoolField(TEXT("can_vote_now")))
		{
			Me.Actions.Add({TEXT("Vote now"), PassOrder({{TEXT("order"), TEXT("vote_now")}, {TEXT("measure"), Me.Id}}),
				FString::Printf(TEXT("Calls the vote before its date. It passes if %d of the three blocs vote for it."), Root->GetIntegerField(TEXT("blocs_to_pass")))});
		}
		if (M->GetBoolField(TEXT("can_send")))
		{
			Me.Actions.Add({TEXT("Send the dispatch"), PassOrder({{TEXT("order"), TEXT("send_dispatch")}, {TEXT("measure"), Me.Id}}),
				TEXT("It is voted on when it reaches London and answered when the reply comes back.")});
		}
		if (Me.Status == TEXT("sent")) F.AtSea.Add(FString::Printf(TEXT("%s: %s"), *Me.Title, *Me.When));
		if (Me.Status == TEXT("open")) F.Open.Add(MoveTemp(Me));
		else if (Me.Status == TEXT("passed") || Me.Status == TEXT("failed") || Me.Status == TEXT("lapsed")) F.Settled.Add(MoveTemp(Me));
		else if (Me.Status != TEXT("sent")) F.Upcoming.Add(MoveTemp(Me));
	}
	return F;
}

PA250PassPanels::FDecision PA250PassPanels::Decision(const pa250::CampaignState& State)
{
	FDecision D;
	const TSharedPtr<FJsonObject> Root = PassParse(pa250::decision_json(State));
	if (!Root.IsValid() || !Root->GetBoolField(TEXT("pending"))) return D;
	D.bPending = true;
	D.Id = Root->GetStringField(TEXT("id"));
	D.Title = Root->GetStringField(TEXT("title"));
	D.Text = Root->GetStringField(TEXT("text"));
	D.Painting = Root->GetStringField(TEXT("painting"));
	D.Note = FString::Printf(TEXT("%s (%s; costs and consequences are the game's)"), *Root->GetStringField(TEXT("note")), *Root->GetStringField(TEXT("basis")));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("options")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FDecisionOption Opt;
		Opt.Id = O->GetStringField(TEXT("id"));
		Opt.Label = O->GetStringField(TEXT("label"));
		Opt.Cost = TEXT("Cost: ") + O->GetStringField(TEXT("cost"));
		Opt.Gain = TEXT("Gain: ") + O->GetStringField(TEXT("gain"));
		Opt.Later = O->GetStringField(TEXT("later"));
		const FString Role = O->GetStringField(TEXT("commit_role"));
		if (Role.IsEmpty())
		{
			Opt.Actions.Add({TEXT("Choose"), PassOrder({{TEXT("order"), TEXT("decide")}, {TEXT("decision"), D.Id}, {TEXT("option"), Opt.Id}}), Opt.Label});
		}
		else
		{
			Opt.Commit = FString::Printf(TEXT("Needs a %s, who leaves the map for %d days:"), *Role, O->GetIntegerField(TEXT("commit_days")));
			for (const TSharedPtr<FJsonValue>& C : O->GetArrayField(TEXT("candidates")))
			{
				const TSharedPtr<FJsonObject> Who = C->AsObject();
				Opt.Actions.Add({FString::Printf(TEXT("Send %s%s"), *Who->GetStringField(TEXT("name")), Who->GetBoolField(TEXT("busy")) ? TEXT(" (gives up his command)") : TEXT("")),
					PassOrder({{TEXT("order"), TEXT("decide")}, {TEXT("decision"), D.Id}, {TEXT("option"), Opt.Id}, {TEXT("person"), Who->GetStringField(TEXT("id"))}}), Opt.Label});
			}
			if (Opt.Actions.Num() == 0) Opt.Commit += TEXT(" nobody is at hand.");
		}
		D.Options.Add(MoveTemp(Opt));
	}
	return D;
}

TArray<PA250PassPanels::FAction> PA250PassPanels::CardAgents(const pa250::CampaignState& State)
{
	TArray<FAction> Out;
	if (!State.pending_battle.active || !pa250::persons_loaded()) return Out;
	const auto Def = State.armies.find(State.pending_battle.defender_army);
	const auto Att = State.armies.find(State.pending_battle.attacker_army);
	if (Def == State.armies.end() || Att == State.armies.end()) return Out;
	const bool bDefending = Def->second.faction == State.player_faction;
	const FString EnemyArmy = PassF(bDefending ? Att->first : Def->first);
	const FString Town = PassF(State.pending_battle.province);
	const TSharedPtr<FJsonObject> Root = PassParse(pa250::persons_json(State));
	if (!Root.IsValid() || !Root->HasField(TEXT("persons"))) return Out;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("persons")))
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const TSharedPtr<FJsonObject>* Ab = nullptr;
		if (!P->TryGetObjectField(TEXT("ability"), Ab) || !P->GetBoolField(TEXT("present"))) continue;
		const FString Ability = (*Ab)->GetStringField(TEXT("id"));
		const FString Id = P->GetStringField(TEXT("id"));
		const FString Name = P->GetStringField(TEXT("name"));
		const FString Left = Uses((*Ab)->GetIntegerField(TEXT("uses_left")));
		FString Order, Label;
		if (Ability == TEXT("alarm"))
		{
			Order = PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Id}});
			Label = FString::Printf(TEXT("%s: raise the alarm (%s)"), *Name, *Left);
		}
		else if (Ability == TEXT("scout"))
		{
			if (PassParse(pa250::army_intel_json(State, State.player_faction, PassStd(EnemyArmy)))->GetBoolField(TEXT("known"))) continue;
			Order = PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Id}, {TEXT("army_id"), EnemyArmy}});
			Label = FString::Printf(TEXT("%s: count the enemy (%s)"), *Name, *Left);
		}
		else if (Ability == TEXT("engineer") && bDefending)
		{
			Order = PassOrder({{TEXT("order"), TEXT("use_agent")}, {TEXT("person"), Id}, {TEXT("province"), Town}});
			Label = FString::Printf(TEXT("%s: throw up works (%s)"), *Name, *Left);
		}
		if (!Order.IsEmpty() && PassAccepted(State, Order)) Out.Add({Label, Order, (*Ab)->GetStringField(TEXT("text"))});
	}
	return Out;
}

// ---- Slate ---------------------------------------------------------------------------

namespace
{
TSharedRef<SWidget> PText(const FString& S, const TCHAR* Face, int32 Size, const FLinearColor& Color, float WrapAt = 0.f)
{
	return SNew(STextBlock).Text(FText::FromString(S)).Font(FPA250HudStyle::Font(Face, Size)).ColorAndOpacity(Color).WrapTextAt(WrapAt);
}

TSharedRef<SWidget> PButton(const PA250PassPanels::FAction& A, const PA250PassPanels::FIssue& Issue, int32 Size = 11)
{
	const FString Order = A.OrderJson;
	return SNew(SButton)
		.ButtonStyle(&FPA250HudStyle::Get().Button)
		.IsFocusable(false)
		.ToolTipText(FText::FromString(A.Tip))
		.ContentPadding(FMargin(9.f, 3.f))
		.OnClicked_Lambda([Issue, Order]() {
			if (Issue) Issue(Order);
			return FReply::Handled();
		})
			[SNew(STextBlock).Text(FText::FromString(A.Label)).Font(FPA250HudStyle::Font(TEXT("Regular"), Size)).ColorAndOpacity(FPA250HudStyle::Cream)];
}

TSharedRef<SWidget> PHeader(const FString& Label)
{
	return SNew(SBorder)
		.BorderImage(&FPA250HudStyle::Get().Header)
		.Padding(FMargin(9.f, 1.f))
			[SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 12)).ColorAndOpacity(FPA250HudStyle::Brass).Text(FText::FromString(Label))];
}

TSharedRef<SWidget> PButtons(const TArray<PA250PassPanels::FAction>& Actions, const PA250PassPanels::FIssue& Issue)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const PA250PassPanels::FAction& A : Actions) Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[PButton(A, Issue)];
	return Row;
}

TSharedRef<SWidget> PTitleBar(const FString& Title, TFunction<void()> Close)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[PText(Title, TEXT("Bold"), 16, FPA250HudStyle::Cream)]
		+ SHorizontalBox::Slot().AutoWidth()
			  [SNew(SButton)
				   .ButtonStyle(&FPA250HudStyle::Get().Button)
				   .IsFocusable(false)
				   .ContentPadding(FMargin(9.f, 2.f))
				   .OnClicked_Lambda([Close]() {
					   if (Close) Close();
					   return FReply::Handled();
				   })
					   [PText(TEXT("Close"), TEXT("Regular"), 11, FPA250HudStyle::Cream)]];
}
}

TSharedRef<SWidget> PA250PassPanels::BuildPeople(const pa250::CampaignState& State, const FString& SelArmy, const FString& SelProvince, FIssue Issue, TFunction<void()> Close)
{
	const FLinearColor Cream = FPA250HudStyle::Cream, Dim = FPA250HudStyle::CreamDim;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	FString Group;
	for (const FPersonRow& R : People(State, SelArmy, SelProvince))
	{
		if (R.Group != Group)
		{
			Group = R.Group;
			List->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)[PHeader(Group.ToUpper())];
		}
		TSharedRef<SVerticalBox> Row = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
				  [SNew(SHorizontalBox)
				   + SHorizontalBox::Slot().AutoWidth()[PText(R.Name, TEXT("Bold"), 13, R.bPresent ? Cream : Dim)]
				   + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Bottom)[PText(R.Where, TEXT("Italic"), 11, FPA250HudStyle::Brass)]];
		if (!R.Traits.IsEmpty()) Row->AddSlot().AutoHeight()[PText(R.Traits, TEXT("Regular"), 11, Cream, 500.f)];
		if (!R.Ability.IsEmpty()) Row->AddSlot().AutoHeight()[PText(R.Ability, TEXT("Regular"), 11, Cream, 500.f)];
		Row->AddSlot().AutoHeight()[PText(R.Note, TEXT("Italic"), 10, Dim, 500.f)];
		if (R.Actions.Num() > 0) Row->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[PButtons(R.Actions, Issue)];
		else if (!R.Hint.IsEmpty() && R.bPresent) Row->AddSlot().AutoHeight()[PText(R.Hint, TEXT("Italic"), 10, FPA250HudStyle::BrassDim, 500.f)];
		List->AddSlot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)[Row];
	}
	return SNew(SBorder)
		.BorderImage(&FPA250HudStyle::Get().Panel)
		.Padding(FMargin(14.f, 10.f))
			[SNew(SVerticalBox)
			 + SVerticalBox::Slot().AutoHeight()[PTitleBar(TEXT("YOUR PEOPLE"), Close)]
			 + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				   [PText(TEXT("One role at a time. An army with no general is a detachment: it takes in few units and cannot besiege. What each can do depends on the town and army you have selected."),
						  TEXT("Italic"), 11, Dim, 520.f)]
			 + SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];
}

TSharedRef<SWidget> PA250PassPanels::BuildFloor(const pa250::CampaignState& State, FIssue Issue, TFunction<void()> Close)
{
	const FLinearColor Cream = FPA250HudStyle::Cream, Dim = FPA250HudStyle::CreamDim;
	const FFloor F = Floor(State);
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)[PHeader(TEXT("THE BLOCS"))];
	for (const FBloc& B : F.Blocs)
	{
		TSharedRef<SVerticalBox> Row = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
				  [SNew(SHorizontalBox)
				   + SHorizontalBox::Slot().AutoWidth()[PText(B.Name, TEXT("Bold"), 13, Cream)]
				   + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Bottom)[PText(B.Lean, TEXT("Italic"), 12, FPA250HudStyle::Brass)]
				   + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Bottom)
						 [PText(B.Delegate.IsEmpty() ? FString() : FString::Printf(TEXT("%s sits with them"), *B.Delegate), TEXT("Italic"), 11, Dim)]]
			+ SVerticalBox::Slot().AutoHeight()[PText(B.Moves, TEXT("Regular"), 11, Dim, 620.f)];
		if (B.Actions.Num() > 0) Row->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[PButtons(B.Actions, Issue)];
		List->AddSlot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)[Row];
	}
	auto AddMeasures = [&](const TCHAR* Heading, const TArray<FMeasure>& Measures, bool bFull) {
		if (Measures.Num() == 0) return;
		List->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)[PHeader(Heading)];
		for (const FMeasure& M : Measures)
		{
			TSharedRef<SVerticalBox> Row = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
					  [SNew(SHorizontalBox)
					   + SHorizontalBox::Slot().AutoWidth()[PText(M.Title, TEXT("Bold"), 13, Cream)]
					   + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Bottom)[PText(M.When, TEXT("Italic"), 11, FPA250HudStyle::Brass)]];
			if (bFull)
			{
				Row->AddSlot().AutoHeight()[PText(M.Text, TEXT("Regular"), 11, Cream, 620.f)];
				Row->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[SNew(STextBlock).Text(FText::FromString(M.Chances)).Font(FCoreStyle::GetDefaultFontStyle("Mono", 9)).ColorAndOpacity(FPA250HudStyle::Brass)];
			}
			Row->AddSlot().AutoHeight()[PText(M.IfPassed, TEXT("Regular"), 11, FPA250HudStyle::Green, 620.f)];
			Row->AddSlot().AutoHeight()[PText(M.IfFailed, TEXT("Regular"), 11, FPA250HudStyle::Red, 620.f)];
			if (bFull) Row->AddSlot().AutoHeight()[PText(M.Note, TEXT("Italic"), 10, Dim, 620.f)];
			if (M.Actions.Num() > 0) Row->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[PButtons(M.Actions, Issue)];
			List->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[Row];
		}
	};
	if (F.AtSea.Num() > 0)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)[PHeader(TEXT("DISPATCHES AT SEA"))];
		for (const FString& L : F.AtSea) List->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[PText(L, TEXT("Regular"), 12, Cream, 620.f)];
	}
	AddMeasures(TEXT("ON THE FLOOR"), F.Open, true);
	AddMeasures(TEXT("NOT YET"), F.Upcoming, false);
	AddMeasures(TEXT("SETTLED"), F.Settled, false);
	return SNew(SBorder)
		.BorderImage(&FPA250HudStyle::Get().Panel)
		.Padding(FMargin(14.f, 10.f))
			[SNew(SVerticalBox)
			 + SVerticalBox::Slot().AutoHeight()[PTitleBar(F.Heading.ToUpper(), Close)]
			 + SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[PText(F.Summary, TEXT("Italic"), 11, Dim, 640.f)]
			 + SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];
}

TSharedRef<SWidget> PA250PassPanels::BuildDecision(const pa250::CampaignState& State, FIssue Issue, const FSlateBrush* Painting)
{
	const FDecision D = Decision(State);
	if (!D.bPending) return SNullWidget::NullWidget;
	const FLinearColor Ink = FPA250HudStyle::Ink;
	const FLinearColor InkDim(0.12f, 0.08f, 0.04f, 0.72f);
	TSharedRef<SHorizontalBox> Options = SNew(SHorizontalBox);
	for (const FDecisionOption& O : D.Options)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[PText(O.Label, TEXT("Bold"), 17, Ink, 400.f)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[PText(O.Cost, TEXT("Regular"), 13, FPA250HudStyle::SRGB(0.55f, 0.10f, 0.08f), 400.f)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[PText(O.Gain, TEXT("Regular"), 13, FPA250HudStyle::SRGB(0.10f, 0.34f, 0.14f), 400.f)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[PText(O.Later, TEXT("Italic"), 13, InkDim, 400.f)];
		if (!O.Commit.IsEmpty()) Col->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[PText(O.Commit, TEXT("Regular"), 12, Ink, 400.f)];
		for (const FAction& A : O.Actions) Col->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 6.f, 0.f, 0.f)[PButton(A, Issue, 13)];
		Options->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 16.f, 0.f)[Col];
	}
	return SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SImage).Image(&FPA250HudStyle::Get().White).ColorAndOpacity(FLinearColor(0.02f, 0.015f, 0.01f, 1.f))]
		+ SOverlay::Slot()[SNew(SImage).Image(Painting ? Painting : &FPA250HudStyle::Get().White).ColorAndOpacity(Painting ? FLinearColor(1.f, 1.f, 1.f, 0.9f) : FLinearColor(0.10f, 0.07f, 0.04f, 1.f))]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 60.f))
			  [SNew(SBox).WidthOverride(980.f)
				   [SNew(SBorder)
						.BorderImage(&FPA250HudStyle::Get().Parchment)
						.Padding(FMargin(26.f, 18.f))
							[SNew(SVerticalBox)
							 + SVerticalBox::Slot().AutoHeight()[PText(TEXT("A DECISION  ·  the clock is stopped"), TEXT("Bold"), 11, InkDim)]
							 + SVerticalBox::Slot().AutoHeight()[PText(D.Title, TEXT("Bold"), 26, Ink)]
							 + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 12.f)[PText(D.Text, TEXT("Regular"), 15, Ink, 920.f)]
							 + SVerticalBox::Slot().AutoHeight()[Options]
							 + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[PText(D.Note, TEXT("Italic"), 11, InkDim, 920.f)]]]];
}

TSharedRef<SWidget> PA250PassPanels::BuildCardAgents(const pa250::CampaignState& State, FIssue Issue)
{
	const TArray<FAction> Agents = CardAgents(State);
	if (Agents.Num() == 0) return SNullWidget::NullWidget;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[PText(TEXT("Before the fight:"), TEXT("Bold"), 13, FPA250HudStyle::Ink)];
	for (const FAction& A : Agents) Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[PButton(A, Issue, 12)];
	return Row;
}
