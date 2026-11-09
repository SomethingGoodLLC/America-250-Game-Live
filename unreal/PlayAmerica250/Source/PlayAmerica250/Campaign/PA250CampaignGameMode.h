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
	/** -PA250ArtProbe=<label>: fixed views + scripted clicks + screenshots for map art checks. Never saves. */
	void RunArtProbeStep();
	FString ArtProbeLabel;
	int32 ArtProbeStep = 0;
	FTimerHandle ArtProbeTimer;
	/** Scripted left-drag (real handlers, real cursor): checks the grabbed map point stays under the cursor. */
	void RunProbeDragFrame();
	FTimerHandle DragTimer;
	int32 DragFrame = 0;
	FVector2D DragGrabUV = FVector2D::ZeroVector;
	FVector2D DragLastCenter = FVector2D::ZeroVector;
	FVector2D DragLastStep = FVector2D::ZeroVector;
	int32 DragReversals = 0;
	FVector2D DragAbsStart = FVector2D::ZeroVector;
	float DragAbsPerLocal = 1.f;
};
