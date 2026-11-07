// MusketWeapon.h — single-shot muzzle-loader that spawns an AMusketProjectile.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusketTuning.h"
#include "MusketWeapon.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class USoundBase;
class AMusketProjectile;

UENUM(BlueprintType)
enum class EMusketState : uint8
{
	Loaded,
	Reloading
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMusketStateChanged, EMusketState, NewState);

UCLASS()
class PLAYAMERICA250_API AMusketWeapon : public AActor
{
	GENERATED_BODY()

public:
	AMusketWeapon();

	/** Fire along the muzzle's forward vector, or along `Direction` if non-zero. Returns true if a ball left the barrel. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	bool Fire(FVector Direction = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartReload();

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool CanFire() const { return State == EMusketState::Loaded; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EMusketState GetState() const { return State; }

	/** 0..1 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetReloadProgress() const;

	const FMusketTuningValues& GetTuning() const { return Tuning ? Tuning->Values : DefaultTuning; }

	UStaticMeshComponent* GetMusketMesh() const { return MusketMesh; }
	USceneComponent* GetMuzzleLocation() const { return MuzzleLocation; }

	/** Optional shared tuning asset (reload time, speed, damage). Falls back to DefaultTuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UMusketTuning> Tuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FMusketTuningValues DefaultTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<AMusketProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<USoundBase> FireSound;

	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnMusketStateChanged OnStateChanged;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> MusketMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USceneComponent> MuzzleLocation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	EMusketState State = EMusketState::Loaded;

	FTimerHandle ReloadTimerHandle;

	UFUNCTION()
	void OnReloadComplete();

	void SetState(EMusketState NewState);
};
