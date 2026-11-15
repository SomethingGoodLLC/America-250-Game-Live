#include "PA250MapWidget.h"
#include "PA250MarchRoute.h"
#include "UI/PA250CampaignPassPanels.h"
#include "UI/PA250StoryArt.h"
#include "UI/PA250BattleStory.h"
#include "MoviePlayer.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SViewport.h"
#include "Campaign/PA250Subsystem.h"
#include "PA250ProvinceMapActor.h"
#include "UI/PA250CityViewWidget.h"
#include "UI/PA250HudStyle.h"
#include "UI/PA250BattleCards.h"
#include "UI/PA250CampaignArcPanels.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Layout/SlateRect.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "pa250/sim.hpp"
#include <cstring>

namespace
{
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

bool NameHas(const FString& Name, const FString& Query)
{
	return Query.IsEmpty() || Name.Contains(Query, ESearchCase::IgnoreCase);
}

/** Standing in a town: not on a march, and not still walking back from a halt (path is empty then). */
bool ArmyStanding(const pa250::Army& A)
{
	return A.path.empty() && A.returning_hours <= 0;
}

struct FFindHit
{
	FString Kind;
	FString Id;
	FString Label;
};

void CollectFindHits(const pa250::CampaignState& St, const FString& Query, TArray<FFindHit>& Out)
{
	for (const auto& Pk : St.provinces)
	{
		const FString Name = ToFStr(Pk.second.name);
		if (!NameHas(Name, Query)) continue;
		Out.Add({TEXT("town"), ToFStr(Pk.first), Name});
	}
	for (const auto& Ak : St.armies)
	{
		const FString Name = ToFStr(Ak.second.name);
		if (!NameHas(Name, Query)) continue;
		Out.Add({TEXT("army"), ToFStr(Ak.first), Name});
	}
}

int32 FitCount(const pa250::Army& A)
{
	int32 N = 0;
	for (const auto& U : A.units) N += U.second;
	return N;
}
FString ProvName(const pa250::CampaignState& St, const std::string& Id)
{
	auto It = St.provinces.find(Id);
	return It == St.provinces.end() ? ToFStr(Id) : ToFStr(It->second.name);
}
/** Route the army is on, as province names, following its remaining edge path. */
TArray<FString> RouteNames(const pa250::CampaignState& St, const pa250::Army& A)
{
	TArray<FString> Out;
	Out.Add(ProvName(St, A.province));
	std::string Cur = A.province;
	for (const std::string& Eid : A.path)
	{
		auto Eit = St.edges.find(Eid);
		if (Eit == St.edges.end()) break;
		Cur = (Eit->second.a == Cur) ? Eit->second.b : Eit->second.a;
		Out.Add(ProvName(St, Cur));
	}
	return Out;
}
FString OrdersText(const pa250::CampaignState& St, const pa250::Army& A)
{
	if (A.path.empty())
	{
		return TEXT("holding");
	}
	const TArray<FString> Route = RouteNames(St, A);
	return FString::Printf(TEXT("→ %s"), Route.Num() > 1 ? *Route.Last() : TEXT("?"));
}
} // namespace

UPA250Subsystem* UPA250MapWidget::Sub() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UPA250Subsystem>() : nullptr;
}

FText UPA250MapWidget::HeaderText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::FromString(TEXT("No campaign"));
	}
	const pa250::CampaignState& St = S->GetState();
	auto Get = [](const std::map<std::string, int>& M, const char* K) {
		auto It = M.find(K);
		return It == M.end() ? 0 : It->second;
	};
	FString Treaties;
	for (const auto& T : St.treaties)
	{
		Treaties += FString::Printf(TEXT("%s%s %s"), Treaties.IsEmpty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(T.type.c_str()),
		                            *UPA250Subsystem::FormatHoursRemaining(T.hours_remaining));
	}
	const FString Date = S->FormatDate();
	const FString Sp = FPA250CampaignSpeed::Format(S->GetHoursPerSecond());
	const TCHAR* Pause = S->IsPaused() ? TEXT("PAUSED") : TEXT("RUNNING");
	const pa250::FactionStores Roll = pa250::rollup_faction_stores(St, pa250::kContinental);
	int32 InTransit = 0;
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status == "in_transit" || Sh.status == "committed") ++InTransit;
	}
	return FText::FromString(FString::Printf(
		TEXT("%s   |   %s  speed %s   |   Gold %d   |   Powder %d  Cart %d  Muskets %d  Prov %d   |   In transit %d   |   Local support %d   |   Supplies Cont %d  Brit %d   |   Attitude %d   |   Treaties: %s"),
		*Date, Pause, *Sp, St.player_gold, Roll.powder,
		[&]() {
			int32 C = 0;
			for (const auto& KV : St.provinces)
			{
				if (KV.second.owner != pa250::kContinental) continue;
				C += pa250::province_ledger(St, KV.first).cartridges;
			}
			return C;
		}(),
		Roll.arms, Roll.provisions, InTransit, St.local_support, Get(St.supplies, "continental"),
		Get(St.supplies, "british"), Get(St.relations, "british"),
		Treaties.IsEmpty() ? TEXT("none") : *Treaties));
}

FText UPA250MapWidget::DateText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::FromString(TEXT("No campaign"));
	}
	return FText::FromString(FString::Printf(TEXT("%s  ·  %s  ·  %s"), *S->FormatDate(),
											 S->IsPaused() ? TEXT("Paused") : TEXT("Running"),
											 *FPA250CampaignSpeed::Format(S->GetHoursPerSecond())));
}

FText UPA250MapWidget::TreasuryText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const int32 Net = S->GetMonthlyNetGold();
	return FText::FromString(FString::Printf(TEXT("Treasury  %d gold   %s%d a month"), S->GetState().player_gold,
											 Net >= 0 ? TEXT("+") : TEXT("−"), FMath::Abs(Net)));
}

void UPA250MapWidget::RefreshTreasuryCache() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign() || S->GetHudEpoch() == TreasuryEpoch) return;
	TreasuryEpoch = S->GetHudEpoch();
	TreasuryLabels.Reset();
	TreasuryAmounts.Reset();
	TreasuryNote.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(S->GetTreasuryJson());
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;
	auto Row = [this](const FString& Label, const FString& Amount) {
		TreasuryLabels += Label + TEXT("\n");
		TreasuryAmounts += Amount + TEXT("\n");
	};
	auto Signed = [](int32 V) { return FString::Printf(TEXT("%s%d"), V >= 0 ? TEXT("+") : TEXT("−"), FMath::Abs(V)); };
	const int32 Debt = Root->GetIntegerField(TEXT("debt"));
	Row(TEXT("Balance"), FString::Printf(TEXT("%d gold"), Root->GetIntegerField(TEXT("gold"))));
	Row(FString::Printf(TEXT("Debt on bills of credit (limit %d)"), Root->GetIntegerField(TEXT("credit_limit"))),
		FString::Printf(TEXT("%d gold"), Debt));
	Row(TEXT(""), TEXT(""));
	Row(TEXT("INCOME, EACH MONTH"), TEXT(""));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("income")))
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		if (L.IsValid()) Row(TEXT("   ") + L->GetStringField(TEXT("label")), Signed(L->GetIntegerField(TEXT("gold"))));
	}
	if (Root->HasField(TEXT("levy_towns")))
	{
		const TArray<TSharedPtr<FJsonValue>>& LevyTowns = Root->GetArrayField(TEXT("levy_towns"));
		if (LevyTowns.Num() > 0)
		{
			Row(TEXT("   Towns that pay (held on the 1st)"), TEXT(""));
			for (const TSharedPtr<FJsonValue>& V : LevyTowns)
			{
				const TSharedPtr<FJsonObject> L = V->AsObject();
				if (L.IsValid())
					Row(TEXT("      ") + L->GetStringField(TEXT("label")), Signed(L->GetIntegerField(TEXT("gold"))));
			}
		}
	}
	Row(TEXT(""), TEXT(""));
	Row(TEXT("EXPENSES, EACH MONTH"), TEXT(""));
	const TArray<TSharedPtr<FJsonValue>>& Expenses = Root->GetArrayField(TEXT("expenses"));
	for (const TSharedPtr<FJsonValue>& V : Expenses)
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		if (L.IsValid()) Row(TEXT("   ") + L->GetStringField(TEXT("label")), Signed(L->GetIntegerField(TEXT("gold"))));
	}
	if (Expenses.Num() == 0) Row(TEXT("   None"), TEXT(""));
	Row(TEXT(""), TEXT(""));
	Row(TEXT("NET, EACH MONTH"), Signed(Root->GetIntegerField(TEXT("net"))) + TEXT(" gold"));
	LoanStep = Root->GetIntegerField(TEXT("loan_step"));
	LoanInterestPct = Root->GetIntegerField(TEXT("loan_interest_pct"));
	bCanBorrow = Root->GetBoolField(TEXT("can_borrow"));
	bCanRepay = Root->GetBoolField(TEXT("can_repay"));
	TreasuryNote = FString::Printf(
		TEXT("Also each month, into your towns' magazines: +%d muskets from workshops, +%d provisions from farms.\n"
			 "ON-DATE LEVY: a town pays only if you hold it on the morning of the 1st — lost in April, nothing in May "
			 "(no pro-rata for days held). The list above is the projection for towns you hold now. After settlement, "
			 "the Ledger shows each town that paid. A shortfall is borrowed up to the limit; beyond it the army goes "
			 "unpaid and its morale falls."),
		Root->GetIntegerField(TEXT("muskets")), Root->GetIntegerField(TEXT("provisions")));
	const int32 Arrears = Root->GetIntegerField(TEXT("arrears_months"));
	if (Arrears > 0) TreasuryNote = FString::Printf(TEXT("THE ARMY IS UNPAID (%d month%s).\n"), Arrears, Arrears == 1 ? TEXT("") : TEXT("s")) + TreasuryNote;
}

void UPA250MapWidget::PressTreasuryToggle()
{
	bShowTreasury = !bShowTreasury;
	InvalidateLayoutAndVolatility();
}

void UPA250MapWidget::PressBorrow()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (!S) return;
	SetStatusMessage(S->TakeLoan(Err) ? S->GetLastResourceChangeReason()
					 : Err.StartsWith(TEXT("credit_exhausted")) ? FString(TEXT("The province's credit is exhausted: hold more towns or redeem bills first."))
					 : TEXT("Cannot borrow: ") + Err);
}

void UPA250MapWidget::PressRepay()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (!S) return;
	SetStatusMessage(S->RepayLoan(Err) ? S->GetLastResourceChangeReason()
					 : Err.StartsWith(TEXT("no_debt")) ? FString(TEXT("Nothing is owed."))
					 : Err.StartsWith(TEXT("insufficient_gold")) ? FString(TEXT("Not enough gold in the treasury to redeem bills."))
					 : TEXT("Cannot repay: ") + Err);
}

FText UPA250MapWidget::StoresText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::CampaignState& St = S->GetState();
	// Compact faction totals for the top bar (detail lives in the province window / ledger).
	int32 Powder = 0, Cart = 0, Muskets = 0, Prov = 0, InTransit = 0;
	for (const auto& Kv : St.provinces)
	{
		if (Kv.second.owner != St.player_faction) continue;
		const pa250::ProvinceStores& Ps = pa250::province_ledger(St, Kv.first);
		Powder += Ps.powder;
		Cart += Ps.cartridges;
		Muskets += Ps.muskets;
		Prov += Ps.provisions;
	}
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status == "in_transit" || Sh.status == "committed") ++InTransit;
	}
	return FText::FromString(PA250PassPanels::StoresChip(Powder, Cart, Muskets, Prov, InTransit));
}

float UPA250MapWidget::HudWidthUnits() const
{
	const float Cached = GetCachedGeometry().GetLocalSize().X;
	if (Cached > 1.f) return Cached;
	// Not laid out yet: the viewport over the UI scale (which follows the window's shorter side).
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
	const float Scale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)));
	return Scale > KINDA_SMALL_NUMBER ? Size.X / Scale : Size.X;
}

FString UPA250MapWidget::TopBarReport(bool& bOutFits) const
{
	bOutFits = true;
	const FGeometry Root = GetCachedGeometry();
	const float Width = Root.GetLocalSize().X;
	FString Out = FString::Printf(TEXT("width %.0f units"), Width);
	for (const TPair<FString, TSharedPtr<SWidget>>& Piece : BarPieces)
	{
		if (!Piece.Value.IsValid()) continue;
		if (!Piece.Value->GetVisibility().IsVisible())
		{
			Out += FString::Printf(TEXT("; %s hidden"), *Piece.Key);
			continue;
		}
		const FGeometry G = Piece.Value->GetTickSpaceGeometry();
		const float Have = G.GetLocalSize().X, Want = Piece.Value->GetDesiredSize().X;
		const float Left = Root.AbsoluteToLocal(G.GetAbsolutePosition()).X;
		const float Right = Left + Have;
		const bool bOk = Have + 0.5f >= Want && Left >= -0.5f && Right <= Width + 0.5f;
		bOutFits &= bOk;
		Out += FString::Printf(TEXT("; %s %.0f of %.0f at %.0f%s"), *Piece.Key, Have, Want, Left, bOk ? TEXT("") : TEXT(" SQUEEZED"));
	}
	return Out;
}

bool UPA250MapWidget::GetPlayerArmyId(int32 Index, FString& OutId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign() || Index < 0)
	{
		return false;
	}
	const pa250::CampaignState& St = S->GetState();
	int32 I = 0;
	for (const auto& Kv : St.armies)
	{
		if (Kv.second.faction != St.player_faction) continue;
		if (I == Index)
		{
			OutId = ToFStr(Kv.first);
			return true;
		}
		++I;
	}
	return false;
}

FText UPA250MapWidget::ArmyButtonText(int32 Index) const
{
	FString Id;
	if (!GetPlayerArmyId(Index, Id))
	{
		return FText::GetEmpty();
	}
	UPA250Subsystem* S = Sub();
	const pa250::CampaignState& St = S->GetState();
	const pa250::Army& A = St.armies.at(ToStd(Id));
	const bool bSel = Id == SelectedArmyId;
	// Each army's general and its men (a detachment says so).
	return FText::FromString(FString::Printf(TEXT("%s%s, %s (%s)\n%s"), bSel ? TEXT("> ") : TEXT(""), *ToFStr(A.name), *ProvName(St, A.province),
											 *OrdersText(St, A), *PA250PassPanels::ForceLine(St, A)));
}

FText UPA250MapWidget::SelectedArmyBannerText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedArmyId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	auto It = St.armies.find(ToStd(SelectedArmyId));
	if (It == St.armies.end())
	{
		return FText::GetEmpty();
	}
	const pa250::Army& A = It->second;
	FString Route;
	const pa250::PathPlan Plan = pa250::march_plan(St, A.id);
	if (Plan.ok && Plan.provinces.size() > 1)
	{
		Route = TEXT(" · Route: ");
		for (size_t i = 0; i < Plan.provinces.size(); ++i)
		{
			Route += (i ? TEXT(" → ") : TEXT("")) + ProvName(St, Plan.provinces[i]);
		}
		const pa250::YMDH When = pa250::to_ymdh(Plan.arrival);
		Route += TEXT(" · ") + PA250MarchRoute::ArrivalLabel(When.d, When.m, When.h);
	}
	FString Orders = Plan.ok && Plan.provinces.size() > 1 ? FString::Printf(TEXT("→ %s"), *ProvName(St, Plan.provinces.back())) : OrdersText(St, A);
	if (A.returning_hours > 0)
	{
		Orders = FString::Printf(TEXT("turning back, %d march %s to %s"), A.returning_hours, A.returning_hours == 1 ? TEXT("hour") : TEXT("hours"),
								 *ProvName(St, A.province));
	}
	const FString Where = (!A.path.empty() && A.hours_into_edge > 0) || A.returning_hours > 0 ? TEXT("on the road from ") : TEXT("at ");
	return FText::FromString(FString::Printf(
		TEXT("%s — %s%s · %d fit · %s%s\nRight-click a town to march · Esc cancels"),
		*ToFStr(A.name), *Where, *ProvName(St, A.province), FitCount(A), *Orders, *Route));
}

void UPA250MapWidget::StartBattleStory()
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	const pa250::BattleSetup& Setup = S->GetBattleSetup();
	FString Why;
	if (!PA250BattleStory::Load(Why))
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 story: %s: going straight to the field"), *Why);
		S->TravelToFlowLevel(this);
		return;
	}
	const PA250BattleStory::FStory Story = PA250BattleStory::StoryFor(Setup);
	const FString Situation = PA250BattleStory::SituationLine(Setup);
	FString Level = UPA250Subsystem::BattleLevelFor(UTF8_TO_TCHAR(Setup.scenario_preset.c_str()), UTF8_TO_TCHAR(Setup.mode.c_str())).ToString();
	if (!Level.StartsWith(TEXT("/"))) Level = TEXT("/Game/PA250/Maps/") + Level;
	StoryLevelPackage = Level;
	const bool bHeadless = !FApp::CanEverRender() || !StoryBox.IsValid();
	UE_LOG(LogTemp, Display, TEXT("PA250 story: '%s' (%s, %s%s) for the battle of %s; %d sentences; situation: %s"), *Story.Title, *Story.Key, *Story.Date,
		   Story.bFallback ? TEXT(", terrain fallback") : TEXT(""), UTF8_TO_TCHAR(Setup.province_name.c_str()), Story.Sentences.Num(), *Situation);
	if (bHeadless)
	{
		// No window to show it in (a headless autoplay run): the story is passed through and the field loads at once.
		UE_LOG(LogTemp, Display, TEXT("PA250 story: no renderer: continue at once"));
		S->TravelToFlowLevel(this);
		return;
	}
	bool bPlaceholder = false;
	UTexture2D* Tex = PA250StoryArt::TextureFor(this, Story.Image, bPlaceholder);
	FSlateBrush Brush;
	Brush.SetResourceObject(Tex);
	Brush.ImageSize = FVector2D(1920.f, 1080.f);
	Brush.DrawAs = ESlateBrushDrawType::Image;
	bStoryUp = true;
	bStoryReady = false;
	bStoryLeaving = false;
	StoryStartSeconds = FPlatformTime::Seconds();
	TWeakObjectPtr<UPA250MapWidget> Weak(this);
	StoryBox->SetContent(SNew(SPA250BattleStoryScreen)
							 .Story(Story)
							 .Situation(Situation)
							 .Picture(Brush)
							 .bPlaceholder(bPlaceholder)
							 .StartSeconds(StoryStartSeconds)
							 .IsReady([Weak]() { return Weak.IsValid() && Weak->IsStoryReady(); })
							 .OnContinue(FSimpleDelegate::CreateLambda([Weak]() {
								 if (Weak.IsValid()) Weak->ContinueToBattle();
							 })));
	// Narration, if it has been rendered and imported; the text stands by itself otherwise.
	const FString NarrationPath = PA250BattleStory::NarrationAssetPath(Story);
	USoundBase* Narration = NarrationPath.IsEmpty() ? nullptr : LoadObject<USoundBase>(nullptr, *NarrationPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Narration) UGameplayStatics::PlaySound2D(this, Narration);
	UE_LOG(LogTemp, Display, TEXT("PA250 story: shown; picture %s%s; narration %s; loading %s behind it"), *Story.Image, bPlaceholder ? TEXT(" (placeholder)") : TEXT(""),
		   Narration ? TEXT("playing") : TEXT("not imported: text only"), *Level);
	// The field loads behind the story; Continue appears when it is in memory.
	LoadPackageAsync(Level, FLoadPackageAsyncDelegate::CreateLambda([Weak](const FName& Name, UPackage* Package, EAsyncLoadingResult::Type Result) {
		if (!Weak.IsValid()) return;
		Weak->bStoryReady = true;
		UE_LOG(LogTemp, Display, TEXT("PA250 story: the field is ready (%s, %s) after %.1f s: Continue is offered"), *Name.ToString(),
			   Result == EAsyncLoadingResult::Succeeded ? TEXT("loaded") : TEXT("not preloaded: it loads on Continue"), FPlatformTime::Seconds() - Weak->StoryStartSeconds);
	}));
}

void UPA250MapWidget::ContinueToBattle()
{
	UPA250Subsystem* S = Sub();
	if (!S || !bStoryUp || bStoryLeaving)
	{
		return;
	}
	bStoryLeaving = true;
	// The same picture stays up through what is left of the load: the engine's loading screen
	// (MoviePlayer) paints it on its own thread while the level opens, and goes when the level is in.
	if (IGameMoviePlayer* Movie = GetMoviePlayer())
	{
		const pa250::BattleSetup& Setup = S->GetBattleSetup();
		const PA250BattleStory::FStory Story = PA250BattleStory::StoryFor(Setup);
		bool bPlaceholder = false;
		FSlateBrush Brush;
		Brush.SetResourceObject(PA250StoryArt::TextureFor(this, Story.Image, bPlaceholder));
		Brush.ImageSize = FVector2D(1920.f, 1080.f);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		FLoadingScreenAttributes Attr;
		Attr.bAutoCompleteWhenLoadingCompletes = true;
		Attr.bMoviesAreSkippable = false;
		Attr.MinimumLoadingScreenDisplayTime = 0.5f;
		Attr.WidgetLoadingScreen = SNew(SPA250BattleStoryScreen)
									   .Story(Story)
									   .Situation(PA250BattleStory::SituationLine(Setup))
									   .Picture(Brush)
									   .bPlaceholder(bPlaceholder)
									   .StartSeconds(StoryStartSeconds);
		Movie->SetupLoadingScreen(Attr);
		UE_LOG(LogTemp, Display, TEXT("PA250 story: Continue after %.1f s; the loading screen keeps the picture up while the level opens (prepared=%d)"),
			   FPlatformTime::Seconds() - StoryStartSeconds, Movie->LoadingScreenIsPrepared() ? 1 : 0);
	}
	S->TravelToFlowLevel(this);
}

void UPA250MapWidget::PressPeopleToggle()
{
	bShowPeople = !bShowPeople;
	if (bShowPeople) bShowFloor = false;
	PeopleKey.Reset();
}

void UPA250MapWidget::PressFloorToggle()
{
	bShowFloor = !bShowFloor;
	if (bShowFloor)
	{
		bShowPeople = false;
		bShowTreasury = false;  // the centred treasury panel would run under the floor at 16:10
	}
	FloorKey.Reset();
}

void UPA250MapWidget::PressMissionTreeToggle()
{
	bShowMissionTree = !bShowMissionTree;
	if (bShowMissionTree) bTrackerMinimized = true;
	MissionTreeKey.Reset();
}

void UPA250MapWidget::PressFindToggle()
{
	bShowFind = !bShowFind;
	if (!bShowFind) FindQuery.Reset();
	RefreshFindPanel();
	InvalidateLayoutAndVolatility();
}

void UPA250MapWidget::FindJumpTo(const FString& Kind, const FString& Id)
{
	FString Label = Id;
	if (Kind == TEXT("town"))
	{
		SelectedProvinceId = Id;
		SelectedArmyId.Reset();
		if (UPA250Subsystem* S = Sub())
		{
			if (const auto It = S->GetState().provinces.find(ToStd(Id)); It != S->GetState().provinces.end())
				Label = ToFStr(It->second.name);
		}
	}
	else
	{
		SelectedArmyId = Id;
		if (UPA250Subsystem* S = Sub())
		{
			if (const auto It = S->GetState().armies.find(ToStd(Id)); It != S->GetState().armies.end())
			{
				SelectedProvinceId = ToFStr(It->second.province);
				Label = ToFStr(It->second.name);
			}
		}
	}
	NotifyMapSelectionChanged();
	PressFocusSelected();
	bShowFind = false;
	FindQuery.Reset();
	RefreshFindPanel();
	SetStatusMessage(Kind == TEXT("town") ? FString::Printf(TEXT("Jumped to %s."), *Label)
										  : FString::Printf(TEXT("Focused on %s."), *Label));
}

void UPA250MapWidget::PressReorganizeToggle()
{
	if (!CanReorganizeSelected())
	{
		SetStatusMessage(TEXT("Reorganize needs another of your armies idle in the same town (allied companies stay their own)."));
		return;
	}
	bShowReorganize = !bShowReorganize;
	ReorganizeKey.Reset();
}

bool UPA250MapWidget::CanReorganizeSelected() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedArmyId.IsEmpty() || !S->GetController().has_campaign()) return false;
	const pa250::CampaignState& St = S->GetState();
	const auto It = St.armies.find(ToStd(SelectedArmyId));
	if (It == St.armies.end() || It->second.faction != St.player_faction || !ArmyStanding(It->second) || It->second.role == "allied_company")
		return false;
	for (const auto& Kv : St.armies)
	{
		if (Kv.first == It->first) continue;
		if (Kv.second.faction == St.player_faction && Kv.second.province == It->second.province && ArmyStanding(Kv.second) &&
			Kv.second.role != "allied_company")
			return true;
	}
	return false;
}

void UPA250MapWidget::TransferUnits(const FString& FromArmy, const FString& ToArmy, const FString& UnitClass, int32 Count)
{
	if (Count <= 0 || UnitClass.IsEmpty()) return;
	const FString Order = FString::Printf(TEXT("{\"order\":\"transfer_units\",\"from_army\":\"%s\",\"to_army\":\"%s\",\"units\":{\"%s\":%d}}"),
										  *FromArmy, *ToArmy, *UnitClass, Count);
	IssueOrder(Order);
}

void UPA250MapWidget::IssueOrder(const FString& OrderJson)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	// While the clock is stopped (pause, battle or decision) the order is carried out at once; else
	// it is queued for the next hour, as a march order is.
	const bool bImmediate = S->IsPaused() || S->IsBattlePending() || S->IsDecisionPending();
	FString Err;
	if (!S->IssueOrderJson(OrderJson, Err, bImmediate))
	{
		SetStatusMessage(PA250PassPanels::OrderRefusal(Err));
		return;
	}
	const FString Note = S->GetLastResourceChangeReason();
	SetStatusMessage(bImmediate && !Note.IsEmpty() ? Note : FString(TEXT("Order given: it takes effect within the hour.")));
	PeopleKey.Reset();
	FloorKey.Reset();
	DecisionKey.Reset();
	if (!ReorganizeKey.IsEmpty()) ReorganizeKey.Reset();
}

void UPA250MapWidget::RefreshPassPanels()
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	TWeakObjectPtr<UPA250MapWidget> Weak(this);
	auto Issue = [Weak](const FString& Order) {
		if (Weak.IsValid()) Weak->IssueOrder(Order);
	};
	// HudEpoch moves every sim hour; rebuilding the floor that often flashes and drops clicks.
	// Key on what the panel shows (as the city view does), not the epoch.
	const FString WantPeople = bShowPeople && !S->IsBattlePending()
								   ? PA250PassPanels::PeopleSignature(St, SelectedArmyId, SelectedProvinceId)
								   : FString();
	if (PeopleBox.IsValid() && WantPeople != PeopleKey)
	{
		PeopleKey = WantPeople;
		PeopleBox->SetContent(WantPeople.IsEmpty()
								  ? SNullWidget::NullWidget
								  : PA250PassPanels::BuildPeople(St, SelectedArmyId, SelectedProvinceId, Issue,
																 [Weak]() {
																	 if (Weak.IsValid()) Weak->PressPeopleToggle();
																 },
																 520.f));
	}
	const float FloorW = PA250PassPanels::FloorPanelWidth(HudWidthUnits(), IsTrackerShown() && !bTrackerMinimized);
	// Width is part of the key so opening the tracker rewraps once. The hour is not: that was the flash.
	const FString WantFloor = bShowFloor && !S->IsBattlePending()
								  ? PA250PassPanels::FloorSignature(St) + FString::Printf(TEXT("|w%.0f"), FloorW)
								  : FString();
	if (FloorBox.IsValid() && WantFloor != FloorKey)
	{
		FloorKey = WantFloor;
		FloorBox->SetContent(WantFloor.IsEmpty()
								 ? SNullWidget::NullWidget
								 : PA250PassPanels::BuildFloor(St, Issue,
															   [Weak]() {
																   if (Weak.IsValid()) Weak->PressFloorToggle();
															   },
															   FloorW - 60.f));
	}
	const FString WantDecision = ToFStr(St.pass.pending_decision);
	if (DecisionBox.IsValid() && WantDecision != DecisionKey)
	{
		DecisionKey = WantDecision;
		if (WantDecision.IsEmpty())
		{
			DecisionBox->SetContent(SNullWidget::NullWidget);
		}
		else
		{
			const PA250PassPanels::FDecision D = PA250PassPanels::Decision(St);
			bool bPlaceholder = false;
			DecisionTexture = PA250StoryArt::TextureFor(this, D.Painting, bPlaceholder);
			DecisionBrush = FSlateBrush();
			DecisionBrush.SetResourceObject(DecisionTexture);
			DecisionBrush.ImageSize = FVector2D(1920.f, 1080.f);
			DecisionBrush.DrawAs = ESlateBrushDrawType::Image;
			DecisionBox->SetContent(PA250PassPanels::BuildDecision(St, Issue, DecisionTexture ? &DecisionBrush : nullptr));
			UE_LOG(LogTemp, Display, TEXT("PA250 decision shown: %s (%s, painting %s%s); the clock is stopped"), *D.Title, *D.Id, *D.Painting,
				   bPlaceholder ? TEXT(", placeholder") : TEXT(""));
		}
	}
}

void UPA250MapWidget::RefreshMissionTreePanel()
{
	UPA250Subsystem* S = Sub();
	if (!MissionTreeBox.IsValid() || !S || !S->GetController().has_campaign()) return;
	FString Sig;
	if (bShowMissionTree && !S->IsBattlePending())
	{
		for (const PA250ArcPanels::FMissionNode& N : PA250ArcPanels::MissionTree(S->GetState()))
			Sig += N.Id + TEXT(":") + N.Status + TEXT(":") + N.Progress + TEXT("|");
	}
	if (Sig == MissionTreeKey) return;
	MissionTreeKey = Sig;
	if (Sig.IsEmpty())
	{
		MissionTreeBox->SetContent(SNullWidget::NullWidget);
		return;
	}
	const FLinearColor Cream = FPA250HudStyle::Cream;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[SNew(STextBlock)
			 .Font(FPA250HudStyle::Font(TEXT("Italic"), 11))
			 .ColorAndOpacity(FPA250HudStyle::CreamDim)
			 .AutoWrapText(true)
			 .Text(FText::FromString(TEXT("Your path of missions. Locked ones stay readable so you can plan ahead.")))];
	for (const PA250ArcPanels::FMissionNode& N : PA250ArcPanels::MissionTree(S->GetState()))
	{
		FLinearColor Col = Cream;
		if (N.Status == TEXT("complete")) Col = FPA250HudStyle::Green;
		else if (N.Status == TEXT("failed")) Col = FPA250HudStyle::Red;
		else if (N.Status == TEXT("locked")) Col = FPA250HudStyle::CreamDim;
		else Col = FPA250HudStyle::Brass;
		const FString Head = FString::Printf(TEXT("%d/%d  %s  ·  %s"), N.Index, N.Total, *N.Title, *N.Status.ToUpper());
		TSharedRef<SVerticalBox> Row = SNew(SVerticalBox) +
									   SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
																			 .Font(FPA250HudStyle::Font(TEXT("Bold"), 13))
																			 .ColorAndOpacity(Col)
																			 .AutoWrapText(true)
																			 .Text(FText::FromString(Head))] +
									   SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
																			 .Font(FPA250HudStyle::Font(TEXT("Regular"), 11))
																			 .ColorAndOpacity(Cream)
																			 .AutoWrapText(true)
																			 .Text(FText::FromString(N.Goal))];
		if (!N.Progress.IsEmpty())
			Row->AddSlot().AutoHeight()[SNew(STextBlock)
											.Font(FPA250HudStyle::Font(TEXT("Italic"), 11))
											.ColorAndOpacity(FPA250HudStyle::Brass)
											.AutoWrapText(true)
											.Text(FText::FromString(N.Progress))];
		if (!N.Deadline.IsEmpty())
			Row->AddSlot().AutoHeight()[SNew(STextBlock)
											.Font(FPA250HudStyle::Font(TEXT("Regular"), 11))
											.ColorAndOpacity(FPA250HudStyle::Red)
											.Text(FText::FromString(N.Deadline))];
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[Row];
		if (N.Index < N.Total)
			List->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)
				[SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 12)).ColorAndOpacity(FPA250HudStyle::BrassDim).Text(FText::FromString(TEXT("↓")))];
	}
	TWeakObjectPtr<UPA250MapWidget> Weak(this);
	MissionTreeBox->SetContent(
		SNew(SBorder)
			.BorderImage(&FPA250HudStyle::Get().Panel)
			.Padding(FMargin(14.f, 10.f))
				[SNew(SVerticalBox) +
				 SVerticalBox::Slot().AutoHeight()
					 [SNew(SHorizontalBox) +
					  SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						  [SNew(STextBlock)
							   .Font(FPA250HudStyle::Font(TEXT("Bold"), 16))
							   .ColorAndOpacity(Cream)
							   .Text(FText::FromString(TEXT("MISSION TREE")))] +
					  SHorizontalBox::Slot().AutoWidth()
						  [SNew(SButton)
							   .ButtonStyle(&FPA250HudStyle::Get().Button)
							   .IsFocusable(false)
							   .ContentPadding(FMargin(9.f, 2.f))
							   .OnClicked_Lambda([Weak]() {
								   if (Weak.IsValid()) Weak->PressMissionTreeToggle();
								   return FReply::Handled();
							   })
								   [SNew(STextBlock)
										.Font(FPA250HudStyle::Font(TEXT("Regular"), 11))
										.ColorAndOpacity(Cream)
										.Text(FText::FromString(TEXT("Close")))]]] +
				 SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 8.f, 0.f, 0.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]]);
}

void UPA250MapWidget::RebuildFindHits()
{
	if (!FindHits.IsValid()) return;
	FindHits->ClearChildren();
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return;
	TArray<FFindHit> Hits;
	CollectFindHits(S->GetState(), FindQuery, Hits);
	const FLinearColor Cream = FPA250HudStyle::Cream;
	TWeakObjectPtr<UPA250MapWidget> Weak(this);
	for (const FFindHit& H : Hits)
	{
		const FString Kind = H.Kind;
		const FString Id = H.Id;
		const FString Line = FString::Printf(TEXT("%s · %s"), Kind == TEXT("town") ? TEXT("Town") : TEXT("Army"), *H.Label);
		FindHits->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[SNew(SButton)
				 .ButtonStyle(&FPA250HudStyle::Get().Button)
				 .IsFocusable(false)
				 .HAlign(HAlign_Left)
				 .ContentPadding(FMargin(8.f, 4.f))
				 .OnClicked_Lambda([Weak, Kind, Id]() {
					 if (Weak.IsValid()) Weak->FindJumpTo(Kind, Id);
					 return FReply::Handled();
				 })
					 [SNew(STextBlock)
						  .Font(FPA250HudStyle::Font(TEXT("Regular"), 12))
						  .ColorAndOpacity(Cream)
						  .Text(FText::FromString(Line))]];
	}
}

void UPA250MapWidget::RefreshFindPanel()
{
	if (!FindBox.IsValid()) return;
	if (!bShowFind)
	{
		FindBox->SetContent(SNullWidget::NullWidget);
		FindField.Reset();
		FindHits.Reset();
		bFindFocusPending = false;
		return;
	}
	if (!FindField.IsValid())
	{
		const FLinearColor Cream = FPA250HudStyle::Cream;
		TWeakObjectPtr<UPA250MapWidget> Weak(this);
		FindHits = SNew(SVerticalBox);
		FindField = SNew(SEditableTextBox)
						.Font(FPA250HudStyle::Font(TEXT("Regular"), 13))
						.HintText(FText::FromString(TEXT("Town or army, then Enter. Esc closes.")))
						.Text(FText::FromString(FindQuery))
						.OnTextChanged_Lambda([Weak](const FText& T) {
							if (!Weak.IsValid()) return;
							Weak->FindQuery = T.ToString();
							Weak->RebuildFindHits();
						})
						.OnTextCommitted_Lambda([Weak](const FText& T, ETextCommit::Type Commit) {
							if (!Weak.IsValid() || Commit != ETextCommit::OnEnter) return;
							Weak->FindQuery = T.ToString();
							UPA250Subsystem* S2 = Weak->Sub();
							if (!S2 || !S2->GetController().has_campaign()) return;
							TArray<FFindHit> Hits;
							CollectFindHits(S2->GetState(), Weak->FindQuery, Hits);
							if (Hits.Num() > 0) Weak->FindJumpTo(Hits[0].Kind, Hits[0].Id);
						})
						.OnKeyDownHandler_Lambda([Weak](const FGeometry&, const FKeyEvent& E) {
							if (E.GetKey() != EKeys::Escape || !Weak.IsValid()) return FReply::Unhandled();
							Weak->bShowFind = false;
							Weak->FindQuery.Reset();
							Weak->bFindDismiss = true;
							return FReply::Handled();
						});
		FindBox->SetContent(
			SNew(SBorder)
				.BorderImage(&FPA250HudStyle::Get().Panel)
				.Padding(FMargin(14.f, 10.f))
					[SNew(SVerticalBox) +
					 SVerticalBox::Slot().AutoHeight()
						 [SNew(STextBlock)
							  .Font(FPA250HudStyle::Font(TEXT("Bold"), 16))
							  .ColorAndOpacity(Cream)
							  .Text(FText::FromString(TEXT("FIND")))] +
					 SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 6.f)[FindField.ToSharedRef()] +
					 SVerticalBox::Slot().AutoHeight().MaxHeight(280.f)[SNew(SScrollBox) + SScrollBox::Slot()[FindHits.ToSharedRef()]]]);
		bFindFocusPending = true;
	}
	RebuildFindHits();
}

void UPA250MapWidget::RefreshReorganizePanel()
{
	UPA250Subsystem* S = Sub();
	if (!ReorganizeBox.IsValid() || !S || !S->GetController().has_campaign()) return;
	const bool bShow = bShowReorganize && CanReorganizeSelected() && !S->IsBattlePending();
	FString Sig;
	if (bShow)
	{
		const pa250::CampaignState& St = S->GetState();
		const auto Mine = St.armies.find(ToStd(SelectedArmyId));
		if (Mine == St.armies.end())
		{
			bShowReorganize = false;
			ReorganizeBox->SetContent(SNullWidget::NullWidget);
			ReorganizeKey.Reset();
			return;
		}
		Sig = SelectedArmyId + TEXT("|") + ToFStr(Mine->second.province);
		for (const auto& Kv : St.armies)
		{
			if (Kv.second.faction != St.player_faction || Kv.second.province != Mine->second.province) continue;
			// Idle or not, not the hour count: returning_hours falls every march hour and would rebuild the buttons.
			const bool bIdle = ArmyStanding(Kv.second) && Kv.second.role != "allied_company";
			Sig += TEXT("|") + ToFStr(Kv.first) + (bIdle ? TEXT(":idle") : TEXT(":away"));
			if (!bIdle) continue;
			for (const auto& U : Kv.second.units) Sig += FString::Printf(TEXT(":%s=%d"), *ToFStr(U.first), U.second);
		}
	}
	if (Sig == ReorganizeKey) return;
	ReorganizeKey = Sig;
	if (!bShow)
	{
		ReorganizeBox->SetContent(SNullWidget::NullWidget);
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	const auto Mine = St.armies.find(ToStd(SelectedArmyId));
	TArray<FString> Others;
	for (const auto& Kv : St.armies)
	{
		if (Kv.first == Mine->first) continue;
		if (Kv.second.faction == St.player_faction && Kv.second.province == Mine->second.province && ArmyStanding(Kv.second) &&
			Kv.second.role != "allied_company")
			Others.Add(ToFStr(Kv.first));
	}
	Others.Sort();
	if (Others.Num() == 0)
	{
		bShowReorganize = false;
		ReorganizeBox->SetContent(SNullWidget::NullWidget);
		return;
	}
	if (ReorganizeOtherArmyId.IsEmpty() || !Others.Contains(ReorganizeOtherArmyId)) ReorganizeOtherArmyId = Others[0];
	const auto Other = St.armies.find(ToStd(ReorganizeOtherArmyId));
	const FLinearColor Cream = FPA250HudStyle::Cream;
	TWeakObjectPtr<UPA250MapWidget> Weak(this);
	auto UnitRows = [&](const pa250::Army& From, const FString& FromId, const FString& ToId) -> TSharedRef<SWidget> {
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[SNew(STextBlock)
				 .Font(FPA250HudStyle::Font(TEXT("Bold"), 13))
				 .ColorAndOpacity(Cream)
				 .Text(FText::FromString(ToFStr(From.name)))];
		for (const auto& U : From.units)
		{
			if (U.second <= 0) continue;
			const FString Cls = ToFStr(U.first);
			const int32 N = U.second;
			Col->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[SNew(SHorizontalBox) +
				 SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					 [SNew(STextBlock)
						  .Font(FPA250HudStyle::Font(TEXT("Regular"), 12))
						  .ColorAndOpacity(Cream)
						  .Text(FText::FromString(FString::Printf(TEXT("%s × %d"), *Cls.Replace(TEXT("_"), TEXT(" ")), N)))] +
				 SHorizontalBox::Slot().AutoWidth()
					 [SNew(SButton)
						  .ButtonStyle(&FPA250HudStyle::Get().Button)
						  .IsFocusable(false)
						  .ContentPadding(FMargin(8.f, 2.f))
						  .ToolTipText(FText::FromString(TEXT("Move one company to the other army.")))
						  .OnClicked_Lambda([Weak, FromId, ToId, Cls]() {
							  if (Weak.IsValid()) Weak->TransferUnits(FromId, ToId, Cls, 1);
							  return FReply::Handled();
						  })
							  [SNew(STextBlock)
								   .Font(FPA250HudStyle::Font(TEXT("Regular"), 11))
								   .ColorAndOpacity(Cream)
								   .Text(FText::FromString(TEXT("Move 1")))]]];
		}
		return Col;
	};
	ReorganizeBox->SetContent(
		SNew(SBorder)
			.BorderImage(&FPA250HudStyle::Get().Panel)
			.Padding(FMargin(14.f, 10.f))
				[SNew(SVerticalBox) +
				 SVerticalBox::Slot().AutoHeight()
					 [SNew(SHorizontalBox) +
					  SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						  [SNew(STextBlock)
							   .Font(FPA250HudStyle::Font(TEXT("Bold"), 16))
							   .ColorAndOpacity(Cream)
							   .Text(FText::FromString(TEXT("REORGANIZE")))] +
					  SHorizontalBox::Slot().AutoWidth()
						  [SNew(SButton)
							   .ButtonStyle(&FPA250HudStyle::Get().Button)
							   .IsFocusable(false)
							   .ContentPadding(FMargin(9.f, 2.f))
							   .OnClicked_Lambda([Weak]() {
								   if (Weak.IsValid()) Weak->PressReorganizeToggle();
								   return FReply::Handled();
							   })
								   [SNew(STextBlock)
										.Font(FPA250HudStyle::Font(TEXT("Regular"), 11))
										.ColorAndOpacity(Cream)
										.Text(FText::FromString(TEXT("Close")))]]] +
				 SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 8.f)
					 [SNew(STextBlock)
						  .Font(FPA250HudStyle::Font(TEXT("Italic"), 11))
						  .ColorAndOpacity(FPA250HudStyle::CreamDim)
						  .AutoWrapText(true)
						  .Text(FText::FromString(TEXT("Move companies between armies standing idle in this town. Allied companies (Stockbridge) cannot merge.")))] +
				 SVerticalBox::Slot().AutoHeight()
					 [SNew(SHorizontalBox) +
					  SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 10.f, 0.f)[UnitRows(Mine->second, SelectedArmyId, ReorganizeOtherArmyId)] +
					  SHorizontalBox::Slot().FillWidth(1.f)[UnitRows(Other->second, ReorganizeOtherArmyId, SelectedArmyId)]]]);
}

FString UPA250MapWidget::HaltMessage(const FString& ArmyName, const FString& TownName, int32 HoursIntoLeg)
{
	if (HoursIntoLeg <= 0)
	{
		return FString::Printf(TEXT("%s halts at %s."), *ArmyName, *TownName);
	}
	return FString::Printf(TEXT("%s turns about: %d march %s back to %s."), *ArmyName, HoursIntoLeg, HoursIntoLeg == 1 ? TEXT("hour") : TEXT("hours"), *TownName);
}

bool UPA250MapWidget::CanHaltSelected() const
{
	UPA250Subsystem* S = Sub();
	return S && !SelectedArmyId.IsEmpty() && !S->IsBattlePending() && S->IsArmyOnTheMarch(SelectedArmyId);
}

void UPA250MapWidget::PressHalt()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedArmyId.IsEmpty() || !S->GetController().has_campaign())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	const auto It = St.armies.find(ToStd(SelectedArmyId));
	if (It == St.armies.end())
	{
		return;
	}
	if (!S->IsArmyOnTheMarch(SelectedArmyId))
	{
		SetStatusMessage(ToFStr(It->second.name) + TEXT(" is not marching."));
		return;
	}
	const FString Name = ToFStr(It->second.name);
	const FString Town = ProvName(St, It->second.province);
	const int32 Into = It->second.path.empty() ? 0 : It->second.hours_into_edge;
	FString Err;
	if (S->HaltArmy(SelectedArmyId, Err))
	{
		SetStatusMessage(HaltMessage(Name, Town, Into));
	}
	else
	{
		SetStatusMessage(TEXT("Halt refused: ") + Err);
	}
}

void UPA250MapWidget::PressSelectArmy(const FString& ArmyId)
{
	UPA250Subsystem* S = Sub();
	if (!S || ArmyId.IsEmpty() || !S->GetController().has_campaign())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	auto It = St.armies.find(ToStd(ArmyId));
	if (It == St.armies.end() || It->second.faction != St.player_faction)
	{
		return;
	}
	SelectedArmyId = ArmyId;
	SelectedProvinceId = ToFStr(It->second.province);
	OpenCityView(SelectedProvinceId);
	SetStatusMessage(FString::Printf(TEXT("%s selected at %s. Right-click a destination town."),
									 *ToFStr(It->second.name), *ProvName(St, It->second.province)));
	NotifyMapSelectionChanged();
	if (APA250ProvinceMapActor* Map = MapActor.Get())
	{
		Map->FocusSelected();
	}
}

void UPA250MapWidget::PressSelectArmyAtSelection()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	for (int32 i = 0;; ++i)
	{
		FString Id;
		if (!GetPlayerArmyId(i, Id)) break;
		const pa250::CampaignState& St = S->GetState();
		if (ToFStr(St.armies.at(ToStd(Id)).province) == SelectedProvinceId)
		{
			PressSelectArmy(Id);
			return;
		}
	}
	SetStatusMessage(TEXT("No friendly army in this province. Use the Your forces panel (top-left)."));
}

FText UPA250MapWidget::BattleReportText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::BattleReport& Br = S->GetState().progress.last_battle_report;
	if (Br.province_id.empty())
	{
		return FText::GetEmpty();
	}
	auto Count = [](const std::map<std::string, std::map<std::string, int>>& M, const char* F) {
		int32 N = 0;
		auto It = M.find(F);
		if (It != M.end())
			for (const auto& Kv : It->second) N += Kv.second;
		return N;
	};
	auto Spent = [](const std::map<std::string, int>& M, const char* F) {
		auto It = M.find(F);
		return It == M.end() ? 0 : It->second;
	};
	int32 Morale = 0;
	for (const auto& Kv : Br.morale_delta) Morale += Kv.second;
	const pa250::CampaignState& St = S->GetState();
	auto NameOf = [&St](const std::string& Id) {
		auto It = St.provinces.find(Id);
		return It == St.provinces.end() ? ToFStr(Id) : ToFStr(It->second.name);
	};
	const std::string& Mine = St.player_faction;
	const char* Enemy = Mine == "british" ? "continental" : "british";
	const FString EnemyName = Mine == "british" ? TEXT("Continentals") : TEXT("British");
	const bool bWon = Br.winner == Mine;
	FString Fate;
	if (Br.loser_fate == "retreated" && !Br.retreat_to.empty())
	{
		Fate = FString::Printf(TEXT("Enemy army: retreated to %s"), *NameOf(Br.retreat_to));
		if (!bWon) Fate = FString::Printf(TEXT("Your army: retreated to %s"), *NameOf(Br.retreat_to));
	}
	else if (Br.loser_fate == "shattered")
	{
		Fate = bWon ? TEXT("Enemy army: shattered and dispersed — it will not march again")
					: TEXT("Your army: shattered and dispersed — it will not march again");
	}
	else if (Br.loser_fate == "destroyed")
	{
		Fate = bWon ? TEXT("Enemy army: destroyed to a man") : TEXT("Your army: destroyed to a man");
	}
	else if (!Br.loser_fate.empty() && Br.loser_fate != "none")
	{
		Fate = FString::Printf(TEXT("Loser fate: %s"), *ToFStr(Br.loser_fate));
	}
	FString Reasons;
	for (const std::string& Line : Br.change_reasons)
	{
		Reasons += TEXT("\n  · ") + ToFStr(Line);
	}
	return FText::FromString(FString::Printf(
		TEXT("After-battle report — Battle of %s\n%s\n"
			 "Ground: %s → %s (%s) · Posts held by %s\n"
			 "Support %d → %d · Gold %d → %d%s\n"
			 "— Casualties —\n"
			 "Yours: %d killed · %d wounded\n"
			 "%s: %d killed · %d wounded\n"
			 "By your own hand: %d killed\n"
			 "— Ammunition —\n"
			 "Cartridges (balls): yours %d · enemy %d · Powder: yours %d · enemy %d\n"
			 "Scavenged from the field: +%d cartridges · +%d powder · +%d muskets\n"
			 "Morale: net %+d%s%s"),
		*NameOf(Br.province_id), bWon ? TEXT("Victory") : TEXT("Defeat"), *ToFStr(Br.province_owner_before),
		*ToFStr(Br.province_owner_after),
		Br.ownership_reason.empty() ? TEXT("unchanged") : *ToFStr(Br.ownership_reason),
		Br.objective_holder.empty() ? TEXT("—") : *ToFStr(Br.objective_holder), Br.local_support_before,
		Br.local_support_after, Br.gold_before, Br.gold_after,
		Br.defender_reward_support > 0
			? *FString::Printf(TEXT(" · defender reward +%d Support"), Br.defender_reward_support)
			: TEXT(""),
		Count(Br.losses, Mine.c_str()), Count(Br.wounded, Mine.c_str()), *EnemyName, Count(Br.losses, Enemy),
		Count(Br.wounded, Enemy), Br.personal_kills, Spent(Br.cartridges_spent, Mine.c_str()),
		Spent(Br.cartridges_spent, Enemy), Spent(Br.powder_spent, Mine.c_str()), Spent(Br.powder_spent, Enemy),
		Br.scavenged_cartridges, Br.scavenged_powder, Br.scavenged_muskets, Morale,
		Fate.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\n%s"), *Fate), *Reasons));
}

FText UPA250MapWidget::LedgerText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::CampaignState& St = S->GetState();
	FString Lines = FString::Printf(TEXT("Treasury 🪙 %d · Support %d\n"), St.player_gold, St.local_support);
	Lines += TEXT("Recent changes (town levies are on-date: held on the 1st only):\n");
	// Keep enough room for a May 1 town-by-town settlement cluster.
	const int32 Start = FMath::Max(0, static_cast<int32>(St.resource_changes.size()) - 28);
	for (int32 i = Start; i < static_cast<int32>(St.resource_changes.size()); ++i)
	{
		Lines += TEXT("  ") + ToFStr(St.resource_changes[static_cast<size_t>(i)].reason) + TEXT("\n");
	}
	if (St.resource_changes.empty()) Lines += TEXT("  (none yet)\n");
	Lines += PendingText().ToString();
	Lines += TEXT("\nProvinces · dev · cart/musk/prov\n");
	for (const auto& Kv : St.provinces)
	{
		if (Kv.second.owner != St.player_faction) continue;
		const pa250::ProvinceStores& Ps = pa250::province_ledger(St, Kv.first);
		Lines += FString::Printf(TEXT("%s · %d · %d/%d/%d\n"), *ToFStr(Kv.second.name),
								 pa250::province_development(Kv.second), Ps.cartridges, Ps.muskets, Ps.provisions);
	}
	bool bAny = false;
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status != "in_transit" && Sh.status != "committed") continue;
		if (!bAny) Lines += TEXT("In transit:\n");
		bAny = true;
		auto Town = [&St](const std::string& Id) {
			const auto It = St.provinces.find(Id);
			return It == St.provinces.end() ? FString(TEXT("the market")) : ToFStr(It->second.name);
		};
		Lines += FString::Printf(TEXT("  %s → %s · arrives %s\n"),
								 *Town(Sh.source_province),
								 *Town(Sh.dest_province),
								 *S->FormatWhen(Sh.deliver_on_hour));
	}
	for (const auto& Job : St.progress.cartridge_jobs)
	{
		if (Job.status != "underway") continue;
		const auto JobTown = St.provinces.find(Job.province_id);
		Lines += FString::Printf(TEXT("Cartridges being made at %s: +%d, ready %s\n"),
								 JobTown == St.provinces.end() ? TEXT("the depot") : *ToFStr(JobTown->second.name),
								 Job.cartridges_out, *S->FormatWhen(Job.complete_on_hour));
	}
	return FText::FromString(Lines);
}

FText UPA250MapWidget::PendingText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	const pa250::CampaignState& St = S->GetState();
	FString Lines = TEXT("Pending:\n");
	bool bAny = false;
	for (const auto& Letter : St.staff.queue)
	{
		if (Letter.resolve_on_hour <= 0) continue;
		if (Letter.status == "delivered" || Letter.status == "refused" || Letter.status == "dismissed") continue;
		if (Letter.awaiting_choice || Letter.status == "needs_approval") continue;
		if (Letter.resolve_on_hour <= St.now.hour_index) continue;
		const int64 HoursLeft = Letter.resolve_on_hour - St.now.hour_index;
		Lines += FString::Printf(TEXT("  %s (%s) — %lldh · %s\n"), *ToFStr(Letter.subject), *ToFStr(Letter.kind),
								 static_cast<long long>(HoursLeft), *ToFStr(Letter.reason));
		bAny = true;
	}
	for (const auto& Sh : St.shipments)
	{
		if (Sh.status != "in_transit" && Sh.status != "committed") continue;
		if (Sh.deliver_on_hour <= St.now.hour_index) continue;
		Lines += FString::Printf(TEXT("  Shipment %s → %s — %lldh\n"), *ToFStr(Sh.id), *ToFStr(Sh.dest_province),
								 static_cast<long long>(Sh.deliver_on_hour - St.now.hour_index));
		bAny = true;
	}
	if (!bAny) Lines += TEXT("  (none)\n");
	return FText::FromString(Lines);
}

FText UPA250MapWidget::AftermathTip(const FString& Choice) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(ToFStr(pa250::aftermath_choice_preview(S->GetState(), ToStd(Choice))));
}

FText UPA250MapWidget::SupportText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	(void)S->GetHudEpoch();
	const pa250::CampaignState& St = S->GetState();
	auto Get = [](const std::map<std::string, int>& M, const char* K) {
		auto It = M.find(K);
		return It == M.end() ? 0 : It->second;
	};
	FString Treaties;
	for (const auto& T : St.treaties)
	{
		Treaties += FString::Printf(TEXT("%s%s %s"), Treaties.IsEmpty() ? TEXT("") : TEXT(", "),
									UTF8_TO_TCHAR(T.type.c_str()), *UPA250Subsystem::FormatHoursRemaining(T.hours_remaining));
	}
	(void)Get;
	return FText::FromString(FString::Printf(TEXT("Support %d · Attitude %d%s"), St.local_support,
											 Get(St.relations, "british"),
											 Treaties.IsEmpty() ? TEXT("") : *FString(TEXT(" · ") + Treaties)));
}

FText UPA250MapWidget::ScoreboardText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(PA250ArcPanels::LiveScoreboard(S->GetState()));
}

FText UPA250MapWidget::AboutText() const
{
	return FText::FromString(TEXT(
		"About this map\n\n"
		"Source: “A plan of the town and harbour of Boston and the country adjacent with the road from Boston to "
		"Concord”, J. De Costa, engraved by Charles Hall, issued 29 July 1775. Working image: Library of Congress "
		"copy of the same plate; planned source NYPL Digital Collections item 46abf040-c5ed-012f-b389-58d385a7bc34.\n\n"
		"Historical-date caveat: this July 1775 engraving includes later siege information. Its printed soldiers, "
		"camps and battle annotations are historical artwork — not live units and not verified April 19 positions. "
		"Who holds a town is shown by its diamond and a soft colour wash: a hollow blue diamond for Continentals "
		"(yours), a solid red diamond for the British. The "
		"wash follows the period practice of hand-colouring and marks the town only; the 1775 plate shows no town "
		"boundaries, so none are drawn. An army is a round "
		"counter with a bar at wide zoom and, where artwork exists for it, a small illustration when zoomed in, "
		"labeled Yours or British — "
		"click it, then right-click a town to march.\n\n"
		"Army illustrations are modern artistic reconstructions in an engraved style, not period images and not a "
		"uniform reference for any particular unit; the number of figures drawn does not show strength. A British "
		"column is drawn marching only while it is on the march, and firing only while a battle is pending; a British "
		"force at rest keeps the counter. The provincial picture shows provincial regulars and is used only for a "
		"Continental force made up mostly of line infantry; any other Continental force is drawn as a painted "
		"militia company (a modern illustration, not in the engraved style). "
		"Left-click a town to inspect it; with an army selected, right-click a town to march. "
		"The game date follows the campaign clock."));
}

FText UPA250MapWidget::DisclaimerText() const
{
	return AboutText();
}

FText UPA250MapWidget::StatusText() const
{
	const FString Line = !LastMessage.IsEmpty() ? LastMessage : Message;
	return FText::FromString(Line.IsEmpty()
								 ? TEXT("Select a town or army. Drag or arrow keys to pan, wheel to zoom, Space pauses, Esc clears.")
								 : Line);
}

FString UPA250MapWidget::DevText() const
{
	UPA250Subsystem* S = Sub();
	if (!bShowDev || !S || !S->GetController().has_campaign()) return FString();
	FString Out = FString::Printf(TEXT("[Dev] province=%s army=%s staff_queue=%d"), *SelectedProvinceId,
								  SelectedArmyId.IsEmpty() ? TEXT("none") : *SelectedArmyId,
								  static_cast<int32>(S->GetState().staff.queue.size()));
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		Out += FString::Printf(TEXT(" verified=%s zoom=%.2f centre=(%.3f, %.3f)"),
							   A->IsProvinceVerified(SelectedProvinceId) ? TEXT("true") : TEXT("false"), A->GetZoomFactor(),
							   A->GetZoomCenterUV().X, A->GetZoomCenterUV().Y);
	}
	return Out;
}

bool UPA250MapWidget::CanEnterBattleAtSelection() const
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty() || !S->IsBattlePending())
	{
		return false;
	}
	return ToFStr(S->GetState().pending_battle.province) == SelectedProvinceId;
}

FText UPA250MapWidget::BattleText() const
{
	UPA250Subsystem* S = Sub();
	if (S && S->IsBattlePending())
	{
		const auto& St = S->GetState();
		// begin_battle fills the controller cache only when the player clicks To arms.
		// The broadside is on screen while the battle is still pending, so build the
		// read-only setup here or tickets and modifier notes stay empty.
		const pa250::BattleSetup Setup =
			pa250::build_battle_setup(St, St.pending_battle.province);
		auto It = St.provinces.find(St.pending_battle.province);
		const FString Name =
			It == St.provinces.end() ? TEXT("?") : UTF8_TO_TCHAR(It->second.name.c_str());
		FString Lines = FString::Printf(TEXT("Battle of %s\n"), *Name);
		auto SideBlock = [&](const char* Faction, const TCHAR* Label) {
			auto Sit = Setup.sides.find(Faction);
			if (Sit == Setup.sides.end()) return;
			int32 Total = 0;
			FString Classes;
			for (const auto& Kv : Sit->second.tickets)
			{
				if (Kv.second <= 0) continue;
				Total += Kv.second;
				Classes += FString::Printf(TEXT("%s %d · "), *ToFStr(Kv.first), Kv.second);
			}
			Lines += FString::Printf(TEXT("%s: %d tickets (%s%d rounds a man, morale %d, ready %d)\n"), Label, Total,
									 *Classes, Sit->second.rounds_per_man, Sit->second.morale,
									 Sit->second.readiness);
		};
		SideBlock(pa250::kContinental, TEXT("Continental"));
		SideBlock(pa250::kBritish, TEXT("British"));
		if (!Setup.modifier_notes.empty())
		{
			Lines += TEXT("Modifiers:\n");
			for (const std::string& Note : Setup.modifier_notes) Lines += TEXT("  · ") + ToFStr(Note) + TEXT("\n");
		}
		else
		{
			Lines += TEXT("No building/event modifiers.\n");
		}
		return FText::FromString(Lines);
	}
	return FText::GetEmpty();
}

FText UPA250MapWidget::StaffPanelText() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->HasStaffEvent())
	{
		return FText::GetEmpty();
	}
	const FString Subject = S->GetPendingLetterSubject();
	const FString Body = S->GetPendingLetterBody();
	const FString Clerk = ToFStr(S->GetState().staff.clerk_name);
	FString Text = FString::Printf(TEXT("%s — correspondence\n\n%s\n\n%s"), *Clerk, *Subject, *Body);
	const int32 HoursLeft = S->IsPendingLetterAwaitingChoice() ? S->GetPendingLetterHoursLeft() : -1;
	if (HoursLeft >= 0)
	{
		Text += FString::Printf(TEXT("\n\nNo need to stop the clock for this. Unanswered in %d h, %s files it as set aside."),
								HoursLeft, *Clerk);
	}
	return FText::FromString(Text);
}

/** The tracker and end-screen text the Slate lambdas read. */
struct FPA250TrackerCache
{
	PA250ArcPanels::FTracker Tracker;
	PA250ArcPanels::FEnd End;
	FString Notices;
	FString EndLines;
};

void UPA250MapWidget::RefreshTrackerCache() const
{
	if (!TrackerCache.IsValid()) TrackerCache = MakeShared<FPA250TrackerCache>();
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return;
	if (S->GetHudEpoch() == TrackerEpoch && SelectedArmyId == TrackerArmy) return;
	TrackerEpoch = S->GetHudEpoch();
	TrackerArmy = SelectedArmyId;
	TrackerCache->Tracker = PA250ArcPanels::Tracker(S->GetState(), SelectedArmyId);
	TrackerCache->End = PA250ArcPanels::EndScreen(S->GetState());
	TrackerCache->Notices = FString::Join(TrackerCache->Tracker.Notices, TEXT("\n"));
	TrackerCache->EndLines = FString::Join(TrackerCache->End.Lines, TEXT("\n"));
}

void UPA250MapWidget::PressSendForGuns()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->SendForSiegeGuns(Err)) SetStatusMessage(S->GetLastResourceChangeReason());
	else SetStatusMessage(TEXT("The guns cannot be sent for: ") + Err);
}

void UPA250MapWidget::PressHaulGuns()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->AttachSiegeGuns(SelectedArmyId, Err)) SetStatusMessage(TEXT("The army hitches up the siege guns: they go where it goes."));
	else SetStatusMessage(TEXT("The guns stay: ") + Err);
}

void UPA250MapWidget::PressLeaveGuns()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->AttachSiegeGuns(FString(), Err)) SetStatusMessage(TEXT("The siege guns are left here."));
	else SetStatusMessage(Err);
}

void UPA250MapWidget::PressReenlist()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->ReenlistArmy(SelectedArmyId, Err))
	{
		SetStatusMessage(Err.IsEmpty() ? S->GetLastResourceChangeReason() : FString::Printf(TEXT("Some companies re-enlisted; the rest could not be paid (%s)."), *Err));
	}
	else
	{
		SetStatusMessage(TEXT("No company re-enlisted: ") + Err);
	}
}

void UPA250MapWidget::PressNewCampaign()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->NewCampaign(Err))
	{
		ClearSelection();
		S->SetPaused(true);
		SetStatusMessage(TEXT("A new campaign: the evening of 18 April 1775."));
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressQuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UPA250MapWidget::RefreshBattleCards()
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	// Before: built here, read-only, because begin_battle only fills the controller's copy when the
	// player clicks To arms. Rebuilt when the pending battle or anything that feeds it changes.
	const FString WantPre = S->IsBattlePending()
		? FString::Printf(TEXT("%s#%d"), *ToFStr(St.pending_battle.province), S->GetHudEpoch()) : FString();
	if (PreBattleBox.IsValid() && WantPre != PreBattleKey)
	{
		PreBattleKey = WantPre;
		if (WantPre.IsEmpty())
		{
			PreBattleBox->SetContent(SNullWidget::NullWidget);
		}
		else
		{
			TWeakObjectPtr<UPA250MapWidget> Weak(this);
			PreBattleBox->SetContent(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[PA250BattleCards::BuildPreBattle(pa250::build_battle_setup(St, St.pending_battle.province))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
					  [PA250PassPanels::BuildCardAgents(St, [Weak](const FString& Order) {
						  if (Weak.IsValid()) Weak->IssueOrder(Order);
					  })]);
		}
	}
	// After: the sim's report of the last battle.
	const pa250::BattleReport& Br = St.progress.last_battle_report;
	const FString WantAfter = Br.province_id.empty()
		? FString() : FString::Printf(TEXT("%s#%d"), *ToFStr(Br.province_id), St.progress.battles_fought);
	if (AfterBattleBox.IsValid() && WantAfter != AfterBattleKey)
	{
		AfterBattleKey = WantAfter;
		if (WantAfter.IsEmpty())
		{
			AfterBattleBox->SetContent(SNullWidget::NullWidget);
		}
		else
		{
			AfterBattleBox->SetContent(PA250BattleCards::BuildAfterBattle(Br, St));
		}
	}
}

void UPA250MapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	// An army can cease to exist between ticks (absorbed into another, or destroyed in battle).
	if (!SelectedArmyId.IsEmpty() && S->GetController().has_campaign() &&
		!S->GetState().armies.count(ToStd(SelectedArmyId)))
	{
		SelectedArmyId.Reset();
		NotifyMapSelectionChanged();
	}
	RefreshBattleCards();
	RefreshPassPanels();
	RefreshMissionTreePanel();
	RefreshReorganizePanel();
	if (bFindDismiss)
	{
		bFindDismiss = false;
		RefreshFindPanel();
	}
	if (bFindFocusPending && FindField.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(FindField, EFocusCause::SetDirectly);
		bFindFocusPending = false;
	}
	// Nobody is there to press Continue in an autoplay or probe run: it is pressed once the field is
	// ready (a probe waits long enough for its still to be taken first).
	if (bStoryUp && bStoryReady && !bStoryLeaving)
	{
		const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("PA250StoryProbe"));
		const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("PA250Autoplay"));
		if ((bAuto || bProbe) && FPlatformTime::Seconds() - StoryStartSeconds >= (bProbe ? 7.0 : 1.0))
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 story: Continue pressed by the %s"), bProbe ? TEXT("probe") : TEXT("autoplay"));
			ContinueToBattle();
		}
	}
	// Desk news (a delivery, a refusal, a new letter) goes to the message line; it never pauses.
	if (S->GetDispatchSerial() != SeenDispatchSerial)
	{
		const bool bFirstLook = SeenDispatchSerial < 0;
		SeenDispatchSerial = S->GetDispatchSerial();
		if (!bFirstLook) SetStatusMessage(S->GetLastDispatch());
	}
	const int32 Epoch = S->GetHudEpoch();
	if (Epoch == PaintedHudEpoch)
	{
		return;
	}
	PaintedHudEpoch = Epoch;
	InvalidateLayoutAndVolatility();
}

void UPA250MapWidget::NotifyMapSelectionChanged()
{
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		A->OnHudSelectionChanged(SelectedArmyId, SelectedProvinceId);
	}
}

void UPA250MapWidget::ClearSelection()
{
	SelectedArmyId.Reset();
	SelectedProvinceId.Reset();
	if (CityView) CityView->CloseCityView();
	LastMessage = TEXT("Orders cleared.");
	NotifyMapSelectionChanged();
}

void UPA250MapWidget::SetStatusMessage(const FString& Msg)
{
	LastMessage = Msg;
	Message = Msg;
	InvalidateLayoutAndVolatility();
}

FString UPA250MapWidget::ClickProvince(const FString& ProvinceId)
{
	UPA250Subsystem* S = Sub();
	if (!S || ProvinceId.IsEmpty())
	{
		return FString();
	}
	const pa250::CampaignState& St = S->GetState();
	const std::string Pid = ToStd(ProvinceId);
	if (!St.provinces.count(Pid))
	{
		return FString();
	}
	if (S->GetFlow() != EPA250Flow::Campaign || S->IsBattlePending())
	{
		LastMessage = TEXT("Resolve the pending battle first.");
		return LastMessage;
	}

	FString OwnArmy;
	for (const auto& KV : St.armies)
	{
		if (KV.second.province == Pid && KV.second.faction == St.player_faction)
		{
			OwnArmy = ToFStr(KV.first);
			break;
		}
	}

	if (!OwnArmy.IsEmpty())
	{
		SelectedArmyId = OwnArmy;
		SelectedProvinceId = ProvinceId;
		const pa250::Army& Own = St.armies.at(ToStd(OwnArmy));
		LastMessage = TEXT("Selected ") + ToFStr(Own.name) + TEXT(" at ") + ToFStr(St.provinces.at(Own.province).name) +
					  TEXT(". Right-click a town to march.");
	}
	else
	{
		SelectedProvinceId = ProvinceId;
		const auto Sel = SelectedArmyId.IsEmpty() ? St.armies.end() : St.armies.find(ToStd(SelectedArmyId));
		if (Sel != St.armies.end())
		{
			LastMessage = ToFStr(St.provinces.at(Pid).name) + TEXT(" (") + ToFStr(St.provinces.at(Pid).owner) +
						  TEXT("). Right-click to march ") + ToFStr(Sel->second.name) + TEXT(" here.");
		}
		else
		{
			SelectedArmyId.Reset();
			LastMessage = ToFStr(St.provinces.at(Pid).name) + TEXT(" (") + ToFStr(St.provinces.at(Pid).owner) + TEXT(")");
		}
	}
	NotifyMapSelectionChanged();
	return LastMessage;
}

FString UPA250MapWidget::OrderProvince(const FString& ProvinceId)
{
	UPA250Subsystem* S = Sub();
	if (!S || ProvinceId.IsEmpty() || SelectedArmyId.IsEmpty())
	{
		return FString();
	}
	const pa250::CampaignState& St = S->GetState();
	const std::string Pid = ToStd(ProvinceId);
	if (!St.provinces.count(Pid) || !St.armies.count(ToStd(SelectedArmyId)))
	{
		return FString();
	}
	if (S->GetFlow() != EPA250Flow::Campaign || S->IsBattlePending())
	{
		LastMessage = TEXT("Resolve the pending battle first.");
		return LastMessage;
	}
	const pa250::Army& A = St.armies.at(ToStd(SelectedArmyId));
	const FString ArmyName = ToFStr(A.name);
	const FString HereName = ToFStr(St.provinces.at(A.province).name);
	if (Pid == A.province)
	{
		LastMessage = ArmyName + TEXT(" is already at ") + HereName + TEXT(".");
		NotifyMapSelectionChanged();
		return LastMessage;
	}
	const FString DestName = ToFStr(St.provinces.at(Pid).name);
	FString Err;
	if (S->QueueMove(SelectedArmyId, ProvinceId, Err))
	{
		// QueueMove replaces the campaign state; plan against the state it left behind.
		const pa250::CampaignState& Now = S->GetState();
		const pa250::PathPlan Plan = pa250::plan_path(Now, ToStd(SelectedArmyId), Pid);
		const FString When = Plan.ok ? S->FormatWhen(Plan.arrival.hour_index) : FString();
		FString Route;
		for (int32 i = 0; i < Plan.provinces.size(); ++i)
		{
			Route += (i ? TEXT(" → ") : TEXT("")) + ProvName(Now, Plan.provinces[i]);
		}
		LastMessage = ArmyName + TEXT(" → ") + DestName +
					  (When.IsEmpty() ? TEXT(".") : TEXT(". Arrives ") + When + TEXT(".")) +
					  (Route.IsEmpty() ? FString() : TEXT(" Route: ") + Route);
		// Marching on a fort it cannot besiege (no general): say so now, not after the battle.
		const std::string Why = pa250::why_cannot_besiege(Now, ToStd(SelectedArmyId), Pid);
		if (!Why.empty()) LastMessage += TEXT("  WARNING: ") + ToFStr(Why) + TEXT(" ") + ToFStr(pa250::how_to_form_army(Now, Now.player_faction));
		SelectedProvinceId = ProvinceId;
	}
	else
	{
		LastMessage = TEXT("Illegal move: ") + Err;
	}
	NotifyMapSelectionChanged();
	return LastMessage;
}

FString UPA250MapWidget::HandleTownOrdered(const FString& ProvinceId)
{
	if (SelectedArmyId.IsEmpty() || ProvinceId.IsEmpty())
	{
		ClearSelection();
		return LastMessage;
	}
	return OrderProvince(ProvinceId);
}

FReply UPA250MapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	const FKey Arrows[] = {EKeys::Left, EKeys::Right, EKeys::Up, EKeys::Down};
	for (int32 i = 0; i < 4; ++i)
	{
		if (Key == Arrows[i])
		{
			HeldArrows |= static_cast<uint8>(1 << i);
			return FReply::Handled();
		}
	}
	if (Key == EKeys::SpaceBar)
	{
		S->SetPaused(!S->IsPaused());
		return FReply::Handled();
	}
	const FKey SpeedKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven };
	for (int32 i = 0; i < UE_ARRAY_COUNT(SpeedKeys) && i < FPA250CampaignSpeed::Count; ++i)
	{
		if (Key == SpeedKeys[i])
		{
			S->SetSpeedIndex(i);
			return FReply::Handled();
		}
	}
	if (Key == EKeys::Equals || Key == EKeys::Add)
	{
		S->SetSpeedIndex(S->GetSpeedIndex() + 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
	{
		S->SetSpeedIndex(S->GetSpeedIndex() - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::H)
	{
		PressHalt();
		return FReply::Handled();
	}
	if (Key == EKeys::F)
	{
		PressFindToggle();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		// Layered: close transient panels first, then clear selection. Pause is
		// untouched (Space owns it), so Escape never changes the clock.
		if (IsCityViewOpen())
		{
			CloseCityView();
			return FReply::Handled();
		}
		if (bShowFind)
		{
			bShowFind = false;
			FindQuery.Reset();
			RefreshFindPanel();
			return FReply::Handled();
		}
		if (bShowMissionTree)
		{
			bShowMissionTree = false;
			MissionTreeKey.Reset();
			RefreshMissionTreePanel();
			return FReply::Handled();
		}
		if (bShowReorganize)
		{
			bShowReorganize = false;
			ReorganizeKey.Reset();
			RefreshReorganizePanel();
			return FReply::Handled();
		}
		if (bShowPeople || bShowFloor)
		{
			bShowPeople = bShowFloor = false;
			return FReply::Handled();
		}
		if (bShowAbout)
		{
			bShowAbout = false;
			return FReply::Handled();
		}
		if (bShowTreasury)
		{
			bShowTreasury = false;
			return FReply::Handled();
		}
		if (bShowLedger)
		{
			bShowLedger = false;
			return FReply::Handled();
		}
		ClearSelection();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UPA250MapWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const FKey Arrows[] = {EKeys::Left, EKeys::Right, EKeys::Up, EKeys::Down};
	for (int32 i = 0; i < 4; ++i)
	{
		if (Key == Arrows[i])
		{
			HeldArrows &= static_cast<uint8>(~(1 << i));
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void UPA250MapWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
	HeldArrows = 0; // a key released while unfocused must not leave the map scrolling
	Super::NativeOnFocusLost(InFocusEvent);
}

FSlateRect UPA250MapWidget::GetTopStripScreenRect() const
{
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return FSlateRect(0.f, 0.f, Size.X, TopStripHeightPx);
}

FSlateRect UPA250MapWidget::GetBottomStripScreenRect() const
{
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return FSlateRect(0.f, Size.Y - BottomStripHeightPx, Size.X, Size.Y);
}

TSharedRef<SWidget> UPA250MapWidget::RebuildWidget()
{
	if (UPA250Subsystem* S = Sub())
	{
		S->EnsureCampaign();
	}

	const FSlateFontInfo HeaderFont = FPA250HudStyle::Font(TEXT("Bold"), 15);
	const FSlateFontInfo BodyFont = FPA250HudStyle::Font(TEXT("Regular"), 13);
	const FSlateFontInfo BroadsideFont = FPA250HudStyle::Font(TEXT("Bold"), 26);

	const FPA250HudStyle& Hud = FPA250HudStyle::Get();
	auto MakeButton = [this, &BodyFont, &Hud](const TCHAR* Label, TFunction<void()> Fn,
	                                    TAttribute<EVisibility> Vis = EVisibility::Visible,
	                                    TAttribute<FText> Tip = TAttribute<FText>()) {
		return SNew(SButton)
			.ButtonStyle(&Hud.Button)
			.Visibility(Vis)
			.IsFocusable(false)
			.ToolTipText(Tip)
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([Fn]() {
				Fn();
				return FReply::Handled();
			})
			.ContentPadding(FMargin(11.f, 4.f))
				[SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont).ColorAndOpacity(FPA250HudStyle::Cream)];
	};
	TAttribute<EVisibility> BattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsBattlePending()) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> NoBattleVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsBattlePending()) ? EVisibility::Collapsed : EVisibility::Visible;
	});
	TAttribute<EVisibility> StaffVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && StaffPanelVisible(S2->IsStaffPanelOpen(), S2->IsBattlePending(), S2->HasUnseenBattleReport() && !BattleReportText().IsEmpty()))
			? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> AftermathChoicesVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || S2->IsBattlePending() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		const FString Kind = S2->GetPendingLetterKind();
		return (Kind == TEXT("request_assistance") || Kind == TEXT("purchase") || Kind.IsEmpty())
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	// Commander orders belong only to the detached commander's own letter; a chapter
	// briefing shares the commander_report kind but is only ever acknowledged.
	auto IsDetachedCommanderLetter = [](UPA250Subsystem* S2) {
		return S2->GetPendingLetterKind() == TEXT("commander_report") &&
			   S2->GetPendingLetterSubject().Contains(TEXT("Detached"));
	};
	TAttribute<EVisibility> CommanderChoicesVis = TAttribute<EVisibility>::CreateLambda([this, IsDetachedCommanderLetter]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		return IsDetachedCommanderLetter(S2) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> ChapterChoicesVis = TAttribute<EVisibility>::CreateLambda([this, IsDetachedCommanderLetter]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		return (S2->GetPendingLetterKind() == TEXT("commander_report") && !IsDetachedCommanderLetter(S2))
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> KnoxChoicesVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		if (!S2 || !S2->HasStaffEvent() || !S2->IsPendingLetterAwaitingChoice())
		{
			return EVisibility::Collapsed;
		}
		return S2->GetPendingLetterKind() == TEXT("knox_support") ? EVisibility::Visible : EVisibility::Collapsed;
	});
	// Delivered / refused letters are notices: one button that clears the popup.
	TAttribute<EVisibility> NoticeVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->HasStaffEvent() && !S2->IsBattlePending() && !S2->IsPendingLetterAwaitingChoice())
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	auto SourceKindAwaiting = [](UPA250Subsystem* S2, const TCHAR* Kind) {
		return S2 && S2->HasStaffEvent() && !S2->IsBattlePending() && S2->IsPendingLetterAwaitingChoice() &&
			   S2->GetPendingLetterKind() == Kind;
	};
	// A source letter's choices come from the catalog: one button per choice, the effect as its
	// tooltip, and "Set aside" for no commitment. Nothing here is hard-wired to a letter.
	TSharedRef<SVerticalBox> SourceChoices = SNew(SVerticalBox).Visibility(TAttribute<EVisibility>::CreateLambda(
		[this, SourceKindAwaiting]() {
			return SourceKindAwaiting(Sub(), TEXT("source_decision")) ? EVisibility::Visible : EVisibility::Collapsed;
		}));
	for (int32 ChoiceIdx = 0; ChoiceIdx < 4; ++ChoiceIdx)
	{
		SourceChoices->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
			[SNew(SButton)
				 .ButtonStyle(&Hud.Button)
				 .IsFocusable(false)
				 .HAlign(HAlign_Left)
				 .ContentPadding(FMargin(11.f, 4.f))
				 .Visibility_Lambda([this, ChoiceIdx]() {
					 UPA250Subsystem* S2 = Sub();
					 return (S2 && ChoiceIdx < S2->GetPendingLetterChoiceCount()) ? EVisibility::Visible : EVisibility::Collapsed;
				 })
				 .ToolTipText_Lambda([this, ChoiceIdx]() {
					 UPA250Subsystem* S2 = Sub();
					 return FText::FromString(S2 ? S2->GetPendingLetterChoiceSummary(ChoiceIdx) : FString());
				 })
				 .OnClicked_Lambda([this, ChoiceIdx]() {
					 if (UPA250Subsystem* S2 = Sub())
					 {
						 const FString ChoiceId = S2->GetPendingLetterChoiceId(ChoiceIdx);
						 if (!ChoiceId.IsEmpty()) PressLogistics(ChoiceId);
					 }
					 return FReply::Handled();
				 })
					 [SNew(SVerticalBox) +
					  SVerticalBox::Slot().AutoHeight()
						  [SNew(STextBlock)
							   .Font(BodyFont)
							   .ColorAndOpacity(FPA250HudStyle::Cream)
							   .AutoWrapText(true)
							   .Text_Lambda([this, ChoiceIdx]() {
								   UPA250Subsystem* S2 = Sub();
								   return FText::FromString(S2 ? S2->GetPendingLetterChoiceLabel(ChoiceIdx) : FString());
							   })] +
					  // What the choice costs and brings, on the button itself.
					  SVerticalBox::Slot().AutoHeight()
						  [SNew(STextBlock)
							   .Font(FPA250HudStyle::Font(TEXT("Regular"), 10))
							   .ColorAndOpacity(FPA250HudStyle::Brass)
							   .AutoWrapText(true)
							   .Text_Lambda([this, ChoiceIdx]() {
								   UPA250Subsystem* S2 = Sub();
								   FString Summary = S2 ? S2->GetPendingLetterChoiceSummary(ChoiceIdx) : FString();
								   Summary.RemoveFromStart(TEXT("Gameplay tuning: "));
								   return FText::FromString(Summary);
							   })]]];
	}
	SourceChoices->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[MakeButton(TEXT("Set aside"), [this]() { PressLogistics(TEXT("dismiss")); })];
	// "Later" puts a desk letter back without answering it; the clock never stopped for it.
	TAttribute<EVisibility> LaterVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->IsStaffPanelOpen() && S2->IsPendingLetterAwaitingChoice() && !S2->IsPendingLetterTimely())
				   ? EVisibility::Visible
				   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> DeskChipVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->HasDeskLetter()) ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> ViewSourceVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (S2 && S2->HasStaffEvent() && !S2->GetPendingLetterSourceUrl().IsEmpty()) ? EVisibility::Visible
																						: EVisibility::Collapsed;
	});

	// The detached commander's letter: each answer with what it costs and gains printed on it, as the
	// source letters do (the words are the sim's: catalog "commander_letter").
	const std::vector<pa250::CommanderLetterChoice> CommanderChoices = pa250::commander_letter_choices();
	auto CommanderChoice = [this, &Hud, CommanderChoices](int32 Index) -> TSharedRef<SWidget> {
		static const TCHAR* Fallback[] = {TEXT("preserve_force"), TEXT("contest_crossing"), TEXT("escalate")};
		const bool bData = Index < static_cast<int32>(CommanderChoices.size());
		const FString Action = bData ? ToFStr(CommanderChoices[Index].id) : FString(Fallback[Index]);
		const FString Label = bData ? ToFStr(CommanderChoices[Index].label) : Action.Replace(TEXT("_"), TEXT(" "));
		const FString Summary = bData ? ToFStr(CommanderChoices[Index].effects_summary) : FString();
		return SNew(SBox).WidthOverride(420.f)
			[SNew(SButton)
				 .ButtonStyle(&Hud.Button)
				 .IsFocusable(false)
				 .ContentPadding(FMargin(10.f, 5.f))
				 .OnClicked_Lambda([this, Action]() {
					 PressCommander(Action);
					 return FReply::Handled();
				 })
					 [SNew(SVerticalBox) +
					  SVerticalBox::Slot().AutoHeight()
						  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 12)).ColorAndOpacity(FPA250HudStyle::Cream).Text(FText::FromString(Label))] +
					  SVerticalBox::Slot().AutoHeight()
						  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Regular"), 10)).ColorAndOpacity(FPA250HudStyle::Brass).AutoWrapText(true).Text(FText::FromString(Summary))]]];
	};
	TAttribute<EVisibility> ForcesVis = TAttribute<EVisibility>::CreateLambda([this]() {
		// An open letter and battles take the screen; the forces list returns after.
		UPA250Subsystem* S2 = Sub();
		if (S2 && (S2->IsBattlePending() || S2->IsStaffPanelOpen())) return EVisibility::Collapsed;
		return EVisibility::Visible;
	});
	TAttribute<EVisibility> TownsVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		if (bShowPeople || bShowFloor || (S2 && (S2->IsBattlePending() || S2->IsStaffPanelOpen()))) return EVisibility::Collapsed;
		return EVisibility::Visible;
	});
	TAttribute<EVisibility> ArmyBannerVis = TAttribute<EVisibility>::CreateLambda([this]() {
		UPA250Subsystem* S2 = Sub();
		return (!SelectedArmyId.IsEmpty() && S2 && !S2->IsBattlePending()) ? EVisibility::Visible
																		   : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> AboutVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowAbout ? EVisibility::Visible : EVisibility::Collapsed;
	});
	TAttribute<EVisibility> LedgerVis = TAttribute<EVisibility>::CreateLambda([this]() {
		return bShowLedger ? EVisibility::Visible : EVisibility::Collapsed;
	});

	const FLinearColor Cream = FPA250HudStyle::Cream;
	const FLinearColor Gold = FPA250HudStyle::Brass;
	// A value in the top strip sits in its own inset box, as on a ledger rule.
	auto MakeGroup = [&BodyFont, &Hud, Cream](TFunction<FText()> Fn, const TCHAR* Tip) {
		return SNew(SBorder)
			.BorderImage(&Hud.Chip)
			.Padding(FMargin(9.f, 2.f))
			.ToolTipText(FText::FromString(Tip))
			[SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda(MoveTemp(Fn))];
	};
	// A piece of the top bar, remembered by name so a probe can measure it (TopBarReport).
	BarPieces.Reset();
	auto Piece = [this](const TCHAR* Name, TSharedRef<SWidget> W) {
		BarPieces.Emplace(FString(Name), W);
		return W;
	};
	auto SectionHeader = [&Hud](const TCHAR* Label) {
		return SNew(SBorder)
			.BorderImage(&Hud.Header)
			.Padding(FMargin(9.f, 1.f))
			[SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 12)).ColorAndOpacity(FPA250HudStyle::Brass).Text(FText::FromString(Label))];
	};

	// One row per army the player has (companies raised in towns appear here as they form).
	TSharedRef<SVerticalBox> ForcesList = SNew(SVerticalBox);
	for (int32 Row = 0; Row < MaxForceRows; ++Row)
	{
		ForcesList->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
			[SNew(SButton)
				 .ButtonStyle(&Hud.Button)
				 .IsFocusable(false)
				 .ContentPadding(FMargin(9.f, 5.f))
				 .Visibility_Lambda([this, Row]() {
					 FString Id;
					 return GetPlayerArmyId(Row, Id) ? EVisibility::Visible : EVisibility::Collapsed;
				 })
				 .OnClicked_Lambda([this, Row]() {
					 FString Id;
					 if (GetPlayerArmyId(Row, Id)) PressSelectArmy(Id);
					 return FReply::Handled();
				 })
				 [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(FPA250HudStyle::Cream).Text_Lambda([this, Row]() {
					 return ArmyButtonText(Row);
				 })]];
	}

	// Towns the player holds, with what is under way in each: the quick way back to a town.
	TSharedRef<SVerticalBox> TownsList = SNew(SVerticalBox);
	if (UPA250Subsystem* S3 = Sub(); S3 && S3->GetController().has_campaign())
	{
		TArray<TPair<FString, FString>> Towns; // name, id — by name, so the list reads alphabetically
		for (const auto& Kv : S3->GetState().provinces) Towns.Emplace(ToFStr(Kv.second.name), ToFStr(Kv.first));
		Towns.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });
		for (const TPair<FString, FString>& Town : Towns)
		{
			const FString Id = Town.Value;
			TownsList->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
				[SNew(SButton)
					 .ButtonStyle(&Hud.Button)
					 .IsFocusable(false)
					 .ContentPadding(FMargin(9.f, 3.f))
					 .ToolTipText(FText::FromString(TEXT("Open this town.")))
					 .Visibility_Lambda([this, Id]() { return IsTownHeld(Id) ? EVisibility::Visible : EVisibility::Collapsed; })
					 .OnClicked_Lambda([this, Id]() {
						 SelectedArmyId.Reset();
						 OpenCityView(Id);
						 if (APA250ProvinceMapActor* A = MapActor.Get()) A->OnHudSelectionChanged(SelectedArmyId, SelectedProvinceId);
						 return FReply::Handled();
					 })
					 [SNew(SHorizontalBox) +
					  SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						  [SNew(STextBlock).Font(BodyFont).Text(FText::FromString(Town.Key)).ColorAndOpacity_Lambda([this, Id]() {
							  return FSlateColor(SelectedProvinceId == Id ? FPA250HudStyle::Brass : FPA250HudStyle::Cream);
						  })] +
					  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Regular"), 11)).ColorAndOpacity(FPA250HudStyle::CreamDim).Text_Lambda([this, Id]() {
							  return TownRowStatus(Id);
						  })]]];
		}
	}

	// Date plaque: the date, one pip per clock speed (lit up to the current one), and the state.
	TSharedRef<SHorizontalBox> SpeedPips = SNew(SHorizontalBox);
	for (int32 i = 0; i < FPA250CampaignSpeed::Count; ++i)
	{
		SpeedPips->AddSlot().AutoWidth().Padding(1.5f, 0.f).VAlign(VAlign_Center)
			[SNew(SBox).WidthOverride(6.f).HeightOverride(15.f)
			 [SNew(SImage).Image(&Hud.White).ColorAndOpacity_Lambda([this, i]() {
				 UPA250Subsystem* S2 = Sub();
				 const bool bLit = S2 && i <= S2->GetSpeedIndex();
				 return FSlateColor(!bLit ? FLinearColor(0.f, 0.f, 0.f, 0.55f)
										  : S2->IsPaused() ? FPA250HudStyle::BrassDim : FPA250HudStyle::Brass);
			 })]];
	}
	TSharedRef<SWidget> DatePlaque =
		SNew(SBorder)
			.BorderImage(&Hud.Header)
			.Padding(FMargin(12.f, 2.f))
			.ToolTipText(FText::FromString(TEXT("Campaign date, clock speed and pause state. Space pauses; 1–7 set speed.")))
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				 [SNew(STextBlock).Font(HeaderFont).ColorAndOpacity(Cream).Text_Lambda([this]() {
					 UPA250Subsystem* S2 = Sub();
					 return FText::FromString(S2 && S2->GetController().has_campaign() ? S2->FormatDate() : FString(TEXT("No campaign")));
				 })] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[SpeedPips] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				 [SNew(SBox).MinDesiredWidth(64.f)
				  [SNew(STextBlock)
					   .Font(FPA250HudStyle::Font(TEXT("Bold"), 12))
					   .ColorAndOpacity_Lambda([this]() {
						   UPA250Subsystem* S2 = Sub();
						   return FSlateColor(S2 && S2->IsPaused() ? FPA250HudStyle::Red : FPA250HudStyle::Cream);
					   })
					   .Text_Lambda([this]() {
						   UPA250Subsystem* S2 = Sub();
						   if (!S2) return FText::GetEmpty();
						   return FText::FromString(S2->IsPaused() ? FString(TEXT("PAUSED")) : FPA250CampaignSpeed::Format(S2->GetHoursPerSecond()));
					   })]]];

	// Soft dark edge over the map, under every panel: the sheet reads as lit from above on a desk.
	if (!VignetteTexture)
	{
		const int32 N = 256;
		VignetteTexture = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (VignetteTexture)
		{
			VignetteTexture->SRGB = true;
			VignetteTexture->Filter = TF_Bilinear;
			VignetteTexture->AddressX = TA_Clamp;
			VignetteTexture->AddressY = TA_Clamp;
			FTexture2DMipMap& Mip = VignetteTexture->GetPlatformData()->Mips[0];
			FColor* Px = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const float DX = (X + 0.5f) / N * 2.f - 1.f;
					const float DY = (Y + 0.5f) / N * 2.f - 1.f;
					const float A = FMath::SmoothStep(0.62f, 1.38f, FMath::Sqrt(DX * DX + DY * DY)) * 0.62f;
					Px[Y * N + X] = FColor(14, 9, 5, static_cast<uint8>(A * 255.f));
				}
			}
			Mip.BulkData.Unlock();
			VignetteTexture->UpdateResource();
			VignetteBrush.SetResourceObject(VignetteTexture);
			VignetteBrush.ImageSize = FVector2D(N, N);
			VignetteBrush.DrawAs = ESlateBrushDrawType::Image;
		}
	}
	auto PanelText = [&BodyFont, Cream](TFunction<FText()> Fn) {
		return SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).AutoWrapText(true).Text_Lambda(MoveTemp(Fn));
	};

	// Transparent centre — the 3D province map is the only map layer.
	return SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SImage).Image(&VignetteBrush).Visibility(EVisibility::HitTestInvisible)]
		+ SOverlay::Slot().VAlign(VAlign_Top).HAlign(HAlign_Fill)
			  [SNew(SVerticalBox) +
			   SVerticalBox::Slot().AutoHeight()
				   [SNew(SBorder)
						.BorderImage(&Hud.Bar)
						.Padding(FMargin(12.f, 6.f))
							[SNew(SHorizontalBox) +
							 // The title plaque gives way first: at the usual play size the row is full without it.
							 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								 [Piece(TEXT("title"), SNew(SBox)
									  .Padding(FMargin(0.f, 0.f, 12.f, 0.f))
									  .Visibility_Lambda([this]() { return PA250PassPanels::TopBarShowsTitle(HudWidthUnits()) ? EVisibility::Visible : EVisibility::Collapsed; })
									  [SNew(SBorder).BorderImage(&Hud.Header).Padding(FMargin(12.f, 2.f))
									   [SNew(STextBlock).Font(HeaderFont).ColorAndOpacity(Gold).Text(FText::FromString(TEXT("PLAY AMERICA 250")))]])] +
							 SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
								 [Piece(TEXT("treasury"), SNew(SButton)
									  .ButtonStyle(&Hud.Button)
									  .IsFocusable(false)
									  .ContentPadding(FMargin(10.f, 2.f))
									  .ToolTipText(FText::FromString(TEXT("The treasury: balance, monthly income and expenses, and borrowing. Click to open.")))
									  .OnClicked_Lambda([this]() {
										  PressTreasuryToggle();
										  return FReply::Handled();
									  })
									  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda([this]() { return TreasuryText(); })])] +
							 SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
								 [Piece(TEXT("stores"), MakeGroup([this]() { return StoresText(); }, TEXT("Powder, cartridges, muskets and provisions in the towns you hold, and shipments on the road when there are any.")))] +
							 // Support takes what is left of the row; if a treaty makes it longer than that, it ends in an ellipsis and the tooltip has the whole line.
							 SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Center)
								 [Piece(TEXT("support"), SNew(SBorder)
									  .BorderImage(&Hud.Chip)
									  .Padding(FMargin(9.f, 2.f))
									  .ToolTipText_Lambda([this]() {
										  return FText::FromString(SupportText().ToString() + TEXT("\nLocal support, the British envoy's attitude, and the treaties in force."));
									  })
									  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text_Lambda([this]() { return SupportText(); })])] +
							 // Campaign pass, in the bar itself (user decision 2026-11-15): the body the player answers to, with Unity and favors, and People.
							 SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 6, 0).VAlign(VAlign_Center)
								 [Piece(TEXT("floor"), SNew(SButton)
									  .ButtonStyle(&Hud.Button)
									  .IsFocusable(false)
									  .ContentPadding(FMargin(11.f, 3.f))
									  .ToolTipText(FText::FromString(TEXT("The body you answer to: its blocs, the measures before it, and what you can do about them. Unity is how close the three blocs of Congress stand: under its floor the troops of other colonies begin to go home. Confidence is the ministry's in its commander. Favors are spent on the floor.")))
									  .Visibility_Lambda([this]() {
										  UPA250Subsystem* S2 = Sub();
										  return S2 && S2->GetController().has_campaign() && !S2->IsBattlePending() && !S2->IsCampaignOver() &&
														 !PA250PassPanels::FloorButton(S2->GetState()).IsEmpty()
													 ? EVisibility::Visible
													 : EVisibility::Collapsed;
									  })
									  .OnClicked_Lambda([this]() {
										  PressFloorToggle();
										  return FReply::Handled();
									  })
									  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(FPA250HudStyle::Cream).Text_Lambda([this]() {
										  UPA250Subsystem* S2 = Sub();
										  if (!S2 || !S2->GetController().has_campaign()) return FText::GetEmpty();
										  return FText::FromString(PA250PassPanels::FloorChip(S2->GetState()));
									  })])] +
							 SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
								 [Piece(TEXT("people"), MakeButton(TEXT("People"), [this]() { PressPeopleToggle(); },
																  TAttribute<EVisibility>::CreateLambda([this]() {
																	  UPA250Subsystem* S2 = Sub();
																	  return S2 && S2->GetController().has_campaign() && !S2->IsBattlePending() && !S2->IsCampaignOver() ? EVisibility::Visible
																																										  : EVisibility::Collapsed;
																  }),
																  FText::FromString(TEXT("Your generals, delegates, organizers and agents, and what each can do."))))] +
							 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Piece(TEXT("date"), DatePlaque)]]] +
			   SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(2.f)[SNew(SImage).Image(&Hud.Rule)]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 0.f, 48.f, 0.f))
				  [SNew(SBorder)
					   .Visibility(BattleVis)
					   .BorderImage(&Hud.Parchment)
					   .Padding(FMargin(28.f, 18.f))
						   [SNew(SVerticalBox) +
							// The pre-battle card: both sides' numbers, each with what produced it.
							SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
								[SAssignNew(PreBattleBox, SBox)] +
							SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
								[MakeButton(TEXT("To arms!"), [this]() { PressFight(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 84.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(350.f)
					   [SNew(SBorder)
							.Visibility(ForcesVis)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(10.f, 8.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SectionHeader(TEXT("YOUR FORCES"))] +
								 SVerticalBox::Slot().AutoHeight()[ForcesList]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 84.f, 16.f, 0.f))
				  [SNew(SBox).WidthOverride(TownsPanelWidth)
					   [SNew(SBorder)
							.Visibility(TownsVis)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(10.f, 8.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("YOUR TOWNS"))] +
								 SVerticalBox::Slot().AutoHeight()[TownsList]]]]
		// Under the bar's right end, below the date: how the war stands (towns, score, days left) and
		// the Ledger. They gave their place in the bar to Congress and People (user decision 2026-11-15).
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 49.f, 16.f, 0.f))
				  [SNew(SHorizontalBox) +
				   SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0).VAlign(VAlign_Center)
					   [Piece(TEXT("score"), SNew(SBox)
							.Visibility_Lambda([this]() { return ScoreboardText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
							[MakeGroup([this]() { return ScoreboardText(); },
									   TEXT("Provinces you hold vs the Crown, then the end-of-campaign window score (each held town plus Support; the Crown's score uses 100 minus Support). When the window closes the higher score wins, unless Boston, headquarters, support or vassalage already decided the war."))])] +
				   SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					   [Piece(TEXT("ledger"), MakeButton(TEXT("Ledger"), [this]() { PressLedgerToggle(); }))]]
		// The people panel and the floor, docked right under that strip. The floor narrows where it
		// would run under the open mission tracker (a 16:10 window).
		+ SOverlay::Slot()
			  .VAlign(VAlign_Fill)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 88.f, 16.f, BottomStripHeightPx + 64.f))
				  [SAssignNew(PeopleBox, SBox).WidthOverride(580.f).Visibility_Lambda([this]() {
					  UPA250Subsystem* S2 = Sub();
					  return bShowPeople && S2 && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
				  })]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Fill)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 88.f, 16.f, BottomStripHeightPx + 64.f))
				  [SAssignNew(FloorBox, SBox)
					   .WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() {
						   return FOptionalSize(PA250PassPanels::FloorPanelWidth(HudWidthUnits(), IsTrackerShown() && !bTrackerMinimized));
					   }))
					   .Visibility_Lambda([this]() {
					  UPA250Subsystem* S2 = Sub();
					  return bShowFloor && S2 && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
				  })]
		// The selected army's panel: bottom left, clear of the town panel, the mission tracker (top
		// centre, which used to cover it) and the toolbar.
		+ SOverlay::Slot()
			  .VAlign(VAlign_Bottom)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(ArmyPanelLeft, 0.f, 0.f, BottomStripHeightPx + 8.f))
				  [SAssignNew(ArmyPanelBox, SBox).WidthOverride(ArmyPanelWidth).Visibility(ArmyBannerVis)
					   [SNew(SBorder)
							.BorderImage(&Hud.Banner)
							.Padding(FMargin(14.f, 8.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Bold"), 13))
										  .ColorAndOpacity(FLinearColor(1.f, 0.95f, 0.80f))
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return SelectedArmyBannerText(); })] +
								 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 6.f, 0.f, 0.f)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
										  [MakeButton(TEXT("Halt  (H)"), [this]() { PressHalt(); },
													  TAttribute<EVisibility>::CreateLambda([this]() { return CanHaltSelected() ? EVisibility::Visible : EVisibility::Collapsed; }),
													  FText::FromString(TEXT("Halt this army. On the road it turns about and marches back to the town it left.")))] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Reorganize"), [this]() { PressReorganizeToggle(); },
													  TAttribute<EVisibility>::CreateLambda([this]() {
														  return CanReorganizeSelected() ? EVisibility::Visible : EVisibility::Collapsed;
													  }),
													  FText::FromString(TEXT("Move companies between your armies idle in this town.")))]]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 88.f, 0.f, 0.f))
				  [SAssignNew(MissionTreeBox, SBox)
					   .WidthOverride(420.f)
					   .MaxDesiredHeight(520.f)
					   .Visibility_Lambda([this]() {
						   UPA250Subsystem* S2 = Sub();
						   return bShowMissionTree && S2 && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
					   })]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Center)
				  [SAssignNew(ReorganizeBox, SBox)
					   .WidthOverride(640.f)
					   .Visibility_Lambda([this]() {
						   UPA250Subsystem* S2 = Sub();
						   return bShowReorganize && S2 && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
					   })]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Center)
			  .Padding(FMargin(0.f, 120.f, 0.f, 0.f))
				  [SAssignNew(FindBox, SBox).WidthOverride(420.f)]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Center)
			  .Padding(FMargin(0.f, 84.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(TreasuryPanelWidth).Visibility_Lambda([this]() {
					  UPA250Subsystem* S2 = Sub();
					  return bShowTreasury && S2 && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
				  })
					   [SNew(SBorder)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(14.f, 10.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("THE PROVINCE TREASURY"))] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(1.f)
										  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda([this]() {
											  RefreshTreasuryCache();
											  return FText::FromString(TreasuryLabels);
										  })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Gold).Justification(ETextJustify::Right).Text_Lambda([this]() {
											  RefreshTreasuryCache();
											  return FText::FromString(TreasuryAmounts);
										  })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Italic"), 11))
										  .ColorAndOpacity(FPA250HudStyle::CreamDim)
										  .WrapTextAt(TreasuryPanelWidth - 30.f)
										  .Text_Lambda([this]() {
											  RefreshTreasuryCache();
											  return FText::FromString(TreasuryNote);
										  })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 6, 0)
										  [SNew(SButton)
											   .ButtonStyle(&Hud.Button)
											   .IsFocusable(false)
											   .HAlign(HAlign_Center)
											   .ContentPadding(FMargin(8.f, 4.f))
											   .IsEnabled_Lambda([this]() { RefreshTreasuryCache(); return bCanBorrow; })
											   .ToolTipText_Lambda([this]() {
												   return FText::FromString(FString::Printf(
													   TEXT("Issue bills of credit: gold now, %d%% interest every month until redeemed."), LoanInterestPct));
											   })
											   .OnClicked_Lambda([this]() { PressBorrow(); return FReply::Handled(); })
											   [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text_Lambda([this]() {
												   return FText::FromString(FString::Printf(TEXT("Borrow %d gold"), LoanStep));
											   })]] +
									  SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 6, 0)
										  [SNew(SButton)
											   .ButtonStyle(&Hud.Button)
											   .IsFocusable(false)
											   .HAlign(HAlign_Center)
											   .ContentPadding(FMargin(8.f, 4.f))
											   .IsEnabled_Lambda([this]() { RefreshTreasuryCache(); return bCanRepay; })
											   .ToolTipText(FText::FromString(TEXT("Redeem bills of credit and stop paying interest on them.")))
											   .OnClicked_Lambda([this]() { PressRepay(); return FReply::Handled(); })
											   [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).Text(FText::FromString(TEXT("Repay")))]] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Close"), [this]() { PressTreasuryToggle(); })]]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(16.f, 196.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(350.f)
					   [SNew(SBorder)
							.Visibility(LedgerVis)
							.BorderImage(&Hud.Panel)
							.Padding(FMargin(12.f, 10.f))
								[SNew(STextBlock)
									 .Font(FPA250HudStyle::Font(TEXT("Regular"), 12))
									 .ColorAndOpacity(Cream)
									 .Text_Lambda([this]() { return LedgerText(); })]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(36.f, 0.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(460.f)
					   [SNew(SVerticalBox) +
						SVerticalBox::Slot().AutoHeight()
							[SNew(SButton)
								 .ButtonStyle(&Hud.Button)
								 .Visibility(DeskChipVis)
								 .IsFocusable(false)
								 .ContentPadding(FMargin(11.f, 5.f))
								 .ToolTipText(FText::FromString(TEXT(
									 "Open the letter and choose. The clock keeps running; an unanswered letter is filed by the clerk.")))
								 .OnClicked_Lambda([this]() {
									 if (UPA250Subsystem* S2 = Sub()) S2->OpenDesk();
									 return FReply::Handled();
								 })
									 [SNew(STextBlock)
										  .Font(BodyFont)
										  .ColorAndOpacity(FPA250HudStyle::Cream)
										  .AutoWrapText(true)
										  .Text_Lambda([this]() {
											  UPA250Subsystem* S2 = Sub();
											  return FText::FromString(S2 ? S2->GetDeskChipText() : FString());
										  })]] +
						SVerticalBox::Slot().AutoHeight()
					   [SNew(SBorder)
							.Visibility(StaffVis)
							.BorderImage(&Hud.Parchment)
							.Padding(FMargin(18.f, 14.f))
								[SNew(SVerticalBox) +
								 // A long letter scrolls inside the panel so its choices always stay on screen.
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(SBox).MaxDesiredHeight(400.f)
										  [SNew(SScrollBox) +
										   SScrollBox::Slot()
											   [SNew(STextBlock)
													.Font(BodyFont)
													.ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f))
													.AutoWrapText(true)
													.Text_Lambda([this]() { return StaffPanelText(); })]]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SWrapBox).PreferredSize(420.f).InnerSlotPadding(FVector2D(6.f, 6.f)).Visibility(AftermathChoicesVis) +
									  SWrapBox::Slot()
										  [MakeButton(TEXT("Purchase"), [this]() { PressLogistics(TEXT("purchase")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return AftermathTip(TEXT("purchase")); }))] +
									  SWrapBox::Slot()
										  [MakeButton(TEXT("Request aid"),
													  [this]() { PressLogistics(TEXT("request_assistance")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda([this]() {
														  return AftermathTip(TEXT("request_assistance"));
													  }))] +
									  SWrapBox::Slot()
										  [MakeButton(TEXT("Requisition"),
													  [this]() { PressLogistics(TEXT("requisition")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return AftermathTip(TEXT("requisition")); }))] +
									  SWrapBox::Slot()
										  [MakeButton(TEXT("Invest capacity"),
													  [this]() { PressLogistics(TEXT("capacity_investment")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda([this]() {
														  return AftermathTip(TEXT("capacity_investment"));
													  }))] +
									  SWrapBox::Slot()
										  [MakeButton(TEXT("Dismiss"), [this]() { PressLogistics(TEXT("dismiss")); },
													  EVisibility::Visible,
													  TAttribute<FText>::CreateLambda(
														  [this]() { return AftermathTip(TEXT("dismiss")); }))]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SWrapBox).PreferredSize(420.f).InnerSlotPadding(FVector2D(6.f, 6.f)).Visibility(CommanderChoicesVis) +
									  SWrapBox::Slot()[CommanderChoice(0)] +
									  SWrapBox::Slot()[CommanderChoice(1)] +
									  SWrapBox::Slot()[CommanderChoice(2)] +
									  SWrapBox::Slot()
										  [MakeButton(TEXT("Dismiss"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(KnoxChoicesVis) +
									  SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
										  [MakeButton(TEXT("Support Knox"),
													  [this]() { PressLogistics(TEXT("knox_support")); })] +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Decline"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(ChapterChoicesVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Acknowledge"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SourceChoices] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(ViewSourceVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("View original source"), [this]() {
											  if (UPA250Subsystem* S2 = Sub())
											  {
												  S2->OpenPendingLetterSourceUrl();
											  }
										  })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(NoticeVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Acknowledge"), [this]() { PressLogistics(TEXT("dismiss")); })]] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [SNew(SHorizontalBox).Visibility(LaterVis) +
									  SHorizontalBox::Slot().AutoWidth()
										  [MakeButton(TEXT("Later"), [this]() {
											  if (UPA250Subsystem* S2 = Sub()) S2->CloseDesk();
										  })]]]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Left)
			  .Padding(FMargin(24.f, 96.f, 0.f, 0.f))
				  [SNew(SBox).WidthOverride(400.f)
					   [SNew(SBorder)
							.Visibility(AboutVis)
							.BorderImage(&Hud.Parchment)
							.Padding(FMargin(16.f, 12.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(BodyFont)
										  .ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f))
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return AboutText(); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
									 [MakeButton(TEXT("Close"), [this]() { PressAboutToggle(); })]]]]
		// Mission tracker: the current goal, where it stands, its deadline, and the standing notices
		// (enlistments, evacuation, headquarters lost, the guns, sieges). Top centre: the towns list
		// has the right edge and the forces and letters the left. Hidden while a battle is pending or
		// its report is up, and once the campaign is over. Hide folds it to one line (heading, title,
		// how many notices are waiting) when it covers something; Show opens it again.
		+ SOverlay::Slot()
			  .VAlign(VAlign_Top)
			  .HAlign(HAlign_Center)
			  .Padding(FMargin(0.f, 40.f, 0.f, 0.f))
				  [SNew(SBox)
					   .WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return bTrackerMinimized ? FOptionalSize() : FOptionalSize(350.f); }))
					   .Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return IsTrackerShown() ? EVisibility::Visible : EVisibility::Collapsed; }))
					   [SNew(SBorder)
							.BorderImage(&Hud.Panel)
							.Padding(TAttribute<FMargin>::CreateLambda([this]() { return bTrackerMinimized ? FMargin(12.f, 4.f, 6.f, 4.f) : FMargin(12.f, 9.f); }))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(SHorizontalBox) +
									  SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
										  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 11)).ColorAndOpacity(FPA250HudStyle::Brass).Text_Lambda([this]() {
											  RefreshTrackerCache();
											  const PA250ArcPanels::FTracker& T = TrackerCache->Tracker;
											  if (!bTrackerMinimized) return FText::FromString(T.Heading);
											  FString Line = T.Heading;
											  if (!T.Title.IsEmpty()) Line += TEXT("  ·  ") + T.Title;
											  if (T.Notices.Num() > 0) Line += FString::Printf(TEXT("  ·  %d %s"), T.Notices.Num(), T.Notices.Num() == 1 ? TEXT("notice") : TEXT("notices"));
											  return FText::FromString(Line);
										  })] +
									  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
										  [MakeButton(TEXT("Tree"), [this]() { PressMissionTreeToggle(); },
											  TAttribute<EVisibility>::CreateLambda([this]() { return bTrackerMinimized ? EVisibility::Collapsed : EVisibility::Visible; }),
											  FText::FromString(TEXT("All missions on your path: locked, active, done or failed.")))] +
									  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
										  [MakeButton(TEXT("Hide"), [this]() { PressTrackerToggle(); },
											  TAttribute<EVisibility>::CreateLambda([this]() { return bTrackerMinimized ? EVisibility::Collapsed : EVisibility::Visible; }),
											  FText::FromString(TEXT("Fold the mission down to one line.")))] +
									  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
										  [MakeButton(TEXT("Show"), [this]() { PressTrackerToggle(); },
											  TAttribute<EVisibility>::CreateLambda([this]() { return bTrackerMinimized ? EVisibility::Visible : EVisibility::Collapsed; }),
											  FText::FromString(TEXT("Show the mission's goal, progress and notices.")))]] +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(SVerticalBox).Visibility_Lambda([this]() { return bTrackerMinimized ? EVisibility::Collapsed : EVisibility::Visible; }) +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 16)).ColorAndOpacity(Cream).WrapTextAt(322.f).Text_Lambda([this]() {
										 RefreshTrackerCache();
										 return FText::FromString(TrackerCache->Tracker.Title);
									 })] +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Italic"), 11)).ColorAndOpacity(FPA250HudStyle::CreamDim).WrapTextAt(322.f)
										  .Visibility_Lambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.Flavor.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
										  .Text_Lambda([this]() { return FText::FromString(TrackerCache->Tracker.Flavor); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
									 [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Cream).WrapTextAt(322.f)
										  .Visibility_Lambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.Goal.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
										  .Text_Lambda([this]() { return FText::FromString(TrackerCache->Tracker.Goal); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
									 [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(Gold).WrapTextAt(322.f)
										  .Visibility_Lambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.Progress.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
										  .Text_Lambda([this]() { return FText::FromString(TrackerCache->Tracker.Progress); })] +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock).Font(BodyFont).ColorAndOpacity(FPA250HudStyle::Red).WrapTextAt(322.f)
										  .Visibility_Lambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.Deadline.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
										  .Text_Lambda([this]() { return FText::FromString(TrackerCache->Tracker.Deadline); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
									 [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Regular"), 12)).ColorAndOpacity(Cream).WrapTextAt(322.f)
										  .Visibility_Lambda([this]() { RefreshTrackerCache(); return TrackerCache->Notices.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
										  .Text_Lambda([this]() { return FText::FromString(TrackerCache->Notices); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
									 [SNew(SWrapBox).PreferredSize(322.f).InnerSlotPadding(FVector2D(6.f, 6.f)) +
									  SWrapBox::Slot()[MakeButton(TEXT("Send for the guns"), [this]() { PressSendForGuns(); },
										  TAttribute<EVisibility>::CreateLambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.bCanSendForGuns ? EVisibility::Visible : EVisibility::Collapsed; }),
										  TAttribute<FText>::CreateLambda([this]() { RefreshTrackerCache(); return FText::FromString(FString::Printf(TEXT("%d gold now; the guns reach headquarters after the winter haul."), TrackerCache->Tracker.GunsCost)); }))] +
									  SWrapBox::Slot()[MakeButton(TEXT("Haul the guns"), [this]() { PressHaulGuns(); },
										  TAttribute<EVisibility>::CreateLambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.bCanHaulGuns ? EVisibility::Visible : EVisibility::Collapsed; }),
										  FText::FromString(TEXT("The selected army takes the siege guns along wherever it marches.")))] +
									  SWrapBox::Slot()[MakeButton(TEXT("Leave the guns here"), [this]() { PressLeaveGuns(); },
										  TAttribute<EVisibility>::CreateLambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.bCanLeaveGuns ? EVisibility::Visible : EVisibility::Collapsed; }),
										  FText::FromString(TEXT("The selected army unhitches the siege guns in this town.")))] +
									  SWrapBox::Slot()[MakeButton(TEXT("Re-enlist this army"), [this]() { PressReenlist(); },
										  TAttribute<EVisibility>::CreateLambda([this]() { RefreshTrackerCache(); return TrackerCache->Tracker.bCanReenlist ? EVisibility::Visible : EVisibility::Collapsed; }),
										  TAttribute<FText>::CreateLambda([this]() { RefreshTrackerCache(); return FText::FromString(FString::Printf(TEXT("%d gold keeps every company of %s past the new year."), TrackerCache->Tracker.ReenlistCost, *TrackerCache->Tracker.SelectedArmyName)); }))]]]]]]
		// After a battle the report opens by itself, over the map, until it is closed.
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Center)
				  [SNew(SBox).WidthOverride(660.f)
					   [SNew(SBorder)
							.Visibility(TAttribute<EVisibility>::CreateLambda([this]() {
								UPA250Subsystem* S2 = Sub();
								return (S2 && S2->HasUnseenBattleReport() && !BattleReportText().IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;
							}))
							.BorderImage(&Hud.Parchment)
							.Padding(FMargin(22.f, 16.f))
								[SNew(SVerticalBox) +
								 // The after-battle card: why it ended, losses, prisoners, what changed on the map.
								 SVerticalBox::Slot().AutoHeight()
									 [SAssignNew(AfterBattleBox, SBox)] +
								 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 12, 0, 0)
									 [MakeButton(TEXT("Continue"), [this]() {
										 if (UPA250Subsystem* S2 = Sub()) S2->AcknowledgeBattleReport();
									 })]]]]
		// A mission that has ended: what happened and what it paid or cost, until it is closed.
		+ SOverlay::Slot()
			  .VAlign(VAlign_Center)
			  .HAlign(HAlign_Center)
				  [SNew(SBox).WidthOverride(560.f)
					   [SNew(SBorder)
							.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return IsMissionCardShown() ? EVisibility::Visible : EVisibility::Collapsed; }))
							.BorderImage(&Hud.Parchment)
							.Padding(FMargin(22.f, 16.f))
								[SNew(SVerticalBox) +
								 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Bold"), 13))
										  .ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() {
											  return FSlateColor(MissionCardNow().bDone ? FPA250HudStyle::SRGB(0.10f, 0.34f, 0.14f) : FPA250HudStyle::SRGB(0.55f, 0.10f, 0.08f));
										  }))
										  .Text_Lambda([this]() { return FText::FromString(MissionCardNow().Heading); })] +
								 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 6)
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Bold"), 22))
										  .ColorAndOpacity(FPA250HudStyle::Ink)
										  .Text_Lambda([this]() { return FText::FromString(MissionCardNow().Title); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Italic"), 14))
										  .ColorAndOpacity(FPA250HudStyle::Ink)
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return FText::FromString(MissionCardNow().Summary); })] +
								 SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Regular"), 14))
										  .ColorAndOpacity(FPA250HudStyle::Ink)
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return FText::FromString(FString::Join(MissionCardNow().Lines, TEXT("\n"))); })] +
								 SVerticalBox::Slot().AutoHeight()
									 [SNew(STextBlock)
										  .Font(FPA250HudStyle::Font(TEXT("Regular"), 12))
										  .ColorAndOpacity(FPA250HudStyle::Ink)
										  .AutoWrapText(true)
										  .Text_Lambda([this]() { return FText::FromString(MissionCardNow().Next); })] +
								 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 12, 0, 0)
									 [MakeButton(TEXT("Continue"), [this]() { PressMissionCardContinue(); })]]]]
		+ SOverlay::Slot()
			  .VAlign(VAlign_Bottom)
			  .HAlign(HAlign_Right)
			  .Padding(FMargin(0.f, 0.f, 12.f, 56.f))
				  [SNew(SBorder)
					   .Visibility(NoBattleVis)
					   .BorderImage(&Hud.Panel)
					   .Padding(FMargin(6.f, 5.f))
						   [SNew(SHorizontalBox) +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Overview"), [this]() { PressResetOverview(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Focus"), [this]() { PressFocusSelected(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Zoom +"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get()) A->ZoomBy(1.4f);
								})] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Zoom −"), [this]() {
									if (APA250ProvinceMapActor* A = MapActor.Get()) A->ZoomBy(1.f / 1.4f);
								})] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Parley"), [this]() { PressParley(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Save"), [this]() { PressSave(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("Load"), [this]() { PressLoad(); })] +
							SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
								[MakeButton(TEXT("About"), [this]() { PressAboutToggle(); })] +
							SHorizontalBox::Slot().AutoWidth()
								[MakeButton(TEXT("Dev"), [this]() { PressDevToggle(); })]]]
		+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Fill)
			  [SNew(SBorder)
				   .BorderImage(&Hud.Bar)
				   .Padding(FMargin(14.f, 6.f))
					   [SNew(SHorizontalBox) +
						SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(BodyFont)
								 .ColorAndOpacity(Cream)
								 .Text_Lambda([this]() { return StatusText(); })] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Pause / Resume"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetPaused(!S2->IsPaused());
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Speed −"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetSpeedIndex(S2->GetSpeedIndex() - 1);
									}
								},
								NoBattleVis)] +
						SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
							[MakeButton(
								TEXT("Speed +"),
								[this]() {
									if (UPA250Subsystem* S2 = Sub())
									{
										S2->SetSpeedIndex(S2->GetSpeedIndex() + 1);
									}
								},
								NoBattleVis)]]]
		// The end of the campaign: over everything, until a new campaign is begun.
		+ SOverlay::Slot()
			  [SNew(SBorder)
				   .Visibility(TAttribute<EVisibility>::CreateLambda([this]() {
					   RefreshTrackerCache();
					   return (TrackerCache.IsValid() && TrackerCache->End.bDecided) ? EVisibility::Visible : EVisibility::Collapsed;
				   }))
				   .BorderImage(&Hud.White)
				   .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.78f))
				   .HAlign(HAlign_Center)
				   .VAlign(VAlign_Center)
					   [SNew(SBox).WidthOverride(640.f)
							[SNew(SBorder)
								 .BorderImage(&Hud.Parchment)
								 .Padding(FMargin(34.f, 24.f))
									 [SNew(SVerticalBox) +
									  SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
										  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 38))
											   .ColorAndOpacity_Lambda([this]() {
												   RefreshTrackerCache();
												   return FSlateColor(TrackerCache->End.bPlayerWon ? FPA250HudStyle::SRGB(0.10f, 0.34f, 0.14f) : FPA250HudStyle::SRGB(0.55f, 0.10f, 0.08f));
											   })
											   .Text_Lambda([this]() { RefreshTrackerCache(); return FText::FromString(TrackerCache->End.Headline); })] +
									  SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 12.f)
										  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Italic"), 16)).ColorAndOpacity(FPA250HudStyle::Ink).WrapTextAt(570.f)
											   .Text_Lambda([this]() { RefreshTrackerCache(); return FText::FromString(TrackerCache->End.Subline); })] +
									  SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(1.f)[SNew(SImage).Image(&Hud.White).ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.04f, 0.35f))]] +
									  SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
										  [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Regular"), 15)).ColorAndOpacity(FPA250HudStyle::Ink).WrapTextAt(570.f).LineHeightPercentage(1.25f)
											   .Text_Lambda([this]() { RefreshTrackerCache(); return FText::FromString(TrackerCache->EndLines); })] +
									  SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 18.f, 0.f, 0.f)
										  [SNew(SHorizontalBox) +
										   SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)[MakeButton(TEXT("New campaign"), [this]() { PressNewCampaign(); })] +
										   SHorizontalBox::Slot().AutoWidth()[MakeButton(TEXT("Quit"), [this]() { PressQuitGame(); })]]]]]]
		// The battle story covers everything from "To arms!" until the field is ready.
		+ SOverlay::Slot()[SAssignNew(StoryBox, SBox).Visibility_Lambda([this]() { return bStoryUp ? EVisibility::Visible : EVisibility::Collapsed; })]
		// A decision takes the whole screen; the clock is stopped until it is answered.
		+ SOverlay::Slot()
			  [SAssignNew(DecisionBox, SBox).Visibility_Lambda([this]() {
				  UPA250Subsystem* S2 = Sub();
				  return S2 && S2->GetController().has_campaign() && S2->IsDecisionPending() && !S2->IsBattlePending() ? EVisibility::Visible : EVisibility::Collapsed;
			  })];
}

void UPA250MapWidget::PressEndTurn()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->EndTurn(Err))
	{
		ClearSelection();
		LastMessage = S->IsBattlePending() ? TEXT("The armies have met! Prepare for battle.") : S->FormatDate();
		NotifyMapSelectionChanged();
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressParley()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (S && S->BeginDiplomacy(TEXT("british"), Err))
	{
		S->TravelToFlowLevel(this);
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressFight()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	if (bStoryUp)
	{
		return; // the story is already up: Continue takes the player to the field
	}
	if (S && S->BeginBattle(Err))
	{
		StartBattleStory();
	}
	else
	{
		SetStatusMessage(Err);
	}
}

void UPA250MapWidget::PressResetOverview()
{
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		A->ResetOverview();
		SetStatusMessage(TEXT("Campaign overview: Boston to Concord."));
	}
}

void UPA250MapWidget::PressFocusSelected()
{
	if (APA250ProvinceMapActor* A = MapActor.Get())
	{
		if (SelectedProvinceId.IsEmpty())
		{
			SetStatusMessage(TEXT("Select a town or army first, then Focus selected."));
			return;
		}
		A->FocusSelected();
		SetStatusMessage(TEXT("Focused on the selection."));
	}
}

void UPA250MapWidget::PressLedgerToggle()
{
	bShowLedger = !bShowLedger;
	SetStatusMessage(bShowLedger ? TEXT("Ledger open — every province you hold, its development and stores.")
								 : TEXT("Ledger closed."));
}

void UPA250MapWidget::PressAboutToggle()
{
	bShowAbout = !bShowAbout;
}

void UPA250MapWidget::PressTrackerToggle()
{
	bTrackerMinimized = !bTrackerMinimized;
}

bool UPA250MapWidget::IsTrackerShown() const
{
	RefreshTrackerCache();
	UPA250Subsystem* S2 = Sub();
	return TrackerCache.IsValid() && TrackerCache->Tracker.bVisible && S2 && !S2->IsBattlePending() && !S2->HasUnseenBattleReport() &&
		   !IsMissionCardShown() && !TrackerCache->End.bDecided;
}

bool UPA250MapWidget::IsMissionCardShown() const
{
	// One thing at a time: the battle's cards first, then the mission's.
	const UPA250Subsystem* S2 = Sub();
	return S2 && S2->HasUnseenMissionResult() && !S2->IsBattlePending() && !(S2->HasUnseenBattleReport() && !BattleReportText().IsEmpty());
}

PA250ArcPanels::FMissionCard UPA250MapWidget::MissionCardNow() const
{
	const UPA250Subsystem* S2 = Sub();
	const pa250::MissionResult* R = S2 ? S2->PeekMissionResult() : nullptr;
	return R ? PA250ArcPanels::MissionCard(*R, S2->GetState()) : PA250ArcPanels::FMissionCard();
}

void UPA250MapWidget::PressMissionCardContinue()
{
	if (UPA250Subsystem* S2 = Sub())
	{
		if (const pa250::MissionResult* R = S2->PeekMissionResult())
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 mission card closed: %s (%s)"), UTF8_TO_TCHAR(R->title.c_str()), UTF8_TO_TCHAR(R->outcome.c_str()));
		}
		S2->AcknowledgeMissionResult();
	}
}

void UPA250MapWidget::PressDevToggle()
{
	bShowDev = !bShowDev;
	SetStatusMessage(bShowDev ? TEXT("Developer diagnostics on (town panel footer).") : TEXT("Developer diagnostics off."));
}

bool UPA250MapWidget::IsCityViewOpen() const
{
	return CityView && CityView->GetVisibility() != ESlateVisibility::Collapsed &&
		   CityView->GetVisibility() != ESlateVisibility::Hidden;
}

bool UPA250MapWidget::IsViewportPointOver(const TSharedPtr<SWidget>& Widget, FVector2D ViewportPx)
{
	if (!Widget.IsValid() || !Widget->GetVisibility().IsVisible() || !GEngine || !GEngine->GameViewport)
	{
		return false;
	}
	const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
	if (!Viewport.IsValid())
	{
		return false;
	}
	const FGeometry& VG = Viewport->GetTickSpaceGeometry();
	const FVector2D Abs = VG.LocalToAbsolute(ViewportPx / FMath::Max(VG.Scale, KINDA_SMALL_NUMBER));
	return Widget->GetTickSpaceGeometry().IsUnderLocation(Abs);
}

bool UPA250MapWidget::IsPointOverHud(FVector2D ScreenPos) const
{
	// The town panel is docked, not modal: only its own rectangle blocks the map.
	if (CityView && IsCityViewOpen() && CityView->IsViewportPointOverPanel(ScreenPos))
	{
		return true;
	}
	FVector2D Size(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	if (ScreenPos.Y <= TopStripHeightPx || ScreenPos.Y >= Size.Y - BottomStripHeightPx)
	{
		return true;
	}
	// The mission tracker's heading row (top centre), where its Hide / Show button sits.
	if (IsTrackerShown() && FMath::Abs(ScreenPos.X - Size.X * 0.5f) <= 183.f && ScreenPos.Y <= 40.f + 38.f)
	{
		return true;
	}
	// A decision or the battle story owns the whole screen; the people panel and the floor block their own rectangles.
	if (bStoryUp)
	{
		return true;
	}
	if (UPA250Subsystem* PassSub = Sub(); PassSub && PassSub->GetController().has_campaign() && PassSub->IsDecisionPending())
	{
		return true;
	}
	if (IsViewportPointOver(PeopleBox, ScreenPos) || IsViewportPointOver(FloorBox, ScreenPos) ||
		IsViewportPointOver(MissionTreeBox, ScreenPos) || IsViewportPointOver(FindBox, ScreenPos) ||
		IsViewportPointOver(ReorganizeBox, ScreenPos))
	{
		return true;
	}
	// The selected army's panel (bottom left, beside the town panel), where its Halt button sits.
	if (IsViewportPointOver(ArmyPanelBox, ScreenPos))
	{
		return true;
	}
	// Your-forces list (top-left, one row per army), ledger (when open), toolbar (bottom-right).
	int32 ArmyRows = 0;
	for (FString Id; ArmyRows < MaxForceRows && GetPlayerArmyId(ArmyRows, Id);) ++ArmyRows;
	if (ScreenPos.X <= 332.f && ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 76.f + 34.f * FMath::Max(1, ArmyRows))
	{
		return true;
	}
	if (bShowTreasury && FMath::Abs(ScreenPos.X - Size.X * 0.5f) <= TreasuryPanelWidth * 0.5f + 16.f &&
		ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 430.f)
	{
		return true;
	}
	// Your-towns list (top-right, one row per town held).
	if (ScreenPos.X >= Size.X - (TownsPanelWidth + 32.f) && ScreenPos.Y >= TopStripHeightPx &&
		ScreenPos.Y <= TopStripHeightPx + 50.f + 32.f * HeldTownCount())
	{
		return true;
	}
	if (bShowLedger && ScreenPos.X <= 332.f && ScreenPos.Y >= TopStripHeightPx + 110.f &&
		ScreenPos.Y <= TopStripHeightPx + 480.f)
	{
		return true;
	}
	if (ScreenPos.X >= Size.X - 720.f && ScreenPos.Y >= Size.Y - BottomStripHeightPx - 64.f &&
		ScreenPos.Y <= Size.Y - BottomStripHeightPx)
	{
		return true;
	}
	if (bShowAbout && ScreenPos.X <= 448.f && ScreenPos.Y >= TopStripHeightPx && ScreenPos.Y <= TopStripHeightPx + 420.f)
	{
		return true;
	}
	return false;
}

void UPA250MapWidget::PressEnterBattleAtSelection()
{
	if (!CanEnterBattleAtSelection())
	{
		SetStatusMessage(TEXT("Enter battle only when contact is pending at the selected town."));
		return;
	}
	PressFight();
}

void UPA250MapWidget::PressSave()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	const bool bOk = S && S->SaveCampaign(Err);
	SetStatusMessage(bOk ? TEXT("Campaign saved.") : TEXT("Save failed: ") + Err);
}

void UPA250MapWidget::PressLoad()
{
	UPA250Subsystem* S = Sub();
	FString Err;
	const bool bOk = S && S->LoadCampaign(Err);
	ClearSelection();
	SetStatusMessage(bOk ? TEXT("Campaign loaded.") : TEXT("Load failed: ") + Err);
	NotifyMapSelectionChanged();
}

namespace
{
// One purchase order for the selected town; the status line quotes the sim's own ledger entry.
void BuyForTown(UPA250MapWidget* W, UPA250Subsystem* S, const FString& Town, int32 Cartridges, int32 Muskets, int32 Provisions, int32 Powder = 0)
{
	if (!S || Town.IsEmpty()) return;
	FString Err;
	if (S->ApprovePurchaseOrder(Town, Powder, Cartridges, Muskets, 0, Provisions, 0, 0, Err))
	{
		const FString Entry = S->GetLastResourceChangeReason();
		W->SetStatusMessage((Entry.IsEmpty() ? FString(TEXT("Purchase approved")) : Entry) + TEXT(" — in transit, not delivered yet."));
	}
	else
	{
		W->SetStatusMessage(Err.StartsWith(TEXT("powder_needs_harbor_depot"))
								? FString(TEXT("Powder comes in by sea: it can be bought only into a harbour town that has a Depot."))
								: TEXT("Purchase failed: ") + Err);
	}
}
} // namespace

void UPA250MapWidget::PressBuyPowder() { BuyForTown(this, Sub(), SelectedProvinceId, 0, 0, 0, BuyPowderQty); }

void UPA250MapWidget::PressBuyCartridges() { BuyForTown(this, Sub(), SelectedProvinceId, BuyCartridgesQty, 0, 0); }
void UPA250MapWidget::PressBuyMuskets() { BuyForTown(this, Sub(), SelectedProvinceId, 0, BuyMusketsQty, 0); }
void UPA250MapWidget::PressBuyProvisions() { BuyForTown(this, Sub(), SelectedProvinceId, 0, 0, BuyProvisionsQty); }

bool UPA250MapWidget::IsTownHeld(const FString& ProvinceId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return false;
	const auto It = S->GetState().provinces.find(ToStd(ProvinceId));
	return It != S->GetState().provinces.end() && It->second.owner == S->GetState().player_faction;
}

int32 UPA250MapWidget::HeldTownCount() const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return 0;
	int32 N = 0;
	for (const auto& Kv : S->GetState().provinces) N += Kv.second.owner == S->GetState().player_faction ? 1 : 0;
	return N;
}

FText UPA250MapWidget::TownRowStatus(const FString& ProvinceId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign()) return FText::GetEmpty();
	const pa250::CampaignState& St = S->GetState();
	const std::string Pid = ToStd(ProvinceId);
	// What is under way wins the line; otherwise how many plots are still free.
	TArray<FString> Parts;
	for (const auto& Job : St.progress.construction_jobs)
	{
		if (Job.status == "underway" && Job.province_id == Pid)
			Parts.Add(FString::Printf(TEXT("building %dd"), (Job.hours_remaining + 23) / 24));
	}
	for (const auto& Job : St.progress.recruit_jobs)
	{
		if (Job.province_id == Pid) Parts.Add(FString::Printf(TEXT("muster %dd"), (Job.hours_remaining + 23) / 24));
	}
	if (St.pending_battle.active && St.pending_battle.province == Pid) Parts.Insert(TEXT("BATTLE"), 0);
	if (Parts.Num() > 0) return FText::FromString(FString::Join(Parts, TEXT(" · ")));
	const int32 Free = pa250::province_slot_count(Pid) - pa250::province_slots_occupied(St, Pid);
	return FText::FromString(Free > 0 ? FString::Printf(TEXT("%d free plot%s"), Free, Free == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("full")));
}

void UPA250MapWidget::PressTransferToDepot()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	// The whole magazine but its provisions (powder, cartridges, muskets, bayonets, lead, paper), by
	// the sim's own rule. Until 2026-11-14 this sent two powder a press and nothing else, so a
	// magazine could never be emptied (mission 1 asks for exactly that).
	FString Err;
	if (S->SendStoresToDepot(SelectedProvinceId, Err))
	{
		const pa250::CampaignState& St = S->GetState();
		const auto Depot = St.provinces.find(pa250::default_depot_province(St, St.player_faction));
		SetStatusMessage(FString::Printf(TEXT("The stores are on the road to %s. This magazine is empty of arms and powder; they are credited when they arrive."),
			Depot == St.provinces.end() ? TEXT("the depot") : *ToFStr(Depot->second.name)));
	}
	else
	{
		SetStatusMessage(SendStoresRefusal(Err));
	}
}

FString UPA250MapWidget::SendStoresRefusal(const FString& ErrorCode)
{
	if (ErrorCode.Contains(TEXT("already_at_depot"))) return TEXT("This town is the depot: its stores are already where they are kept.");
	if (ErrorCode.Contains(TEXT("empty_order"))) return TEXT("This magazine holds no arms or powder to send.");
	if (ErrorCode.Contains(TEXT("not_your_province"))) return TEXT("You must hold both this town and the depot to move stores.");
	if (ErrorCode.Contains(TEXT("battle_pending"))) return TEXT("No wagons leave while a battle is waiting to be fought.");
	return TEXT("The stores could not be sent.");
}

FString UPA250MapWidget::HandleTownClicked(const FString& ProvinceId)
{
	// Left click inspects. A march is a right click (HandleTownOrdered) so an enemy town can be
	// opened while an army stays selected.
	const FString Result = ClickProvince(ProvinceId);
	if (!Result.IsEmpty() && SelectedProvinceId == ProvinceId)
	{
		OpenCityView(ProvinceId);
	}
	return Result;
}

void UPA250MapWidget::OpenCityView(const FString& ProvinceId)
{
	if (ProvinceId.IsEmpty()) return;
	if (SelectedProvinceId != ProvinceId)
	{
		SelectedProvinceId = ProvinceId;
		NotifyMapSelectionChanged();
	}
	if (!CityView)
	{
		// WBP_CityView (optional) only overrides the header/plot pictures; the layout is Slate.
		UClass* Class = CityViewClass
							? CityViewClass.Get()
							: StaticLoadClass(UPA250CityViewWidget::StaticClass(), nullptr,
											  TEXT("/Game/PA250/UI/CityView/WBP_CityView.WBP_CityView_C"));
		if (!Class) Class = UPA250CityViewWidget::StaticClass();
		CityView = CreateWidget<UPA250CityViewWidget>(GetOwningPlayer(), Class);
		if (CityView)
		{
			CityView->SetHud(this);
			// Closing the panel (X or Esc) ends the selection: the panel is the selection's window.
			CityView->OnClosed.AddUniqueDynamic(this, &UPA250MapWidget::ClearSelection);
			CityView->AddToViewport(40); // below the hover tooltip (50)
		}
	}
	if (!CityView)
	{
		SetStatusMessage(TEXT("Town panel failed to open."));
		return;
	}
	CityView->OpenForProvince(ProvinceId);
}

void UPA250MapWidget::CloseCityView()
{
	if (CityView) CityView->CloseCityView();
}

void UPA250MapWidget::PressMakeCartridges()
{
	UPA250Subsystem* S = Sub();
	if (!S || SelectedProvinceId.IsEmpty())
	{
		return;
	}
	FString ArmyId = SelectedArmyId;
	if (ArmyId.IsEmpty())
	{
		const std::string Pid = ToStd(SelectedProvinceId);
		for (const auto& KV : S->GetState().armies)
		{
			if (KV.second.province == Pid && KV.second.faction == S->GetState().player_faction)
			{
				ArmyId = ToFStr(KV.first);
				break;
			}
		}
	}
	if (ArmyId.IsEmpty())
	{
		SetStatusMessage(TEXT("Need a friendly army in this province to assign workers."));
		return;
	}
	FString Err;
	if (S->StartCartridgeCraft(SelectedProvinceId, ArmyId, 2, 1, 1, 1, Err))
	{
		SetStatusMessage(TEXT("Cartridge job underway — workers unavailable until it finishes."));
	}
	else
	{
		SetStatusMessage(TEXT("Cartridge job failed: ") + Err);
	}
}

void UPA250MapWidget::PressLogistics(const FString& Choice)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	FString Err;
	const FString LetterId = S->GetPendingLetterId();
	if (S->ChooseLogistics(LetterId, Choice, Err))
	{
		const pa250::CampaignState& St = S->GetState();
		FString Msg = St.resource_changes.empty()
						  ? FString::Printf(TEXT("Staff action: %s."), *Choice)
						  : ToFStr(St.resource_changes.back().reason);
		Msg += TEXT(" Check Pending in the Ledger for timed follow-ups.");
		SetStatusMessage(Msg);
	}
	else
	{
		SetStatusMessage(TEXT("Staff action failed: ") + Err);
	}
}

void UPA250MapWidget::PressCommander(const FString& Action)
{
	UPA250Subsystem* S = Sub();
	if (!S)
	{
		return;
	}
	FString Err;
	if (S->ResolveCommander(Action, Err))
	{
		if (Action == TEXT("escalate"))
		{
			SetStatusMessage(TEXT("Escalated. If this company loses the field, the town is forfeit even without the post."));
		}
		else
		{
			SetStatusMessage(FString::Printf(TEXT("Orders to detached command: %s."), *Action));
		}
	}
	else
	{
		SetStatusMessage(TEXT("Commander order failed: ") + Err);
	}
}
