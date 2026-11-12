// RFHealthComponent.h — ported from the recovered RevWarUnreal sources (health + OnDied).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RFHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDied, AActor*, Victim);
/** A hit the man survived: damage applied and where it came from. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWounded, float, const FVector&);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PLAYAMERICA250_API URFHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URFHealthComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	float MaxHealth = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 100.f;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDied OnDied;

	FOnWounded OnWounded;

	/** Controller that dealt the killing blow (valid inside OnDied). */
	UPROPERTY(BlueprintReadOnly, Category = "Health")
	TObjectPtr<AController> LastInstigator;

	/** World location of the last damage event (for direction wedge / ragdoll impulse). */
	UPROPERTY(BlueprintReadOnly, Category = "Health")
	FVector LastDamageLocation = FVector::ZeroVector;

	/** True after at least one damage event this life. */
	UPROPERTY(BlueprintReadOnly, Category = "Health")
	bool bHasLastDamageLocation = false;

	/** One-shot: the next damage event cannot take health below this (then it resets to 0). Set by the
	 *  melee component for frontal blows, after every multiplier, so a full-health man is never run through in one. */
	float MinHealthAfterNextDamage = 0.f;

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return CurrentHealth <= 0.f; }

	UFUNCTION()
	void OnTakeAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
	                     class AController* InstigatedBy, AActor* DamageCauser);

protected:
	virtual void BeginPlay() override;
};
