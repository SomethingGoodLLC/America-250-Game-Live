#include "PA250ArmyToken.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

APA250ArmyToken::APA250ArmyToken()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FlagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flag"));
	FlagMesh->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlagMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Plane.Succeeded())
	{
		FlagMesh->SetStaticMesh(Plane.Object);
		if (FlagMat.Succeeded())
		{
			// Plane's default slot renders as grey checker; tint via this material.
			FlagMesh->SetMaterial(0, FlagMat.Object);
		}
		// Tall pennant / flag rectangle. The token actor is pitched flat for its
		// text, which would turn this plane face-down and cull it; keep the flag's
		// world rotation absolute so it lies in the map XY plane facing +Z.
		FlagMesh->SetRelativeScale3D(FVector(0.65f, 0.45f, 1.f));
		FlagMesh->SetAbsolute(false, false, true);
		FlagMesh->SetWorldRotation(FRotator::ZeroRotator);
		// North of the anchor (token local -Z is screen south, so +Z is north),
		// clear of the province label that often shares the anchor.
		FlagMesh->SetRelativeLocation(FVector(0.f, 0.f, 38.f));
	}

	NameText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Name"));
	NameText->SetupAttachment(Root);
	NameText->SetRelativeLocation(FVector(0.f, 0.f, -34.f));
	NameText->SetHorizontalAlignment(EHTA_Center);
	NameText->SetVerticalAlignment(EVRTA_TextCenter);
	NameText->SetWorldSize(24.f);
	NameText->SetTextRenderColor(FColor(20, 12, 4));

	StrengthText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Strength"));
	StrengthText->SetupAttachment(Root);
	StrengthText->SetRelativeLocation(FVector(0.f, 0.f, -58.f));
	StrengthText->SetHorizontalAlignment(EHTA_Center);
	StrengthText->SetVerticalAlignment(EVRTA_TextCenter);
	StrengthText->SetWorldSize(16.f);
	StrengthText->SetTextRenderColor(FColor(28, 20, 10));
}

void APA250ArmyToken::Configure(const FString& InArmyId, const FString& Faction, const FString& DisplayName,
								const FString& StrengthLabel, int32 StackIndex, bool bSelected)
{
	ArmyId = InArmyId;
	NameText->SetText(FText::FromString(DisplayName));
	NameText->SetTextRenderColor(bSelected ? FColor(150, 20, 10) : FColor(20, 12, 4));
	NameText->SetWorldSize(bSelected ? 30.f : 24.f);
	StrengthText->SetText(FText::FromString(StrengthLabel));
	// Re-assert after spawn/registration: constructor-time world rotation can be
	// reset before the component registers, which left the flag face-down.
	FlagMesh->SetAbsolute(false, false, true);
	FlagMesh->SetWorldRotation(FRotator::ZeroRotator);
	FlagMesh->SetRelativeScale3D(bSelected ? FVector(0.85f, 0.6f, 1.f) : FVector(0.65f, 0.45f, 1.f));
	SetActorScale3D(FVector::OneVector * (bSelected ? 1.25f : 1.f));

	// British: pre-1801 Union (FACT) — blue field stand-in (no St Patrick saltire).
	// Provincial: plain buff pennant (GAME INVENTION; no standard Provincial flag at Lexington).
	const bool bBrit = Faction.Equals(TEXT("british"), ESearchCase::IgnoreCase);
	const FLinearColor Color = bBrit ? FLinearColor(0.05f, 0.12f, 0.48f) : FLinearColor(0.72f, 0.42f, 0.06f);
	if (UMaterialInterface* Base = FlagMesh->GetMaterial(0))
	{
		if (UMaterialInstanceDynamic* MID = FlagMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
	}
	else
	{
		FlagMesh->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(Color));
	}
	(void)StackIndex; // caller applies world stacking offset
}
