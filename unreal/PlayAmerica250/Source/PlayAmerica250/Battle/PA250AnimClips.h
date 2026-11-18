#pragma once

#include "CoreMinimal.h"
#include "Battle/PA250BattleFeel.h"
#include "RF/ActiveReloadSequencer.h"

class UAnimSequence;
class UPoseSearchDatabase;
class ARFCharacter;
struct FPA250ActionParams;

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
	/** Takes of the clip (package paths); empty: Asset alone. */
	TArray<FString> Variants;
	/** Gun crew: where he stands for it and the point he faces, in the cannon's mesh space (cm). */
	bool bHasStation = false;
	FVector2D StationCm = FVector2D::ZeroVector;
	FVector2D FaceCm = FVector2D::ZeroVector;
	FName Stage;
	FName Stance;
};

/** What a gun crewman holds while one of his clips plays (anim_clips.json gun_tools.by_clip). */
enum class EPA250ToolHold : uint8 { Bore, Upright, ToVent, InHand, Planted };
struct FPA250CrewToolRule
{
	FName Tool;               // a key of gun_tools.meshes
	EPA250ToolHold Hold = EPA250ToolHold::Upright;
	bool bSpongeHead = true;  // bore: the sponge (+X) into the barrel, else the rammer (-X)
	float MidCm = 0.f;        // upright, planted: the tool's middle above the feet / the ground
	float OffsetCm = 0.f;     // to_vent: its middle this far from the hand toward the vent
	float UntilSeconds = 0.f; // in_hand: shown until this far into the clip
	FVector2D PlantedCm = FVector2D::ZeroVector;
};
/** The crew's tools (gun_tools): the measured bore line and vent, in the cannon's mesh space (cm). */
struct FPA250GunTools
{
	bool bLoaded = false;
	FVector BoreOriginCm = FVector::ZeroVector;
	FVector BoreDirection = FVector::ForwardVector;
	FVector VentCm = FVector::ZeroVector;
	TMap<FName, FString> Meshes;
	TMap<FName, FPA250CrewToolRule> ByClip;
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
/** The gun crew's tools (gun_tools; read with the registry). */
PLAYAMERICA250_API const FPA250GunTools& GunTools();
/** The tool rule for a crew clip, or null: he holds nothing for it. */
PLAYAMERICA250_API const FPA250CrewToolRule* CrewToolRule(FName Clip);
/**
 * The clip to play for a name, or null: its placeholder shows. A name with no asset (or one whose
 * asset does not load on this machine) logs "PA250 anim clips: PLACEHOLDER <name>" once; a name the
 * registry does not hold logs an Error once.
 */
PLAYAMERICA250_API UAnimSequence* Clip(FName Name);
/** Names asked for so far that fell to their placeholder, and that the registry does not hold. */
PLAYAMERICA250_API int32 PlaceholdersLogged();
PLAYAMERICA250_API int32 UnknownLogged();

/** How many takes a name has (its variants; 1 for an asset alone, 0 with no clip). */
PLAYAMERICA250_API int32 VariantCount(FName Name);
/** The take of a name a seed picks (the same seed, the same take). Null as Clip() is, with the same lines. */
PLAYAMERICA250_API UAnimSequence* ClipVariant(FName Name, uint32 Seed);
/** Pure: the registry suffix for where a blast was, seen from the man: f (in front of him), b, l, r. */
PLAYAMERICA250_API const TCHAR* BlastSuffix(uint8 BlastDir);
/**
 * Plays a registered clip over a soldier's whole body (UPA250SoldierAnimInstance::PlayAction; the
 * soldier has no montage slot). The take is picked by Seed. OnNotify gets the name of each notify
 * the clip passes, and NAME_None when it is over. Returns the seconds it will show; 0 when the name
 * has no clip on this machine (its placeholder shows, as the registry says) or the man is no
 * period soldier.
 */
PLAYAMERICA250_API float PlayCombatClip(ARFCharacter* Man, FName Name, uint32 Seed,
	const FPA250ActionParams* Params = nullptr, TFunction<void(FName)> OnNotify = nullptr);
}
