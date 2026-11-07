// PA250BattleGameMode.h — Battlefront-style ticket battle driven by the sim's BattleSetup; ends with a BattleResult.
#pragma once

#include "CoreMinimal.h"
#include "RFGameMode.h"
#include "pa250/types.hpp"
#include "PA250BattleGameMode.generated.h"

class APA250ControlPoint;
class ARFCharacter;
class UMusketTuning;
class UPA250BattleHUD;

UCLASS()
class PLAYAMERICA250_API APA250BattleGameMode : public ARFGameMode
{
	GENERATED_BODY()

public:
	APA250BattleGameMode();

	/** AI-controlled soldiers per side (default 12). Override with -PA250Bots=N. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	int32 BotsPerSide = 12;

	/** Game-time limit; reaching it ends the battle by ticket count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float TimeLimitSeconds = 240.f;

	/** Holding the objective this many seconds wins the battle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float ObjectiveHoldSeconds = 75.f;

	/** Real/game seconds between the end of the fight and the return to the campaign. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float EndDelaySeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float AutoplayTimeDilation = 10.f;

	// ---- HUD queries ----
	int32 TicketsRemaining(FName Faction) const;
	float TimeRemaining() const;
	FName GetPlayerFaction() const { return PlayerFaction; }
	FName GetObjectiveHolder() const;
	float GetObjectiveHold() const;
	bool IsBattleOver() const { return bBattleOver; }
	FString GetBannerText() const { return Banner; }
	const pa250::BattleResult& GetResult() const { return Result; }

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnCharacterDied(ARFCharacter* Victim, AController* VictimController, AController* Killer) override;
	virtual AActor* ChooseRespawnPoint(AController* Controller) override;
	virtual void OnControllerRespawned(AController* Controller, APawn* NewPawn) override;

private:
	void EnsureInitialized();
	void EnsureVisibleLighting();
	void SpawnBots(FName Faction, int32 Count);
	ARFCharacter* SpawnBot(FName Faction, FName UnitClass);
	void ConfigureSoldier(ARFCharacter* Soldier, FName Faction, FName UnitClass);
	FName ChooseClassFor(FName Faction, FName Preferred) const;
	void SpendTicket(FName Faction, FName UnitClass);
	int32 PoolTotal(FName Faction) const;
	AActor* PickStart(FName Faction) const;
	void EndBattle(FName Winner, const FString& Reason);
	void FinishAndReturn();
	void WriteBattleFile(const FString& Reason) const;
	FName OtherFaction(FName F) const { return F == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")); }

	bool bInitialized = false;
	bool bBattleOver = false;
	bool bAutoplayBots = false;
	bool bSelftest = false;
	bool bDebugBattle = false;
	float StartTime = 0.f;
	FString Banner;
	FString EndReason;
	FName PlayerFaction;

	pa250::BattleSetup Setup;
	pa250::BattleResult Result;
	TMap<FName, TMap<FName, int32>> InitialPool;
	TMap<FName, TMap<FName, int32>> Pool;
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> Starts;
	TMap<TWeakObjectPtr<AController>, FName> ControllerFaction;
	TMap<TWeakObjectPtr<AController>, FName> ControllerClass;
	TWeakObjectPtr<APA250ControlPoint> Objective;
	UPROPERTY(Transient)
	TObjectPtr<UMusketTuning> Tuning;
	UPROPERTY(Transient)
	TObjectPtr<UPA250BattleHUD> Hud;
};
