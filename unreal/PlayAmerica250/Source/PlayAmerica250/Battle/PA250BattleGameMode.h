// PA250BattleGameMode.h — Conquest ticket battle driven by the sim's BattleSetup; ends with a BattleResult.
#pragma once

#include "CoreMinimal.h"
#include "RFGameMode.h"
#include "PA250TicketLedger.h"
#include "PA250BattleEndRules.h"
#include "pa250/types.hpp"
#include "PA250BattleGameMode.generated.h"

class APA250ControlPoint;
class APA250FieldWork;
class APA250Colours;
class APA250FieldGun;
class APA250RowBoat;
enum class EActiveReloadHit : uint8;
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
	/** BattleSetup.mode: conquest | assault | landing (PA250BattleModes.h). The scripted crossing and
	 *  return-march fights are always conquest. */
	FName Mode = TEXT("conquest");
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
	/** How long the field takes to fade in from black after the battle story (presentation only). */
	UPROPERTY(EditAnywhere, Category = "PA250|Story")
	float StoryFadeInSeconds = 1.5f;

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
	/** Men of a class the army still has off the field: ClassMenLeft less its surrendered and the men
	 *  of the class standing. A man can only come on as a class that has one (otherwise 18 line
	 *  infantry could fall for an army that brought 16: autoplay, 2026-11-14). */
	int32 ClassMenInReserve(FName Faction, FName UnitClass) const;
	/** The class this controller last fought as (NAME_None before its first spawn). */
	FName GetControllerClass(const AController* Controller) const;
	/** Opening choice: the living player becomes UnitClass where he stands (no ticket, no respawn). */
	bool RekitPlayer(APlayerController* PC, FName UnitClass);
	/** The class picker is up before the player's first shot of the battle (Deploy re-kits, it does not spawn). */
	bool IsOpeningClassPick() const { return bOpeningClassPick; }
	/** Class / spawn picker is on screen (opening pick or respawn). Battle HUD chrome hides while this is true. */
	bool IsClassPickerOpen() const;
	class UPA250BattleHUD* GetBattleHud() const { return Hud; }
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
	 *  left is a remnant: it breaks and quits the field after the remnant seconds; from
	 *  mark_men down its men are marked on the enemy player's HUD so they can be found. */
	/** Pure: is this side a remnant about to break? Never the human player's side while he is alive
	 *  on it (user decision 2026-11-10: the battle is not lost under a man who is still fighting).
	 *  The numbers are battle_rules.json "remnant". */
	static bool RemnantBreaks(int32 Pool, int32 Alive, bool bHumanAliveOnSide, int32 RemnantMen);
	/** Men of the side's army who are neither on the field nor down: the army's men (campaign units x
	 *  SoldiersPerUnit) less its dead, its surrendered and the men standing. A side cannot put a man on
	 *  the field that its army does not have, whatever its tickets say (a militiaman costs half a
	 *  ticket, so tickets alone let 112 men fight and fall for an army of 88: autoplay, 2026-11-14). */
	int32 MenInReserve(FName Faction) const;
	/** Pure: the reserve a side can still call on - never more tickets than men, never more men than tickets. */
	static int32 ReserveFor(int32 Tickets, int32 ArmyMen, int32 Down, int32 Standing)
	{
		return FMath::Max(0, FMath::Min(Tickets, ArmyMen - Down - Standing));
	}
	/** Every man a side still has: its reserve plus the men standing on the field. The HUD's number. */
	int32 MenRemaining(FName Faction) const { return PoolTotal(Faction) + CountAlive(Faction); }
	/** No reserve and mark_men or fewer standing: the HUD marks these men for the other side. */
	bool IsMarkedRemnant(FName Faction) const;
	/** Seconds until this side's remnant quits the field, or -1 when it is not breaking. */
	float RemnantCountdown(FName Faction) const;
	/** No reserves left: the player steps into the nearest of his side's men still standing (no ticket
	 *  is spent, the bot's controller is retired). False when nobody is left to take over. */
	bool TakeOverSoldier(APlayerController* PC);
	const pa250::BattleResult& GetResult() const { return Result; }
	/** Pure: the battle's own ledger as BattleResult.casualties, in men. The men shot or bayoneted down
	 *  (Deaths) are split into killed and wounded by the sim's ratio (pa250::split_fallen,
	 *  battle_rules.json); the men who surrendered are prisoners. Every man who fell is exactly one of
	 *  the three, so a side's card total is its deaths plus its surrendered, to the man. */
	static std::map<std::string, std::map<std::string, pa250::MenDown>> CasualtiesFromLedger(
		const TMap<FName, TMap<FName, int32>>& Deaths, const TMap<FName, TMap<FName, int32>>& Surrendered,
		const TMap<FName, TMap<FName, int32>>& SavedBySurgeon = TMap<FName, TMap<FName, int32>>());

	// ---- Field guns (PA250FieldGun.h; values in battle_rules.json cannon) ----
	const TArray<TObjectPtr<APA250FieldGun>>& GetGuns() const { return Guns; }
	/** The gun a gunner of Faction should go to: the one he already has a post at, else the nearest of
	 *  his side with a post free. OutPost is his place at it. Null when his side has none for him. */
	APA250FieldGun* GunFor(ARFCharacter* Gunner, int32& OutPost) const;
	/** The gun of his side the player stands at (within the crew radius), or null. */
	APA250FieldGun* GunAt(const ARFCharacter* Soldier) const;
	/** The player's trigger at a gun he serves: true when the key belonged to the gun (fired or not). */
	bool PlayerFiresGun(ARFCharacter* Soldier);
	/** His reload key at a gun he serves works its drill: true when the key belonged to the gun. */
	bool PlayerWorksGun(ARFCharacter* Soldier, EActiveReloadHit& OutHit);
	void NotifyGunTaken(APA250FieldGun* Gun, FName From);
	void NotifyGunFired(APA250FieldGun* Gun, bool bCanister);
	void NotifyRoundShot(ARFCharacter* Man, bool bStruck);
	void NotifyShove(ARFCharacter* By, ARFCharacter* Victim);
	int32 GetShoves() const { return Shoves; }
	void NotifyWorkBattered(APA250FieldWork* Work, bool bBroken, FName ByFaction);
	/** Guns a side has taken and still holds at this moment (BattleResult.guns_taken at the end). */
	int32 GetGunsTakenBy(FName Faction) const;
	// ---- Support roles (PA250Support.h; values in unit_classes.json) ----
	/** A fallen man a surgeon may still save. */
	struct FFallenMan
	{
		FVector Where = FVector::ZeroVector;
		float FellAt = 0.f;
		FName Faction;
		FName UnitClass;
		float Treated = 0.f;
		bool bSaved = false;
	};
	/** The nearest fallen man of Faction within SeekCm of From who can still be saved, or INDEX_NONE. */
	int32 NearestSavable(FName Faction, const FVector& From, float SeekCm) const;
	FVector FallenLocation(int32 Index) const { return FallenMen.IsValidIndex(Index) ? FallenMen[Index].Where : FVector::ZeroVector; }
	/** A surgeon's seconds with a fallen man; once he has had the treat time the man counts as wounded. */
	void TreatFallen(int32 Index, float Seconds, const ARFCharacter* Surgeon);
	int32 GetSavedBySurgeon(FName Faction) const;
	/** Fallen colours within SeekCm of From (either side's), or null. */
	APA250Colours* FallenColoursNear(const FVector& From, float SeekCm) const;
	int32 GetColoursTaken(FName ByFaction) const { return ColoursTaken.FindRef(ByFaction); }
	bool IsAutoplayMode() const { return bAutoplayBots; }
	/** A general's station (near his side's home post) and how far he lets himself be drawn from it.
	 *  False when the man is not a general. */
	bool GeneralStation(const ARFCharacter* Man, FVector& OutStation, float& OutRadiusCm) const;
	bool IsGeneral(const ARFCharacter* Man) const;
	/** The general's aura for a side in cm (0: no general standing). */
	float GeneralAuraCm(FName Faction) const;
	// ---- The battle reads the campaign (BattleSetup; numbers in battle_rules.json) ----
	/** Soldiers a side has on the field at once: -PA250Bots, else the sim's field_strength, else BotsPerSide. */
	int32 GetFieldStrength(FName Faction) const;
	/** Seconds until the side holding every post wins (posts_swept); OutFaction is that side. -1 when nobody is. */
	float PostsSweptCountdown(FName& OutFaction) const;
	/** Seconds until this side's army breaks (army_broke), or -1 when it is not breaking. */
	float ArmyBrokeCountdown(FName Faction) const;
	/** Cartridges in a full box for this side (0 = boxes are not counted in this fight). */
	int32 GetRoundsPerMan(FName Faction) const;
	/** Rounds the side still has to hand out at its posts. */
	int32 GetAmmoPool(FName Faction) const { return Ammo.Remaining(Faction); }
	/** Is there anything left for this man to draw at a post his side holds? */
	bool CanResupply(FName Faction, bool bHuman) const { return GetRoundsPerMan(Faction) > 0 && Ammo.Available(Faction, bHuman) > 0; }
	/** Nearest post this side holds (where a man refills his cartridge box), or null. */
	APA250ControlPoint* NearestHeldPost(FName Faction, const FVector& From) const;
	const FPA250AmmoPool& GetAmmo() const { return Ammo; }
	// ---- Class verbs (PA250UnitClasses; values in unit_classes.json) ----
	/** The player's T key: order a volley (regular), a charge (grenadier), rally or order (officer).
	 *  Passive verbs answer with what they do. Shows a notice either way. */
	void UseClassAbility(ARFCharacter* Soldier);
	/** The player's order to his file: 1 follow me, 2 form line here, 3 hold, 4 take cover, 5 charge,
	 *  6 fall back to resupply. An officer's order also reaches the squads in his aura. Shows a notice. */
	void GivePlayerOrder(ARFCharacter* Soldier, int32 OrderIndex);
	/** One line for the HUD: his file's men, loaded muskets and standing order. Empty with no file. */
	FString GetFileLine(const ARFCharacter* Soldier) const;
	/** Pure: the HUD's words for a file. */
	/** WithYou: how many of them are close to him now (-1 leaves it out). */
	static FString FileLineText(int32 Men, int32 Loaded, int32 Presented, const TCHAR* OrderWords, bool bVolleyRole, int32 WithYou = -1);
	/** Pure: where the leader is aiming. The nearest enemy within RayCm of the line from Eye to
	 *  HitPoint is his man (a ray at a man's chest lands far behind him); else the point the ray struck. */
	static FVector ResolveLeaderAim(const FVector& Eye, const FVector& HitPoint, const TArray<FVector>& Enemies, float RayCm);
	/** One line for the HUD: the class verb and whether it is ready. */
	FString GetAbilityLine(const ARFCharacter* Soldier) const;
	/** Men of a side taken prisoner so far (a dragoon riding down routers). */
	int32 GetPrisonersTaken(FName Faction) const;
	const pa250::BattleRules& GetRules() const { return Rules; }
	const TArray<TObjectPtr<APA250FieldWork>>& GetWorks() const { return Works; }
	/** The nearest work within ReachCm of From that needs a sapper's labour, or null. */
	APA250FieldWork* NearestWorkNeedingLabour(const FVector& From, float ReachCm) const;
	/** Is the post nearest Where held by Faction? (A sapper digs only on ground his side holds.) */
	bool SideHoldsGroundAt(FName Faction, const FVector& Where) const;
	/** Seconds of one sapper's labour on a work (unit_classes.json verbs.sapper.build_seconds raises a
	 *  whole breastwork; a stronger work takes longer in proportion). Counts what it achieves. */
	void LabourOnWork(APA250FieldWork* Work, float Seconds, const ARFCharacter* Sapper);
	int32 GetWorksRaised(FName Faction) const { return WorksRaised.FindRef(Faction); }
	/** A work changed (built up, or battered below cover): squads look again at where the cover is. */
	void NotifyWorksChanged();
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
	static FPA250ScenarioFight ResolveScenario(const FString& Preset, const TArray<FString>& TerrainTags, const FString& Mode = TEXT("conquest"));
	/** Assault and landing: the post the attacker must take next (the only one he can contest in an
	 *  assault; the landing post until he has landed). Null in the open fight or once it is all open. */
	APA250ControlPoint* GetModeOpenPost() const;
	FName GetBattleMode() const { return ScenarioFight.Mode; }
	bool HasLanded() const { return bLanded; }
	static void ConfigurePostForScenario(APA250ControlPoint* Post, const FPA250ScenarioFight& Fight);
	/** Concord: the east bank (village side, tagged british in the level) is the easier ground.
	 *  The faction that holds the town takes it; the other side starts on the west bank. */
	static void ConcordBanks(FName TownHolder, FName& East, FName& West);
	/** Higher share of the starting pool wins. Equal shares go to DefenderOnTie. */
	static FName PreservationWinner(int32 RemainingA, int32 InitialA, int32 RemainingB, int32 InitialB, FName SideA,
		FName SideB, FName DefenderOnTie);
	static bool UsesPostBleed(const FPA250ScenarioFight& Fight) { return !Fight.bRewardForcePreservation && Fight.Mode != TEXT("assault"); }
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
	/** -PA250GunStill: two stills of the first field gun with a soldier beside it, for scale. */
	void MaybeStartGunStill();
	/** -PA250LandingStill: stills of a landing: the boats on the way in (12 s) and the men ashore. */
	void MaybeStartLandingStill();
	void GatherPosts();
	/** Post ownership, which posts can be taken, and how the clock is won, from the BattleSetup. */
	void ApplyScenarioPreset();
	/** Concord only: give the town's owner the east bank (starts, muster points, and the far bridgehead). */
	void AssignConcordGround();
	/** Field works (battle_rules.json works). Places for them are the level's PA250WorksSlot markers,
	 *  or made up about the defender's post when the level has none. The defender's fortification_level
	 *  stands some of them at the start (breastworks; a redoubt at the highest level); the rest are
	 *  places a sapper can build on. Replaces the three cubes of the old fort bonus. */
	void SpawnFieldWorks();
	UPROPERTY(Transient)
	TArray<TObjectPtr<APA250FieldWork>> Works;
	/** A side's guns: as many as the setup says its army brings (sides.*.guns), at the level's
	 *  PA250GunSlot markers or drawn up by the side's start; a crew comes on at each. */
	void SpawnFieldGuns();
	/** The opening: a crew of the gun class comes on at each of the side's guns, as far as the army
	 *  has gunners. Returns the men put on the field (they count in the side's field strength). */
	int32 SpawnGunCrews(FName Faction, int32 MostMen);
	UPROPERTY(Transient)
	TArray<TObjectPtr<APA250FieldGun>> Guns;
	TMap<FName, int32> GunShots;        // discharges by side (battle trace)
	TMap<FName, int32> CanisterShots;
	int32 RoundShotStruck = 0;          // men a round shot went through
	int32 RoundShotKnockedDown = 0;     // men knocked off their feet beside its path
	TMap<FName, int32> WorksBroken;     // works battered below cover, by the side whose shot did it
	int32 GunCaptures = 0;              // times a gun changed hands
	int32 Shoves = 0;                   // shoves that landed (battle trace)
	/** One colours a side at a time: carried by its ensign, fallen, or taken. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<APA250Colours>> Colours;
	/** Colours a side has lost to the enemy (it has no more than its ensign units to lose). */
	TMap<FName, int32> ColoursLost;
	TMap<FName, int32> ColoursTaken;   // by the captor
	TMap<FName, int32> ColoursFell;    // times a side's colours went down with their bearer
	// ---- The generals (campaign pass). One a side at most: an officer-class figure who keeps near
	// his side's home post, steadies men over a wider radius than a company officer, and whose fall
	// is a morale check on his side and a line in the result (BattleResult.commander_events).
	TMap<FName, TWeakObjectPtr<ARFCharacter>> Generals;
	TMap<FName, FVector> GeneralStations;
	TMap<FName, FString> GeneralFate;  // faction -> wounded | killed | captured, once he has fallen
	int32 GeneralAuraChecks = 0;       // times a man was steadied by a general's aura (trace)
	int32 AlarmMusteredAtOnce = 0;     // militia on the field at the start who would otherwise have come later (trace)
	void SpawnGenerals();
	/** A letter's orders for this fight (BattleSetup factors withdraw_early / stand_fast): the value
	 *  of that factor on the side, or 0. */
	int32 SideOrder(FName Faction, const char* Label) const;
	TArray<FFallenMan> FallenMen;
	TMap<FName, TMap<FName, int32>> SavedBySurgeon;
	float SupportTimer = 0.f;
	int32 SpawnedOnColours = 0;
	/** Times a volley-role man was found loading in a musician's cadence (twice a second each). */
	int32 CadenceManChecks = 0;
	/** Colours taken up or carried off, the news to the coordinator; twice a second. */
	void TickSupport(float DeltaSeconds);
	/** An ensign taking the field raises his side's colours if it has colours left and none are up. */
	void MaybeRaiseColours(ARFCharacter* Soldier);
	/** Works a side's sappers have raised to cover in this battle (battle trace). */
	TMap<FName, int32> WorksRaised;
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
	/** posts_swept and army_broke (PA250BattleEndRules.h). */
	void TickEndConditions(float DeltaSeconds);
	/** Opening boxes and refills at held posts. */
	void TickAmmo(float DeltaSeconds);
	/** Officers rally routing squads near them; mounted dragoons take routers they overtake. */
	void TickClassVerbs();
	/** The human player's file: made up from the field, told where he aims. */
	void TickPlayerFile(float DeltaSeconds);
	float FileTimer = 0.f;
	FVector LeaderAim = FVector::ZeroVector;
	/** -PA250LedVolley=<side> (bot battles): one man of that side leads a file as the player would;
	 *  he holds his aim on the nearest enemy and fires when his line is presented. */
	void TickLedVolleyProbe(float DeltaSeconds);
	/** -PA250OrderProbe: the player's own soldier is walked about and the order keys are pressed
	 *  through the real input path; whether his file follows, holds and comes back is measured and
	 *  written to Saved/PA250/order_probe.json (PA250OrderProbe.cpp). */
	void TickOrderProbe(float DeltaSeconds);
	int32 OrderProbeStep = -1;
	float OrderProbeTime = 0.f;
	FVector OrderProbeLeaderStart = FVector::ZeroVector;
	TArray<FVector> OrderProbeMarks;
	FString OrderProbeJson;
	FName ProbeSide;
	TWeakObjectPtr<ARFCharacter> ProbeLeader;
	float ProbeHold = 0.f;
	float ProbeNextAt = 0.f;
	float ProbeOrderAt = 0.f;
	float ProbeWatchUntil = -1.f;
	int32 ProbeTargetSquad = -1;
	int32 ProbeVolleys = 0;
	int32 ProbeShook = 0;
	void TakePrisoner(ARFCharacter* Prisoner, const ARFCharacter* Captor);
	/** Debug matchup (-PA250HoldAt=<m>): the two sides drawn up that far apart, holding their ground. */
	void ArrangeMatchup(float MetersApart);
	/** faction -> class -> soldiers taken prisoner (BattleResult.prisoners, in units, at the end). */
	TMap<FName, TMap<FName, int32>> Prisoners;
	TMap<TWeakObjectPtr<ARFCharacter>, float> RallyReadyAt;
	float PlayerAbilityReadyAt = 0.f;
	/** The class being placed by PickSpawnTransform (light infantry may come on nearer the enemy). */
	mutable FName SpawnClassHint;
	bool bMatchupHold = false;
	bool bBotsOverride = false;
	pa250::BattleRules Rules;
	TMap<FName, float> SweepTime;
	// ---- Assault and landing (PA250BattleModes.h; battle_rules.json modes) ----
	/** Locks the posts the mode closes, counts posts carried, adds the assault's clock, ends an assault the attacker has carried. */
	void TickBattleMode();
	/** Landing: the attacker's men on the field go into boats lying off the landing places. */
	void SetUpLanding();
	/** Starts the boats (they wait a little for a player who attacks), seats him, counts men set
	 *  ashore, and brings on the replacements held back until the first boat is in. */
	void TickLanding(float DeltaSeconds);
	/** A replacement for the landing side cannot come on before a boat has reached the shore. */
	bool HoldsLandingSpawn(FName Faction) const;
	UPROPERTY(Transient)
	TArray<TObjectPtr<APA250RowBoat>> Boats;
	bool bBoatsRowing = false;
	bool bPlayerRode = false;
	float BoatsWaited = 0.f;
	int32 HeldLandingSpawns = 0;
	int32 MenLanded = 0;
	int32 MenEmbarked = 0;
	float FirstBoatInAt = -1.f;
	TArray<int32> ModeOrder;      // indices into Posts, nearest the attacker's start first
	int32 ModeCarried = 0;        // posts the attacker holds (assault: carried in order)
	bool bLanded = false;         // landing: the attacker has held the landing post
	float ModeClockAdded = 0.f;   // seconds the assault has added to the clock
	/** A side that has held at least one post in this fight (the other side can then sweep). */
	TMap<FName, bool> EverHeldAPost;
	TMap<FName, float> BrokeTime;
	/** Longest the break clock has run for a side (battle trace; tuning evidence). */
	TMap<FName, float> BrokePeak;
	TMap<FName, int32> PeakSquads;
	FPA250AmmoPool Ammo;
	TSet<TWeakObjectPtr<ARFCharacter>> AmmoIssued;
	TMap<TWeakObjectPtr<ARFCharacter>, float> ResupplyClock;
	float AmmoTimer = 0.f;
	void RegisterConsoleCommands();

	bool bInitialized = false;
	FPA250ScenarioFight ScenarioFight;
	bool bBattleOver = false;
	bool bAutoplayBots = false;
	bool bSelftest = false;
	bool bDebugBattle = false;
	float StartTime = 0.f;
	int32 PostFlips = 0;
	/** Battle trace (how long a side held): the game-second of the first post flip, and the second
	 *  each side was first down to half the men it opened with on the field (-1: never). */
	float FirstFlipAt = -1.f;
	TMap<FName, float> HalfStrengthAt;
	TMap<FName, int32> OpeningField;
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
