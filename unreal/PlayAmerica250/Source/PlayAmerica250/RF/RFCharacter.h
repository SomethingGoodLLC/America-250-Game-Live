// RFCharacter.h — third-person musket infantryman. Fires an AMusketWeapon (projectile); hitscan is retired.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "RFCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputMappingContext;
class UInputAction;
class URFHealthComponent;
class AMusketWeapon;
class UStaticMeshComponent;

UCLASS()
class PLAYAMERICA250_API ARFCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARFCharacter();

	// ----- Team / class (set by the battle game mode from the sim's BattleSetup) -----
	/** "british" or "continental". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	FName FactionId = NAME_None;

	/** Sim unit class this soldier draws a ticket from ("line_infantry", "militia"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	FName UnitClass = TEXT("line_infantry");

	// ----- Components -----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	TObjectPtr<URFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Team")
	TObjectPtr<UStaticMeshComponent> TeamMarker;

	// ----- Weapon -----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSubclassOf<AMusketWeapon> MusketClass;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AMusketWeapon> Musket;

	/** Player fire: aims from the camera through the crosshair. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool FireMusket();

	/** AI fire: aims at a world location. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool FireMusketAt(FVector TargetLocation);

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintCallable, Category = "Team")
	void SetTeam(FName InFaction, FName InUnitClass);

	// ----- Input assets (optional; defaults are built at runtime so no editor setup is needed) -----
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> IMC_ThirdPerson;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Move;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Look;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Jump;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Sprint;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Aim;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Fire;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Reload;

	/** Extra accuracy multiplier from formation (1 = normal, 0.8 = tighter spread). */
	float FormationAccuracyScale = 1.f;
	float SuppressionSwayDegrees = 0.f;
	float SuppressionTimer = 0.f;
	bool bInLine = false;
	int32 LineSize = 0;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAiming() const { return bAiming; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInLine() const { return bInLine; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetLineSize() const { return LineSize; }

	void ApplySuppression(float Seconds, float SwayDegrees);

	UPROPERTY(EditAnywhere, Category = "Camera")
	float DefaultFOV = 90.f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	float AimedFOV = 65.f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	float FOVInterpSpeed = 20.f;
	UPROPERTY(EditAnywhere, Category = "Movement")
	float WalkSpeed = 450.f;
	UPROPERTY(EditAnywhere, Category = "Movement")
	float SprintSpeed = 650.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Destroyed() override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void BuildDefaultInput();
	void SpawnMusket();
	void ApplyTeamMarker();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void StartAim();
	void StopAim();
	void OnFirePressed();
	void OnReloadStarted();
	void ApplyThirdPartySoldierMesh();
	void OnReloadCompleted();

	UFUNCTION()
	void OnDeath(AActor* Victim);

private:
	bool bAiming = false;
	bool bIsDead = false;
	float CurrentFOV = 90.f;
	float TargetFOV = 90.f;
};
