#include "PA250AnimBuildLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "AnimNotifyState_MotionWarping.h"
#include "RootMotionModifier.h"
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

namespace
{
// The clips run along +Y of their own space with the man's right hand toward -X.
FString Triple(const FVector& V)
{
	return FString::Printf(TEXT("[%.1f, %.1f, %.1f]"), V.Y, -V.X, V.Z);
}

FVector RootAt(const UAnimSequence* Clip, double Time)
{
	return Clip->ExtractRootTrackTransform(FAnimExtractContext(Time), nullptr).GetLocation();
}

/** A bone's place in the clip's own space (its local transforms composed up to and through the root). */
bool BoneAt(const UAnimSequence* Clip, FName Bone, double Time, FVector& Out)
{
	const USkeleton* Skeleton = Clip ? Clip->GetSkeleton() : nullptr;
	if (!Skeleton) return false;
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	int32 Index = Ref.FindBoneIndex(Bone);
	if (Index == INDEX_NONE) return false;
	FTransform Total = FTransform::Identity;
	while (Index != INDEX_NONE)
	{
		FTransform Local;
		Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Index), FAnimExtractContext(Time), false);
		Total = Total * Local;
		Index = Ref.GetParentIndex(Index);
	}
	Out = Total.GetLocation();
	return true;
}
}

FString UPA250AnimBuildLibrary::BuildTraversalMontage(UAnimMontage* Source, UAnimSequence* SoldierClip, const FString& OutPackagePath)
{
	if (!Source || !SoldierClip || !FPackageName::IsValidLongPackageName(OutPackagePath)) return FString();
	if (FPackageName::DoesPackageExist(OutPackagePath) || FindPackage(nullptr, *OutPackagePath))
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s exists; it is not overwritten"), *OutPackagePath);
		return FString();
	}
	if (Source->SlotAnimTracks.Num() != 1 || Source->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s is not one clip on one slot"), *Source->GetPathName());
		return FString();
	}
	const FAnimSegment& SourceSegment = Source->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
	const UAnimSequence* SourceClip = Cast<UAnimSequence>(SourceSegment.GetAnimReference());
	if (!SourceClip || !FMath::IsNearlyEqual(SourceSegment.AnimPlayRate, 1.f) || !FMath::IsNearlyZero(SourceSegment.AnimStartTime) ||
		!FMath::IsNearlyZero(SourceSegment.StartPos))
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s does not play its clip from the start at rate 1"), *Source->GetPathName());
		return FString();
	}

	UPackage* Package = CreatePackage(*OutPackagePath);
	const FString Name = FPackageName::GetLongPackageAssetName(OutPackagePath);
	UAnimMontage* Out = DuplicateObject<UAnimMontage>(Source, Package, *Name);
	if (!Out) return FString();
	Out->SetFlags(RF_Public | RF_Standalone);
	Out->SetSkeleton(SoldierClip->GetSkeleton());
	Out->SetPreviewMesh(nullptr);
	Out->SlotAnimTracks[0].AnimTrack.AnimSegments[0].SetAnimReference(SoldierClip, false);
	// The sample marks, with a notify of its own Blueprint class, when the clip may be left for
	// ordinary movement (what follows is only its run-out). The class is not kept; the moment is.
	double BlendOut = -1.0;
	for (const FAnimNotifyEvent& Event : Out->Notifies)
	{
		// (By name: the Blueprint class itself is not staged, so the object does not load here.)
		if (Event.NotifyName.ToString().Contains(TEXT("TraversalBlendOut")))
		{
			BlendOut = Event.GetTriggerTime();
		}
	}
	Out->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
	{
		const UAnimNotifyState_MotionWarping* Warp = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
		return !Warp || !Cast<URootMotionModifier_Warp>(Warp->RootMotionModifier);
	});
	Out->Notifies.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.GetTriggerTime() < B.GetTriggerTime(); });

	// The soldier's root travels a little farther than the sample's (longer legs): the sample's warp
	// points are scaled by the same share.
	const double Length = Out->GetPlayLength();
	const double SourceTravel = (RootAt(SourceClip, Length) - RootAt(SourceClip, 0.0)).Size2D();
	const double SoldierTravel = (RootAt(SoldierClip, Length) - RootAt(SoldierClip, 0.0)).Size2D();
	const double Scale = SourceTravel > 1.0 ? SoldierTravel / SourceTravel : 1.0;

	FString Windows;
	for (int32 i = 0; i < Out->Notifies.Num(); ++i)
	{
		FAnimNotifyEvent& Event = Out->Notifies[i];
		URootMotionModifier_Warp* Modifier = CastChecked<URootMotionModifier_Warp>(CastChecked<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass)->RootMotionModifier);
		const double Start = Event.GetTriggerTime(), End = Event.GetEndTriggerTime();
		FString Point = TEXT("null");
		if (Modifier->WarpPointAnimProvider == EWarpPointAnimProvider::Bone)
		{
			FVector At;
			if (!BoneAt(SourceClip, Modifier->WarpPointAnimBoneName, End, At))
			{
				UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s warps to bone %s, which its clip lacks"), *Source->GetPathName(), *Modifier->WarpPointAnimBoneName.ToString());
				return FString();
			}
			Point = Triple(At * Scale);
		}
		else if (Modifier->WarpPointAnimProvider == EWarpPointAnimProvider::Static)
		{
			Point = Triple(Modifier->WarpPointAnimTransform.GetLocation() * Scale);
		}
		const FString WindowName = FString::Printf(TEXT("PA250Warp%d"), i);
		Windows += FString::Printf(TEXT("%s{\"name\": \"%s\", \"target\": \"%s\", \"start_s\": %.3f, \"end_s\": %.3f, \"root_cm\": %s, \"point_cm\": %s}"),
			i ? TEXT(", ") : TEXT(""), *WindowName, *Modifier->WarpTargetName.ToString(), Start, End, *Triple(RootAt(SoldierClip, End)), *Point);
		Modifier->WarpTargetName = FName(*WindowName);
		Modifier->WarpPointAnimProvider = EWarpPointAnimProvider::None;
		Modifier->WarpPointAnimBoneName = NAME_None;
		Modifier->WarpPointAnimTransform = FTransform::Identity;
	}
	if (Out->Notifies.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim build: %s has no motion-warping window"), *Source->GetPathName());
		return FString();
	}
	FString Approach;
	for (double T = 0.0; T <= Length + 1.e-3; T += 0.1)
	{
		Approach += FString::Printf(TEXT("%s[%.2f, %.1f]"), Approach.IsEmpty() ? TEXT("") : TEXT(", "), T, RootAt(SoldierClip, FMath::Min(T, Length)).Y);
	}
	Out->RefreshCacheData();
	FAssetRegistryModule::AssetCreated(Out);
	Package->MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("PA250 anim build: %s, %d warp windows, scale %.3f"), *OutPackagePath, Out->Notifies.Num(), Scale);
	if (BlendOut < 0.0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250 anim build: %s has no blend-out mark; the end of its last window is used"), *Source->GetPathName());
		BlendOut = Out->Notifies.Last().GetEndTriggerTime();
	}
	return FString::Printf(TEXT("{\"length_s\": %.3f, \"scale\": %.4f, \"blend_out_s\": %.3f, \"windows\": [%s], \"approach\": [%s]}"), Length, Scale, BlendOut,
		*Windows, *Approach);
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
