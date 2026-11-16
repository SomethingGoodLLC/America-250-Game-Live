#pragma once

#include "CoreMinimal.h"
#include "Battle/PA250BattleFeel.h"
#include "RF/ActiveReloadSequencer.h"

class UAnimSequence;
class UPoseSearchDatabase;
class ARFCharacter;

/** One name of the clip registry (anim_clips.json clips): a clip the battle can show, with or without an asset. */
struct FPA250ClipEntry
{
	FName Name;
	FString Asset;      // package path; empty: the placeholder shows
	FName Group;
	FName Placeholder;  // procedural, locomotion, none
	FString Shows;      // what the player sees in place of the clip
	bool bLoop = false;
	bool bWired = false;
	FName Stage;
	FName Stance;
};

/**
 * Animation assets named by data (unreal/PA250Sim/data/anim_clips.json). M1: the motion-matching
 * databases Scripts/build_locomotion.py made on the soldier's skeleton, by gait.
 * Game thread only. A database that does not load is an Error line, and the soldiers stay on the
 * cheap path (LocomotionReady() is false).
 */
namespace PA250AnimClips
{
/** unreal/PA250Sim/data/anim_clips.json, or the file named by -PA250AnimClips=<file>. */
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

// The clip registry (M4). Game thread only.
/** Pure: the registry name for a stage of the active reload, reload_<stand|kneel>_<stage>
 *  (shared vector drill_clip_names.json). NAME_None when nothing is being loaded. */
PLAYAMERICA250_API FName DrillClipName(EActiveReloadPhase Phase, bool bKneel);
/** Reads the clips block. False, with the reasons, when it is missing or an entry is malformed. */
PLAYAMERICA250_API bool LoadRegistry(const FString& Path, TArray<FString>* OutErrors = nullptr);
/** The registry's names (DataPath(), read on first use). */
PLAYAMERICA250_API const TArray<FPA250ClipEntry>& Registry();
PLAYAMERICA250_API const FPA250ClipEntry* FindClip(FName Name);
/**
 * The clip to play for a name, or null: its placeholder shows. A name with no asset (or one whose
 * asset does not load on this machine) logs "PA250 anim clips: PLACEHOLDER <name>" once; a name the
 * registry does not hold logs an Error once.
 */
PLAYAMERICA250_API UAnimSequence* Clip(FName Name);
/** Names asked for so far that fell to their placeholder, and that the registry does not hold. */
PLAYAMERICA250_API int32 PlaceholdersLogged();
PLAYAMERICA250_API int32 UnknownLogged();

/**
 * Combat montage drop folder (Artillery M9). Found path: /Game/PA250/Anim
 * (anim_clips.json locomotion.clip_folder = /Game/PA250/Anim/Loco; rebuild deletes Content/PA250/Anim).
 * Reads manifest.json there. Server clips are AnimSequences AM_*_vN; played as dynamic montages.
 * Missing name → closest fallback; one Warning per name, never Error. Never downloads.
 */
PLAYAMERICA250_API FString CombatDropFolder();
PLAYAMERICA250_API bool LoadCombatManifest(TArray<FString>* OutWarnings = nullptr);
/** Pick a variant AnimSequence for a logical name (e.g. AM_GetUp_Back) using Seed. Null if missing. */
PLAYAMERICA250_API class UAnimSequence* PickCombatClip(const FString& LogicalName, uint32 Seed);
/**
 * Play a combat AnimSequence as a dynamic montage on DefaultSlot.
 * Returns length, or 0 if the clip is missing / the mesh has no AnimInstance.
 */
PLAYAMERICA250_API float PlayCombatClip(ARFCharacter* Man, const FString& LogicalName, uint32 Seed,
	float BlendIn = 0.1f, float BlendOut = 0.2f, float PlayRate = 1.f);
}
