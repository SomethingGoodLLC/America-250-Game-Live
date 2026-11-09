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
	MarkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mark"));
	MarkMesh->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Disc(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Bar(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlagMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Disc.Succeeded())
	{
		FlagMesh->SetStaticMesh(Disc.Object);
		if (FlagMat.Succeeded())
		{
			FlagMesh->SetMaterial(0, FlagMat.Object);
		}
		// Round counter. Towns are diamond rings; this must not be another diamond
		// or a flat rectangle. Cylinder stands in Z, so a top-down camera sees a circle.
		// Scale stays large enough to pass the framing check (X >= 0.5, Y >= 0.35).
		FlagMesh->SetRelativeScale3D(FVector(0.55f, 0.55f, 0.04f));
		FlagMesh->SetRelativeLocation(FVector::ZeroVector);
		FlagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (Bar.Succeeded())
	{
		MarkMesh->SetStaticMesh(Bar.Object);
		if (FlagMat.Succeeded())
		{
			MarkMesh->SetMaterial(0, FlagMat.Object);
		}
		MarkMesh->SetRelativeScale3D(FVector(0.32f, 0.08f, 0.04f));
		MarkMesh->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
		MarkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	NameText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Name"));
	NameText->SetupAttachment(Root);
	NameText->SetRelativeLocation(FVector(0.f, 0.f, -34.f));
	NameText->SetHorizontalAlignment(EHTA_Center);
	NameText->SetVerticalAlignment(EVRTA_TextCenter);
	NameText->SetWorldSize(24.f);
	NameText->SetTextRenderColor(FColor(20, 12, 4));
	NameText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	StrengthText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Strength"));
	StrengthText->SetupAttachment(Root);
	StrengthText->SetRelativeLocation(FVector(0.f, 0.f, -58.f));
	StrengthText->SetHorizontalAlignment(EHTA_Center);
	StrengthText->SetVerticalAlignment(EVRTA_TextCenter);
	StrengthText->SetWorldSize(16.f);
	StrengthText->SetTextRenderColor(FColor(28, 20, 10));
	StrengthText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void APA250ArmyToken::Configure(const FString& InArmyId, const FString& Faction, const FString& DisplayName,
								const FString& StrengthLabel, int32 StackIndex, bool bPlayerForce, bool bSelected)
{
	ArmyId = InArmyId;
	// A round counter with a bar is an army. Towns stay hollow diamonds.
	// The side word is always printed: colour is not the only cue.
	// No collision: the map plane receives the click, and the map hit-tests this actor.
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
	NameText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StrengthText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Absolute world transform: the token actor is pitched for its text, which
	// would squash a relative cylinder into a sliver and move it off the click spot.
	const FVector DiscScale = bSelected ? FVector(0.68f, 0.68f, 0.05f) : FVector(0.55f, 0.55f, 0.04f);
	const FVector Here = GetActorLocation();
	FlagMesh->SetAbsolute(true, true, true);
	FlagMesh->SetWorldLocation(Here + FVector(0.f, 0.f, 1.f));
	FlagMesh->SetWorldRotation(FRotator::ZeroRotator);
	FlagMesh->SetWorldScale3D(DiscScale);
	FlagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MarkMesh)
	{
		MarkMesh->SetAbsolute(true, true, true);
		MarkMesh->SetWorldLocation(Here + FVector(0.f, 0.f, 3.f));
		MarkMesh->SetWorldRotation(FRotator::ZeroRotator);
		MarkMesh->SetWorldScale3D(bSelected ? FVector(0.40f, 0.09f, 0.05f) : FVector(0.32f, 0.08f, 0.04f));
		MarkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FLinearColor Bar = FLinearColor(0.93f, 0.88f, 0.74f);
		if (UMaterialInstanceDynamic* BarMid = MarkMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			BarMid->SetVectorParameterValue(TEXT("Color"), Bar);
			BarMid->SetVectorParameterValue(TEXT("BaseColor"), Bar);
		}
	}
	SetActorScale3D(FVector::OneVector);

	// Bright faction colour on the disc. No flag or insignia is claimed as authentic.
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
