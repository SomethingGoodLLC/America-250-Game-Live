#include "PA250CampaignPlayerController.h"
#include "PA250CampaignGameMode.h"
#include "PA250MapWidget.h"
#include "PA250ProvinceMapActor.h"
#include "PA250Subsystem.h"
#include "EngineUtils.h"
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

	if (bMmbDown && bHasUV)
	{
		const FVector2D DeltaUV = LastCursorUV - CurUV;
		Map->PanByUV(DeltaUV);
		LastCursorUV = CurUV;
	}
	else if (bLmbDown && !bPressOverHud)
	{
		const float Dist = FVector2D::Distance(CurScreen, LmbPressScreen);
		if (!bLmbDragging && Dist >= DragThresholdPx)
		{
			bLmbDragging = true;
		}
		if (bLmbDragging && bHasUV)
		{
			const FVector2D DeltaUV = LastCursorUV - CurUV;
			Map->PanByUV(DeltaUV);
			LastCursorUV = CurUV;
		}
	}

	LastCursorScreen = CurScreen;
	if (bHasUV)
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
	const FString Pid = Map->ProvinceAtWorld(World);
	if (!Pid.IsEmpty())
	{
		Map->HandleProvinceClick(Pid);
	}
}
