#include "PA250CampaignPlayerController.h"
#include "PA250ProvinceMapActor.h"
#include "PA250Subsystem.h"
#include "EngineUtils.h"

APA250CampaignPlayerController::APA250CampaignPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void APA250CampaignPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameAndUI());
	bShowMouseCursor = true;
}

void APA250CampaignPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &APA250CampaignPlayerController::OnLeftClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &APA250CampaignPlayerController::OnRightClick);
}

void APA250CampaignPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
}

APA250ProvinceMapActor* APA250CampaignPlayerController::FindMap() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	TActorIterator<APA250ProvinceMapActor> It(GetWorld());
	return It ? *It : nullptr;
}

void APA250CampaignPlayerController::OnLeftClick()
{
	APA250ProvinceMapActor* Map = FindMap();
	if (!Map)
	{
		return;
	}
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, true, Hit) || !Hit.bBlockingHit)
	{
		return;
	}
	const FString Pid = Map->ProvinceAtWorld(Hit.ImpactPoint);
	if (!Pid.IsEmpty())
	{
		Map->HandleProvinceClick(Pid);
	}
}

void APA250CampaignPlayerController::OnRightClick()
{
	if (APA250ProvinceMapActor* Map = FindMap())
	{
		Map->HandleRightClick();
	}
}
