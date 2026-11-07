#include "PA250CampaignGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Campaign/PA250Autoplay.h"
#include "Campaign/PA250Subsystem.h"
#include "GameFramework/PlayerController.h"
#include "PA250MapWidget.h"
#include "SPA250Map.h"
#include "TimerManager.h"

APA250CampaignGameMode::APA250CampaignGameMode()
{
	DefaultPawnClass = nullptr;
}

void APA250CampaignGameMode::BeginPlay()
{
	Super::BeginPlay();

	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	Sub->EnsureCampaign();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeGameAndUI());
		MapWidget = CreateWidget<UPA250MapWidget>(PC, UPA250MapWidget::StaticClass());
		if (MapWidget)
		{
			MapWidget->AddToViewport();
		}
	}

	if (PA250Autoplay::IsAutoplay())
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunAutoplayCampaign), 0.7f, false);
	}
	else if (PA250Autoplay::SelftestName() == TEXT("campaign"))
	{
		// Let the widget build once, then run the scripted check.
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunCampaignSelftest), 0.5f, false);
	}
}

void APA250CampaignGameMode::RunCampaignSelftest()
{
	using namespace PA250Autoplay;
	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	Check(Sub, MapWidget != nullptr && MapWidget->GetMap().IsValid(), TEXT("map widget created in L_Campaign"));
	if (!Sub || !MapWidget || !MapWidget->GetMap().IsValid())
	{
		Finish(Sub, TEXT("selftest_campaign"));
		return;
	}
	TSharedPtr<SPA250Map> Map = MapWidget->GetMap();
	const pa250::CampaignState& S = Sub->GetState();

	// Point-in-polygon: a point inside Lexington and the label anchors resolve to their provinces.
	Check(Sub, Map->ProvinceAt(FVector2D(430, 150)) == TEXT("lexington"), TEXT("point-in-polygon resolves Lexington"));
	Check(Sub, Map->ProvinceAt(FVector2D(755, 550)) == TEXT("boston"), TEXT("point-in-polygon resolves Boston"));
	Check(Sub, Map->ProvinceAt(FVector2D(2000, 2000)).IsEmpty(), TEXT("point outside every province selects nothing"));

	// Select the militia, try an illegal (non-adjacent) move, then a legal one.
	MapWidget->ClickProvince(TEXT("lexington"));
	Check(Sub, Map->SelectedArmyId == TEXT("continental_militia"), TEXT("clicking Lexington selects the militia"));
	MapWidget->ClickProvince(TEXT("boston"));
	Check(Sub, Sub->GetController().orders().orders.empty(), TEXT("non-adjacent move is rejected"));
	Check(Sub, Map->LastMessage.Contains(TEXT("Illegal move")), TEXT("illegal move explained to the player"));
	MapWidget->ClickProvince(TEXT("cambridge"));
	Check(Sub, Sub->GetController().orders().orders.size() == 1, TEXT("adjacent move order queued"));

	MapWidget->PressEndTurn();
	Check(Sub, S.turn == 1, TEXT("turn advanced to 1"));
	Check(Sub, S.armies.at("continental_militia").province == "cambridge", TEXT("militia marched to Cambridge"));
	Check(Sub, S.armies.at("british_regulars").province == "charlestown", TEXT("British marched to Charlestown"));
	Check(Sub, !Sub->IsBattlePending(), TEXT("no battle yet"));

	MapWidget->PressEndTurn();  // British enter Cambridge where the militia stand
	Check(Sub, Sub->IsBattlePending() && S.pending_battle.province == "cambridge", TEXT("armies meeting records a pending battle"));
	FString Err;
	Check(Sub, !Sub->EndTurn(Err), TEXT("cannot end turn with a pending battle"));
	Finish(Sub, TEXT("selftest_campaign"));
}

// ------------------------------------------------------------------ full-loop autoplay (campaign legs)

void APA250CampaignGameMode::RunAutoplayCampaign()
{
	using namespace PA250Autoplay;
	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	if (!Sub || !MapWidget)
	{
		Finish(Sub, TEXT("autoplay"));
		return;
	}
	const pa250::CampaignState& S = Sub->GetState();
	FString Err;
	const FString Stage = Sub->AutoplayStage;
	Sub->GetControllerMutable().record("autoplay_stage", TCHAR_TO_UTF8(*(Stage.IsEmpty() ? FString(TEXT("start")) : Stage)));

	if (Stage.IsEmpty())
	{
		// Leg 1: new campaign, scripted orders until the armies meet at Lexington.
		Check(Sub, Sub->NewCampaign(Err), TEXT("new campaign"));
		Check(Sub, S.turn == 0 && S.provinces.size() == 6, TEXT("Boston 1775: six provinces at turn 0"));
		Check(Sub, S.provinces.at("boston").owner == "british" && S.provinces.at("lexington").owner == "continental", TEXT("initial ownership"));
		Check(Sub, !Sub->QueueMove(TEXT("continental_militia"), TEXT("boston"), Err), TEXT("illegal move rejected through the subsystem"));
		Check(Sub, Sub->QueueMove(TEXT("continental_militia"), TEXT("lexington"), Err), TEXT("hold order accepted"));
		for (int32 i = 0; i < 8 && !Sub->IsBattlePending(); ++i)
		{
			MapWidget->PressEndTurn();
		}
		Check(Sub, Sub->IsBattlePending() && S.pending_battle.province == "lexington", TEXT("armies met at Lexington"));
		Check(Sub, S.turn == 3, TEXT("the British marched Boston-Charlestown-Cambridge-Lexington in three turns"));
		Sub->AutoplayStage = TEXT("battle");
		MapWidget->PressFight();
		Check(Sub, Sub->GetFlow() == EPA250Flow::Battle, TEXT("campaign handed off to the battle flow"));
	}
	else if (Stage == TEXT("battle_done"))
	{
		// Leg 2: the BattleResult was applied in L_Lexington; open diplomacy with the envoy.
		Check(Sub, Sub->GetFlow() == EPA250Flow::Campaign && !Sub->IsBattlePending(), TEXT("after the battle: campaign flow, nothing pending"));
		Check(Sub, S.turn == 3, TEXT("battle does not advance the turn"));
		int32 Units = 0;
		for (const auto& A : S.armies) for (const auto& U : A.second.units) Units += U.second;
		Check(Sub, Units > 0 && Units <= 24 + 22, TEXT("army strengths consistent after losses"));
		Sub->AutoplayStage = TEXT("diplomacy");
		MapWidget->PressParley();
		Check(Sub, Sub->GetFlow() == EPA250Flow::Diplomacy, TEXT("campaign handed off to the diplomacy flow"));
	}
	else if (Stage == TEXT("diplomacy_done"))
	{
		// Leg 3: the validated ceasefire(3) is in force; the British AI must respect it; save/load.
		Check(Sub, Sub->GetFlow() == EPA250Flow::Campaign, TEXT("after diplomacy: campaign flow"));
		Check(Sub, S.treaties.size() == 1 && S.treaties[0].type == "ceasefire" && S.treaties[0].turns_remaining == 3, TEXT("ceasefire(3) in force"));
		Check(Sub, S.provinces.at("boston").owner == "british", TEXT("Boston still British after the jailbreak line"));
		std::map<std::string, std::string> Before;
		for (const auto& A : S.armies) Before[A.first] = A.second.province;
		const int32 TurnBefore = S.turn;
		MapWidget->PressEndTurn();
		Check(Sub, S.turn == TurnBefore + 1, TEXT("turn advanced under the ceasefire"));
		// Ceasefire blocks entry into enemy-owned provinces. Movement on friendly ground is still legal.
		bool bRespected = true;
		for (const auto& A : S.armies)
		{
			const auto Prev = Before.find(A.first);
			if (Prev == Before.end() || Prev->second == A.second.province) continue;
			if (S.provinces.at(A.second.province).owner != A.second.faction) bRespected = false;
		}
		Check(Sub, bRespected, TEXT("the British AI respected the ceasefire (no army entered enemy ground)"));
		Check(Sub, !Sub->IsBattlePending(), TEXT("no new battle under the ceasefire"));
		Check(Sub, S.treaties.size() == 1 && S.treaties[0].turns_remaining == 2, TEXT("ceasefire ticked down to 2"));

		const FString Hash = Sub->GetStateHash();
		Check(Sub, Sub->SaveCampaign(Err), TEXT("save"));
		Check(Sub, Sub->LoadCampaign(Err), TEXT("load"));
		Check(Sub, Sub->GetStateHash() == Hash, TEXT("save/load round trip: identical state hash"));
		Finish(Sub, TEXT("autoplay"));
	}
	else
	{
		Check(Sub, false, FString::Printf(TEXT("unexpected autoplay stage '%s' in L_Campaign"), *Stage));
		Finish(Sub, TEXT("autoplay"));
	}
}
