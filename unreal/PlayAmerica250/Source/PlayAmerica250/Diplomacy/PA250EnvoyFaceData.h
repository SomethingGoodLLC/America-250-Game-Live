// PA250EnvoyFaceData.h — per-envoy face regions for the talking painting (M_PortraitWarp). Display only.
// Positions are pixels of the Still texture; the material warps the painting's own pixels around them.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PA250EnvoyFaceData.generated.h"

class UTexture2D;

UCLASS(BlueprintType)
class PLAYAMERICA250_API UPA250EnvoyFaceData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The portrait (eyes open) and the same portrait with the eyes closed (only the eyes differ). */
	UPROPERTY(EditAnywhere, Category = "Face")
	TObjectPtr<UTexture2D> Still;
	UPROPERTY(EditAnywhere, Category = "Face")
	TObjectPtr<UTexture2D> Blink;

	/** Still texture size in pixels (all positions below use it). */
	UPROPERTY(EditAnywhere, Category = "Face")
	FVector2D ImageSize = FVector2D(1920, 1088);
	/** Share of the image width shown in the 3:4 frame (centred). */
	UPROPERTY(EditAnywhere, Category = "Face")
	float CropWidth = 0.42f;

	/** Middle of the lip seam, half the mouth width, and the lips' thickness above / below the seam. */
	UPROPERTY(EditAnywhere, Category = "Mouth")
	FVector2D MouthCenter = FVector2D(970, 628);
	UPROPERTY(EditAnywhere, Category = "Mouth")
	float MouthHalfWidth = 89.f;
	UPROPERTY(EditAnywhere, Category = "Mouth")
	float UpperLip = 16.f;
	UPROPERTY(EditAnywhere, Category = "Mouth")
	float LowerLip = 22.f;
	/** Bottom of the chin and half the jaw width: the region that drops when the jaw opens. */
	UPROPERTY(EditAnywhere, Category = "Mouth")
	FVector2D Chin = FVector2D(970, 765);
	UPROPERTY(EditAnywhere, Category = "Mouth")
	float JawHalfWidth = 130.f;
	/** Largest jaw drop in pixels (JawOpen = 1). */
	UPROPERTY(EditAnywhere, Category = "Mouth")
	float MaxJawDrop = 40.f;

	UPROPERTY(EditAnywhere, Category = "Eyes")
	FVector2D EyeLeft = FVector2D(877, 424);
	UPROPERTY(EditAnywhere, Category = "Eyes")
	FVector2D EyeRight = FVector2D(1065, 417);
	/** Half size of each eye opening in pixels. */
	UPROPERTY(EditAnywhere, Category = "Eyes")
	FVector2D EyeRadius = FVector2D(40, 22);
	/** Head region (centre and radii) that drifts and nods. */
	UPROPERTY(EditAnywhere, Category = "Head")
	FVector2D HeadCenter = FVector2D(970, 520);
	UPROPERTY(EditAnywhere, Category = "Head")
	FVector2D HeadRadius = FVector2D(230, 400);
};
