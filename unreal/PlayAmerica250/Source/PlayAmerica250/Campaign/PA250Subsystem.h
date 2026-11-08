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

UCLASS()
class PLAYAMERICA250_API UPA250Subsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UPA250Subsystem* Get(const UObject* WorldContext);

	static FString ScenarioPath();
	static FString SavePath();
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

	FString AutoplayStage;
	bool WriteTrace(const FString& Extra = FString()) const;

	static FName LevelNameForFlow(EPA250Flow Flow);
	void TravelToFlowLevel(UObject* WorldContext) const;

	static constexpr float HoursPerSecond[5] = { 6.f, 12.f, 24.f, 48.f, 120.f };
	static constexpr int32 MaxHoursPerFrame = 32;

private:
	void MaybeAutosave(const pa250::CampaignState& Before, const pa250::CampaignState& After);
	void RouteInterrupt(const pa250::TickOutput& Out);

	pa250::CampaignController Controller;
	bool bCampaignActive = false;
	bool bPaused = true;
	int32 SpeedIndex = 0;
	float HourAccumulator = 0.f;
	TArray<FString> AutosavePaths;
};
