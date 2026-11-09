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
								const FString& StrengthLabel, int32 StackIndex, bool bPlayerForce, bool bSelected)
{
	ArmyId = InArmyId;
	// A solid rectangular flag is an army. Towns are hollow diamonds, so the two
	// never share a shape. The side word is always printed: colour is not the only cue.
	const bool bBrit = Faction.Equals(TEXT("british"), ESearchCase::IgnoreCase);
	const FString Side = bPlayerForce ? TEXT("Yours") : (bBrit ? TEXT("British") : Faction);
	const FColor Ink = bBrit ? FColor(145, 24, 18) : FColor(16, 48, 130);
	NameText->SetText(FText::FromString(DisplayName));
	NameText->SetTextRenderColor(Ink);
	NameText->SetWorldSize(bSelected ? 24.f : 20.f);
	const FString Plate = bSelected ? (Side + TEXT(" · ") + StrengthLabel) : Side;
	StrengthText->SetText(FText::FromString(Plate));
	StrengthText->SetTextRenderColor(Ink);
	StrengthText->SetWorldSize(bSelected ? 16.f : 18.f);
	StrengthText->SetVisibility(true);
	// Re-assert after spawn/registration: constructor-time world rotation can be
	// reset before the component registers, which left the flag face-down.
	// Keep the constructor's large flag (0.65 x 0.45). A smaller runtime scale
	// disappeared into the town diamonds.
	FlagMesh->SetAbsolute(false, false, true);
	FlagMesh->SetWorldRotation(FRotator::ZeroRotator);
	FlagMesh->SetRelativeScale3D(bSelected ? FVector(0.78f, 0.54f, 1.f) : FVector(0.65f, 0.45f, 1.f));
	FlagMesh->SetRelativeLocation(FVector(0.f, 0.f, 36.f));
	SetActorScale3D(FVector::OneVector);

	// Bright faction colour on the flag; the printed side word carries the same fact.
	// No flag or insignia is claimed as authentic.
	const FLinearColor Color = bBrit ? FLinearColor(0.82f, 0.14f, 0.11f) : FLinearColor(0.12f, 0.34f, 0.82f);
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
