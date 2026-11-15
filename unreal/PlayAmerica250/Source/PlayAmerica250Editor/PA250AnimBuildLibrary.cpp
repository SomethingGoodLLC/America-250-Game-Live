#include "PA250AnimBuildLibrary.h"

#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "UObject/Package.h"

bool UPA250AnimBuildLibrary::SetSchemaSkeleton(UPoseSearchSchema* Schema, USkeleton* Skeleton)
{
	if (!Schema || !Skeleton) return false;
	FPoseSearchRoledSkeleton* Roled = Schema->GetRoledSkeleton(UE::PoseSearch::DefaultRole);
	if (!Roled)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: schema %s has no skeleton for the default role"), *Schema->GetPathName());
		return false;
	}
	Schema->Modify();
	Roled->Skeleton = Skeleton;
	Roled->MirrorDataTable = nullptr;
	FPropertyChangedEvent Changed(nullptr);
	Schema->PostEditChangeProperty(Changed);
	Schema->MarkPackageDirty();
	return true;
}

UPoseSearchDatabase* UPA250AnimBuildLibrary::BuildDatabase(UPoseSearchDatabase* Source, UPoseSearchSchema* Schema, const FString& ClipFolder,
	const FString& OutPackagePath, TArray<FString>& OutMissing)
{
	OutMissing.Reset();
	if (!Source || !Schema || !FPackageName::IsValidLongPackageName(OutPackagePath)) return nullptr;
	if (FPackageName::DoesPackageExist(OutPackagePath) || FindPackage(nullptr, *OutPackagePath))
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s exists; it is not overwritten"), *OutPackagePath);
		return nullptr;
	}
	UPackage* Package = CreatePackage(*OutPackagePath);
	const FString Name = FPackageName::GetLongPackageAssetName(OutPackagePath);
	UPoseSearchDatabase* Out = DuplicateObject<UPoseSearchDatabase>(Source, Package, *Name);
	if (!Out) return nullptr;
	Out->SetFlags(RF_Public | RF_Standalone);
	Out->Schema = Schema;
	Out->NormalizationSet = nullptr;
	// Back to front, so dropping an entry does not move the ones still to be read.
	for (int32 i = Out->GetNumAnimationAssets() - 1; i >= 0; --i)
	{
		FPoseSearchDatabaseAnimationAsset* Entry = Out->GetMutableDatabaseAnimationAsset(i);
		const UObject* Old = Entry ? Entry->GetAnimationAsset() : nullptr;
		if (!Old)
		{
			Out->RemoveAnimationAssetAt(i);
			continue;
		}
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), *ClipFolder, *Old->GetName(), *Old->GetName());
		UObject* Clip = StaticLoadObject(UAnimationAsset::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn);
		if (!Clip)
		{
			OutMissing.Add(Old->GetName());
			Out->RemoveAnimationAssetAt(i);
			continue;
		}
		Entry->AnimAsset = Clip;
#if WITH_EDITORONLY_DATA
		Entry->MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
#endif
	}
	if (Out->GetNumAnimationAssets() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s would hold no clip"), *OutPackagePath);
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Out);
	Package->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("PA250 anim build: %s holds %d clips (%d missing)"), *OutPackagePath, Out->GetNumAnimationAssets(), OutMissing.Num());
	return Out;
}

TArray<FString> UPA250AnimBuildLibrary::DatabaseClips(const UPoseSearchDatabase* Database)
{
	TArray<FString> Out;
	if (!Database) return Out;
	for (int32 i = 0; i < Database->GetNumAnimationAssets(); ++i)
	{
		const FPoseSearchDatabaseAnimationAsset* Entry = Database->GetDatabaseAnimationAsset(i);
		const UObject* Clip = Entry ? Entry->GetAnimationAsset() : nullptr;
		Out.Add(Clip ? Clip->GetPathName() : FString());
	}
	return Out;
}
