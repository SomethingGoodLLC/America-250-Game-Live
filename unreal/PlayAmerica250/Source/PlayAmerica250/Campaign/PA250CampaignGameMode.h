// PA250CampaignGameMode.h — L_Campaign: shows the map widget and runs the campaign phase of autoplay/selftests.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PA250CampaignGameMode.generated.h"

class UPA250MapWidget;

UCLASS()
class PLAYAMERICA250_API APA250CampaignGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APA250CampaignGameMode();

	UPROPERTY(Transient)
	TObjectPtr<UPA250MapWidget> MapWidget;

protected:
	virtual void BeginPlay() override;

private:
	void RunCampaignSelftest();
	void RunAutoplayCampaign();
};
