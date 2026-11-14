#include "PA250BattleCards.h"
#include "Battle/PA250BattleModes.h"
#include "PA250HudStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

FString FactionName(const std::string& Faction) { return Faction == "british" ? TEXT("British") : TEXT("Continentals"); }

/** The order the card reads in, and the words for each number the sim explains. */
struct FLabel
{
	const char* Id;
	const TCHAR* Words;
};
const FLabel GLabels[] = {
	{"men", TEXT("Men")},
	{"field_strength", TEXT("On the field")},
	{"rounds_per_man", TEXT("Rounds a man")},
	{"morale", TEXT("Morale")},
	{"readiness", TEXT("Readiness")},
	{"fortification", TEXT("Fortification")},
	{"reinforcement", TEXT("Reinforcements")},
	{"militia_muster", TEXT("Militia muster")},
};

int32 Sum(const std::map<std::string, std::map<std::string, int>>& M, const std::string& Faction)
{
	int32 N = 0;
	const auto It = M.find(Faction);
	if (It != M.end()) for (const auto& Kv : It->second) N += Kv.second;
	return N;
}

/** A town's holder in words, as the sim's own lines say it. */
FString OwnerWord(const std::string& Faction) { return Faction == "british" ? TEXT("British") : Faction == "continental" ? TEXT("Patriot") : TEXT("nobody's"); }

FString Plural(int32 N, const TCHAR* One, const TCHAR* Many) { return FString::Printf(TEXT("%d %s"), N, N == 1 ? One : Many); }
}

TArray<PA250BattleCards::FRow> PA250BattleCards::SideRows(const pa250::SideSetup& Side)
{
	TArray<FRow> Rows;
	auto Final = [&Side](const std::string& Label, int32 Base) {
		// The headline is the number the battle will use, not the base the changes start from.
		if (Label == "morale") return Side.morale;
		if (Label == "readiness") return Side.readiness;
		if (Label == "rounds_per_man") return Side.rounds_per_man;
		if (Label == "field_strength") return Side.field_strength;
		if (Label == "fortification") return Side.fortification_level;
		if (Label == "men")
		{
			// The men who fight: the army's strength less anyone stood down (each has its own line below).
			int32 N = 0;
			for (const pa250::SideFactor& F : Side.factors) if (F.label == "men") N += F.value;
			return N;
		}
		return Base;
	};
	auto AddLabel = [&](const std::string& Id, const FString& Words) {
		bool bFirst = true;
		for (const pa250::SideFactor& F : Side.factors)
		{
			if (F.label != Id) continue;
			FRow R;
			R.Source = ToF(F.source);
			if (bFirst)
			{
				R.Label = Words;
				R.Value = Id == "fortification" ? (Final(Id, F.value) > 0 ? FString::Printf(TEXT("L%d"), Final(Id, F.value)) : FString(TEXT("none")))
					: Id == "reinforcement" ? FString::Printf(TEXT("+%d%%"), F.value)
					: Id == "militia_muster" ? FString(TEXT("yes"))
					: FString::FromInt(Final(Id, F.value));
			}
			else
			{
				R.bChange = true;
				R.Value = FString::Printf(TEXT("%+d"), F.value);
			}
			bFirst = false;
			Rows.Add(R);
		}
	};
	for (const FLabel& L : GLabels) AddLabel(L.Id, L.Words);
	// Anything the sim explains that this card has no words for yet still shows, under its own id.
	for (const pa250::SideFactor& F : Side.factors)
	{
		bool bKnown = false;
		for (const FLabel& L : GLabels) bKnown = bKnown || F.label == L.Id;
		if (bKnown) continue;
		FRow R;
		// In words: a label id such as "night_march" reads "Night march".
		R.Label = ToF(F.label).Replace(TEXT("_"), TEXT(" "));
		if (!R.Label.IsEmpty()) R.Label[0] = FChar::ToUpper(R.Label[0]);
		R.Value = FString::FromInt(F.value);
		R.Source = ToF(F.source);
		Rows.Add(R);
	}
	return Rows;
}

FString PA250BattleCards::EndReasonSentence(const std::string& Reason, bool bPlayerWon, const std::string& Mode)
{
	if (Mode == "assault" && Reason == "time_limit") return bPlayerWon ? TEXT("Your men held their works until the attack was spent.") : TEXT("The enemy held his works until your attack was spent.");
	if (Mode == "assault" && Reason == "posts_swept") return bPlayerWon ? TEXT("You carried every post of the works.") : TEXT("The enemy carried every post of your works.");
	if (Reason == "tickets_exhausted") return bPlayerWon ? TEXT("The enemy fought until none were left standing.") : TEXT("Your men fought until none were left standing.");
	if (Reason == "time_limit") return bPlayerWon ? TEXT("The day ended with more of your men standing.") : TEXT("The day ended with more of the enemy standing.");
	if (Reason == "force_preserved") return bPlayerWon ? TEXT("You kept more of your force together on the road.") : TEXT("The enemy kept more of his force together on the road.");
	if (Reason == "routed") return bPlayerWon ? TEXT("The last handful of the enemy quit the field.") : TEXT("The last handful of your men quit the field.");
	if (Reason == "army_broke") return bPlayerWon ? TEXT("The enemy army broke and fell back with its survivors.") : TEXT("Your army broke and fell back with its survivors.");
	if (Reason == "posts_swept") return bPlayerWon ? TEXT("You took every post and held them.") : TEXT("The enemy took every post and held them.");
	return TEXT("The fight ended.");
}

PA250BattleCards::FAfter PA250BattleCards::AfterBattle(const pa250::BattleReport& Report, const pa250::CampaignState& State)
{
	FAfter A;
	const std::string Mine = State.player_faction;
	const std::string Theirs = Mine == "british" ? "continental" : "british";
	const bool bWon = Report.winner == Mine;
	const auto Town = State.provinces.find(Report.province_id);
	A.Title = FString::Printf(TEXT("Battle of %s"), Town == State.provinces.end() ? *ToF(Report.province_id) : *ToF(Town->second.name));
	A.Verdict = bWon ? TEXT("Victory") : TEXT("Defeat");
	A.Reason = EndReasonSentence(Report.end_reason, bWon, Report.mode);

	// Men, as the battle counted them: every man who fell is killed, wounded or a prisoner. A report
	// saved before battles counted men has only whole units.
	const bool bMen = !Report.casualties.empty();
	auto Men = [&Report](const std::string& Faction) {
		pa250::MenDown Total;
		const auto It = Report.casualties.find(Faction);
		if (It != Report.casualties.end())
		{
			for (const auto& Kv : It->second)
			{
				Total.killed += Kv.second.killed;
				Total.wounded += Kv.second.wounded;
				Total.prisoners += Kv.second.prisoners;
			}
		}
		return Total;
	};
	const pa250::MenDown MineDown = Men(Mine), TheirsDown = Men(Theirs);
	A.LossHeading = bMen ? TEXT("Losses (men)") : TEXT("Losses (units)");
	if (bMen)
	{
		A.Losses.Add(FString::Printf(TEXT("Yours: %d killed, %d wounded (%s down)"), MineDown.killed, MineDown.wounded,
			*Plural(MineDown.killed + MineDown.wounded, TEXT("man"), TEXT("men"))));
		A.Losses.Add(FString::Printf(TEXT("%s: %d killed, %d wounded (%s down)"), *FactionName(Theirs), TheirsDown.killed, TheirsDown.wounded,
			*Plural(TheirsDown.killed + TheirsDown.wounded, TEXT("man"), TEXT("men"))));
	}
	else
	{
		A.Losses.Add(FString::Printf(TEXT("Yours: %d killed, %d wounded"), Sum(Report.losses, Mine), Sum(Report.wounded, Mine)));
		A.Losses.Add(FString::Printf(TEXT("%s: %d killed, %d wounded"), *FactionName(Theirs), Sum(Report.losses, Theirs), Sum(Report.wounded, Theirs)));
	}

	// prisoners[faction] = men of that faction taken.
	const int32 LostToThem = bMen ? MineDown.prisoners : Sum(Report.prisoners, Mine);
	const int32 TakenByUs = bMen ? TheirsDown.prisoners : Sum(Report.prisoners, Theirs);
	if (LostToThem == 0 && TakenByUs == 0)
	{
		A.Prisoners.Add(TEXT("None taken on either side"));
	}
	else if (bMen)
	{
		A.Prisoners.Add(FString::Printf(TEXT("Taken by you: %s"), *Plural(TakenByUs, TEXT("man"), TEXT("men"))));
		A.Prisoners.Add(FString::Printf(TEXT("Taken from you: %s"), *Plural(LostToThem, TEXT("man"), TEXT("men"))));
	}
	else
	{
		A.Prisoners.Add(FString::Printf(TEXT("Taken by you: %s"), *Plural(TakenByUs, TEXT("unit"), TEXT("units"))));
		A.Prisoners.Add(FString::Printf(TEXT("Taken from you: %s"), *Plural(LostToThem, TEXT("unit"), TEXT("units"))));
	}

	for (const std::string& Line : Report.map_changes) A.MapChanges.Add(ToF(Line));
	if (A.MapChanges.Num() == 0)
	{
		// A report saved before the card existed: say at least who holds the town.
		A.MapChanges.Add(Report.province_owner_before == Report.province_owner_after
			? FString::Printf(TEXT("The town stays %s"), *OwnerWord(Report.province_owner_after))
			: FString::Printf(TEXT("The town changes hands: %s to %s"), *OwnerWord(Report.province_owner_before), *OwnerWord(Report.province_owner_after)));
	}

	int32 Morale = 0;
	for (const auto& Kv : Report.morale_delta)
	{
		const auto Army = State.armies.find(Kv.first);
		if (Army != State.armies.end() && Army->second.faction == Mine) Morale += Kv.second;
	}
	auto Spent = [](const std::map<std::string, int>& M, const std::string& F) {
		const auto It = M.find(F);
		return It == M.end() ? 0 : It->second;
	};
	A.Notes.Add(FString::Printf(TEXT("Cartridges spent from the magazines: yours %d, the enemy's %d"), Spent(Report.cartridges_spent, Mine), Spent(Report.cartridges_spent, Theirs)));
	if (Report.scavenged_cartridges || Report.scavenged_powder || Report.scavenged_muskets)
	{
		A.Notes.Add(FString::Printf(TEXT("Gathered from the field: %d cartridges, %d powder, %d muskets"), Report.scavenged_cartridges, Report.scavenged_powder, Report.scavenged_muskets));
	}
	if (Morale != 0) A.Notes.Add(FString::Printf(TEXT("Your army's morale: %+d"), Morale));
	if (Report.local_support_after != Report.local_support_before)
	{
		A.Notes.Add(FString::Printf(TEXT("Support: %d to %d"), Report.local_support_before, Report.local_support_after));
	}
	if (bMen)
	{
		// What the men down cost the armies on the map: whole units leave the ranks, the rest carry.
		const int32 MineOut = Sum(Report.losses, Mine) + Sum(Report.wounded, Mine) + Sum(Report.prisoners, Mine);
		const int32 TheirsOut = Sum(Report.losses, Theirs) + Sum(Report.wounded, Theirs) + Sum(Report.prisoners, Theirs);
		A.Notes.Add(FString::Printf(TEXT("Units out of the ranks: yours %d, the enemy's %d (men short of a whole unit are carried to the next battle)"), MineOut, TheirsOut));
	}
	if (Report.personal_kills > 0) A.Notes.Add(FString::Printf(TEXT("By your own hand: %s"), *Plural(Report.personal_kills, TEXT("man"), TEXT("men"))));
	return A;
}

namespace
{
const FLinearColor InkDim(0.12f, 0.08f, 0.04f, 0.72f);

TSharedRef<SWidget> Text(const FString& S, const TCHAR* Face, int32 Size, const FLinearColor& Color, float WrapAt = 0.f)
{
	return SNew(STextBlock).Text(FText::FromString(S)).Font(FPA250HudStyle::Font(Face, Size)).ColorAndOpacity(Color).WrapTextAt(WrapAt);
}

TSharedRef<SWidget> Rule()
{
	return SNew(SBox).HeightOverride(1.f)[SNew(SImage).Image(&FPA250HudStyle::Get().White).ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f, 0.35f))];
}

TSharedRef<SWidget> SideColumn(const pa250::BattleSetup& Setup, const std::string& Faction)
{
	const FLinearColor Ink = FPA250HudStyle::Ink;
	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	const auto It = Setup.sides.find(Faction);
	const bool bMine = Faction == Setup.player_faction;
	const bool bAttacks = Faction == Setup.attacker_faction;
	Col->AddSlot().AutoHeight()[Text(FString::Printf(TEXT("%s  -  %s"), *FactionName(Faction).ToUpper(), bMine ? TEXT("yours") : TEXT("the enemy")), TEXT("Bold"), 15, Ink)];
	Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[Text(bAttacks ? TEXT("attacking") : TEXT("defending"), TEXT("Italic"), 12, InkDim)];
	Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Rule()];
	if (It == Setup.sides.end())
	{
		Col->AddSlot().AutoHeight()[Text(TEXT("No army"), TEXT("Italic"), 13, InkDim)];
		return Col;
	}
	for (const PA250BattleCards::FRow& R : PA250BattleCards::SideRows(It->second))
	{
		Col->AddSlot().AutoHeight().Padding(0.f, R.bChange ? 0.f : 5.f, 0.f, 0.f)
			[SNew(SHorizontalBox)
			 + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(112.f)[Text(R.Label, TEXT("Bold"), 13, Ink)]]
			 + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
				   [SNew(SBox).WidthOverride(46.f).HAlign(HAlign_Right)[Text(R.Value, R.bChange ? TEXT("Regular") : TEXT("Bold"), R.bChange ? 12 : 15, R.bChange ? InkDim : Ink)]]
			 + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text(R.Source, TEXT("Regular"), 12, InkDim, 250.f)]];
	}
	return Col;
}

TSharedRef<SWidget> Section(const TCHAR* Heading, const TArray<FString>& Lines)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)[Text(Heading, TEXT("Bold"), 13, FPA250HudStyle::Ink)];
	for (const FString& L : Lines)
	{
		Box->AddSlot().AutoHeight().Padding(12.f, 1.f, 0.f, 0.f)[Text(L, TEXT("Regular"), 13, FPA250HudStyle::Ink, 560.f)];
	}
	return Box;
}
}

FString PA250BattleCards::ModeLine(const pa250::BattleSetup& Setup)
{
	const FName Mode(UTF8_TO_TCHAR(Setup.mode.c_str()));
	const FString Kind = FString(PA250Modes::ModeWords(Mode)).ToLower();
	return FString::Printf(TEXT("The %s attack: %s. %s"), *FactionName(Setup.attacker_faction), *Kind, PA250Modes::ModeRule(Mode));
}

TSharedRef<SWidget> PA250BattleCards::BuildPreBattle(const pa250::BattleSetup& Setup)
{
	const FLinearColor Ink = FPA250HudStyle::Ink;
	const std::string Mine = Setup.player_faction.empty() ? std::string("continental") : Setup.player_faction;
	const std::string Theirs = Mine == "british" ? "continental" : "british";
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			  [Text(FString::Printf(TEXT("Battle of %s"), *ToF(Setup.province_name)), TEXT("Bold"), 22, Ink)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 10.f)
			  [Text(ModeLine(Setup) + TEXT(" Each number below names what produced it."), TEXT("Italic"), 12, InkDim, 860.f)]
		+ SVerticalBox::Slot().AutoHeight()
			  [SNew(SHorizontalBox)
			   + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(420.f)[SideColumn(Setup, Mine)]]
			   + SHorizontalBox::Slot().AutoWidth().Padding(18.f, 0.f)
					 [SNew(SBox).WidthOverride(1.f)[SNew(SImage).Image(&FPA250HudStyle::Get().White).ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f, 0.35f))]]
			   + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(420.f)[SideColumn(Setup, Theirs)]]];
}

TSharedRef<SWidget> PA250BattleCards::BuildAfterBattle(const pa250::BattleReport& Report, const pa250::CampaignState& State)
{
	const FAfter A = AfterBattle(Report, State);
	const FLinearColor Ink = FPA250HudStyle::Ink;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(A.Title, TEXT("Bold"), 22, Ink)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			  [Text(A.Verdict, TEXT("Bold"), 17, A.Verdict == TEXT("Victory") ? FPA250HudStyle::SRGB(0.10f, 0.34f, 0.14f) : FPA250HudStyle::SRGB(0.55f, 0.10f, 0.08f))]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 4.f)[Text(A.Reason, TEXT("Italic"), 14, Ink, 600.f)]
		+ SVerticalBox::Slot().AutoHeight()[Rule()]
		+ SVerticalBox::Slot().AutoHeight()[Section(*A.LossHeading, A.Losses)]
		+ SVerticalBox::Slot().AutoHeight()[Section(TEXT("Prisoners"), A.Prisoners)]
		+ SVerticalBox::Slot().AutoHeight()[Section(TEXT("What changed on the map"), A.MapChanges)]
		+ SVerticalBox::Slot().AutoHeight()[Section(TEXT("Also"), A.Notes)];
}
