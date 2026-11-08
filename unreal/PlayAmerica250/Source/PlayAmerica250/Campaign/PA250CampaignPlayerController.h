// PA250CampaignPlayerController.h — map picking + clock hotkeys for L_Campaign.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PA250CampaignPlayerController.generated.h"

class APA250ProvinceMapActor;
class UPA250MapWidget;

UCLASS()
class PLAYAMERICA250_API APA250CampaignPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APA250CampaignPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnLeftClick();
	void OnRightClick();
	APA250ProvinceMapActor* FindMap() const;
};
