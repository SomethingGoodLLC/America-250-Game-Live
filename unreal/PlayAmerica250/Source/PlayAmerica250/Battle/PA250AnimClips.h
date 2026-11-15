#pragma once

#include "CoreMinimal.h"
#include "Battle/PA250BattleFeel.h"

class UPoseSearchDatabase;

/**
 * Animation assets named by data (unreal/PA250Sim/data/anim_clips.json). M1: the motion-matching
 * databases Scripts/build_locomotion.py made on the soldier's skeleton, by gait.
 * Game thread only. A database that does not load is an Error line, and the soldiers stay on the
 * cheap path (LocomotionReady() is false).
 */
namespace PA250AnimClips
{
PLAYAMERICA250_API FString DataPath();
/** Loads the locomotion block and its databases. False, with the reasons, when any is missing. */
PLAYAMERICA250_API bool LoadLocomotion(const FString& Path, TArray<FString>* OutErrors = nullptr);
/** True when every database of every gait loaded. Loads DataPath() on first use. */
PLAYAMERICA250_API bool LocomotionReady();
/**
 * The databases to search for a soldier moving at this gait. Standing: idles and turns in place.
 * Moving: that gait's loops, pivots and starts. Coming to a halt (bStopping): that gait's stops and
 * the idles, so a stop is matched as a stop and not as a slow loop.
 */
PLAYAMERICA250_API const TArray<UPoseSearchDatabase*>& LocomotionDatabases(EPA250FootGait Gait, bool bStopping);
/** How many databases are loaded (14 when the pipeline's output is whole). */
PLAYAMERICA250_API int32 LocomotionDatabaseCount();
}
