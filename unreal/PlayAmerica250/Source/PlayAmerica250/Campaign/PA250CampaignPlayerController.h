// PA250CampaignPlayerController.h — map picking + pan/zoom + clock hotkeys for L_Campaign.
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

	/** Place cursor over a UV and run the same click path as a mouse press (probe/tests). */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void ClickMapAtUV(float U, float V);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnLeftPressed();
	void OnLeftReleased();
	void OnRightClick();
	void OnMiddlePressed();
	void OnMiddleReleased();
	void OnWheelAxis(float Value);
	APA250ProvinceMapActor* FindMap() const;
	bool CursorMapUV(FVector2D& OutUV) const;

	bool bLmbDown = false;
	bool bLmbDragging = false;
	bool bPressOverHud = false;
	bool bMmbDown = false;
	FVector2D LmbPressScreen = FVector2D::ZeroVector;
	FVector2D LastCursorScreen = FVector2D::ZeroVector;
	FVector2D LastCursorUV = FVector2D(0.5f, 0.5f);
	static constexpr float DragThresholdPx = 5.f;
};
