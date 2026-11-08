// PA250BotController.h — spaced squad AI: slots, engage band, volley, bayonet charge.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "PA250AIReload.h"
#include "PA250CombatProfiles.h"
#include "PA250BotController.generated.h"

class ARFCharacter;
class APA250ControlPoint;
class UPA250BotTuning;
class UCrowdFollowingComponent;

UENUM()
enum class EBotState : uint8
{
	Advance,
	FormLine,
	Aim,
	Fire,
	Engage,
	Reload,
	HoldObjective,
	Defend,
	Charge,
	SeekCover,
	Rout
};

/** What the squad tells one man this frame (see UPA250VolleyCoordinator::TickFormation). */
struct FPA250FormationOrder
{
	FVector Target = FVector::ZeroVector;      // his place in the ranks, a covered firing spot, or (rout) the rear
	FVector Facing = FVector::ForwardVector;
	TWeakObjectPtr<ARFCharacter> Contact;
	float ArriveCm = 0.f;                      // "in position" tolerance (training)
	bool bEngaged = false;
	bool bPresent = false;                     // volley: aim at the contact
	bool bFireAtWill = false;                  // skirmish roles: fire individually when in position
	bool bMayCharge = true;
	bool bRout = false;
	bool bCharge = false;                      // squad bayonet charge: run in and use the bayonet
	bool bInCover = false;                     // Target is a covered firing position behind a wall
};

/** Pure: duck behind the wall unless moving, charging, routing, or loaded with a target in sight (rise to fire). */
inline bool PA250ShouldDuck(bool bInCover, bool bAtSpot, bool bLoaded, bool bTargetInSight, bool bRout, bool bCharge)
{
	return bInCover && bAtSpot && !bRout && !bCharge && !(bLoaded && bTargetInSight);
}

UCLASS()
class PLAYAMERICA250_API APA250BotController : public AAIController
{
	GENERATED_BODY()

public:
	APA250BotController(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category = "Bot")
	float AimErrorDegrees = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float SightRange = 9000.f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float EngageRange = 8000.f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float ReloadSkill = 0.55f;

	UPROPERTY(VisibleAnywhere, Category = "Bot")
	EBotState State = EBotState::Advance;

	UPROPERTY(EditAnywhere, Category = "Bot")
	TObjectPtr<UPA250BotTuning> BotTuning;

	int32 SquadId = -1;
	int32 FileIndex = 0;
	int32 ClaimedSlot = INDEX_NONE;
	TWeakObjectPtr<APA250ControlPoint> ClaimedPost;

	void SetPosts(const TArray<TWeakObjectPtr<APA250ControlPoint>>& InPosts) { Posts = InPosts; }
	void SetObjective(APA250ControlPoint* InObjective);
	void ConfigureCrowd(UPA250BotTuning* Tuning);
	void AssignSlot(APA250ControlPoint* Post, int32 SlotIndex, int32 InSquadId, int32 InFileIndex);
	/** Training + tactical role for this soldier (from DA_CombatProfiles) and his reload RNG seed. */
	void ApplyCombatProfile(const FPA250TrainingProfile& InTraining, const FPA250TacticalRole& InRole, int32 Seed);
	const FPA250TrainingProfile& GetTraining() const { return Training; }
	const FPA250TacticalRole& GetRole() const { return Role; }
	bool HasCombatProfile() const { return bHasCombatProfile; }
	/** 0 calm .. 1 overwhelmed, from suppression, squad losses and squad morale. */
	float ComputeStress01(const ARFCharacter* Me) const;
	/** Squad formation order (refreshed every frame by the volley coordinator while the squad has 2+ men). */
	void SetFormationOrder(const FPA250FormationOrder& Order);
	const FPA250FormationOrder& GetFormationOrder() const { return Form; }
	bool HasFormationOrder() const { return FormOrderAge < 0.5f; }
	FVector GetFormationTarget() const { return Form.Target; }

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	ARFCharacter* FindNearestEnemy(const ARFCharacter* Me, float& OutDist) const;
	bool HasLineOfSight(const ARFCharacter* Me, const ARFCharacter* Enemy) const;
	void MoveToward(const ARFCharacter* Me, const FVector& Dest, float Acceptance);
	APA250ControlPoint* PickGoal(const ARFCharacter* Me) const;
	FVector DesiredSlotLocation(const ARFCharacter* Me, APA250ControlPoint* Goal) const;
	void EnsureSlot(ARFCharacter* Me, APA250ControlPoint* Goal);

	TArray<TWeakObjectPtr<APA250ControlPoint>> Posts;
	void TickReload(ARFCharacter* Me);
	FPA250TrainingProfile Training;
	FPA250TacticalRole Role;
	FPA250AIReloadParams ReloadParams;
	FRandomStream ReloadRng;
	bool bHasCombatProfile = false;
	int32 SeenReloads = 0;
	bool TryBayonet(ARFCharacter* Me, ARFCharacter* Enemy, float Dist, const struct FPA250BotTuningValues& T);
	FPA250FormationOrder Form;
	float BaseWalkSpeed = -1.f;
	float FormOrderAge = 1.0e6f;
	TWeakObjectPtr<APA250ControlPoint> Objective;
	/** Stable offset used only when every ring slot is taken. Recomputed when the anchor post changes. */
	FVector LooseSlotOffset = FVector::ZeroVector;
	TWeakObjectPtr<APA250ControlPoint> LooseGoal;
	float ThinkTimer = 0.f;
	float ReactionTimer = 0.f;
	float VolleyJitter = 0.f;
	bool bReactionRolled = false;
};
