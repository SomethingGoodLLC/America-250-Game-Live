// PA250FactionVisuals.h — stopgap coat/facing/hat/nameplate colors per faction.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PA250FactionVisuals.generated.h"

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250FactionLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FLinearColor CoatColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FLinearColor FacingColor = FLinearColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FLinearColor NameplateColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FName HatStyle = TEXT("tricorn");
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UPA250FactionVisuals : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Looks live in the constructor: DA_FactionVisuals is created from the CDO. */
	UPA250FactionVisuals();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	FPA250FactionLook British;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	FPA250FactionLook Continental;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	FPA250FactionLook Militia;

	const FPA250FactionLook& LookFor(FName Faction, FName UnitClass) const;
	static UPA250FactionVisuals* MakeRuntimeDefaults();
};
