#include "PA250Traversal.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Battle/PA250AnimClips.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool ReadTriple(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 3) return false;
	Out = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
	return true;
}

EPA250Traversal ActionOf(const FString& Name)
{
	if (Name == TEXT("hurdle")) return EPA250Traversal::Hurdle;
	if (Name == TEXT("vault")) return EPA250Traversal::Vault;
	if (Name == TEXT("mantle")) return EPA250Traversal::Mantle;
	return EPA250Traversal::None;
}

struct FRegistry
{
	bool bTried = false;
	TArray<FPA250TraversalClip> Clips;
	TArray<TStrongObjectPtr<UAnimMontage>> Owned;
};

FRegistry& Registry()
{
	static FRegistry R;
	return R;
}
}

float PA250Traversal::LedgeForwardCm(const FPA250TraversalClip& Clip)
{
	for (const FPA250TraversalWindow& W : Clip.Windows)
	{
		if (W.bHasPoint) return static_cast<float>(W.PointCm.X);
	}
	return Clip.Windows.Num() ? static_cast<float>(Clip.Windows[0].RootCm.X) : 0.f;
}

float PA250Traversal::AuthoredHeightCm(const FPA250TraversalClip& Clip)
{
	for (const FPA250TraversalWindow& W : Clip.Windows)
	{
		if (W.bHasPoint) return static_cast<float>(W.PointCm.Z);
	}
	return 0.f;
}

float PA250Traversal::StartTimeFor(const FPA250TraversalClip& Clip, float DistanceCm, float LeadS)
{
	if (Clip.Approach.Num() < 2 || Clip.Windows.IsEmpty()) return 0.f;
	const float Ledge = LedgeForwardCm(Clip);
	const float Latest = FMath::Max(0.f, Clip.Windows[0].EndS - FMath::Max(0.f, LeadS));
	auto Gap = [&Clip, Ledge](int32 i) { return Ledge - static_cast<float>(Clip.Approach[i].Y); };
	if (Gap(0) <= DistanceCm) return 0.f;
	for (int32 i = 1; i < Clip.Approach.Num(); ++i)
	{
		const float T = static_cast<float>(Clip.Approach[i].X);
		if (T >= Latest) break;
		if (Gap(i) <= DistanceCm)
		{
			const float Span = Gap(i - 1) - Gap(i);
			const float Share = Span > UE_KINDA_SMALL_NUMBER ? (Gap(i - 1) - DistanceCm) / Span : 1.f;
			return FMath::Min(Latest, FMath::Lerp(static_cast<float>(Clip.Approach[i - 1].X), T, FMath::Clamp(Share, 0.f, 1.f)));
		}
	}
	return Latest;
}

FVector PA250Traversal::WindowTarget(const FPA250TraversalClip& Clip, int32 Index, const FPA250Obstacle& Obstacle, float LandingClearCm)
{
	if (!Clip.Windows.IsValidIndex(Index)) return Obstacle.FrontLedge;
	const FPA250TraversalWindow& W = Clip.Windows[Index];
	const FVector Across = Obstacle.Across.GetSafeNormal2D();
	const FVector Right(-Across.Y, Across.X, 0.f);
	if (W.bHasPoint)
	{
		const FVector Offset = W.RootCm - W.PointCm;
		const float Authored = static_cast<float>(W.PointCm.Z);
		const float UpShare = Authored > 1.f ? Obstacle.HeightCm / Authored : 1.f;
		const FVector Base = W.Target == TEXT("BackLedge") ? Obstacle.FrontLedge + Across * Obstacle.DepthCm : Obstacle.FrontLedge;
		return Base + Across * Offset.X + Right * Offset.Y + FVector::UpVector * (Offset.Z * UpShare);
	}
	if (W.Target == TEXT("BackLedge")) return Obstacle.FrontLedge + Across * Obstacle.DepthCm;
	const float Beyond = FMath::Max(static_cast<float>(W.RootCm.X) - LedgeForwardCm(Clip), Obstacle.DepthCm + LandingClearCm);
	FVector At = Obstacle.FrontLedge + Across * Beyond;
	At.Z = Obstacle.BackFloor.Z;
	return At;
}

float PA250Traversal::BeyondAtBlendOutCm(const FPA250TraversalClip& Clip)
{
	if (Clip.Approach.IsEmpty()) return 0.f;
	double Forward = Clip.Approach.Last().Y;
	for (int32 i = 1; i < Clip.Approach.Num(); ++i)
	{
		if (Clip.Approach[i].X >= Clip.BlendOutS)
		{
			const double Span = Clip.Approach[i].X - Clip.Approach[i - 1].X;
			const double Share = Span > UE_KINDA_SMALL_NUMBER ? (Clip.BlendOutS - Clip.Approach[i - 1].X) / Span : 1.0;
			Forward = FMath::Lerp(Clip.Approach[i - 1].Y, Clip.Approach[i].Y, FMath::Clamp(Share, 0.0, 1.0));
			break;
		}
	}
	return static_cast<float>(Forward) - LedgeForwardCm(Clip);
}

const FPA250TraversalClip* PA250Traversal::PickClip(const TArray<FPA250TraversalClip>& Clips, EPA250Traversal Action, FName Gait, int32 Pick, float MinBeyondCm)
{
	static const FName Run(TEXT("run")), Walk(TEXT("walk")), Stand(TEXT("stand"));
	const FName Order[3] = {Gait, Gait == Stand ? Walk : Run, Run};
	// First only clips that clear the obstacle; if none does, any clip of the action.
	for (const float Least : {MinBeyondCm, 0.f})
	{
		for (const FName Wanted : Order)
		{
			TArray<const FPA250TraversalClip*, TInlineAllocator<8>> Found;
			for (const FPA250TraversalClip& C : Clips)
			{
				if (C.Action != Action || C.Gait != Wanted) continue;
				if (Least > 0.f && Action != EPA250Traversal::Mantle && BeyondAtBlendOutCm(C) < Least) continue;
				Found.Add(&C);
			}
			if (Found.Num()) return Found[FMath::Abs(Pick) % Found.Num()];
		}
		if (MinBeyondCm <= 0.f) break;
	}
	return nullptr;
}

bool PA250Traversal::LoadClips(const FString& Path, bool bLoadAssets, TArray<FPA250TraversalClip>& Out, TArray<FString>* OutErrors)
{
	Out.Reset();
	TArray<FString> Errors;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Block = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Errors.Add(FString::Printf(TEXT("cannot read %s"), *Path));
	}
	else if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Errors.Add(FString::Printf(TEXT("%s is not valid JSON"), *Path));
	}
	else if (!Root->TryGetObjectField(TEXT("traversal"), Block) || !(*Block)->TryGetArrayField(TEXT("clips"), List))
	{
		Errors.Add(TEXT("no \"traversal.clips\" list (run Scripts/build_locomotion.py)"));
	}
	if (List)
	{
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> J = Value->AsObject();
			if (!J.IsValid()) continue;
			FPA250TraversalClip C;
			C.Asset = J->GetStringField(TEXT("asset"));
			C.Action = ActionOf(J->GetStringField(TEXT("action")));
			C.Gait = FName(*J->GetStringField(TEXT("gait")));
			C.bLeftFoot = J->GetStringField(TEXT("foot")) == TEXT("l");
			C.Variant = static_cast<int32>(J->GetNumberField(TEXT("variant")));
			C.LengthS = static_cast<float>(J->GetNumberField(TEXT("length_s")));
			C.BlendOutS = static_cast<float>(J->GetNumberField(TEXT("blend_out_s")));
			const TArray<TSharedPtr<FJsonValue>>* Windows = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Approach = nullptr;
			if (C.Action == EPA250Traversal::None || !J->TryGetArrayField(TEXT("windows"), Windows) || Windows->IsEmpty() ||
				!J->TryGetArrayField(TEXT("approach"), Approach) || Approach->Num() < 2)
			{
				Errors.Add(FString::Printf(TEXT("%s: no action, windows or approach"), *C.Asset));
				continue;
			}
			bool bGood = true;
			for (const TSharedPtr<FJsonValue>& WV : *Windows)
			{
				const TSharedPtr<FJsonObject> WJ = WV->AsObject();
				FPA250TraversalWindow W;
				W.Name = FName(*WJ->GetStringField(TEXT("name")));
				W.Target = WJ->GetStringField(TEXT("target"));
				W.StartS = static_cast<float>(WJ->GetNumberField(TEXT("start_s")));
				W.EndS = static_cast<float>(WJ->GetNumberField(TEXT("end_s")));
				bGood &= ReadTriple(WJ, TEXT("root_cm"), W.RootCm);
				W.bHasPoint = ReadTriple(WJ, TEXT("point_cm"), W.PointCm);
				C.Windows.Add(W);
			}
			for (const TSharedPtr<FJsonValue>& AV : *Approach)
			{
				const TArray<TSharedPtr<FJsonValue>>& Pair = AV->AsArray();
				if (Pair.Num() == 2) C.Approach.Add(FVector2D(Pair[0]->AsNumber(), Pair[1]->AsNumber()));
			}
			if (!bGood || C.Approach.Num() < 2)
			{
				Errors.Add(FString::Printf(TEXT("%s: a window or the approach is malformed"), *C.Asset));
				continue;
			}
			if (bLoadAssets)
			{
				const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *C.Asset, *FPackageName::GetLongPackageAssetName(C.Asset));
				C.Montage = Cast<UAnimMontage>(StaticLoadObject(UAnimMontage::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn));
				if (C.Montage && C.Montage->SlotAnimTracks.Num() == 1 && C.Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1)
				{
					C.Sequence = Cast<UAnimSequence>(C.Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference());
				}
				if (!C.Montage || !C.Sequence)
				{
					Errors.Add(FString::Printf(TEXT("%s does not load as a montage of one clip"), *C.Asset));
					continue;
				}
			}
			Out.Add(MoveTemp(C));
		}
		if (Out.IsEmpty() && Errors.IsEmpty()) Errors.Add(TEXT("the traversal block holds no clip"));
	}
	if (OutErrors) *OutErrors = Errors;
	return Errors.IsEmpty();
}

const TArray<FPA250TraversalClip>& PA250Traversal::Clips()
{
	FRegistry& R = Registry();
	if (!R.bTried)
	{
		R.bTried = true;
		TArray<FString> Errors;
		if (!LoadClips(PA250AnimClips::DataPath(), true, R.Clips, &Errors))
		{
			for (const FString& E : Errors) UE_LOG(LogTemp, Error, TEXT("PA250 traversal clips: %s"), *E);
			UE_LOG(LogTemp, Error, TEXT("PA250 traversal clips: MISSING, nobody vaults (%d loaded)"), R.Clips.Num());
		}
		for (const FPA250TraversalClip& C : R.Clips) R.Owned.Emplace(C.Montage);
	}
	return R.Clips;
}
