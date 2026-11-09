#include "PA250CampaignPlayerController.h"
#include "PA250CampaignGameMode.h"
#include "PA250MapWidget.h"
#include "PA250ProvinceMapActor.h"
#include "PA250Subsystem.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"

APA250CampaignPlayerController::APA250CampaignPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void APA250CampaignPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyCampaignInputMode();
	bShowMouseCursor = true;
}

void APA250CampaignPlayerController::ApplyCampaignInputMode()
{
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	if (UGameViewportClient* GVC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		if (!bSavedViewportInput)
		{
			SavedCaptureMode = GVC->GetMouseCaptureMode();
			SavedLockMode = GVC->GetMouseLockMode();
			bSavedViewportInput = true;
		}
		GVC->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
		GVC->SetMouseLockMode(EMouseLockMode::DoNotLock);
		UE_LOG(LogTemp, Display, TEXT("PA250 input: campaign capture=CaptureDuringMouseDown lock=DoNotLock hide_during_capture=0 (saved capture=%d lock=%d)"),
			   static_cast<int32>(SavedCaptureMode), static_cast<int32>(SavedLockMode));
	}
}

void APA250CampaignPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Leave the viewport as the battle expects it (project defaults / whatever was set before).
	if (bSavedViewportInput)
	{
		if (UGameViewportClient* GVC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			GVC->SetMouseCaptureMode(SavedCaptureMode);
			GVC->SetMouseLockMode(SavedLockMode);
			UE_LOG(LogTemp, Display, TEXT("PA250 input: campaign end, restored capture=%d lock=%d"), static_cast<int32>(SavedCaptureMode),
				   static_cast<int32>(SavedLockMode));
		}
	}
	Super::EndPlay(EndPlayReason);
}

void APA250CampaignPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &APA250CampaignPlayerController::OnLeftPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &APA250CampaignPlayerController::OnLeftReleased);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &APA250CampaignPlayerController::OnRightClick);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &APA250CampaignPlayerController::OnMiddlePressed);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &APA250CampaignPlayerController::OnMiddleReleased);
	InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &APA250CampaignPlayerController::OnWheelAxis);
}

bool APA250CampaignPlayerController::CursorMapUV(FVector2D& OutUV) const
{
	APA250ProvinceMapActor* Map = FindMap();
	if (!Map)
	{
		return false;
	}
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, true, Hit) || !Hit.bBlockingHit)
	{
		return false;
	}
	return Map->WorldToUV(Hit.ImpactPoint, OutUV);
}

namespace
{
UPA250MapWidget* CampaignWidget(const APlayerController* PC)
{
	if (!PC || !PC->GetWorld())
	{
		return nullptr;
	}
	if (APA250CampaignGameMode* GM = Cast<APA250CampaignGameMode>(PC->GetWorld()->GetAuthGameMode()))
	{
		return GM->MapWidget;
	}
	return nullptr;
}
bool OverHud(const APlayerController* PC, FVector2D Pos)
{
	UPA250MapWidget* W = CampaignWidget(PC);
	return W && W->IsPointOverHud(Pos);
}
} // namespace

void APA250CampaignPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	APA250ProvinceMapActor* Map = FindMap();
	if (!Map)
	{
		return;
	}

	float MX = 0.f;
	float MY = 0.f;
	GetMousePosition(MX, MY);
	const FVector2D CurScreen(MX, MY);

	FVector2D CurUV;
	const bool bHasUV = CursorMapUV(CurUV);

	// Screen-space drag: the view centre = centre at press minus cursor travel since press, at the
	// scale measured when the drag began. Nothing is ray-cast through the moving camera, so the
	// camera's own movement can never feed back as cursor motion (the old per-frame UV delta did,
	// and the view flipped back and forth: the drag "shake").
	auto DragTo = [this, Map, CurScreen](const FVector2D& PressScreen) {
		if (!bDragAnchored || !FMath::IsNearlyEqual(DragAnchorZoom, Map->GetZoomFactor(), 1e-3f))
		{
			// Anchor at the press; re-anchor here if the wheel changed zoom mid-drag.
			const bool bReanchor = bDragAnchored;
			bDragAnchored = true;
			DragAnchorScreen = bReanchor ? CurScreen : PressScreen;
			DragAnchorCenterUV = Map->GetZoomCenterUV();
			DragAnchorPxPerUU = Map->MeasurePxPerUU();
			DragAnchorZoom = Map->GetZoomFactor();
		}
		if (DragAnchorPxPerUU > 0.f)
		{
			const FVector2D D = CurScreen - DragAnchorScreen;
			Map->SetCenterUV(DragAnchorCenterUV - FVector2D(D.X / (DragAnchorPxPerUU * Map->MapWidthUU),
															D.Y / (DragAnchorPxPerUU * Map->MapHeightUU)));
		}
	};
	bool bPanning = false;
	if (bMmbDown)
	{
		bPanning = true;
		DragTo(LastCursorScreen);
	}
	else if (bLmbDown && !bPressOverHud)
	{
		const float Dist = FVector2D::Distance(CurScreen, LmbPressScreen);
		if (!bLmbDragging && Dist >= DragThresholdPx)
		{
			bLmbDragging = true;
		}
		if (bLmbDragging)
		{
			bPanning = true;
			DragTo(LmbPressScreen);
		}
	}
	if (!bPanning)
	{
		bDragAnchored = false;
		DragAnchorZoom = 0.f;
	}

	// Arrow keys: constant screen speed at any zoom (measured scale, so window size and DPI don't matter).
	if (!bPanning)
	{
		// Keys reach player input when the viewport has focus (PIE after a click), and the HUD widget
		// when it has focus (it consumes them); accept either.
		const UPA250MapWidget* W = CampaignWidget(this);
		FVector2D Axis = W ? W->GetArrowPanAxis() : FVector2D::ZeroVector;
		Axis.X += (IsInputKeyDown(EKeys::Right) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::Left) ? 1.f : 0.f);
		Axis.Y += (IsInputKeyDown(EKeys::Down) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::Up) ? 1.f : 0.f);
		Axis = FVector2D(FMath::Clamp(Axis.X, -1.f, 1.f), FMath::Clamp(Axis.Y, -1.f, 1.f));
		const float PxPerUU = Map->MeasurePxPerUU();
		if (!Axis.IsZero() && PxPerUU > 0.f)
		{
			const float Px = ArrowPanPxPerSecond * DeltaTime;
			Map->PanByUV(FVector2D(Axis.X * Px / (PxPerUU * Map->MapWidthUU), Axis.Y * Px / (PxPerUU * Map->MapHeightUU)));
		}
	}

	LastCursorScreen = CurScreen;
	if (bHasUV && !bPanning)
	{
		LastCursorUV = CurUV;
	}
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


void APA250CampaignPlayerController::OnLeftPressed()
{
	bLmbDown = true;
	bLmbDragging = false;
	float MX = 0.f;
	float MY = 0.f;
	GetMousePosition(MX, MY);
	LmbPressScreen = FVector2D(MX, MY);
	bPressOverHud = OverHud(this, LmbPressScreen);
	FVector2D UV;
	if (CursorMapUV(UV))
	{
		LastCursorUV = UV;
	}
}

void APA250CampaignPlayerController::OnLeftReleased()
{
	const bool bWasDrag = bLmbDragging;
	const bool bWasOverHud = bPressOverHud;
	bLmbDown = false;
	bLmbDragging = false;
	bPressOverHud = false;
	if (bWasDrag || bWasOverHud)
	{
		return; // drags pan; HUD clicks must never fall through to map orders
	}
	float MX = 0.f;
	float MY = 0.f;
	GetMousePosition(MX, MY);
	if (OverHud(this, FVector2D(MX, MY)))
	{
		return;
	}
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
	// The round counter is the army. Prefer it over the town ring under the cursor
	// so a click on your force selects it and the next town click is the march order.
	const FString ArmyId = Map->ArmyAtWorld(Hit.ImpactPoint);
	const FString ArmyProv = Map->ProvinceForArmy(ArmyId);
	const FString Pid = !ArmyProv.IsEmpty() ? ArmyProv : Map->ProvinceAtWorld(Hit.ImpactPoint);
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

void APA250CampaignPlayerController::OnMiddlePressed()
{
	bMmbDown = true;
	FVector2D UV;
	if (CursorMapUV(UV))
	{
		LastCursorUV = UV;
	}
}

void APA250CampaignPlayerController::OnMiddleReleased()
{
	bMmbDown = false;
}

void APA250CampaignPlayerController::OnWheelAxis(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	float MX = 0.f;
	float MY = 0.f;
	GetMousePosition(MX, MY);
	if (OverHud(this, FVector2D(MX, MY)))
	{
		return;
	}
	if (APA250ProvinceMapActor* Map = FindMap())
	{
		const float Factor = Value > 0.f ? 1.25f : 1.f / 1.25f;
		FVector2D UV;
		if (CursorMapUV(UV))
		{
			Map->ZoomAtUV(UV, Factor); // cursor-centred
		}
		else
		{
			Map->ZoomBy(Factor);
		}
	}
}

void APA250CampaignPlayerController::ClickMapAtUV(float U, float V)
{
	APA250ProvinceMapActor* Map = FindMap();
	if (!Map)
	{
		return;
	}
	const FVector World = Map->UVToWorld(FVector2D(U, V), 2.f);
	const FString ArmyProv = Map->ProvinceForArmy(Map->ArmyAtWorld(World));
	const FString Pid = !ArmyProv.IsEmpty() ? ArmyProv : Map->ProvinceAtWorld(World);
	if (!Pid.IsEmpty())
	{
		Map->HandleProvinceClick(Pid);
	}
}
