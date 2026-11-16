// PA250Traversal.h — automatic traversal (M2): the clips the pipeline baked, and the pure rules that
// fit one to an obstacle. Numbers come from battle_feel.json (traversal) and anim_clips.json
// (traversal, written by Scripts/build_locomotion.py); none live here.
#pragma once

#include "CoreMinimal.h"
#include "Battle/PA250BattleFeel.h"

class UAnimMontage;
class UAnimSequence;

/** One motion-warping window of a traversal clip, in the clip's own space (cm: X forward, Y right, Z up). */
struct FPA250TraversalWindow
{
	FName Name;                         // the warp target the montage's window asks for
	FString Target;                     // what the sample aimed it at: FrontLedge, BackLedge, BackFloor
	float StartS = 0.f;
	float EndS = 0.f;
	FVector RootCm = FVector::ZeroVector;   // where the root stands at the window's end
	bool bHasPoint = false;
	FVector PointCm = FVector::ZeroVector;  // the point on the obstacle the sample warped to
};

struct FPA250TraversalClip
{
	FString Asset;
	EPA250Traversal Action = EPA250Traversal::None;
	FName Gait;                          // run, walk, stand
	bool bLeftFoot = false;
	int32 Variant = 1;
	float LengthS = 0.f;
	float BlendOutS = 0.f;               // when the clip may be left for ordinary movement
	TArray<FPA250TraversalWindow> Windows;
	TArray<FVector2D> Approach;          // (time s, root forward cm)
	UAnimMontage* Montage = nullptr;     // kept alive by the registry
	UAnimSequence* Sequence = nullptr;   // the montage's one clip
};

/** An obstacle as a man running at it meets it. */
struct FPA250Obstacle
{
	FVector FrontLedge = FVector::ZeroVector; // top of the near face, where he meets it
	FVector Across = FVector::ForwardVector;  // level unit vector, the way over
	float HeightCm = 0.f;                     // from the ground he stands on
	float DepthCm = 0.f;                      // through it, along Across
	FVector BackFloor = FVector::ZeroVector;  // the ground beyond (a mantle: the top he ends on)
};

namespace PA250Traversal
{
/** Pure: the clip-space forward coordinate of the obstacle's near face (the first warp point; the
 *  root's place at the first window's end when the clip has none). */
PLAYAMERICA250_API float LedgeForwardCm(const FPA250TraversalClip& Clip);
/** Pure: the height the clip was made for (its first warp point's height), 0 when it has none. */
PLAYAMERICA250_API float AuthoredHeightCm(const FPA250TraversalClip& Clip);
/** Pure: where in the clip to begin so that its run-up is no longer than DistanceCm, the man's
 *  distance from the near face: the first moment the clip's root is that close. Never later than
 *  LeadS before the first window ends; 0 when he is farther off than the clip's whole run-up. */
PLAYAMERICA250_API float StartTimeFor(const FPA250TraversalClip& Clip, float DistanceCm, float LeadS);
/** Pure: where the root must stand at the end of window Index, in the world. A window with a point
 *  keeps the clip's offset from it, its height share scaled to the real obstacle. A window with
 *  none goes to the place itself: BackLedge is the top of the far face; anything else (the landing)
 *  is the ground beyond, at least LandingClearCm past the far face and no nearer than the clip's
 *  own landing. */
PLAYAMERICA250_API FVector WindowTarget(const FPA250TraversalClip& Clip, int32 Index, const FPA250Obstacle& Obstacle, float LandingClearCm);
/** Pure: the clip for an action at a gait: the Pick-th of those that match (walk falls back to
 *  run, stand to walk then run). A hurdle or vault clip that does not carry him MinBeyondCm past
 *  the near face by the time it may be left is passed over (it would leave him inside a deep
 *  wall), unless no clip of the action does. Null when the action has none. */
PLAYAMERICA250_API const FPA250TraversalClip* PickClip(const TArray<FPA250TraversalClip>& Clips, EPA250Traversal Action, FName Gait, int32 Pick,
	float MinBeyondCm = 0.f);
/** Pure: how far past the obstacle's near face the clip's root is when the clip may be left. */
PLAYAMERICA250_API float BeyondAtBlendOutCm(const FPA250TraversalClip& Clip);

/** Reads the traversal block of anim_clips.json; loads the montages when bLoadAssets. False, with
 *  the reasons, when the block or a montage is missing. */
PLAYAMERICA250_API bool LoadClips(const FString& Path, bool bLoadAssets, TArray<FPA250TraversalClip>& Out, TArray<FString>* OutErrors = nullptr);
/** The loaded clips (PA250AnimClips::DataPath(), montages loaded), read on first use. Empty, after
 *  one Error line, when the pipeline's output is missing: nobody traverses. */
PLAYAMERICA250_API const TArray<FPA250TraversalClip>& Clips();
}
