// PA250CampaignPlayerController.h — map picking + pan/zoom + clock hotkeys for L_Campaign.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Engine/EngineBaseTypes.h"
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

	/**
	 * Campaign input: cursor visible and unlocked while a button is held, captured only during the press.
	 * The project defaults (CapturePermanently_IncludingInitialMouseDown + LockOnCapture, cursor hidden
	 * during capture) suit the battle's mouse look but froze the cursor here, so drag-to-pan never
	 * tracked. The previous viewport capture/lock modes are restored in EndPlay.
	 */
	void ApplyCampaignInputMode();

	/** Probe/tests: left press/release through the real handlers; the caller places the cursor. */
	void ProbeLeftPress() { OnLeftPressed(); }
	void ProbeLeftRelease() { OnLeftReleased(); }
	bool ProbeCursorUV(FVector2D& OutUV) const { return CursorMapUV(OutUV); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
	static constexpr float ArrowPanPxPerSecond = 900.f;
	/** Hover tooltip for towns and armies; shown after a short still hover. */
	UPROPERTY(Transient)
	TObjectPtr<class UPA250MapTooltip> MapTooltip;
	FString HoverKey;
	double HoverSince = 0.0;
	static constexpr double HoverDelaySeconds = 0.25;
	/** Screen-space drag anchor: press position, view centre and scale at the start of the drag. */
	bool bDragAnchored = false;
	FVector2D DragAnchorScreen = FVector2D::ZeroVector;
	FVector2D DragAnchorCenterUV = FVector2D::ZeroVector;
	float DragAnchorPxPerUU = 0.f;
	float DragAnchorZoom = 0.f;
	bool bSavedViewportInput = false;
	EMouseCaptureMode SavedCaptureMode = EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
	EMouseLockMode SavedLockMode = EMouseLockMode::LockOnCapture;
};
