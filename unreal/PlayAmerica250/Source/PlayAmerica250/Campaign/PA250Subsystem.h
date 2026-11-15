// PA250Subsystem.h — exposes the PA250Sim core (pa250::CampaignController) to Unreal.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "pa250/controller.hpp"
#include "PA250Subsystem.generated.h"

UENUM(BlueprintType)
enum class EPA250Flow : uint8
{
	Campaign,
	Battle,
	Diplomacy
};

/** Campaign hours advanced per real second. The default is 1 h/s; slower steps sit below it. */
struct FPA250CampaignSpeed
{
	static constexpr float HoursPerSecond[] = { 0.25f, 0.5f, 1.f, 2.f, 6.f, 24.f, 120.f };
	static constexpr int32 Count = sizeof(HoursPerSecond) / sizeof(HoursPerSecond[0]);
	static constexpr int32 DefaultIndex = 2;

	static int32 Clamp(int32 Idx) { return FMath::Clamp(Idx, 0, Count - 1); }
	static float HoursAt(int32 Idx) { return HoursPerSecond[Clamp(Idx)]; }
	static FString Format(float HoursPerRealSecond)
	{
		if (FMath::IsNearlyEqual(HoursPerRealSecond, 0.25f))
		{
			return TEXT("¼ h/s");
		}
		if (FMath::IsNearlyEqual(HoursPerRealSecond, 0.5f))
		{
			return TEXT("½ h/s");
		}
		return FString::Printf(TEXT("%d h/s"), FMath::RoundToInt(HoursPerRealSecond));
	}
};

UCLASS()
class PLAYAMERICA250_API UPA250Subsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UPA250Subsystem* Get(const UObject* WorldContext);

	static FString ScenarioPath();
	static FString CorrespondenceCatalogPath();
	static FString LogisticsTuningPath();
	static FString BuildingsCatalogPath();
	static FString ProvinceSizesPath();
	static FString BattleRulesPath();
	static FString CampaignRulesPath();
	static FString MissionsPath();
	/** A file of the sim's data directory (unreal/PA250Sim/data). */
	static FString SimDataPath(const TCHAR* FileName);
	static FString SavePath();
	bool EnsureCorrespondenceCatalog(FString& OutError);
	static FString TracePath();
	static FString AutosaveDir();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	void SetCampaignActive(bool bActive);
	void SetPaused(bool bInPaused);
	bool IsPaused() const { return bPaused; }
	void SetSpeedIndex(int32 Idx);
	int32 GetSpeedIndex() const { return SpeedIndex; }
	float GetHoursPerSecond() const { return FPA250CampaignSpeed::HoursAt(SpeedIndex); }

	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool NewCampaign(FString& OutError);
	bool EnsureCampaign();

	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool QueueMove(const FString& ArmyId, const FString& ToProvince, FString& OutError);
	// ---- Campaign pass: people, lobbying, decisions ----
	/** Issue a campaign order (protocol/schemas/campaign_orders.yaml). OutError is the sim's refusal code.
	 *  ApplyNow: carry it out at this hour (paused clock, pending battle or decision); else queue for the next hour. */
	bool IssueOrderJson(const FString& OrderJson, FString& OutError, bool ApplyNow = false);
	bool IsDecisionPending() const { return !Controller.state().pass.pending_decision.empty(); }
	/** Halt one army (an order command, applied on the next hour). Part-way along a leg it turns
	 *  about and marches back to where the leg began (sim: halt_army). */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool HaltArmy(const FString& ArmyId, FString& OutError);
	/** True when the army is marching, has a march queued, or is turning back from a halt. */
	bool IsArmyOnTheMarch(const FString& ArmyId) const;
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void ClearOrders();
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool EndTurn(FString& OutError);
	bool AdvanceHours(int32 Hours, FString& OutError);

	UFUNCTION(BlueprintPure, Category = "PA250")
	bool IsBattlePending() const { return Controller.battle_pending(); }
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool BeginBattle(FString& OutError);
	bool PrepareDebugBattle(FString& OutError);
	const pa250::BattleSetup& GetBattleSetup() const { return Controller.battle_setup(); }
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString GetBattleSetupJson() const;
	bool ApplyBattleResult(const pa250::BattleResult& Result, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool ApplyBattleResultJson(const FString& ResultJson, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool BeginDiplomacy(const FString& Faction, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString GetDiplomacyContextJson() const;
	TArray<FString> ValidateProposalJson(const FString& ProposalJson) const;
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool AcceptProposalJson(const FString& ProposalJson, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool LeaveDiplomacy(FString& OutError);
	// An envoy act that does not end the talks (e.g. taking offense); re-validated by the sim.
	bool ApplyEnvoyActJson(const FString& ProposalJson, FString& OutError);
	// The player talked and walked away without agreement: the envoy likes them less.
	bool RegisterDiplomacyFailure(FString& OutError);

	/** Staff / logistics: purchase, request_assistance, requisition, capacity_investment, knox_support, dismiss. */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool ChooseLogistics(const FString& LetterId, const FString& Choice, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool ResolveCommander(const FString& Action, FString& OutError);
	/** Province supply pipeline (commit ≠ receive) and cartridge craft. */
	bool ApprovePurchaseOrder(const FString& DestProvince, int32 Powder, int32 Cartridges, int32 Muskets,
							  int32 Bayonets, int32 Provisions, int32 Lead, int32 Paper, FString& OutError);
	bool ApproveTransferOrder(const FString& SourceProvince, const FString& DestProvince, int32 Powder,
							  int32 Cartridges, int32 Muskets, int32 Bayonets, int32 Provisions, int32 Lead,
							  int32 Paper, FString& OutError);
	/** The town panel's "Send stores to depot": the whole magazine but its provisions leaves for the
	 *  headquarters depot (sim: send_stores_to_depot). OutError is the sim's code when it is refused. */
	bool SendStoresToDepot(const FString& SourceProvince, FString& OutError);
	bool DevelopProvince(const FString& ProvinceId, const FString& Track, FString& OutError);
	FString GetProvinceDevelopmentJson() const;
	/** City View: timed build/upgrade, demolish, cancel. Gold via resource_changes + HudEpoch. */
	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	bool StartConstruction(const FString& ProvinceId, const FString& BuildingId, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	bool CancelConstruction(const FString& ProvinceId, const FString& BuildingId, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	bool DemolishBuilding(const FString& ProvinceId, const FString& BuildingId, FString& OutError);
	/** Treasury: borrow / repay one loan step; the budget the 1st of the month will settle. */
	UFUNCTION(BlueprintCallable, Category = "PA250|Treasury")
	bool TakeLoan(FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250|Treasury")
	bool RepayLoan(FString& OutError);
	UFUNCTION(BlueprintPure, Category = "PA250|Treasury")
	FString GetTreasuryJson() const;
	// ---- Campaign arc: missions, enlistments, the siege guns, win and loss ----
	/** The mission tracker's data (pa250::missions_json): every mission, the current one, guns, enlistments. */
	UFUNCTION(BlueprintPure, Category = "PA250|Campaign")
	FString GetMissionsJson() const;
	/** Towns the player's current mission points at (the map marks them). */
	TArray<FString> GetMissionTargetProvinces() const;
	UFUNCTION(BlueprintPure, Category = "PA250|Campaign")
	bool IsCampaignOver() const { return Controller.state().progress.outcome.decided; }
	UFUNCTION(BlueprintPure, Category = "PA250|Campaign")
	FString GetOutcomeJson() const;
	/** Re-enlist every company of the army the treasury can pay for, class by class. Returns the gold spent. */
	UFUNCTION(BlueprintCallable, Category = "PA250|Campaign")
	bool ReenlistArmy(const FString& ArmyId, FString& OutError);
	/** Gold it would take to keep every company of the army. */
	int32 GetReenlistCost(const FString& ArmyId) const;
	UFUNCTION(BlueprintCallable, Category = "PA250|Campaign")
	bool SendForSiegeGuns(FString& OutError);
	/** Hitch the guns to this army (it must stand where they lie); an empty id leaves them where they are. */
	UFUNCTION(BlueprintCallable, Category = "PA250|Campaign")
	bool AttachSiegeGuns(const FString& ArmyId, FString& OutError);
	UFUNCTION(BlueprintPure, Category = "PA250|Treasury")
	int32 GetMonthlyNetGold() const;
	/** Raise a company in a town the player holds / send a mustering company home (full return). */
	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	bool StartRecruitment(const FString& ProvinceId, const FString& UnitId, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	bool CancelRecruitment(const FString& ProvinceId, FString& OutError);
	UFUNCTION(BlueprintPure, Category = "PA250|CityView")
	FString GetRecruitmentPreview(const FString& ProvinceId, const FString& UnitId) const;
	UFUNCTION(BlueprintPure, Category = "PA250|CityView")
	FString GetCityViewJson(const FString& ProvinceId) const;
	UFUNCTION(BlueprintPure, Category = "PA250|CityView")
	FString GetBuildingPreview(const FString& ProvinceId, const FString& BuildingId) const;
	UFUNCTION(BlueprintPure, Category = "PA250|CityView")
	int32 GetProvinceSlotCount(const FString& ProvinceId) const;
	/** Newest sim ledger line ("Build Fort, Lexington: -45 gold"); the City View status echoes it so
	 *  the panel, the ledger and the top bar always quote the same amount. */
	UFUNCTION(BlueprintPure, Category = "PA250|CityView")
	FString GetLastResourceChangeReason() const;
	/** Sim tuning + City View catalog/sizes. Each file is optional: the sim keeps its defaults. */
	static void LoadSimDataFiles();
	bool StartCartridgeCraft(const FString& ProvinceId, const FString& ArmyId, int32 Workers, int32 Powder,
							 int32 Lead, int32 Paper, FString& OutError);
	FString GetLastBattleReportJson() const;
	FString GetShipmentsJson() const;
	UFUNCTION(BlueprintPure, Category = "PA250")
	bool HasStaffEvent() const;
	/** The letter panel is on screen: a letter that needs an answer now, or the desk the player opened. */
	bool IsStaffPanelOpen() const;
	/** A letter is waiting on the desk with the panel closed (the clock keeps running). */
	bool HasDeskLetter() const;
	void OpenDesk();
	void CloseDesk();
	FString GetDeskChipText() const;
	bool IsPendingLetterTimely() const;
	/** Hours left before the clerk files the shown letter unanswered; -1 = no limit. */
	int32 GetPendingLetterHoursLeft() const;
	/** Catalog choices of the shown source letter (empty for other kinds). */
	int32 GetPendingLetterChoiceCount() const;
	FString GetPendingLetterChoiceId(int32 Index) const;
	FString GetPendingLetterChoiceLabel(int32 Index) const;
	FString GetPendingLetterChoiceSummary(int32 Index) const;
	/** Display only: the latest desk news (delivery, refusal, new letter) for the HUD message line. */
	int32 GetDispatchSerial() const { return DispatchSerial; }
	const FString& GetLastDispatch() const { return LastDispatch; }
	/** True when the shown letter still needs a choice; false for a delivered/refused notice. */
	bool IsPendingLetterAwaitingChoice() const;
	FString GetPendingLetterId() const;
	FString GetPendingLetterSubject() const;
	FString GetPendingLetterBody() const;
	FString GetPendingLetterKind() const;
	FString GetPendingLetterSourceId() const;
	FString GetPendingLetterSourceUrl() const;
	/** Opens the institutional URL. Never changes campaign state. */
	bool OpenPendingLetterSourceUrl() const;

	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool SaveCampaign(FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool LoadCampaign(FString& OutError);
	UFUNCTION(BlueprintPure, Category = "PA250")
	FString GetStateHash() const;
	UFUNCTION(BlueprintPure, Category = "PA250")
	EPA250Flow GetFlow() const;
	UFUNCTION(BlueprintPure, Category = "PA250")
	int32 GetTurn() const { return static_cast<int32>(Controller.state().now.hour_index / 24); }
	int64 GetHourIndex() const { return Controller.state().now.hour_index; }
	FString FormatDate() const;
	FString FormatWhen(int64 HourIndex) const;
	static FString FormatHoursRemaining(int32 Hours);

	const pa250::CampaignState& GetState() const { return Controller.state(); }
	const pa250::CampaignController& GetController() const { return Controller; }
	pa250::CampaignController& GetControllerMutable() { return Controller; }

	/** Bumped after any state mutation so Slate Text_Lambdas refresh treasury/support/stores. */
	void BumpHudEpoch() { ++HudEpoch; }
	int32 GetHudEpoch() const { return HudEpoch; }

	/** Display only: a battle result was applied and its report has not been shown on the map yet. */
	bool HasUnseenBattleReport() const { return bUnseenBattleReport; }
	void AcknowledgeBattleReport() { bUnseenBattleReport = false; }
	/** Display only: a mission of the player's has ended and its card has not been closed yet. The
	 *  clock stops when one ends and starts again when the card is closed (if it was running). */
	bool HasUnseenMissionResult() const;
	/** The oldest result not yet shown, or null. */
	const pa250::MissionResult* PeekMissionResult() const;
	void AcknowledgeMissionResult();

	FString AutoplayStage;
	bool WriteTrace(const FString& Extra = FString()) const;

	static FName LevelNameForFlow(EPA250Flow Flow);
	/** The battle level for a BattleSetup.scenario_preset: Concord's bridge has its own level
	 *  (L_Concord) when that map is in the project; every other fight is on L_Lexington. */
	static FName BattleLevelForPreset(const FString& ScenarioPreset);
	/** The level a battle is fought on: a landing (BattleSetup.mode) on the harbour level when it is
	 *  built; otherwise the preset's level. */
	static FName BattleLevelFor(const FString& ScenarioPreset, const FString& Mode);
	void TravelToFlowLevel(UObject* WorldContext) const;

	static constexpr int32 MaxHoursPerFrame = 32;

private:
	void MaybeAutosave(const pa250::CampaignState& Before, const pa250::CampaignState& After);
	void RouteInterrupt(const pa250::TickOutput& Out);
	void NoteDispatches(const pa250::CampaignState& Before, const pa250::CampaignState& After);
	/** On the morning of the 1st: put the Month settled ledger line on the status strip. */
	void NoteMonthSettlement(const pa250::CampaignState& Before, const pa250::CampaignState& After);
	void AfterLetterAnswered();

	pa250::CampaignController Controller;
	bool bCampaignActive = false;
	bool bPaused = true;
	int32 SpeedIndex = FPA250CampaignSpeed::DefaultIndex;
	float HourAccumulator = 0.f;
	TArray<FString> AutosavePaths;
	int32 HudEpoch = 0;
	bool bUnseenBattleReport = false;
	/** How many of the state's mission results the player has been shown. */
	int32 MissionResultsSeen = 0;
	/** The clock was running when a mission ended: closing its card starts the clock again. */
	bool bPausedByMission = false;
	/** The clock was stopped by a letter, not by the player: answering it starts the clock again. */
	bool bPausedByLetter = false;
	/** The clock was running when a decision was put: answering it starts the clock again. */
	bool bPausedByDecision = false;
	bool bDeskOpen = false;
	int32 DispatchSerial = 0;
	FString LastDispatch;
};
