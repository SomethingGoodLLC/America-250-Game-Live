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

/** What a bot does with his bayonet this instant. */
enum class EPA250BotMelee : uint8 { Close, Thrust, Butt, Charge };

struct FPA250BotMeleeView
{
	float DistCm = 0.f;
	float ThrustReachCm = 260.f;   // reach + radius
	float ButtReachCm = 195.f;
	float LungeCm = 600.f;
	bool bFoeGuardsMe = false;     // his musket is up and turned on me
	bool bReadsGuard = false;      // this exchange I answer a guard properly
	bool bResting = false;         // still recovering from my last blow
	bool bChargeReady = false;
	bool bFacing = true;           // a dash goes where I face
};

/** Pure: close in, thrust, butt-stroke or charge. A guard read correctly is answered by closing to
 *  butt-stroke reach and using the stock (the guard-breaker), never by thrusting into it; a man
 *  recovering from his last blow only closes. */
inline EPA250BotMelee PA250ChooseMeleeMove(const FPA250BotMeleeView& V)
{
	if (V.bResting) return EPA250BotMelee::Close;
	if (V.bFoeGuardsMe && V.bReadsGuard) return V.DistCm <= V.ButtReachCm ? EPA250BotMelee::Butt : EPA250BotMelee::Close;
	if (V.DistCm <= V.ThrustReachCm) return EPA250BotMelee::Thrust;
	if (V.DistCm <= V.LungeCm && V.bChargeReady && V.bFacing) return EPA250BotMelee::Charge;
	return EPA250BotMelee::Close;
}

/**
 * Dragoons (GAME INVENTION for 1775): mounted infantry. They ride to the fight, get down out of musket
 * range, fight on foot as skirmishers and walk back to their horse to remount when there is ground to cover.
 */
enum class EPA250DragoonAct : uint8 { Ride, Dismount, FightOnFoot, ReturnToHorse, Mount };

struct FPA250DragoonView
{
	bool bMounted = false;
	bool bHorseAlive = false;
	float EnemyDistCm = -1.f; // nearest enemy; < 0 when none is known
	float GoalDistCm = 0.f;   // to the post he is making for
	float HorseDistCm = 0.f;  // to his own (picketed) horse
	bool bReturning = false;  // already walking back to (or mounting) his horse
};

inline constexpr float PA250DragoonDismountCm = 6000.f;  // get down at the edge of musket range (60 m)
inline constexpr float PA250DragoonClearCm = 12000.f;    // remount only with no enemy inside 120 m
inline constexpr float PA250DragoonGoalNearCm = 2500.f;  // at the post: fight there on foot
inline constexpr float PA250DragoonMountReachCm = 250.f; // close enough to his horse to mount

inline EPA250DragoonAct PA250DragoonDecide(const FPA250DragoonView& V)
{
	const bool bEnemyNear = V.EnemyDistCm >= 0.f && V.EnemyDistCm <= PA250DragoonDismountCm;
	if (V.bMounted) return (bEnemyNear || V.GoalDistCm <= PA250DragoonGoalNearCm) ? EPA250DragoonAct::Dismount : EPA250DragoonAct::Ride;
	if (!V.bHorseAlive) return EPA250DragoonAct::FightOnFoot;
	const bool bClear = V.EnemyDistCm < 0.f || V.EnemyDistCm > PA250DragoonClearCm;
	// Hysteresis: set off for the horse when the post is over 60 m away, keep going until it is within 35 m.
	const float Need = PA250DragoonGoalNearCm * (V.bReturning ? 1.4f : 2.4f);
	if (bClear && V.GoalDistCm > Need)
	{
		return V.HorseDistCm <= PA250DragoonMountReachCm ? EPA250DragoonAct::Mount : EPA250DragoonAct::ReturnToHorse;
	}
	return EPA250DragoonAct::FightOnFoot;
}

/** Gait for the ground still to cover: gallop beyond 40 m, canter beyond 15 m, else trot. */
inline uint8 PA250DragoonGait(float DistCm) { return DistCm > 4000.f ? 3 : DistCm > 1500.f ? 2 : 1; }

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
	/** One hand-to-hand decision against Target (he is already being closed with): see PA250ChooseMeleeMove. */
	void MeleeAttack(ARFCharacter* Me, ARFCharacter* Target, float Dist, bool bFacing);
	/** This man's marksmanship relative to his training's average (fixed for his life). */
	float GetAimScale() const { return AimScale; }
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
	float AimScale = 1.f;
	int32 SeenReloads = 0;
	bool TryBayonet(ARFCharacter* Me, ARFCharacter* Enemy, float Dist, const struct FPA250BotTuningValues& T);
	/** Dragoon riding and remounting; false when he should fight on foot like any skirmisher this think. */
	bool TickDragoon(ARFCharacter* Me, ARFCharacter* Enemy, float Dist);
	EPA250DragoonAct LastDragoonAct = EPA250DragoonAct::FightOnFoot;
	float MeleeRest = 0.f;
	bool bWasMeleeBusy = false;
	bool bReadsGuard = true;
	bool bLastMoveWasButt = false;
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
