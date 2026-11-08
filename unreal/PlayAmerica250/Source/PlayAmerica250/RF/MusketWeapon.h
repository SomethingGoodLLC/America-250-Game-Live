// MusketWeapon.h — single-shot muzzle-loader with active reload and spread.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ActiveReloadSequencer.h"
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
	Reloading,
	Jammed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMusketStateChanged, EMusketState, NewState);

UCLASS()
class PLAYAMERICA250_API AMusketWeapon : public AActor
{
	GENERATED_BODY()

public:
	AMusketWeapon();

	/** Fire along Direction (or muzzle forward). Applies spread from context. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	bool Fire(FVector Direction = FVector::ZeroVector, bool bIsAiming = false, float OwnerSpeed = 0.f, float AccuracyScale = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartReload();

	EActiveReloadHit CommitReload();

	void SetReloadHeld(bool bHeld) { bReloadHeld = bHeld; }

	/** Bot: auto-commit phases using skill. */
	void TickBotReload(float Skill01, FRandomStream& Rng);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool CanFire() const { return State == EMusketState::Loaded; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsJammed() const { return State == EMusketState::Jammed || Sequencer.IsJammed(); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const { return State == EMusketState::Reloading || Sequencer.IsReloading(); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EMusketState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetReloadProgress() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	FString GetReloadPhaseLabel() const { return Sequencer.PhaseLabel(); }

	float GetSweetStart() const { return Sequencer.GetSweetStart(); }
	float GetSweetEnd() const { return Sequencer.GetSweetEnd(); }
	float GetPerfectStart() const { return Sequencer.GetPerfectStart(); }
	float GetPerfectEnd() const { return Sequencer.GetPerfectEnd(); }

	const FActiveReloadStats& GetReloadStats() const { return Sequencer.GetStats(); }
	FActiveReloadSequencer& GetSequencer() { return Sequencer; }

	const FMusketTuningValues& GetTuning() const { return Tuning ? Tuning->Values : DefaultTuning; }

	UStaticMeshComponent* GetMusketMesh() const { return MusketMesh; }
	USceneComponent* GetMuzzleLocation() const { return MuzzleLocation; }

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
	void SpawnMuzzleSmoke(const FVector& Location, const FRotator& Rotation) const;
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> MusketMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USceneComponent> MuzzleLocation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	EMusketState State = EMusketState::Loaded;

	void SetState(EMusketState NewState);
	void SyncTuningToSequencer();
	void SyncStateFromSequencer();

	FActiveReloadSequencer Sequencer;
	bool bReloadHeld = false;
	float BotCommitCooldown = 0.f;
};
