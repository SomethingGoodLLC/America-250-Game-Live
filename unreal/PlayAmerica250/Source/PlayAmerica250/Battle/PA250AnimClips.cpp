#include "PA250AnimClips.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "RFCharacter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FLocomotionSet
{
	bool bTried = false;
	bool bReady = false;
	/** Every database, kept alive here. */
	TArray<TStrongObjectPtr<UPoseSearchDatabase>> Owned;
	/** [gait][0 = moving or standing, 1 = stopping] */
	TArray<UPoseSearchDatabase*> Sets[4][2];
};

FLocomotionSet& Set()
{
	static FLocomotionSet S;
	return S;
}

const TCHAR* const GaitKeys[4] = {TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("sprint")};
}

FString PA250AnimClips::DataPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/anim_clips.json")));
}

bool PA250AnimClips::LoadLocomotion(const FString& Path, TArray<FString>* OutErrors)
{
	FLocomotionSet& S = Set();
	S = FLocomotionSet();
	S.bTried = true;
	TArray<FString> Errors;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Loco = nullptr;
	const TSharedPtr<FJsonObject>* Databases = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Errors.Add(FString::Printf(TEXT("cannot read %s"), *Path));
	}
	else if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Errors.Add(FString::Printf(TEXT("%s is not valid JSON"), *Path));
	}
	else if (!Root->TryGetObjectField(TEXT("locomotion"), Loco) || !(*Loco)->TryGetObjectField(TEXT("databases"), Databases))
	{
		Errors.Add(TEXT("no \"locomotion.databases\" object (run Scripts/build_locomotion.py)"));
	}
	TMap<FString, UPoseSearchDatabase*> ByKey;
	if (Databases)
	{
		for (int32 g = 0; g < 4; ++g)
		{
			const TSharedPtr<FJsonObject>* Kinds = nullptr;
			if (!(*Databases)->TryGetObjectField(GaitKeys[g], Kinds))
			{
				Errors.Add(FString::Printf(TEXT("no databases for gait \"%s\""), GaitKeys[g]));
				continue;
			}
			for (const auto& Pair : (*Kinds)->Values)
			{
				const FString Package = Pair.Value->AsString();
				const FString ObjectPath = Package + TEXT(".") + FPaths::GetBaseFilename(Package);
				UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *ObjectPath);
				if (!Database)
				{
					Errors.Add(FString::Printf(TEXT("database %s (%s %s) does not load"), *Package, GaitKeys[g], *Pair.Key));
					continue;
				}
				S.Owned.Emplace(Database);
				ByKey.Add(FString(GaitKeys[g]) + TEXT(".") + Pair.Key, Database);
			}
		}
	}
	auto Add = [&ByKey, &Errors](TArray<UPoseSearchDatabase*>& Out, const TCHAR* Gait, const TCHAR* Kind)
	{
		if (UPoseSearchDatabase** Found = ByKey.Find(FString(Gait) + TEXT(".") + Kind)) Out.Add(*Found);
		else Errors.Add(FString::Printf(TEXT("no database \"%s.%s\""), Gait, Kind));
	};
	if (Databases)
	{
		Add(S.Sets[0][0], TEXT("idle"), TEXT("idles"));
		Add(S.Sets[0][0], TEXT("idle"), TEXT("turn_in_place"));
		S.Sets[0][1] = S.Sets[0][0];
		for (int32 g = 1; g < 4; ++g)
		{
			Add(S.Sets[g][0], GaitKeys[g], TEXT("loops"));
			Add(S.Sets[g][0], GaitKeys[g], TEXT("pivots"));
			Add(S.Sets[g][0], GaitKeys[g], TEXT("starts"));
			Add(S.Sets[g][1], GaitKeys[g], TEXT("stops"));
			Add(S.Sets[g][1], TEXT("idle"), TEXT("idles"));
		}
	}
	for (const FString& E : Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: %s"), *E);
	}
	if (OutErrors) *OutErrors = Errors;
	S.bReady = Errors.IsEmpty();
	if (S.bReady)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 anim clips: %d locomotion databases loaded"), S.Owned.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: FALLBACK, soldiers stay on the cheap animation path. Fix %s."), *Path);
	}
	return S.bReady;
}

bool PA250AnimClips::LocomotionReady()
{
	FLocomotionSet& S = Set();
	if (!S.bTried) LoadLocomotion(DataPath());
	return S.bReady;
}

const TArray<UPoseSearchDatabase*>& PA250AnimClips::LocomotionDatabases(EPA250FootGait Gait, bool bStopping)
{
	LocomotionReady();
	return Set().Sets[static_cast<int32>(Gait)][bStopping && Gait != EPA250FootGait::Idle ? 1 : 0];
}

int32 PA250AnimClips::LocomotionDatabaseCount()
{
	LocomotionReady();
	return Set().Owned.Num();
}

namespace
{
struct FCombatClips
{
	bool bTried = false;
	/** Logical name → package paths of variants (AM_X_v1..). */
	TMap<FString, TArray<FString>> Variants;
	TSet<FString> Warned;
};

FCombatClips& Combat()
{
	static FCombatClips C;
	return C;
}
}

FString PA250AnimClips::CombatDropFolder()
{
	// Found: anim_clips.json locomotion.clip_folder = /Game/PA250/Anim/Loco → drop root /Game/PA250/Anim.
	return TEXT("/Game/PA250/Anim");
}

bool PA250AnimClips::LoadCombatManifest(TArray<FString>* OutWarnings)
{
	FCombatClips& C = Combat();
	C.bTried = true;
	C.Variants.Reset();
	const FString Folder = CombatDropFolder();
	const FString Disk = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("PA250/Anim/manifest.json"));
	FString Text;
	TArray<FString> Warnings;
	if (!FFileHelper::LoadFileToString(Text, *Disk))
	{
		// Missing manifest is fine until Andrew drops clips; one soft note.
		Warnings.Add(FString::Printf(TEXT("no combat manifest at %s (drop folder %s)"), *Disk, *Folder));
		if (OutWarnings) *OutWarnings = Warnings;
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Warnings.Add(TEXT("combat manifest is not valid JSON"));
		if (OutWarnings) *OutWarnings = Warnings;
		return false;
	}
	for (const auto& Pair : Root->Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Pair.Value->TryGetArray(Arr) && Arr)
		{
			TArray<FString> Paths;
			for (const TSharedPtr<FJsonValue>& V : *Arr) Paths.Add(V->AsString());
			if (Paths.Num() > 0) C.Variants.Emplace(Pair.Key, MoveTemp(Paths));
		}
		else
		{
			const FString One = Pair.Value->AsString();
			if (!One.IsEmpty())
			{
				TArray<FString> Paths;
				Paths.Add(One);
				C.Variants.Emplace(Pair.Key, MoveTemp(Paths));
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 anim clips: combat manifest %d names from %s"), C.Variants.Num(), *Disk);
	if (OutWarnings) *OutWarnings = Warnings;
	return C.Variants.Num() > 0;
}

UAnimSequence* PA250AnimClips::PickCombatClip(const FString& LogicalName, uint32 Seed)
{
	FCombatClips& C = Combat();
	if (!C.bTried) LoadCombatManifest();
	const TArray<FString>* Paths = C.Variants.Find(LogicalName);
	if (!Paths || Paths->Num() == 0)
	{
		if (!C.Warned.Contains(LogicalName))
		{
			C.Warned.Add(LogicalName);
			UE_LOG(LogTemp, Warning, TEXT("PA250 anim clips: missing combat clip %s (fallback)"), *LogicalName);
		}
		return nullptr;
	}
	FRandomStream Rng(Seed);
	const FString& Package = (*Paths)[Rng.RandRange(0, Paths->Num() - 1)];
	const FString ObjectPath = Package.Contains(TEXT(".")) ? Package : (Package + TEXT(".") + FPaths::GetBaseFilename(Package));
	return LoadObject<UAnimSequence>(nullptr, *ObjectPath);
}

float PA250AnimClips::PlayCombatClip(ARFCharacter* Man, const FString& LogicalName, uint32 Seed,
	float BlendIn, float BlendOut, float PlayRate)
{
	if (!Man || !Man->GetMesh()) return 0.f;
	UAnimInstance* Anim = Man->GetMesh()->GetAnimInstance();
	if (!Anim) return 0.f;
	UAnimSequence* Seq = PickCombatClip(LogicalName, Seed);
	if (!Seq) return 0.f;
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		Seq, TEXT("DefaultSlot"), BlendIn, BlendOut, PlayRate);
	if (!Montage) return 0.f;
	return Anim->Montage_Play(Montage, PlayRate);
}
