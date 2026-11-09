#include "PA250MapSingleLayerCommandlet.h"
#include "Campaign/PA250MapWidget.h"
#include "Campaign/PA250ProvinceMapActor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

int32 UPA250MapSingleLayerCommandlet::Main(const FString& Params)
{
	// Headless stand-in for Automation PA250.Map.SingleLayer (no CreateWorld — crashes under -nullrhi on Mac).

	// 1) HUD must not mount the legacy SPA250Map canvas.
	UPA250MapWidget* Hud = NewObject<UPA250MapWidget>(GetTransientPackage());
	if (!Hud || Hud->HasLegacyMapCanvas())
	{
		UE_LOG(LogTemp, Error, TEXT("SingleLayer: HUD still mounts legacy SPA250Map canvas"));
		return 1;
	}

	// 2) Exactly one map actor class is the NEW layer; Legacy Slate canvas file is a no-op stub.
	const FString StubPath = FPaths::ProjectDir() / TEXT("Source/PlayAmerica250/Campaign/SPA250Map.cpp");
	FString StubText;
	if (FFileHelper::LoadFileToString(StubText, *StubPath))
	{
		if (StubText.Contains(TEXT("OnPaint")) || StubText.Contains(TEXT("MakeLines")) || StubText.Contains(TEXT("T_CampaignMap")))
		{
			UE_LOG(LogTemp, Error, TEXT("SingleLayer: SPA250Map.cpp still contains legacy drawing path"));
			return 2;
		}
	}

	// 3) MapWidget must not recreate SPA250Map (source check).
	const FString WidgetCpp = FPaths::ProjectDir() / TEXT("Source/PlayAmerica250/Campaign/PA250MapWidget.cpp");
	FString WidgetText;
	if (!FFileHelper::LoadFileToString(WidgetText, *WidgetCpp) || WidgetText.Contains(TEXT("SPA250Map")) ||
		WidgetText.Contains(TEXT("SNew(SPA250Map")))
	{
		if (WidgetText.Contains(TEXT("SPA250Map")) || WidgetText.Contains(TEXT("SNew(SPA250Map")))
		{
			UE_LOG(LogTemp, Error, TEXT("SingleLayer: PA250MapWidget.cpp still references SPA250Map"));
			return 3;
		}
	}

	// 4) ProvinceMapActor is the sole NEW map class (header present; no legacy mesh overlay names in cpp).
	const FString ActorCpp = FPaths::ProjectDir() / TEXT("Source/PlayAmerica250/Campaign/PA250ProvinceMapActor.cpp");
	FString ActorText;
	if (!FFileHelper::LoadFileToString(ActorText, *ActorCpp))
	{
		UE_LOG(LogTemp, Error, TEXT("SingleLayer: missing ProvinceMapActor.cpp"));
		return 4;
	}
	if (ActorText.Contains(TEXT("T_CampaignMap")) || ActorText.Contains(TEXT("campaign_map.png")))
	{
		UE_LOG(LogTemp, Error, TEXT("SingleLayer: ProvinceMapActor still binds legacy campaign map texture"));
		return 5;
	}
	if (!ActorText.Contains(TEXT("T_DeCostaBostonConcord")) && !ActorText.Contains(TEXT("M01-de-costa-boston-concord")))
	{
		UE_LOG(LogTemp, Error, TEXT("SingleLayer: ProvinceMapActor missing de Costa display texture binding"));
		return 8;
	}
	const FString LocJson = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/decosta_locations.json");
	if (!FPaths::FileExists(LocJson))
	{
		UE_LOG(LogTemp, Error, TEXT("SingleLayer: missing decosta_locations.json"));
		return 9;
	}

	// 5) Class defaults: constructing the actor CDO must not attach legacy-named components.
	APA250ProvinceMapActor* CDO = GetMutableDefault<APA250ProvinceMapActor>();
	if (!CDO || !CDO->MapMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("SingleLayer: ProvinceMapActor CDO missing MapMesh"));
		return 6;
	}
	TArray<UActorComponent*> Comps;
	CDO->GetComponents(Comps);
	for (UActorComponent* C : Comps)
	{
		if (!C)
		{
			continue;
		}
		const FString N = C->GetName();
		if (N.Contains(TEXT("Legacy"), ESearchCase::IgnoreCase) || N.Contains(TEXT("CampaignMapOverlay"), ESearchCase::IgnoreCase) ||
			N.Contains(TEXT("Adjacency"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Error, TEXT("SingleLayer: legacy component on CDO: %s"), *N);
			return 7;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("PA250.Map.SingleLayer passed"));
	return 0;
}
