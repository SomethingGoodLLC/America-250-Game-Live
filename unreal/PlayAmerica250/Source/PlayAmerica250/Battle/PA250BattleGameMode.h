// PA250BattleGameMode.h — Conquest ticket battle driven by the sim's BattleSetup; ends with a BattleResult.
#pragma once

#include "CoreMinimal.h"
#include "RFGameMode.h"
#include "pa250/types.hpp"
#include "PA250BattleGameMode.generated.h"

class APA250ControlPoint;
class ARFCharacter;
class UMusketTuning;
class UPA250BattleHUD;
class UPA250SpawnScreen;
class UPA250VolleyCoordinator;

UCLASS()
class PLAYAMERICA250_API APA250BattleGameMode : public ARFGameMode
{
	GENERATED_BODY()

public:
	APA250BattleGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	int32 BotsPerSide = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float TimeLimitSeconds = 720.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float EndDelaySeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float AutoplayTimeDilation = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float BleedIntervalSeconds = 12.f;

	/** Battle respawns per campaign unit (see FConquestTuning::SoldiersPerUnit). */
	UPROPERTY(EditAnywhere, Category = "PA250|Battle")
	int32 SoldiersPerUnit = 4;

	// ---- HUD / spawn queries ----
	int32 TicketsRemaining(FName Faction) const;
	int32 TicketsRemaining(FName Faction, FName UnitClass) const;
	float TimeRemaining() const;
	FName GetPlayerFaction() const { return PlayerFaction; }
	/** Plurality post owner, or NAME_None on tie / none. */
	FName GetObjectiveHolder() const;
	int32 PostsOwned(FName Faction) const;
	const TArray<TWeakObjectPtr<APA250ControlPoint>>& GetPosts() const;
	bool IsBattleOver() const { return bBattleOver; }
	FString GetBannerText() const { return Banner; }
	const pa250::BattleResult& GetResult() const { return Result; }
	bool IsAutoplayMode() const { return bAutoplayBots; }
	UMusketTuning* GetTuning() const;
	UPA250VolleyCoordinator* GetVolleys() const { return Volleys; }
	void NotifyShot(ARFCharacter* Shooter, const FVector& Dir, bool bHit);

	/** Player or autoplay: spend a ticket and spawn at the chosen post/base. */
	bool RequestSpawn(AController* Controller, FName UnitClass, APA250ControlPoint* PostOrNull);

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
	void ConfigureBattleCamera();
	void ApplyLowCinematicSpectator(APlayerController* PC, ARFCharacter* ViewSoldier);
	void SpectateNextSoldier(APlayerController* PC, const AActor* Exclude);
	void FrameHumanCamera(APlayerController* PC, APawn* P);
	APA250ControlPoint* FindGreenPost() const;
	void PollPreviewFile(const FString& SavedPath, const FString& Dest, int32 Index, int32 Total, int32 Attempt);
	void MaybeStartPreviewCapture();
	void MaybeStartFpsSample();
	void GatherPosts();
	void CapturePreviewShot(const FString& Tag, const FString& OutStem, int32 Index, int32 Total);

	FString PreviewLabel;
	bool bFpsSampling = false;
	float FpsSampleAccum = 0.f;
	float FpsSampleTime = 0.f;
	int32 FpsSampleFrames = 0;
	double FpsGameMs = 0.0;
	double FpsRenderMs = 0.0;
	double FpsGpuMs = 0.0;
	void SpawnBots(FName Faction, int32 Count);
	ARFCharacter* SpawnBot(FName Faction, FName UnitClass, APA250ControlPoint* PreferredPost);
	void ConfigureSoldier(ARFCharacter* Soldier, FName Faction, FName UnitClass);
	FName ChooseClassFor(FName Faction, FName Preferred) const;
	bool TrySpendTicket(FName Faction, FName UnitClass);
	void BleedTickets(float DeltaSeconds);
	int32 PoolTotal(FName Faction) const;
	int32 CountAlive(FName Faction, FName UnitClass) const;
	AActor* PickStart(FName Faction) const;
	FTransform PickSpawnTransform(FName Faction, APA250ControlPoint* PreferredPost) const;
	void ShowSpawnScreen(APlayerController* PC);
	void EndBattle(FName Winner, const FString& Reason);
	void FinishAndReturn();
	void WriteBattleFile(const FString& Reason) const;
	FName PluralityHolder() const;
	FName OtherFaction(FName F) const { return F == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")); }
	void CheckTicketExhaustion();

	bool bInitialized = false;
	bool bBattleOver = false;
	bool bAutoplayBots = false;
	bool bSelftest = false;
	bool bDebugBattle = false;
	float StartTime = 0.f;
	float BleedAccum = 0.f;
	int32 BleedTicks = 0;
	int32 PostFlips = 0;
	FString Banner;
	FString EndReason;
	FName PlayerFaction;
	FName PendingPlayerClass = TEXT("line_infantry");
	TWeakObjectPtr<APA250ControlPoint> PendingPlayerPost;

	pa250::BattleSetup Setup;
	pa250::BattleResult Result;
	TMap<FName, TMap<FName, int32>> InitialPool;
	TMap<FName, TMap<FName, int32>> Pool;
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> Starts;
	TMap<TWeakObjectPtr<AController>, FName> ControllerFaction;
	TMap<TWeakObjectPtr<AController>, FName> ControllerClass;
	TArray<TWeakObjectPtr<APA250ControlPoint>> Posts;
	UPROPERTY(Transient)
	TObjectPtr<UMusketTuning> Tuning;
	UPROPERTY(Transient)
	TObjectPtr<UPA250BattleHUD> Hud;
	UPROPERTY(Transient)
	TObjectPtr<UPA250SpawnScreen> SpawnScreen;
	UPROPERTY(Transient)
	TObjectPtr<UPA250VolleyCoordinator> Volleys;
};
