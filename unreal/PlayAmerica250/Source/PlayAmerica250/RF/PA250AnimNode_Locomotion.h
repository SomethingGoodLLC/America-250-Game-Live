#pragma once

#include "CoreMinimal.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PA250AnimNode_Locomotion.generated.h"

class UPoseSearchDatabase;

/**
 * Motion-matched locomotion for the period soldier, hosted natively by FPA250SoldierAnimProxy
 * (no Animation Blueprint). TRIAL (plan of 2026-11-15, change 3): it runs the engine's pose search
 * (UPoseSearchLibrary::MotionMatch, experimental in 5.8) over the databases the proxy hands it and
 * plays the result through the engine's standalone blend stack. The engine's own
 * FAnimNode_MotionMatching cannot be owned natively: it reads folded node data, which only a
 * compiled Animation Blueprint supplies.
 *
 * It must sit under an FAnimNode_PoseSearchHistoryCollector (the search reads the pose history from
 * the graph message that node publishes). Every number it uses is handed in from battle_feel.json.
 */
USTRUCT()
struct FPA250AnimNode_Locomotion : public FAnimNode_BlendStack_Standalone
{
	GENERATED_BODY()

	/** What to search this frame. Raw pointers: the anim instance keeps the databases alive. */
	TArray<const UPoseSearchDatabase*> Databases;
	float BlendSeconds = 0.f;
	/** Least time between two searches (the player: 0, every frame). */
	float SearchThrottleSeconds = 0.f;
	FFloatInterval PlayRate = FFloatInterval(1.f, 1.f);

	/** Searches run and jumps to a new clip since the node was reset (for the probe and the cases). */
	int32 Searches = 0;
	int32 Jumps = 0;
	/** The last update found a pose history above it. False means the node is wired wrongly. */
	bool bHadHistory = false;

	/** True once a clip is playing: until then the proxy shows the cheap path. */
	bool HasPose() const { return !AnimPlayers.IsEmpty(); }
	/** Forget what was playing (a soldier leaving full quality). */
	void ResetLocomotion();

	// FAnimNode_AssetPlayerBase
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;

private:
	FMotionMatchingState State;
};
