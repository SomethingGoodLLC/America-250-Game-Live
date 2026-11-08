// PA250BattleGameMode.h — Conquest ticket battle driven by the sim's BattleSetup; ends with a BattleResult.
#pragma once

#include "CoreMinimal.h"
#include "RFGameMode.h"
#include "PA250TicketLedger.h"
#include "pa250/types.hpp"
#include "PA250BattleGameMode.generated.h"

class APA250ControlPoint;
class ARFCharacter;
class UMusketTuning;
class UPA250BattleHUD;
class UPA250SpawnScreen;
class UPA250VolleyCoordinator;
class UPA250WeaponTuning;
class UPA250BotTuning;
class UPA250MeleeTuning;
class UPA250FactionVisuals;
class UPA250CombatProfiles;

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
	float BleedIntervalSeconds = 5.f;

	/** Battle respawns per campaign unit (see FConquestTuning::SoldiersPerUnit). */
	UPROPERTY(EditAnywhere, Category = "PA250|Battle")
	int32 SoldiersPerUnit = 4;

	/** Active-reload preset: Authentic (15-45 s) is the permanent default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	uint8 ReloadPreset = 1; // 1 = Authentic (15-45 s), the permanent default; 0 = Arcade (~10 s)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float DeathCamSeconds = 3.f;

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
	UPA250WeaponTuning* GetWeaponTuning() const { return WeaponTuningAsset; }
	UPA250BotTuning* GetBotTuning() const { return BotTuningAsset; }
	UPA250FactionVisuals* GetFactionVisuals() const { return FactionVisualsAsset; }
	UPA250VolleyCoordinator* GetVolleys() const { return Volleys; }
	void NotifyShot(ARFCharacter* Shooter, const FVector& Dir, bool bHit);
	void NotifyProjectileHit(ARFCharacter* Shooter, ARFCharacter* Victim, float Damage);
	void PushKillFeed(const FString& Line);
	/** Bots report each finished reload (seconds, fumbles) by training profile for the battle trace. */
	void RecordAIReload(FName Training, float Seconds, int32 Fumbles);
	/** Where a routing squad runs: the side's spawn area, else Fallback. */
	FVector GetRearPoint(FName Faction, const FVector& Fallback) const;
	/** Militia muster state for tests / HUD: men still to arrive and the cap on militia in the field. */
	int32 GetMusterCap(FName Faction) const { return MusterCap.FindRef(Faction); }
	/** Pure muster plan: of Count places, how many non-militia, how many militia at the start, and the
	 *  cap on militia alive once companies have arrived. */
	static void MusterPlan(int32 Count, int32 MilitiaUnits, int32 TotalUnits, float InitialFraction, float MaxAliveScale,
		int32& OutOthers, int32& OutInitialMilitia, int32& OutMilitiaCap);
	/** Pure: men in the next company - never past the cap, never more men in the field than tickets left. */
	static int32 MusterWave(int32 MilitiaCap, int32 MilitiaAlive, int32 TicketsLeft, int32 SideAlive, int32 WaveSize);
	UPA250CombatProfiles* GetCombatProfiles() const { return CombatProfilesAsset; }
	const TArray<FString>& GetKillFeed() const { return KillFeed; }
	int32 GetBattleResultEmitCount() const { return BattleResultEmitCount; }
	/** Deaths so far, both sides (FTEST_BotSpacing kill criterion). */
	int32 GetTotalDeaths() const
	{
		int32 N = 0;
		for (const auto& FK : Ledger.Deaths) for (const auto& CK : FK.Value) N += CK.Value;
		return N;
	}

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
	bool CanSpawn(FName Faction) const;
	void SpendDeathTicket(FName Faction, FName UnitClass);
	void BleedTickets(float DeltaSeconds);
	int32 PoolTotal(FName Faction) const;
	int32 CountAlive(FName Faction, FName UnitClass) const;
	AActor* PickStart(FName Faction) const;
	FTransform PickSpawnTransform(FName Faction, APA250ControlPoint* PreferredPost) const;
	void ShowSpawnScreen(APlayerController* PC);
	void CloseSpawnScreenToSpectate(APlayerController* PC);
	void BeginDeathCamThenSpawn(APlayerController* PC);
	void EndBattle(FName Winner, const FString& Reason);
	void FinishAndReturn();
	void WriteBattleFile(const FString& Reason) const;
	FName PluralityHolder() const;
	FName OtherFaction(FName F) const { return F == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")); }
	void CheckTicketExhaustion();
	void RegisterConsoleCommands();

	bool bInitialized = false;
	bool bBattleOver = false;
	bool bAutoplayBots = false;
	bool bSelftest = false;
	bool bDebugBattle = false;
	float StartTime = 0.f;
	int32 PostFlips = 0;
	float SpacingReportTimer = 5.f;
	FString Banner;
	FString EndReason;
	FName PlayerFaction;
	FName PendingPlayerClass = TEXT("line_infantry");
	TWeakObjectPtr<APA250ControlPoint> PendingPlayerPost;

	pa250::BattleSetup Setup;
	pa250::BattleResult Result;
	TMap<FName, TMap<FName, int32>> InitialPool;
	/** Remaining tickets (death + bleed) and per-class deaths. HUD shows Ledger.Pool. */
	FPA250TicketLedger Ledger;
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> Starts;
	TMap<TWeakObjectPtr<AController>, FName> ControllerFaction;
	TMap<TWeakObjectPtr<AController>, FName> ControllerClass;
	TArray<TWeakObjectPtr<APA250ControlPoint>> Posts;
	TArray<FString> KillFeed;
	int32 BattleResultEmitCount = 0;
	UPROPERTY(Transient)
	TObjectPtr<UMusketTuning> Tuning;
	UPROPERTY(Transient)
	TObjectPtr<UPA250WeaponTuning> WeaponTuningAsset;
	UPROPERTY(Transient)
	TObjectPtr<UPA250BotTuning> BotTuningAsset;
	UPROPERTY(Transient)
	TObjectPtr<UPA250MeleeTuning> MeleeTuningAsset;
	UPROPERTY(Transient)
	TObjectPtr<UPA250FactionVisuals> FactionVisualsAsset;

	UPROPERTY()
	TObjectPtr<UPA250CombatProfiles> CombatProfilesAsset;

	int32 BotProfileSerial = 0;
	TMap<FName, int32> MusterCap;              // max militia alive for a side (0 = no muster)
	TMap<FName, float> MusterTimer;
	TMap<FName, int32> MusterArrivals;
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> MusterPoints;
	void SpawnInitialForces(FName Faction, int32 Count);
	void TickMuster(float DeltaSeconds);
	ARFCharacter* SpawnBotAt(FName Faction, FName UnitClass, const FTransform& At);
	TMap<FName, TArray<float>> AIReloadSeconds;
	TMap<FName, int32> AIReloadFumbles;
	FString ReloadByTrainingJson() const;
	UPROPERTY(Transient)
	TObjectPtr<UPA250BattleHUD> Hud;
	UPROPERTY(Transient)
	TObjectPtr<UPA250SpawnScreen> SpawnScreen;
	UPROPERTY(Transient)
	TObjectPtr<UPA250VolleyCoordinator> Volleys;
};
