#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Battle/PA250Traversal.h"
#include "PA250TraversalComponent.generated.h"

class ARFCharacter;
class UAnimMontage;
class UPrimitiveComponent;

/** Why a traversal did or did not begin (logged, and read by the cases). */
enum class EPA250TraversalStart : uint8
{
	Started,
	Busy,           // already over something, dead, mounted, or inside the cooldown
	NoClips,        // the pipeline's montages are not on disk
	NotGrounded,
	NoObstacle,     // nothing ahead within reach
	WrongObstacle,  // something ahead that is not a wall, fence or field work
	BadApproach,    // too far off the square of it
	NotCrossable,   // too low to matter, too high or too deep: it stays cover
	BlockedPath,    // no room over the top
	BlockedLanding  // no ground, or no room, beyond
};

PLAYAMERICA250_API const TCHAR* PA250TraversalStartName(EPA250TraversalStart Why);

/**
 * Automatic traversal (M2, GAME INVENTION): a man sprinting at a wall, fence or field work goes
 * over it with no key pressed. The obstacle is measured with traces, classified by the data's
 * limits (PA250BattleFeel::ClassifyTraversal), and one of the retargeted sample montages is played
 * with the engine's Motion Warping fitting its root motion to the obstacle. Bots are sent over by
 * the traversal links (PA250TraversalLinks), which call TryStart.
 * The body's pose is shown by the soldier's anim proxy; this component only publishes the clip,
 * its time and its weight. No bone is written here.
 */
UCLASS(ClassGroup = (Movement))
class PLAYAMERICA250_API UPA250TraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPA250TraversalComponent();

	/** Measures what lies within ReachCm along Direction (level) and how it is crossed. */
	EPA250TraversalStart Measure(const FVector& Direction, float ReachCm, FPA250Obstacle& OutObstacle, EPA250Traversal& OutAction,
		UPrimitiveComponent*& OutComponent) const;
	/** Measures, and when the obstacle can be crossed begins the crossing. */
	EPA250TraversalStart TryStart(const FVector& Direction, float ReachCm, FName Gait);
	/** Stops at once (death, the end of play): collision and movement are given back. */
	void Cancel();

	bool IsTraversing() const { return bActive; }
	EPA250Traversal GetAction() const { return Action; }
	int32 GetStartedCount() const { return Started; }
	EPA250TraversalStart GetLastResult() const { return LastResult; }
	const FPA250Obstacle& GetObstacle() const { return Obstacle; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Finish();
	void PublishPose();

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> Montage;
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> Ignored;
	const FPA250TraversalClip* Clip = nullptr;
	FPA250Obstacle Obstacle;
	EPA250Traversal Action = EPA250Traversal::None;
	EPA250TraversalStart LastResult = EPA250TraversalStart::NoObstacle;
	bool bActive = false;   // movement and collision are ours
	bool bPosing = false;   // the montage still shows (it blends out after the movement is given back)
	bool bSavedYaw = false;
	bool bSavedOrient = false;
	float CheckTimer = 0.f;
	float Cooldown = 0.f;
	/** Where the clip stood when last seen, and where it was entered (for the log). */
	float LastClipTime = 0.f;
	float EnteredAt = 0.f;
	int32 Started = 0;
};
