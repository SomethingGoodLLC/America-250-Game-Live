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

/** Rules read from BattleSetup.scenario_preset and terrain_tags. The Common and Battle Road fights
 *  are on L_Lexington; the bridge fight is on L_Concord (UPA250Subsystem::BattleLevelForPreset). */
struct FPA250ScenarioFight
{
	bool bContestSingleCrossing = false;
	bool bRewardForcePreservation = false;
	FName CrossingPostId = TEXT("green");
};

/** A plank span across a creek. The deck is longer than the water is wide, so it reaches both banks. */
struct FPA250CreekBridge
{
	FVector CreekCenter = FVector::ZeroVector;
	FVector DeckCenter = FVector::ZeroVector;
	/** Full sizes in centimeters. X crosses the creek; Y runs along it. */
	FVector CreekSizeCm = FVector(900.f, 3600.f, 50.f);
	FVector DeckSizeCm = FVector(1200.f, 420.f, 28.f);

	bool SpansCreek() const
	{
		return DeckSizeCm.X > CreekSizeCm.X && DeckSizeCm.Y < CreekSizeCm.Y &&
			FVector::Dist2D(CreekCenter, DeckCenter) < 50.f;
	}
};

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

	/** Ticket exhaustion is ignored until this many seconds have elapsed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle")
	float MinSecondsBeforeExhaustion = 120.f;

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
	/** One row of the class picker: a class this side brought to the field. */
	struct FClassChoice
	{
		FName Id;
		int32 MenLeft = 0; // of this class, standing or in reserve (starting soldiers minus its dead)
		int32 Alive = 0;
	};
	/** Classes the side's army holds, in display order. A class with no men left is listed but cannot be chosen. */
	TArray<FClassChoice> GetClassChoices(FName Faction) const;
	/** Men of a class not yet dead: campaign units x SoldiersPerUnit minus the class's deaths. */
	int32 ClassMenLeft(FName Faction, FName UnitClass) const;
	/** The class this controller last fought as (NAME_None before its first spawn). */
	FName GetControllerClass(const AController* Controller) const;
	/** Opening choice: the living player becomes UnitClass where he stands (no ticket, no respawn). */
	bool RekitPlayer(APlayerController* PC, FName UnitClass);
	/** The class picker is up before the player's first shot of the battle (Deploy re-kits, it does not spawn). */
	bool IsOpeningClassPick() const { return bOpeningClassPick; }
	void EndOpeningClassPick();
	int32 TicketsRemaining(FName Faction, FName UnitClass) const;
	float TimeRemaining() const;
	FName GetPlayerFaction() const { return PlayerFaction; }
	/** Plurality post owner, or NAME_None on tie / none. */
	FName GetObjectiveHolder() const;
	int32 PostsOwned(FName Faction) const;
	int32 CountPosts() const;
	/** Tickets this side is losing per bleed interval right now (0 if it isn't bleeding). For the HUD. */
	int32 GetBleedPerInterval(FName Faction) const;
	const TArray<TWeakObjectPtr<APA250ControlPoint>>& GetPosts() const;
	bool IsBattleOver() const { return bBattleOver; }
	/** The battle banner, or a timed notice to the player while one is up. */
	FString GetBannerText() const { return (GetWorld() && GetWorld()->GetTimeSeconds() < NoticeUntil) ? Notice : Banner; }
	void ShowNotice(const FString& Text, float Seconds);
	/** Pure: has a side lost on tickets? Never while the human player is alive on it; otherwise at 0
	 *  tickets (a side that started with none only once it has fielded men and been wiped). */
	static bool SideIsOut(int32 Pool, int32 InitialUnits, bool bEverFielded, int32 Alive, bool bHumanAliveOnSide);
	/** GAMEPLAY SETTINGS (user report 2026-11-14: both sides at 0 on the bar, the last enemies could
	 *  not be found and the fight only ended on the clock). A side with no reserve and this few men
	 *  left is a remnant: it breaks and quits the field after RemnantRoutSeconds; from
	 *  RemnantMarkMen down its men are marked on the enemy player's HUD so they can be found. */
	static constexpr int32 RemnantMen = 3;
	static constexpr int32 RemnantMarkMen = 5;
	static constexpr float RemnantRoutSeconds = 45.f;
	/** Pure: is this side a remnant about to break? Never the human player's side while he is alive
	 *  on it (user decision 2026-11-10: the battle is not lost under a man who is still fighting). */
	static bool RemnantBreaks(int32 Pool, int32 Alive, bool bHumanAliveOnSide);
	/** Every man a side still has: its reserve plus the men standing on the field. The HUD's number. */
	int32 MenRemaining(FName Faction) const { return PoolTotal(Faction) + CountAlive(Faction); }
	/** No reserve and RemnantMarkMen or fewer standing: the HUD marks these men for the other side. */
	bool IsMarkedRemnant(FName Faction) const;
	/** Seconds until this side's remnant quits the field, or -1 when it is not breaking. */
	float RemnantCountdown(FName Faction) const;
	/** No reserves left: the player steps into the nearest of his side's men still standing (no ticket
	 *  is spent, the bot's controller is retired). False when nobody is left to take over. */
	bool TakeOverSoldier(APlayerController* PC);
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
	/** A bot's musket flashed in the pan (battle statistics only; powder horn waste). */
	void RecordMisfire(FName Faction);
	int32 GetMisfires() const { return Misfires; }
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
	/** Campaign Army.morale (0..100) → extra battlefield pressure bias. Distinct from squad Steady/Shaken/Rout. */
	float GetCampaignConfidencePressure(FName Faction) const;
	/** concord_bridge / bridge_crossing contests one post; battle_road / return_march rewards men kept. */
	static FPA250ScenarioFight ResolveScenario(const FString& Preset, const TArray<FString>& TerrainTags);
	static void ConfigurePostForScenario(APA250ControlPoint* Post, const FPA250ScenarioFight& Fight);
	/** Higher share of the starting pool wins. Equal shares go to DefenderOnTie. */
	static FName PreservationWinner(int32 RemainingA, int32 InitialA, int32 RemainingB, int32 InitialB, FName SideA,
		FName SideB, FName DefenderOnTie);
	static bool UsesPostBleed(const FPA250ScenarioFight& Fight) { return !Fight.bRewardForcePreservation; }
	/** Ticket exhaustion is ignored until ElapsedSeconds reaches MinSeconds (default 120). */
	static bool TicketsMayExhaust(float ElapsedSeconds, float MinSecondsBeforeExhaustion)
	{
		return ElapsedSeconds >= MinSecondsBeforeExhaustion;
	}
	/** Creek and plank span centered on a ground point. The deck reaches both banks. */
	static FPA250CreekBridge PlanCreekBridge(const FVector& GroundPoint);
	/** Spawns the creek, the banks, and the deck. Returns how many pieces were placed. */
	static int32 SpawnCreekBridge(UWorld* World, const FVector& GroundPoint);
	/** A level built with its own river (L_Concord) closes the channel with unseen blocks tagged
	 *  PA250Creek. Narrow them to men only: shot, sight lines and cameras pass. Returns the count. */
	static int32 SealLevelRiver(UWorld* World);
	const TArray<FString>& GetKillFeed() const { return KillFeed; }
	/** Last killer label for the local player's death (spawn screen "Killed by"). */
	FString GetLastKillerLabel() const { return LastKillerLabel; }
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool bRestoreScreenMessages = false;
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
	/** -PA250MeleeProbe: stage the player against one enemy, run every melee move, save stills (PA250MeleeProbe.cpp). */
	void MaybeStartMeleeProbe();
	void MaybeStartClassPortraits();
	void GatherPosts();
	/** Post ownership, which posts can be taken, and how the clock is won, from the BattleSetup. */
	void ApplyScenarioPreset();
	/** Fort bonus: simple PA250Cover walls near posts the defender starts holding. */
	void SpawnFortCoverNearHeldPosts();
	int32 InitialSoldiers(FName Faction) const;
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
	ARFCharacter* SpawnBot(FName Faction, FName UnitClass, APA250ControlPoint* PreferredPost, uint32 ClothingSeed = 0);
	void ConfigureSoldier(ARFCharacter* Soldier, FName Faction, FName UnitClass);
	FName ChooseClassFor(FName Faction, FName Preferred) const;
	bool CanSpawn(FName Faction) const;
	void SpendDeathTicket(FName Faction, FName UnitClass);
	void BleedTickets(float DeltaSeconds);
	void TickRemnants(float DeltaSeconds);
	int32 PoolTotal(FName Faction) const;
	int32 CountAlive(FName Faction, FName UnitClass) const;
	int32 CountAlive(FName Faction) const;
	void TickOfficers(float DeltaSeconds);
	void MaybeOpenClassPick(APlayerController* PC);
	float OfficerTimer = 0.f;
	bool bOpeningClassPick = false;
	AActor* PickStart(FName Faction) const;
	FTransform PickSpawnTransform(FName Faction, APA250ControlPoint* PreferredPost) const;
	/** Ours, not the crossing being contested, and no living enemy within SafeSpawnEnemyCm. */
	bool PostIsSafeSpawn(const APA250ControlPoint* Post, FName Faction) const;
	/** GAMEPLAY SETTING: reinforcements do not come on at a post with an enemy this close. */
	float SafeSpawnEnemyCm = 6000.f;
	/** Where RFGameMode respawns a controller (ChooseRespawnPoint); never a level PlayerStart. */
	TWeakObjectPtr<AActor> RespawnAnchor;
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
	FPA250ScenarioFight ScenarioFight;
	bool bBattleOver = false;
	bool bAutoplayBots = false;
	bool bSelftest = false;
	bool bDebugBattle = false;
	float StartTime = 0.f;
	int32 PostFlips = 0;
	float SpacingReportTimer = 5.f;
	FString Banner;
	FString Notice;
	float NoticeUntil = -1.f;
	FString EndReason;
	FName PlayerFaction;
	FName PendingPlayerClass = TEXT("line_infantry");
	TWeakObjectPtr<APA250ControlPoint> PendingPlayerPost;

	pa250::BattleSetup Setup;
	pa250::BattleResult Result;
	TMap<FName, TMap<FName, int32>> InitialPool;
	/** True once a side has had at least one living soldier this battle (for zero-ticket wipe rules). */
	TMap<FName, bool> EverFielded;
	/** Seconds each side has been a breaking remnant (RemnantBreaks) without a break. */
	TMap<FName, float> RemnantTime;
	/** Remaining tickets (death + bleed) and per-class deaths. HUD shows Ledger.Pool. */
	FPA250TicketLedger Ledger;
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> Starts;
	TMap<TWeakObjectPtr<AController>, FName> ControllerFaction;
	TMap<TWeakObjectPtr<AController>, FName> ControllerClass;
	TArray<TWeakObjectPtr<APA250ControlPoint>> Posts;
	TArray<FString> KillFeed;
	FString LastKillerLabel;
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
	int32 Misfires = 0;
	int32 PersonalKills = 0;
	TMap<FName, int32> ShotsFiredByFaction;
	TMap<FName, int32> MisfiresByFaction;
	int32 ClothesSerial = 0;
	uint32 PlayerClothingSeed = 0;
	TMap<FName, int32> MusterCap;              // max militia alive for a side (0 = no muster)
	TMap<FName, float> MusterTimer;
	TMap<FName, int32> MusterArrivals;
	TMap<FName, TArray<TWeakObjectPtr<AActor>>> MusterPoints;
	void SpawnInitialForces(FName Faction, int32 Count);
	void TickMuster(float DeltaSeconds);
	ARFCharacter* SpawnBotAt(FName Faction, FName UnitClass, const FTransform& At, uint32 ClothingSeed = 0);
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
