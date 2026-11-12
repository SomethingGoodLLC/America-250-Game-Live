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

	FString AutoplayStage;
	bool WriteTrace(const FString& Extra = FString()) const;

	static FName LevelNameForFlow(EPA250Flow Flow);
	void TravelToFlowLevel(UObject* WorldContext) const;

	static constexpr int32 MaxHoursPerFrame = 32;

private:
	void MaybeAutosave(const pa250::CampaignState& Before, const pa250::CampaignState& After);
	void RouteInterrupt(const pa250::TickOutput& Out);

	pa250::CampaignController Controller;
	bool bCampaignActive = false;
	bool bPaused = true;
	int32 SpeedIndex = FPA250CampaignSpeed::DefaultIndex;
	float HourAccumulator = 0.f;
	TArray<FString> AutosavePaths;
	int32 HudEpoch = 0;
};
