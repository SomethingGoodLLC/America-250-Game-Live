#include "PA250CampaignGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Campaign/PA250Autoplay.h"
#include <map>
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
#include "PA250ProvinceMapActor.h"
#include "TimerManager.h"

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
		PC->SetInputMode(FInputModeGameAndUI());
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
