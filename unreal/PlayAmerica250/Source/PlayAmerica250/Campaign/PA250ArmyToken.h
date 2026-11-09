#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250ArmyToken.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class PLAYAMERICA250_API APA250ArmyToken : public AActor
{
	GENERATED_BODY()
public:
	APA250ArmyToken();
	void Configure(const FString& InArmyId, const FString& Faction, const FString& DisplayName, const FString& StrengthLabel,
			   int32 StackIndex, bool bPlayerForce, bool bSelected);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FlagMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StrengthText;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> NameText;

	UPROPERTY()
	FString ArmyId;
};
