// PA250Subsystem.h — exposes the PA250Sim core (pa250::CampaignController) to Unreal.
// The sim is authoritative; this class only forwards commands and holds state across OpenLevel.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
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
class PLAYAMERICA250_API UPA250Subsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPA250Subsystem* Get(const UObject* WorldContext);

	// ---- paths ----
	static FString ScenarioPath();   // <repo>/unreal/PA250Sim/data/boston1775.json
	static FString SavePath();       // Saved/PA250/campaign.json
	static FString TracePath();      // Saved/PA250/autoplay_trace.json

	// ---- campaign ----
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool NewCampaign(FString& OutError);
	/** Starts a campaign if none exists. */
	bool EnsureCampaign();

	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool QueueMove(const FString& ArmyId, const FString& ToProvince, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void ClearOrders() { Controller.clear_orders(); }
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool EndTurn(FString& OutError);

	// ---- battle ----
	UFUNCTION(BlueprintPure, Category = "PA250")
	bool IsBattlePending() const { return Controller.battle_pending(); }
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool BeginBattle(FString& OutError);
	/** Debug/selftest: starts a campaign, marches the British to the meeting and enters Battle flow. */
	bool PrepareDebugBattle(FString& OutError);
	const pa250::BattleSetup& GetBattleSetup() const { return Controller.battle_setup(); }
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString GetBattleSetupJson() const;
	bool ApplyBattleResult(const pa250::BattleResult& Result, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool ApplyBattleResultJson(const FString& ResultJson, FString& OutError);

	// ---- diplomacy ----
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool BeginDiplomacy(const FString& Faction, FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString GetDiplomacyContextJson() const;
	/** Sim-side validation of a proposal against the pending context. Returns the error list (empty = valid). */
	TArray<FString> ValidateProposalJson(const FString& ProposalJson) const;
	/** Accepted proposal -> DiplomacyResult -> applied by the sim (re-validated there). */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool AcceptProposalJson(const FString& ProposalJson, FString& OutError);
	/** Leave / Reject: back to the campaign with no state change. */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool LeaveDiplomacy(FString& OutError);

	// ---- persistence / inspection ----
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool SaveCampaign(FString& OutError);
	UFUNCTION(BlueprintCallable, Category = "PA250")
	bool LoadCampaign(FString& OutError);
	UFUNCTION(BlueprintPure, Category = "PA250")
	FString GetStateHash() const;
	UFUNCTION(BlueprintPure, Category = "PA250")
	EPA250Flow GetFlow() const;
	UFUNCTION(BlueprintPure, Category = "PA250")
	int32 GetTurn() const { return Controller.state().turn; }

	const pa250::CampaignState& GetState() const { return Controller.state(); }
	const pa250::CampaignController& GetController() const { return Controller; }
	pa250::CampaignController& GetControllerMutable() { return Controller; }

	/** Autoplay progress across OpenLevel: "" / battle / battle_done / diplomacy / diplomacy_done. */
	FString AutoplayStage;

	/** Writes Saved/PA250/autoplay_trace.json (every flow transition with its state hash). */
	bool WriteTrace(const FString& Extra = FString()) const;

	// ---- level travel ----
	static FName LevelNameForFlow(EPA250Flow Flow);
	/** Opens the level for the current flow (L_Campaign / L_Lexington / L_DiplomacyRoom). */
	void TravelToFlowLevel(UObject* WorldContext) const;

private:
	pa250::CampaignController Controller;
};
