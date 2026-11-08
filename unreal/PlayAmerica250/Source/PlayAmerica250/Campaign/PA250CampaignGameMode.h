// PA250CampaignGameMode.h — L_Campaign: map actor + HUD; autoplay/selftests; -PA250Shot.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PA250CampaignGameMode.generated.h"

class UPA250MapWidget;
class APA250ProvinceMapActor;

UCLASS()
class PLAYAMERICA250_API APA250CampaignGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APA250CampaignGameMode();

	UPROPERTY(Transient)
	TObjectPtr<UPA250MapWidget> MapWidget;

	UPROPERTY(Transient)
	TObjectPtr<APA250ProvinceMapActor> MapActor;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RunCampaignSelftest();
	void RunAutoplayCampaign();
};
