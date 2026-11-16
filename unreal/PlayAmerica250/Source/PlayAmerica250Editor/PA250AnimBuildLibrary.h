#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PA250AnimBuildLibrary.generated.h"

class UAnimMontage;
class UAnimSequence;
class UPoseSearchDatabase;
class UPoseSearchSchema;
class USkeleton;

/**
 * Editor steps of the animation pipeline (Scripts/build_locomotion.py) that need C++: the engine
 * exposes no script call that fills a pose-search database or names a schema's skeleton.
 */
UCLASS()
class PLAYAMERICA250EDITOR_API UPA250AnimBuildLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Point a (duplicated) schema at another skeleton. Its mirror table is cleared: it belonged to the old one. */
	UFUNCTION(BlueprintCallable, Category = "PA250|Animation")
	static bool SetSchemaSkeleton(UPoseSearchSchema* Schema, USkeleton* Skeleton);

	/**
	 * A copy of Source at OutPackagePath (a new package; an existing one is refused) whose clips are
	 * the clips of the same name in ClipFolder and whose schema is Schema. Sampling ranges and cost
	 * settings are kept. Entries are set to unmirrored (the schema has no mirror table) and the
	 * normalization set is cleared. A clip that is missing from ClipFolder is dropped and named in
	 * OutMissing. Returns null when nothing could be built. The caller saves the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "PA250|Animation")
	static UPoseSearchDatabase* BuildDatabase(UPoseSearchDatabase* Source, UPoseSearchSchema* Schema, const FString& ClipFolder,
		const FString& OutPackagePath, TArray<FString>& OutMissing);

	/**
	 * A soldier copy of one of the sample's traversal montages at OutPackagePath (a new package; an
	 * existing one is refused): the same span of SoldierClip (the retargeted sequence), and only the
	 * motion-warping windows kept (the sample's other notifies name its own Blueprints). The sample
	 * warps to a bone, `attach`, that the soldier's skeleton lacks; here each window is given a warp
	 * target name of its own (PA250Warp0, 1, ...) with no warp point, and where the root stands
	 * against the sample's point at the window's end is measured and returned, so the battle can
	 * place each target itself.
	 * Returns JSON (empty on failure): length_s, scale (soldier root travel over the sample's),
	 * blend_out_s (when the sample lets the clip be left for ordinary movement),
	 * windows[] {name, target, start_s, end_s, root_cm [forward, right, up], point_cm or null} and
	 * approach[] [time_s, root forward cm], all in the clip's own space, soldier-sized.
	 * The caller saves the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "PA250|Animation")
	static FString BuildTraversalMontage(UAnimMontage* Source, UAnimSequence* SoldierClip, const FString& OutPackagePath);

	/** The clips a database holds, as object paths (for checks). */
	UFUNCTION(BlueprintCallable, Category = "PA250|Animation")
	static TArray<FString> DatabaseClips(const UPoseSearchDatabase* Database);
};
