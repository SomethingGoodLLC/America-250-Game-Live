#include "PA250AnimClips.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PA250Blast.h"
#include "RF/PA250SoldierAnimInstance.h"
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
	// -PA250AnimClips=<file>: another registry, for trying a dropped clip before it is registered for good.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250AnimClips="), Override) && !Override.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(Override);
	}
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

namespace
{
struct FClipRegistry
{
	bool bTried = false;
	TArray<FPA250ClipEntry> Entries;
	TMap<FName, int32> Index;
	/** Assets asked for so far (a null entry: asked, and it does not load). */
	TMap<FName, TStrongObjectPtr<UAnimSequence>> Loaded;
	TSet<FName> Placeholders;
	TSet<FName> Unknown;
	/** Takes asked for so far, by package (a null entry: asked, and it does not load). */
	TMap<FString, TStrongObjectPtr<UAnimSequence>> LoadedTakes;
};

FClipRegistry& ClipRegistry()
{
	static FClipRegistry R;
	return R;
}
}

FName PA250AnimClips::DrillClipName(EActiveReloadPhase Phase, bool bKneel)
{
	const TCHAR* Stage = nullptr;
	switch (Phase)
	{
	case EActiveReloadPhase::PrimePan: Stage = TEXT("prime_pan"); break;
	case EActiveReloadPhase::RamCharge: Stage = TEXT("ram_charge"); break;
	case EActiveReloadPhase::ShoulderAim: Stage = TEXT("shoulder_aim"); break;
	case EActiveReloadPhase::MissDelay: Stage = TEXT("miss_delay"); break;
	case EActiveReloadPhase::Jammed: Stage = TEXT("jammed"); break;
	default: return NAME_None;
	}
	return FName(*FString::Printf(TEXT("reload_%s_%s"), bKneel ? TEXT("kneel") : TEXT("stand"), Stage));
}

bool PA250AnimClips::LoadRegistry(const FString& Path, TArray<FString>* OutErrors)
{
	FClipRegistry& R = ClipRegistry();
	R = FClipRegistry();
	R.bTried = true;
	TArray<FString> Errors;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Clips = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Errors.Add(FString::Printf(TEXT("cannot read %s"), *Path));
	}
	else if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Errors.Add(FString::Printf(TEXT("%s is not valid JSON"), *Path));
	}
	else if (!Root->TryGetObjectField(TEXT("clips"), Clips))
	{
		Errors.Add(TEXT("no \"clips\" object"));
	}
	if (Clips)
	{
		for (const auto& Pair : (*Clips)->Values)
		{
			const TSharedPtr<FJsonObject>* J = nullptr;
			FPA250ClipEntry E;
			E.Name = FName(*Pair.Key);
			FString Group, Placeholder, Stage, Stance;
			if (!Pair.Value->TryGetObject(J) || !(*J)->TryGetStringField(TEXT("group"), Group) || !(*J)->TryGetStringField(TEXT("placeholder"), Placeholder) ||
				!(*J)->TryGetStringField(TEXT("shows"), E.Shows) || !(*J)->TryGetBoolField(TEXT("loop"), E.bLoop))
			{
				Errors.Add(FString::Printf(TEXT("clip \"%s\" lacks group, placeholder, shows or loop"), *Pair.Key));
				continue;
			}
			(*J)->TryGetStringField(TEXT("asset"), E.Asset); // null: no asset yet
			(*J)->TryGetBoolField(TEXT("wired"), E.bWired);
			const TArray<TSharedPtr<FJsonValue>>* Station = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Face = nullptr;
			if ((*J)->TryGetArrayField(TEXT("station_cm"), Station) && (*J)->TryGetArrayField(TEXT("face_cm"), Face) && Station->Num() == 2 && Face->Num() == 2)
			{
				E.bHasStation = true;
				E.StationCm = FVector2D((*Station)[0]->AsNumber(), (*Station)[1]->AsNumber());
				E.FaceCm = FVector2D((*Face)[0]->AsNumber(), (*Face)[1]->AsNumber());
			}
			const TArray<TSharedPtr<FJsonValue>>* Takes = nullptr;
			if ((*J)->TryGetArrayField(TEXT("variants"), Takes))
			{
				for (const TSharedPtr<FJsonValue>& Take : *Takes) E.Variants.Add(Take->AsString());
			}
			(*J)->TryGetStringField(TEXT("stage"), Stage);
			(*J)->TryGetStringField(TEXT("stance"), Stance);
			E.Group = FName(*Group);
			E.Placeholder = FName(*Placeholder);
			E.Stage = Stage.IsEmpty() ? NAME_None : FName(*Stage);
			E.Stance = Stance.IsEmpty() ? NAME_None : FName(*Stance);
			R.Index.Add(E.Name, R.Entries.Add(MoveTemp(E)));
		}
	}
	for (const FString& E : Errors) UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: %s"), *E);
	if (OutErrors) *OutErrors = Errors;
	if (Errors.IsEmpty())
	{
		int32 WithAsset = 0;
		for (const FPA250ClipEntry& E : R.Entries) WithAsset += E.Asset.IsEmpty() ? 0 : 1;
		UE_LOG(LogTemp, Display, TEXT("PA250 anim clips: registry of %d names, %d with a clip"), R.Entries.Num(), WithAsset);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: FALLBACK, every named clip shows its placeholder. Fix %s."), *Path);
	}
	return Errors.IsEmpty();
}

const TArray<FPA250ClipEntry>& PA250AnimClips::Registry()
{
	FClipRegistry& R = ClipRegistry();
	if (!R.bTried) LoadRegistry(DataPath());
	return R.Entries;
}

const FPA250ClipEntry* PA250AnimClips::FindClip(FName Name)
{
	Registry();
	const FClipRegistry& R = ClipRegistry();
	const int32* At = R.Index.Find(Name);
	return At ? &R.Entries[*At] : nullptr;
}

UAnimSequence* PA250AnimClips::Clip(FName Name)
{
	const FPA250ClipEntry* Entry = FindClip(Name);
	FClipRegistry& R = ClipRegistry();
	if (!Entry)
	{
		if (!R.Unknown.Contains(Name))
		{
			R.Unknown.Add(Name);
			UE_LOG(LogTemp, Error, TEXT("PA250 anim clips: UNKNOWN %s (not a name in anim_clips.json)"), *Name.ToString());
		}
		return nullptr;
	}
	if (const TStrongObjectPtr<UAnimSequence>* Found = R.Loaded.Find(Name)) return Found->Get();
	UAnimSequence* Asset = nullptr;
	if (!Entry->Asset.IsEmpty())
	{
		const FString ObjectPath = Entry->Asset + TEXT(".") + FPaths::GetBaseFilename(Entry->Asset);
		Asset = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn));
	}
	R.Loaded.Add(Name, TStrongObjectPtr<UAnimSequence>(Asset));
	if (!Asset && !R.Placeholders.Contains(Name))
	{
		R.Placeholders.Add(Name);
		UE_LOG(LogTemp, Display, TEXT("PA250 anim clips: PLACEHOLDER %s (%s): %s"), *Name.ToString(),
			Entry->Asset.IsEmpty() ? TEXT("no clip yet") : *FString::Printf(TEXT("%s is not on this machine"), *Entry->Asset), *Entry->Shows);
	}
	return Asset;
}

int32 PA250AnimClips::PlaceholdersLogged()
{
	return ClipRegistry().Placeholders.Num();
}

int32 PA250AnimClips::UnknownLogged()
{
	return ClipRegistry().Unknown.Num();
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

int32 PA250AnimClips::VariantCount(FName Name)
{
	const FPA250ClipEntry* Entry = FindClip(Name);
	if (!Entry) return 0;
	return Entry->Variants.Num() > 0 ? Entry->Variants.Num() : (Entry->Asset.IsEmpty() ? 0 : 1);
}

UAnimSequence* PA250AnimClips::ClipVariant(FName Name, uint32 Seed)
{
	const FPA250ClipEntry* Entry = FindClip(Name);
	if (!Entry || Entry->Variants.Num() <= 1) return Clip(Name); // unknown names and placeholders are logged there
	FClipRegistry& R = ClipRegistry();
	const FString& Package = Entry->Variants[Seed % static_cast<uint32>(Entry->Variants.Num())];
	if (const TStrongObjectPtr<UAnimSequence>* Found = R.LoadedTakes.Find(Package))
	{
		return Found->Get() ? Found->Get() : Clip(Name);
	}
	const FString ObjectPath = Package + TEXT(".") + FPaths::GetBaseFilename(Package);
	UAnimSequence* Take = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn));
	R.LoadedTakes.Add(Package, TStrongObjectPtr<UAnimSequence>(Take));
	// A take that is not on this machine falls back to the name's first clip (or its placeholder).
	return Take ? Take : Clip(Name);
}

const TCHAR* PA250AnimClips::BlastSuffix(uint8 BlastDir)
{
	switch (static_cast<EPA250BlastDir>(BlastDir))
	{
	case EPA250BlastDir::Back: return TEXT("b");
	case EPA250BlastDir::Left: return TEXT("l");
	case EPA250BlastDir::Right: return TEXT("r");
	default: return TEXT("f");
	}
}

float PA250AnimClips::PlayCombatClip(ARFCharacter* Man, FName Name, uint32 Seed, const FPA250ActionParams* Params, TFunction<void(FName)> OnNotify)
{
	if (!Man || !Man->GetMesh()) return 0.f;
	UPA250SoldierAnimInstance* Anim = Cast<UPA250SoldierAnimInstance>(Man->GetMesh()->GetAnimInstance());
	if (!Anim) return 0.f;
	UAnimSequence* Take = ClipVariant(Name, Seed);
	if (!Take) return 0.f;
	return Anim->PlayAction(Take, Params ? *Params : FPA250ActionParams(), MoveTemp(OnNotify));
}
