// PA250AimComponent.h — camera→aim then muzzle→aim dual-trace (projectile still deals damage).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PA250AimComponent.generated.h"

class UCameraComponent;
class USceneComponent;
class ARFCharacter;

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250AimResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	FVector CameraAimPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	FVector MuzzleHitPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	FVector FireDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	bool bBlocked = false;

	/** Camera ray hit geometry. False means CameraAimPoint is just max range along the ray
	 *  (sky / horizon), which must not be used as a drop-compensation distance. */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	bool bCameraHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	bool bHostileUnderReticle = false;

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float SpreadDegrees = 0.f;
};

UCLASS(ClassGroup = (PA250), meta = (BlueprintSpawnableComponent))
class PLAYAMERICA250_API UPA250AimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPA250AimComponent();

	/** Pure dual-trace used by FireMusket and PA250.Battle.Aim. */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	static FPA250AimResult ResolveAim(
		UWorld* World,
		const FVector& CameraLocation,
		const FVector& CameraDirection,
		const FVector& MuzzleLocation,
		float SpreadDegrees,
		float MaxRangeCm,
		const AActor* IgnoreActor,
		const AActor* IgnoreMuzzleOwner,
		FName FriendlyFaction = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Aim")
	FPA250AimResult ResolveFromOwner(float SpreadDegrees, float MaxRangeCm = 20000.f);

	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	FPA250AimResult LastResult;
};
