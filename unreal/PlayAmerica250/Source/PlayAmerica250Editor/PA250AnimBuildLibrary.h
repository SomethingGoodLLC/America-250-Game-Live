#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PA250AnimBuildLibrary.generated.h"

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

	/** The clips a database holds, as object paths (for checks). */
	UFUNCTION(BlueprintCallable, Category = "PA250|Animation")
	static TArray<FString> DatabaseClips(const UPoseSearchDatabase* Database);
};
