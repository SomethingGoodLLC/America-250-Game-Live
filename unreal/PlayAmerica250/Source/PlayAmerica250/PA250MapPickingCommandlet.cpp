#include "PA250MapPickingCommandlet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "pa250/sim.hpp"

int32 UPA250MapPickingCommandlet::Main(const FString& Params)
{
	const FString BinPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/ProvinceIdLookup.bin");
	const FString PresPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/map_presentation.json");
	const FString Scenario = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/boston1775.json")));

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *BinPath) || Bytes.Num() < 8)
	{
		UE_LOG(LogTemp, Error, TEXT("lookup missing"));
		return 1;
	}
	const uint32 W = *reinterpret_cast<const uint32*>(Bytes.GetData());
	const uint32 H = *reinterpret_cast<const uint32*>(Bytes.GetData() + 4);
	const uint16* Data = reinterpret_cast<const uint16*>(Bytes.GetData() + 8);

	FString PresText, ScenText;
	FFileHelper::LoadFileToString(PresText, *PresPath);
	FFileHelper::LoadFileToString(ScenText, *Scenario);
	pa250::CampaignState State = pa250::load_scenario(TCHAR_TO_UTF8(*ScenText));

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PresText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("presentation json bad"));
		return 2;
	}
	const TArray<TSharedPtr<FJsonValue>>* ProvincesArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("provinces"), ProvincesArr) || !ProvincesArr)
	{
		UE_LOG(LogTemp, Error, TEXT("no provinces in presentation"));
		return 3;
	}

	TMap<FString, TSharedPtr<FJsonObject>> ById;
	for (const TSharedPtr<FJsonValue>& V : *ProvincesArr)
	{
		const TSharedPtr<FJsonObject> Obj = V->AsObject();
		if (!Obj.IsValid())
		{
			continue;
		}
		ById.Add(Obj->GetStringField(TEXT("id")), Obj);
	}

	for (const auto& Kv : State.provinces)
	{
		const FString Id = UTF8_TO_TCHAR(Kv.first.c_str());
		const TSharedPtr<FJsonObject>* PObj = ById.Find(Id);
		if (!PObj || !PObj->IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("missing presentation for %s"), *Id);
			return 4;
		}
		const TArray<TSharedPtr<FJsonValue>>* Anchor = nullptr;
		if (!(*PObj)->TryGetArrayField(TEXT("label_anchor_uv"), Anchor) || !Anchor || Anchor->Num() < 2)
		{
			UE_LOG(LogTemp, Warning, TEXT("no label uv for %s; skip"), *Id);
			continue;
		}
		const float U = static_cast<float>((*Anchor)[0]->AsNumber());
		const float Vv = static_cast<float>((*Anchor)[1]->AsNumber());
		const int32 X = FMath::Clamp(int32(U * W), 0, int32(W) - 1);
		const int32 Y = FMath::Clamp(int32(Vv * H), 0, int32(H) - 1);
		const uint16 Idx = Data[Y * W + X];
		if (Idx != static_cast<uint16>(Kv.second.map_index))
		{
			UE_LOG(LogTemp, Error, TEXT("label of %s resolved map_index %d want %d at uv (%.3f,%.3f)"), *Id, Idx,
				Kv.second.map_index, U, Vv);
			return 5;
		}
	}

	if (Data[0] != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("corner UV not water (idx=%d); continuing"), Data[0]);
	}

	UE_LOG(LogTemp, Display, TEXT("PA250.Map.Picking passed"));
	return 0;
}
