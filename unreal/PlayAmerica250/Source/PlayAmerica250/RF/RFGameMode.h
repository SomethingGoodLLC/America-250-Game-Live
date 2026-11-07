// RFGameMode.h — base game mode: spawns RFCharacters and respawns players after a delay.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RFGameMode.generated.h"

class ARFCharacter;

UCLASS()
class PLAYAMERICA250_API ARFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARFGameMode();

	/** Seconds between a death and the replacement spawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Respawn")
	float RespawnDelay = 3.f;

	/** Called by ARFCharacter::BeginPlay so the mode can observe deaths. */
	void RegisterCharacter(ARFCharacter* Character);

protected:
	/** A registered character died. Default: respawn its controller after RespawnDelay. */
	virtual void OnCharacterDied(ARFCharacter* Victim, AController* VictimController, AController* Killer);

	UFUNCTION()
	void HandleCharacterDied(AActor* Victim);

	/** Respawn `Controller` (spawns a new pawn at a spawn point and possesses it). */
	UFUNCTION()
	virtual void RespawnController(AController* Controller);

	void ScheduleRespawn(AController* Controller, float Delay);

	/** Where `Controller` should respawn; nullptr falls back to any PlayerStart. */
	virtual AActor* ChooseRespawnPoint(AController* Controller);

	/** Pawn class to spawn on respawn for this controller. */
	virtual TSubclassOf<APawn> ChooseRespawnPawnClass(AController* Controller);

	/** Called after a respawn pawn is possessed. */
	virtual void OnControllerRespawned(AController* Controller, APawn* NewPawn) {}

private:
	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;
};
