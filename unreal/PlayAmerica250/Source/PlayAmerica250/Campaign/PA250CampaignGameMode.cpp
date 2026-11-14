#include "PA250CampaignGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Campaign/PA250Autoplay.h"
#include <map>
#include "Battle/PA250BattleAudio.h"
#include "Campaign/PA250Subsystem.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "PA250CampaignPlayerController.h"
#include "PA250MapWidget.h"
#include "UI/PA250CityViewWidget.h"
#include "PA250ProvinceMapActor.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "PixelFormat.h"

APA250CampaignGameMode::APA250CampaignGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APA250CampaignPlayerController::StaticClass();
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
	Sub->SetCampaignActive(true);
	FPA250BattleAudio::StartCampaignMusic(this);

	if (UWorld* World = GetWorld())
	{
		// Empty L_Campaign has no lights; tokens/labels use lit materials. Keep a key light even with Unlit map.
		FActorSpawnParameters LightParams;
		LightParams.Name = TEXT("PA250KeyLight");
		if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0.f, 0.f, 500.f),
																		  FRotator(-80.f, 30.f, 0.f), LightParams))
		{
			if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
			{
				C->SetIntensity(8.f);
				C->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
			}
		}
		LightParams.Name = TEXT("PA250SkyLight");
		if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, LightParams))
		{
			if (USkyLightComponent* C = Sky->GetLightComponent())
			{
				C->SetIntensity(2.f);
				C->bRealTimeCapture = true;
			}
		}

		FActorSpawnParameters Params;
		Params.Name = TEXT("PA250ProvinceMap");
		MapActor = World->SpawnActor<APA250ProvinceMapActor>(APA250ProvinceMapActor::StaticClass(), FVector::ZeroVector,
															 FRotator::ZeroRotator, Params);
	}

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = true;
		PC->bAutoManageActiveCameraTarget = false;
		if (APA250CampaignPlayerController* CampaignPC = Cast<APA250CampaignPlayerController>(PC))
		{
			CampaignPC->ApplyCampaignInputMode();
		}
		else
		{
			PC->SetInputMode(FInputModeGameAndUI());
		}
		MapWidget = CreateWidget<UPA250MapWidget>(PC, UPA250MapWidget::StaticClass());
		if (MapWidget)
		{
			MapWidget->SetMapActor(MapActor);
			MapWidget->AddToViewport();
			MapWidget->SetKeyboardFocus();
		}
	}

	// Re-assert view target after the player camera manager settles.
	if (MapActor)
	{
		FTimerHandle CamHandle;
		GetWorldTimerManager().SetTimer(CamHandle, FTimerDelegate::CreateLambda([this]() {
			if (MapActor)
			{
				MapActor->FrameCamera();
			}
		}),
									   0.05f, false);
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("PA250ArtProbe="), ArtProbeLabel) && !ArtProbeLabel.IsEmpty())
	{
		// First capture after 12 s (never at frame 5), then one step every 3 s.
		Sub->SetPaused(true);
		ArtProbeStep = 0;
		GetWorldTimerManager().SetTimer(ArtProbeTimer, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunArtProbeStep),
										3.f, true, 12.f);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PA250DecostaProbe")))
	{
		FTimerHandle ProbeHandle;
		GetWorldTimerManager().SetTimer(
			ProbeHandle,
			FTimerDelegate::CreateLambda([this]() {
				UE_LOG(LogTemp, Display, TEXT("DECOSTA_PROBE: start"));
				if (MapActor)
				{
					UE_LOG(LogTemp, Display, TEXT("DECOSTA_PROBE: decosta=%d presentation=%d display=%s"),
						   MapActor->IsDeCostaMode() ? 1 : 0, MapActor->IsPresentationLoaded() ? 1 : 0,
						   MapActor->GetDisplayTexture() ? *MapActor->GetDisplayTexture()->GetName() : TEXT("NONE"));
				}
				if (APA250CampaignPlayerController* CPC =
						Cast<APA250CampaignPlayerController>(GetWorld()->GetFirstPlayerController()))
				{
					CPC->ClickMapAtUV(0.300f, 0.230f); // lexington hotspot (engraved town, decosta_locations.json)
					UE_LOG(LogTemp, Display, TEXT("DECOSTA_PROBE: clicked lexington UV"));
				}
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				{
					PC->ConsoleCommand(TEXT("Shot showui"));
					UE_LOG(LogTemp, Display, TEXT("DECOSTA_PROBE: Shot showui"));
				}
				if (FParse::Param(FCommandLine::Get(), TEXT("PA250Quit")))
				{
					FTimerHandle QuitHandle;
					GetWorldTimerManager().SetTimer(
						QuitHandle, FTimerDelegate::CreateLambda([this]() {
							if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
							{
								PC->ConsoleCommand(TEXT("QUIT"));
							}
						}),
						2.f, false);
				}
			}),
			12.f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PA250CityProbe")))
	{
		// Town panel check in a real window: five "Shot showui" captures (build list, works under way,
		// the Forces and Magazine tabs of the army's town, an enemy city), first at 12 s, then quit. Never saves.
		auto Step = [this](float At, TFunction<void()> Fn) {
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this, Fn]() {
				Fn();
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("Shot showui"));
			}), At, false);
		};
		Step(12.f, [this]() {
			if (!MapWidget) return;
			MapWidget->HandleTownClicked(TEXT("lexington"));
			if (UPA250CityViewWidget* CV = MapWidget->GetCityView()) CV->SelectPlot(TEXT("+"));
			UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: lexington build list open=%d"), MapWidget->IsCityViewOpen() ? 1 : 0);
		});
		auto Advance = [this](int32 Hours) {
			FString Err;
			UPA250Subsystem* PS = UPA250Subsystem::Get(this);
			const bool bOk = PS && PS->AdvanceHours(Hours, Err);
			UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: advance %dh ok=%d %s staff=%d battle=%d"), Hours, bOk ? 1 : 0, *Err,
				   PS && PS->HasStaffEvent() ? 1 : 0, PS && PS->IsBattlePending() ? 1 : 0);
		};
		auto Armies = [this]() {
			FString Out;
			if (UPA250Subsystem* PS = UPA250Subsystem::Get(this))
			{
				for (const auto& Kv : PS->GetState().armies)
				{
					int32 Fit = 0;
					for (const auto& U : Kv.second.units) Fit += U.second;
					Out += FString::Printf(TEXT("%s@%s=%d "), UTF8_TO_TCHAR(Kv.first.c_str()), UTF8_TO_TCHAR(Kv.second.province.c_str()), Fit);
				}
			}
			return Out;
		};
		Step(15.f, [this, Advance]() {
			UPA250CityViewWidget* CV = MapWidget ? MapWidget->GetCityView() : nullptr;
			if (!CV) return;
			// The British march on Concord within hours of the opening, and a pending battle stops
			// the clock. A 30-day ceasefire through the ordinary diplomacy path (the sim validates
			// it like any other) keeps the roads quiet long enough to see a muster through.
			if (UPA250Subsystem* PS = UPA250Subsystem::Get(this))
			{
				FString Err;
				const bool bTalks = PS->BeginDiplomacy(TEXT("british"), Err);
				const bool bTruce = bTalks && PS->AcceptProposalJson(TEXT("{\"spoken_line\":\"probe\",\"clauses\":[{\"type\":\"ceasefire\",\"days\":30}]}"), Err);
				UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: ceasefire talks=%d accepted=%d %s"), bTalks ? 1 : 0, bTruce ? 1 : 0, *Err);
			}
			CV->RequestBuild(TEXT("militia_muster"));
			Advance(24);
			CV->SelectTab(1);
			CV->RequestRecruit(TEXT("militia_company"));
			UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: muster started; %s"), *UPA250Subsystem::Get(this)->GetLastResourceChangeReason());
		});
		Step(18.f, [this, Advance, Armies]() {
			Advance(48);
			UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: after muster armies: %s"), *Armies());
		});
		Step(21.f, [this, Advance, Armies]() {
			// Order the new company to the next town along the road and let it march.
			if (!MapWidget) return;
			FString Raised;
			for (const auto& Kv : UPA250Subsystem::Get(this)->GetState().armies)
			{
				if (Kv.first.rfind("raised_", 0) == 0) Raised = UTF8_TO_TCHAR(Kv.first.c_str());
			}
			if (!Raised.IsEmpty())
			{
				MapWidget->PressSelectArmy(Raised);
				const FString Msg = MapWidget->HandleTownClicked(TEXT("menotomy"));
				Advance(3);
				UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: march order: %s | armies: %s"), *Msg, *Armies());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("CITY_PROBE: no raised company found; armies: %s"), *Armies());
			}
		});
		Step(24.f, [this]() {
			if (!MapWidget) return;
			MapWidget->ClearSelection();
			MapWidget->HandleTownClicked(TEXT("concord"));
			if (UPA250CityViewWidget* CV = MapWidget->GetCityView()) CV->SelectTab(2);
			UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: concord magazine tab open=%d"), MapWidget->IsCityViewOpen() ? 1 : 0);
		});
		Step(27.f, [this]() {
			if (!MapWidget) return;
			MapWidget->ClearSelection();
			MapWidget->HandleTownClicked(TEXT("boston"));
			MapWidget->PressBorrow();
			if (!MapWidget->IsTreasuryOpen()) MapWidget->PressTreasuryToggle();
			UE_LOG(LogTemp, Display, TEXT("CITY_PROBE: boston (enemy) open=%d; treasury %s"), MapWidget->IsCityViewOpen() ? 1 : 0,
				   *UPA250Subsystem::Get(this)->GetTreasuryJson());
		});
		FTimerHandle QuitHandle;
		GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda([this]() {
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), 30.f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PA250DeskProbe")))
	{
		// Staff-desk check in a real window: fights the first battle through the sim, then captures
		// ("Shot showui", first at 12 s) the commander's question (the one letter that stops the clock),
		// the desk button, the supply letter, the running clock with a source letter waiting, and that
		// letter's choices. Never saves.
		auto Step = [this](float At, TFunction<void()> Fn) {
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this, Fn]() {
				Fn();
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("Shot showui"));
			}), At, false);
		};
		auto Report = [this](const TCHAR* Stage) {
			UPA250Subsystem* PS = UPA250Subsystem::Get(this);
			if (!PS) return;
			const pa250::YMDH D = pa250::to_ymdh(PS->GetState().now);
			UE_LOG(LogTemp, Display,
				   TEXT("DESK_PROBE: %s | %02d-%02d %02dh paused=%d panel=%d timely=%d desk=%d gold=%d | letter='%s' choices=%d | chip='%s' | news='%s'"),
				   Stage, D.m, D.d, D.h, PS->IsPaused() ? 1 : 0, PS->IsStaffPanelOpen() ? 1 : 0,
				   PS->IsPendingLetterTimely() ? 1 : 0, PS->HasDeskLetter() ? 1 : 0, PS->GetState().player_gold,
				   *PS->GetPendingLetterSubject(), PS->GetPendingLetterChoiceCount(), *PS->GetDeskChipText(), *PS->GetLastDispatch());
		};
		Step(12.f, [this, Report]() {
			UPA250Subsystem* PS = UPA250Subsystem::Get(this);
			if (!PS) return;
			FString Err;
			for (int32 i = 0; i < 72 && !PS->IsBattlePending(); ++i) PS->AdvanceHours(1, Err);
			const FString Field = UTF8_TO_TCHAR(PS->GetState().pending_battle.province.c_str());
			const bool bBegun = PS->BeginBattle(Err);
			const bool bDone = bBegun && PS->ApplyBattleResultJson(FString::Printf(
				TEXT("{\"province_id\":\"%s\",\"winner\":\"continental\",\"objective_holder\":\"continental\",\"losses\":{\"continental\":{\"militia\":1},\"british\":{\"line_infantry\":2}}}"),
				*Field), Err);
			PS->AcknowledgeBattleReport();
			UE_LOG(LogTemp, Display, TEXT("DESK_PROBE: battle at %s begun=%d applied=%d %s"), *Field, bBegun ? 1 : 0, bDone ? 1 : 0, *Err);
			Report(TEXT("after battle: commander's question"));
		});
		Step(13.5f, [Report]() { Report(TEXT("commander's question, settled frame")); });
		Step(15.f, [this, Report]() {
			if (MapWidget) MapWidget->PressCommander(TEXT("preserve_force"));
			Report(TEXT("commander answered (the clock must be running again): desk button"));
			UPA250Subsystem* PS = UPA250Subsystem::Get(this);
			if (!PS) return;
			FString Err;
			// A ceasefire through the ordinary diplomacy path keeps the roads quiet, so the only
			// things that could stop the clock from here on are letters and deliveries. Talks pause it;
			// the probe holds it paused until the supply letter is answered.
			const bool bTalks = PS->BeginDiplomacy(TEXT("british"), Err);
			const bool bTruce = bTalks && PS->AcceptProposalJson(TEXT("{\"spoken_line\":\"probe\",\"clauses\":[{\"type\":\"ceasefire\",\"days\":30}]}"), Err);
			UE_LOG(LogTemp, Display, TEXT("DESK_PROBE: ceasefire talks=%d accepted=%d %s"), bTalks ? 1 : 0, bTruce ? 1 : 0, *Err);
		});
		Step(18.f, [this, Report]() {
			if (UPA250Subsystem* PS = UPA250Subsystem::Get(this)) PS->OpenDesk();
			Report(TEXT("desk opened: supply letter"));
		});
		Step(21.f, [this, Report]() {
			if (MapWidget) MapWidget->PressLogistics(TEXT("purchase"));
			if (UPA250Subsystem* PS = UPA250Subsystem::Get(this))
			{
				PS->SetSpeedIndex(4);  // 6 h/s: a day and a half before the next capture
				PS->SetPaused(false);
			}
			Report(TEXT("supply answered: clock started by the probe"));
		});
		Step(27.f, [this, Report]() { Report(TEXT("6 s of running clock: deliveries and letters must not pause it")); });
		Step(30.f, [this, Report]() {
			if (UPA250Subsystem* PS = UPA250Subsystem::Get(this)) PS->OpenDesk();
			Report(TEXT("desk opened: source letter"));
		});
		Step(31.5f, [Report]() { Report(TEXT("source letter, settled frame")); });
		Step(33.f, [this, Report]() {
			UPA250Subsystem* PS = UPA250Subsystem::Get(this);
			if (PS && MapWidget && PS->GetPendingLetterChoiceCount() > 0) MapWidget->PressLogistics(PS->GetPendingLetterChoiceId(0));
			Report(TEXT("first choice taken"));
		});
		FTimerHandle QuitHandle;
		GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda([this]() {
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), 37.f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PA250Shot")))
	{
		FTimerHandle ShotHandle;
		GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateLambda([this]() {
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("HighResShot 1920x1080"));
			}
			FTimerHandle QuitHandle;
			GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda([this]() {
				UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
			}),
										   2.f, false);
		}),
									   3.f, false);
	}
	else if (PA250Autoplay::IsAutoplay())
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunAutoplayCampaign), 0.7f,
										false);
	}
	else if (PA250Autoplay::SelftestName() == TEXT("campaign"))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunCampaignSelftest), 0.5f,
										false);
	}
}

void APA250CampaignGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FPA250BattleAudio::StopCampaignMusic();
	if (UPA250Subsystem* Sub = UPA250Subsystem::Get(this))
	{
		Sub->SetCampaignActive(false);
	}
	Super::EndPlay(EndPlayReason);
}

void APA250CampaignGameMode::RunCampaignSelftest()
{
	using namespace PA250Autoplay;
	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	Check(Sub, MapWidget != nullptr, TEXT("map widget created in L_Campaign"));
	Check(Sub, MapActor != nullptr, TEXT("province map actor spawned"));
	Check(Sub, MapWidget && !MapWidget->HasLegacyMapCanvas(), TEXT("no legacy Slate map canvas"));
	if (!Sub || !MapWidget)
	{
		Finish(Sub, TEXT("selftest_campaign"));
		return;
	}
	const pa250::CampaignState& S0 = Sub->GetState();
	Check(Sub, S0.provinces.size() == 13, TEXT("Boston 1775: thirteen provinces"));
	Check(Sub, S0.provinces.at("charlestown").owner == "continental", TEXT("Charlestown starts Provincial"));

	FString Err;
	Check(Sub, Sub->QueueMove(TEXT("continental_militia"), TEXT("menotomy"), Err), TEXT("path to menotomy"));
	Sub->AdvanceHours(24, Err);
	Check(Sub, Sub->GetState().armies.at("continental_militia").province == "menotomy" ||
				   !Sub->GetState().armies.at("continental_militia").path.empty() || Sub->IsBattlePending(),
		  TEXT("militia advancing or in contact"));
	Finish(Sub, TEXT("selftest_campaign"));
}

namespace
{
// Resolve every staff popup the way the map widget lets a player: notices are
// acknowledged, chapter briefings are acknowledged, the detached commander gets his
// preserve-force answer, Knox is declined, anything else is a purchase.
void ResolveAllStaffEvents(UPA250Subsystem* Sub)
{
	FString StaffErr;
	for (int32 Guard = 0; Sub->HasStaffEvent() && Guard < 16; ++Guard)
	{
		const FString Kind = Sub->GetPendingLetterKind();
		const FString LetterId = Sub->GetPendingLetterId();
		if (!Sub->IsPendingLetterAwaitingChoice() || Kind == TEXT("knox_support"))
		{
			Sub->ChooseLogistics(LetterId, TEXT("dismiss"), StaffErr);
		}
		else if (Kind == TEXT("commander_report") && Sub->GetPendingLetterSubject().Contains(TEXT("Detached")))
		{
			Sub->ResolveCommander(TEXT("preserve_force"), StaffErr);
		}
		else if (Kind == TEXT("commander_report"))
		{
			Sub->ChooseLogistics(LetterId, TEXT("dismiss"), StaffErr);
		}
		else if (Kind == TEXT("historical_notice"))
		{
			Sub->ChooseLogistics(LetterId, TEXT("dismiss"), StaffErr);
		}
		else if (Kind == TEXT("source_decision"))
		{
			const FString Sid = Sub->GetPendingLetterSourceId();
			FString Choice = TEXT("dismiss");
			if (Sid == TEXT("L3"))
			{
				Choice = TEXT("shared_lodging");
			}
			else if (Sid == TEXT("L5"))
			{
				Choice = TEXT("evacuate_preserve_boats");
			}
			else if (Sid == TEXT("L7"))
			{
				Choice = TEXT("conserve_ammunition");
			}
			if (!Sub->ChooseLogistics(LetterId, Choice, StaffErr))
			{
				Sub->ChooseLogistics(LetterId, TEXT("dismiss"), StaffErr);
			}
		}
		else if (!Sub->ChooseLogistics(LetterId, TEXT("purchase"), StaffErr))
		{
			Sub->ChooseLogistics(LetterId, TEXT("dismiss"), StaffErr);
		}
		if (StaffErr.Len() > 0)
		{
			break;
		}
	}
}
}  // namespace

void APA250CampaignGameMode::RunAutoplayCampaign()
{
	using namespace PA250Autoplay;
	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	if (!Sub || !MapWidget)
	{
		Finish(Sub, TEXT("autoplay"));
		return;
	}

	if (Sub->AutoplayStage == TEXT("battle_done"))
	{
		// Back on the map after a fight: the after-battle report must be up without a click.
		Check(Sub, Sub->HasUnseenBattleReport() && !MapWidget->BattleReportText().IsEmpty(),
			TEXT("aftermath: the after-battle report opens by itself on the map"));
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PA250/after_battle_report.png"));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Sub->AutoplayStage = TEXT("battle_report_shown");
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunAutoplayCampaign), 2.f, false);
		return;
	}
	if (Sub->AutoplayStage == TEXT("battle_report_shown"))
	{
		Sub->AcknowledgeBattleReport();
		// Resolve the aftermath staff letter so deliveries/events do not strand later hour advances.
		ResolveAllStaffEvents(Sub);
		Check(Sub, Sub->GetState().progress.lexington_fought || Sub->GetState().progress.battles_fought > 0,
			TEXT("aftermath: at least one battle recorded in campaign progress"));
		Sub->AutoplayStage = TEXT("diplomacy");
		FString Err;
		if (Sub->BeginDiplomacy(TEXT("british"), Err))
		{
			Sub->TravelToFlowLevel(this);
		}
		else
		{
			Finish(Sub, TEXT("autoplay"));
		}
		return;
	}
	if (Sub->AutoplayStage == TEXT("diplomacy_done"))
	{
		// Last leg: the validated ceasefire(3 days) holds for a day of hours, then save/load round-trips.
		const pa250::CampaignState& S = Sub->GetState();
		FString Err;
		Check(Sub, Sub->GetFlow() == EPA250Flow::Campaign, TEXT("after diplomacy: campaign flow"));
		Check(Sub, S.treaties.size() == 1 && S.treaties[0].type == "ceasefire" && S.treaties[0].hours_remaining == 72, TEXT("ceasefire(3 days) in force: 72 hours"));
		Check(Sub, S.provinces.at("boston").owner == "british", TEXT("Boston still British after the jailbreak line"));
		std::map<std::string, std::string> Before;
		for (const auto& A : S.armies) Before[A.first] = A.second.province;
		const int64 HourBefore = S.now.hour_index;
		// Staff popups pause the clock for a player; autoplay answers them and keeps
		// advancing, exactly as a player clicking through would.
		for (int32 Advanced = 0; Advanced < 24; ++Advanced)
		{
			ResolveAllStaffEvents(Sub);
			Sub->AdvanceHours(1, Err);
		}
		ResolveAllStaffEvents(Sub);
		Check(Sub, S.now.hour_index == HourBefore + 24, TEXT("24 hours advanced under the ceasefire"));
		// Ceasefire blocks entry into enemy-owned provinces; movement on friendly ground is still legal.
		bool bRespected = true;
		for (const auto& A : S.armies)
		{
			const auto Prev = Before.find(A.first);
			if (Prev == Before.end() || Prev->second == A.second.province) continue;
			if (S.provinces.at(A.second.province).owner != A.second.faction) bRespected = false;
		}
		Check(Sub, bRespected, TEXT("no army entered enemy ground under the ceasefire"));
		Check(Sub, !Sub->IsBattlePending(), TEXT("no new battle under the ceasefire"));
		Check(Sub, S.treaties.size() == 1 && S.treaties[0].hours_remaining < 72 && S.treaties[0].hours_remaining > 0, TEXT("ceasefire hours ticking down"));
		const FString Hash = Sub->GetStateHash();
		Check(Sub, Sub->SaveCampaign(Err), TEXT("save"));
		Check(Sub, Sub->LoadCampaign(Err), TEXT("load"));
		Check(Sub, Sub->GetStateHash() == Hash, TEXT("save/load round trip: identical state hash"));
		Finish(Sub, TEXT("autoplay"));
		return;
	}

	FString Err;
	Sub->QueueMove(TEXT("continental_militia"), TEXT("cambridge"), Err);
	for (int32 i = 0; i < 48 && !Sub->IsBattlePending(); ++i)
	{
		Sub->AdvanceHours(1, Err);
	}
	if (Sub->IsBattlePending())
	{
		Sub->AutoplayStage = TEXT("battle");
		MapWidget->PressFight();
		return;
	}
	Finish(Sub, TEXT("autoplay"));
}

static FVector2D UVOfStatic(const APA250ProvinceMapActor* Map, const FVector& W)
{
	FVector2D UV;
	Map->WorldToUV(W, UV);
	return UV;
}

void APA250CampaignGameMode::RunProbeDragFrame()
{
	APA250CampaignPlayerController* PC = Cast<APA250CampaignPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC || !MapActor)
	{
		GetWorldTimerManager().ClearTimer(DragTimer);
		return;
	}
	++DragFrame;
	const FVector2D Center = MapActor->GetZoomCenterUV();
	const FVector2D Step = Center - DragLastCenter;
	// A reversal = the view stepped against its previous direction (the shake).
	if (!DragLastStep.IsNearlyZero(1e-6f) && !Step.IsNearlyZero(1e-6f) && FVector2D::DotProduct(Step, DragLastStep) < 0.f)
	{
		++DragReversals;
	}
	if (!Step.IsNearlyZero(1e-6f))
	{
		DragLastStep = Step;
	}
	DragLastCenter = Center;
	const TSet<FKey> Lmb({EKeys::LeftMouseButton});
	if (DragFrame <= 30)
	{
		// 8,4 viewport px per frame up-left (absolute units = local * geometry scale).
		const FVector2D Prev = DragAbsStart - FVector2D(8.f, 4.f) * DragAbsPerLocal * static_cast<float>(DragFrame - 1);
		const FVector2D Abs = DragAbsStart - FVector2D(8.f, 4.f) * DragAbsPerLocal * static_cast<float>(DragFrame);
		FSlateApplication::Get().SetCursorPos(Abs);
		FSlateApplication::Get().ProcessMouseMoveEvent(FPointerEvent(0, Abs, Prev, Lmb, EKeys::Invalid, 0.f, FModifierKeysState()));
		return;
	}
	if (DragFrame <= 36)
	{
		return; // cursor held still: the view must settle, not keep moving
	}
	FVector2D Under;
	PC->ProbeCursorUV(Under);
	const float ErrPx = FVector2D::Distance(Under, DragGrabUV) * static_cast<float>(MapActor->GetImageWidthPx());
	const FVector2D Abs = DragAbsStart - FVector2D(8.f, 4.f) * DragAbsPerLocal * 30.f;
	FSlateApplication::Get().ProcessMouseButtonUpEvent(
		FPointerEvent(0, Abs, Abs, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	UE_LOG(LogTemp, Display,
		   TEXT("ART_PROBE: drag end center=(%.4f,%.4f) under_cursor=(%.4f,%.4f) grab=(%.4f,%.4f) grab_error=%.0f image px reversals=%d"),
		   Center.X, Center.Y, Under.X, Under.Y, DragGrabUV.X, DragGrabUV.Y, ErrPx, DragReversals);
	GetWorldTimerManager().ClearTimer(DragTimer);
}

void APA250CampaignGameMode::RunArtProbeStep()
{
	// Scripted map-art check. Fixed views (same in every build) so before/after shots compare directly.
	// It drives the clock only through AdvanceHours (the normal command path) and never saves.
	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	APA250CampaignPlayerController* PC = Cast<APA250CampaignPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Sub || !MapActor || !PC || !MapWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("ART_PROBE: missing sub/map/pc/widget"));
		GetWorldTimerManager().ClearTimer(ArtProbeTimer);
		return;
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/ArtProbe") / ArtProbeLabel);
	auto Shot = [this, Dir](const TCHAR* Name) {
		FTimerHandle H;
		const FString Path = Dir / FString(Name) + TEXT(".png");
		APA250ProvinceMapActor* Map = MapActor;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Path, Map]() {
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogTemp, Display, TEXT("ART_PROBE: shot %s"), *Path);
			if (IsValid(Map))
			{
				// Logged at capture time (after the map has ticked), not when the step changed state.
				UE_LOG(LogTemp, Display, TEXT("ART_PROBE: at shot zoom=%.2f tokens %s"), Map->GetZoomFactor(), *Map->DescribeArmyTokens());
			}
		}),
										1.0f, false);
	};
	auto StateLine = [Sub]() {
		const pa250::CampaignState& St = Sub->GetState();
		FString Out = Sub->FormatDate();
		for (const auto& Kv : St.armies)
		{
			Out += FString::Printf(TEXT(" | %s@%s path=%d"), UTF8_TO_TCHAR(Kv.first.c_str()), UTF8_TO_TCHAR(Kv.second.province.c_str()),
								   static_cast<int32>(Kv.second.path.size()));
		}
		if (St.pending_battle.active)
		{
			Out += FString::Printf(TEXT(" | battle@%s"), UTF8_TO_TCHAR(St.pending_battle.province.c_str()));
		}
		return Out;
	};
	auto UVOf = [this](const FVector& W) {
		FVector2D UV;
		MapActor->WorldToUV(W, UV);
		return UV;
	};
	const FVector2D LexView(0.29f, 0.215f);
	const FVector2D BostonView(0.575f, 0.50f);
	constexpr float CloseZoom = 4.5f;
	// -PA250ArtProbe=tooltip: overview, then straight to the hover steps (no clock advance, no battle).
	if (ArtProbeLabel == TEXT("tooltip") && ArtProbeStep == 1)
	{
		ArtProbeStep = 19;
	}
	if (ArtProbeLabel == TEXT("tooltip") && ArtProbeStep == 21)
	{
		ArtProbeStep = 100; // done
	}
	const int32 Step = ArtProbeStep++;
	UE_LOG(LogTemp, Display, TEXT("ART_PROBE: step %d state: %s"), Step, *StateLine());
	switch (Step)
	{
	case 0:
		for (const TCHAR* Path : {TEXT("/Game/PA250/Textures/Army/T_Army_ProvincialRegulars.T_Army_ProvincialRegulars"),
								  TEXT("/Game/PA250/Textures/Army/T_Army_BritishMarching.T_Army_BritishMarching"),
								  TEXT("/Game/PA250/Textures/Army/T_Army_BritishFiring.T_Army_BritishFiring"),
								  TEXT("/Game/PA250/Textures/Army/T_Army_Militia.T_Army_Militia"),
								  TEXT("/Game/PA250/Textures/Army/T_Army_GroundOval.T_Army_GroundOval")})
		{
			if (const UTexture2D* T = LoadObject<UTexture2D>(nullptr, Path))
			{
				UE_LOG(LogTemp, Display, TEXT("ART_PROBE: texture %s %dx%d format=%s mips=%d srgb=%d"), *T->GetName(), T->GetSizeX(),
					   T->GetSizeY(), GetPixelFormatString(T->GetPixelFormat()),
					   T->GetPlatformData() ? T->GetPlatformData()->Mips.Num() : -1, T->SRGB ? 1 : 0);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ART_PROBE: texture missing %s"), Path);
			}
		}
		MapActor->ResetOverview();
		Shot(TEXT("01_overview"));
		break;
	case 1:
		MapActor->SetView(LexView, CloseZoom);
		Shot(TEXT("02_lexington_close"));
		break;
	case 2:
	{
		FVector Tok;
		if (MapActor->GetArmyTokenLocation(TEXT("continental_militia"), Tok))
		{
			// Click a point on the drawn army (a little above the anchor: the figures stand on it).
			const FVector2D UV = UVOf(Tok + FVector(0.f, -6.f, 0.f));
			PC->ClickMapAtUV(UV.X, UV.Y);
		}
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: select militia -> selected=%s province=%s msg=\"%s\""), *MapWidget->GetSelectedArmyId(),
			   *MapWidget->GetSelectedProvinceId(), *MapWidget->GetLastMessage());
		Shot(TEXT("03_lexington_selected"));
		break;
	}
	case 3:
	{
		// Movement still works: with the militia selected, a town click queues a march; then cancel it.
		const FVector Lincoln = MapActor->GetLabelWorld(TEXT("lincoln"));
		const FVector2D UV = UVOf(Lincoln);
		PC->ClickMapAtUV(UV.X, UV.Y);
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: order lincoln -> msg=\"%s\" pending_commands=%d"), *MapWidget->GetLastMessage(),
			   static_cast<int32>(Sub->GetState().pending_commands.size()));
		Sub->ClearOrders();
		MapWidget->ClearSelection();
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: orders cleared pending_commands=%d selected=%s"),
			   static_cast<int32>(Sub->GetState().pending_commands.size()), *MapWidget->GetSelectedArmyId());
		break;
	}
	case 4:
	{
		// Hit tests: every town hotspot must resolve to its town, never to an army.
		int32 Stolen = 0;
		for (const FString& Id : MapActor->GetAllProvinceIds())
		{
			const FString A = MapActor->ArmyAtWorld(MapActor->GetLabelWorld(Id));
			if (!A.IsEmpty())
			{
				++Stolen;
				UE_LOG(LogTemp, Warning, TEXT("ART_PROBE: town %s hotspot centre hits army %s"), *Id, *A);
			}
		}
		FVector Tok;
		if (MapActor->GetArmyTokenLocation(TEXT("continental_militia"), Tok))
		{
			const FString OnArt = MapActor->ArmyAtWorld(Tok + FVector(0.f, -6.f, 0.f));
			const FString FarRight = MapActor->ArmyAtWorld(Tok + FVector(40.f, -30.f, 0.f));
			UE_LOG(LogTemp, Display, TEXT("ART_PROBE: hit on-art=%s off-art(+40,-30)=%s"), *OnArt, *FarRight);
		}
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: town hotspots stolen by army hit targets: %d"), Stolen);
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: tokens %s"), *MapActor->DescribeArmyTokens());
		MapActor->SetView(BostonView, CloseZoom);
		Shot(TEXT("04_boston_close"));
		break;
	}
	case 5:
	{
		FString Err;
		for (int32 i = 0; i < 12 && Sub->GetState().armies.at("british_regulars").path.empty(); ++i)
		{
			Sub->AdvanceHours(1, Err);
		}
		const pa250::Army& B = Sub->GetState().armies.at("british_regulars");
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: british marching=%d at %s; %s"), B.path.empty() ? 0 : 1, UTF8_TO_TCHAR(B.province.c_str()),
			   *StateLine());
		const FVector2D UV = UVOf(MapActor->GetLabelWorld(UTF8_TO_TCHAR(B.province.c_str())));
		MapActor->SetView(UV + FVector2D(0.015f, 0.01f), CloseZoom);
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: tokens %s"), *MapActor->DescribeArmyTokens());
		Shot(TEXT("05_british_marching"));
		break;
	}
	case 6:
	{
		FString Err;
		for (int32 i = 0; i < 96 && !Sub->IsBattlePending(); ++i)
		{
			Sub->AdvanceHours(1, Err);
		}
		const FString Bp = UTF8_TO_TCHAR(Sub->GetState().pending_battle.province.c_str());
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: battle pending=%d at %s; %s"), Sub->IsBattlePending() ? 1 : 0, *Bp, *StateLine());
		const FVector2D UV = Bp == TEXT("lexington") ? LexView : UVOf(MapActor->GetLabelWorld(Bp));
		MapActor->SetView(UV, CloseZoom);
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: tokens %s"), *MapActor->DescribeArmyTokens());
		Shot(TEXT("06_engaged"));
		break;
	}
	case 7:
		MapActor->ResetOverview();
		Shot(TEXT("07_overview_engaged"));
		break;
	case 8:
	{
		// Existing save still loads (read only; nothing is written).
		FString Err;
		const bool bLoaded = Sub->LoadCampaign(Err);
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: load existing save ok=%d err=\"%s\" hash=%s; %s"), bLoaded ? 1 : 0, *Err,
			   *Sub->GetStateHash(), *StateLine());
		MapActor->ResetOverview();
		Shot(TEXT("08_loaded_save_overview"));
		break;
	}
	case 9:
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: tokens %s"), *MapActor->DescribeArmyTokens());
		MapActor->SetView(LexView, 2.0f);
		Shot(TEXT("09_mid_zoom"));
		break;
	case 10:
		MapActor->SetView(LexView, CloseZoom);
		break;
	case 11:
	{
		// Drag through real Slate routing (capture/lock apply) with viewport focus, as in PIE after a click.
		TSharedPtr<SViewport> VPW = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
		if (!VPW.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("ART_PROBE: drag: no viewport widget"));
			break;
		}
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
		const FGeometry& Geo = VPW->GetCachedGeometry();
		DragAbsStart = Geo.LocalToAbsolute(Geo.GetLocalSize() * 0.5f);
		DragAbsPerLocal = Geo.Scale;
		DragFrame = 0;
		DragReversals = 0;
		DragLastStep = FVector2D::ZeroVector;
		DragLastCenter = MapActor->GetZoomCenterUV();
		FSlateApplication::Get().SetCursorPos(DragAbsStart);
		PC->ProbeCursorUV(DragGrabUV);
		FSlateApplication::Get().ProcessMouseButtonDownEvent(
			nullptr, FPointerEvent(0, DragAbsStart, DragAbsStart, TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0.f,
								   FModifierKeysState()));
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: drag start abs=(%.0f,%.0f) grab_uv=(%.4f,%.4f) center=(%.4f,%.4f)"), DragAbsStart.X,
			   DragAbsStart.Y, DragGrabUV.X, DragGrabUV.Y, DragLastCenter.X, DragLastCenter.Y);
		GetWorldTimerManager().SetTimer(DragTimer, FTimerDelegate::CreateUObject(this, &APA250CampaignGameMode::RunProbeDragFrame), 0.05f,
										true, 0.2f);
		break;
	}
	case 12:
	case 13:
	{
		// Arrow keys through real Slate routing: 12 = viewport focused (PIE after a click), 13 = HUD widget focused.
		const bool bWidgetFocus = Step == 13;
		if (bWidgetFocus)
		{
			MapWidget->SetKeyboardFocus();
		}
		else
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
		MapActor->SetView(LexView, CloseZoom);
		const FVector2D Start = MapActor->GetZoomCenterUV();
		const float PxPerUU = MapActor->MeasurePxPerUU();
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Right, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Down, FModifierKeysState(), 0, false, 0, 0));
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this, Start, PxPerUU, bWidgetFocus]() {
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Right, FModifierKeysState(), 0, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Down, FModifierKeysState(), 0, false, 0, 0));
			const FVector2D End = MapActor->GetZoomCenterUV();
			UE_LOG(LogTemp, Display, TEXT("ART_PROBE: arrows (%s focus) held 0.5s -> moved (%.0f, %.0f) screen px (expect about +450,+450)"),
				   bWidgetFocus ? TEXT("widget") : TEXT("viewport"), (End.X - Start.X) * MapActor->MapWidthUU * PxPerUU,
				   (End.Y - Start.Y) * MapActor->MapHeightUU * PxPerUU);
			FTimerHandle H2;
			GetWorldTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([this, End]() {
				UE_LOG(LogTemp, Display, TEXT("ART_PROBE: arrows released -> drift %.6f UV (expect 0)"),
					   FVector2D::Distance(MapActor->GetZoomCenterUV(), End));
			}),
											1.0f, false);
		}),
										0.5f, false);
		break;
	}
	case 14:
	{
		// Dead zone: shove far past the west clamp, then pan back a little; the view must respond at once.
		MapActor->SetView(LexView, 1.6f);
		MapActor->PanByUV(FVector2D(-1.f, 0.f));
		const FVector2D AtClamp = MapActor->GetZoomCenterUV();
		MapActor->PanByUV(FVector2D(0.01f, 0.f));
		const FVector2D Back = MapActor->GetZoomCenterUV();
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: clamp centre.x=%.4f, after +0.01 pan %.4f (moved %.4f, expect 0.0100)"), AtClamp.X, Back.X,
			   Back.X - AtClamp.X);
		break;
	}
	case 15:
	{
		// March sequence on a fresh scenario (in memory; nothing is saved): wait for the Regulars to set off.
		FString Err;
		Sub->NewCampaign(Err);
		Sub->SetPaused(true);
		for (int32 i = 0; i < 12 && Sub->GetState().armies.at("british_regulars").path.empty(); ++i)
		{
			Sub->AdvanceHours(1, Err);
		}
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: march sequence start; %s"), *StateLine());
		break;
	}
	case 16:
	case 17:
	case 18:
	{
		// One campaign hour per step; view centred between the column's start town and its next town.
		FString Err;
		Sub->AdvanceHours(1, Err);
		const pa250::CampaignState& St = Sub->GetState();
		const pa250::Army& B = St.armies.at("british_regulars");
		FVector2D From = UVOf(MapActor->GetLabelWorld(UTF8_TO_TCHAR(B.province.c_str())));
		FVector2D To = From;
		if (!B.path.empty())
		{
			const auto E = St.edges.find(B.path.front());
			if (E != St.edges.end())
			{
				const std::string Next = E->second.a == B.province ? E->second.b : E->second.a;
				To = UVOf(MapActor->GetLabelWorld(UTF8_TO_TCHAR(Next.c_str())));
			}
		}
		MapActor->SetView((From + To) * 0.5f, CloseZoom);
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: march hour %d; hours_into_edge=%d; %s"), Step - 15, B.hours_into_edge, *StateLine());
		// Sample the drawn column for 0.6 s: it should glide to the new sim position, never jump.
		for (int32 k = 1; k <= 12; ++k)
		{
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this, k]() {
				FVector L;
				if (MapActor && MapActor->GetArmyTokenLocation(TEXT("british_regulars"), L))
				{
					UE_LOG(LogTemp, Display, TEXT("ART_PROBE: column t+%.2fs at (%.1f,%.1f)"), 0.05f * k, L.X, L.Y);
				}
			}),
											0.05f * k, false);
		}
		const FString Name = FString::Printf(TEXT("%d_march_hour%d"), Step, Step - 15);
		Shot(*Name);
		break;
	}
	case 19:
	case 20:
	{
		// Hover tooltip through real Slate mouse moves: 19 = your militia, 20 = Boston (a town).
		const bool bArmy = Step == 19;
		FVector Target = MapActor->GetLabelWorld(TEXT("boston"));
		if (bArmy)
		{
			MapActor->GetArmyTokenLocation(TEXT("continental_militia"), Target);
			Target += FVector(0.f, -6.f, 0.f); // on the drawn counter/figures, above the ground anchor
		}
		MapActor->SetView(UVOf(Target) + FVector2D(0.03f, 0.02f), CloseZoom);
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this, PC, Target, bArmy]() {
			FVector2D Px;
			TSharedPtr<SViewport> VPW = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
			if (!VPW.IsValid() || !PC->ProjectWorldLocationToScreen(Target, Px))
			{
				UE_LOG(LogTemp, Error, TEXT("ART_PROBE: hover: cannot project target"));
				return;
			}
			const FGeometry& Geo = VPW->GetCachedGeometry();
			const FVector2D Abs = Geo.LocalToAbsolute(Px / Geo.Scale);
			FSlateApplication::Get().SetCursorPos(Abs);
			FSlateApplication::Get().ProcessMouseMoveEvent(FPointerEvent(0, Abs, Abs - FVector2D(2.f, 2.f), TSet<FKey>(), EKeys::Invalid, 0.f,
																		 FModifierKeysState()));
			const FVector W = MapActor->UVToWorld(UVOfStatic(MapActor, Target), 2.f);
			UE_LOG(LogTemp, Display, TEXT("ART_PROBE: hover %s at px (%.0f,%.0f): \"%s\""), bArmy ? TEXT("army") : TEXT("town"), Px.X, Px.Y,
				   *MapActor->DescribeHover(bArmy ? MapActor->ArmyAtWorld(W) : FString(), bArmy ? FString() : MapActor->ProvinceAtWorld(W))
						.Replace(TEXT("\n"), TEXT(" / ")));
		}),
										0.4f, false);
		Shot(bArmy ? TEXT("19_tooltip_army") : TEXT("20_tooltip_town"));
		break;
	}
	default:
		UE_LOG(LogTemp, Display, TEXT("ART_PROBE: done"));
		GetWorldTimerManager().ClearTimer(ArtProbeTimer);
		PC->ConsoleCommand(TEXT("QUIT"));
		break;
	}
}
