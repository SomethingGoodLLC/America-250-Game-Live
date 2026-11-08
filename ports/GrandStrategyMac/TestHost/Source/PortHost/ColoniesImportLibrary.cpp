#include "ColoniesImportLibrary.h"
#if WITH_EDITOR
#include "MapObject.h"
#include "BlueprintLibrary/DataManagerFunctionLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#endif
UObject* UColoniesImportLibrary::ImportColonyMap(const FString& AssetPath, UTexture2D* Texture, const FString& LookupPath, const FString& DataPath) {
#if WITH_EDITOR
 bool Ok=false;
 const auto Entries=UAtkDataManagerFunctionLibrary::LoadCustomDataFromJson<FLookupEntry>(LookupPath,Ok);
 if(!Ok || !Texture || Entries.Num()!=15) return nullptr;
 EResultJsonLoad Result;
 TArray<const UScriptStruct*> Types{FBaseMapStruct::StaticStruct()};
 const auto Data=UAtkDataManagerFunctionLibrary::LoadCustomDataFromJson(DataPath,Types,Result);
 if(Data.Num()!=15) return nullptr;
 for(const FInstancedStruct& Tile:Data) if(const FBaseMapStruct* T=Tile.GetPtr<FBaseMapStruct>()) UE_LOG(LogTemp,Display,TEXT("COLONIES_TILE %s"),*T->ToString());
 UPackage* Package=CreatePackage(*AssetPath);
 UMapObject* Map=NewObject<UMapObject>(Package,*FPackageName::GetLongPackageAssetName(AssetPath),RF_Public|RF_Standalone);
 Map->InitLandStructType(FBaseMapStruct::StaticStruct());
 Map->InitOceanStructType(FBaseMapStruct::StaticStruct());
 Map->SetLookupFilePath(LookupPath,Entries);
 Map->SetFilePathMapData(DataPath);
 Map->SetMapData(Data);
 Map->SetLookupTexture(Texture);
 Map->ReconstructMapGenSavedFromData();
 FAssetRegistryModule::AssetCreated(Map);
 Map->MarkPackageDirty();
 return Map;
#else
 return nullptr;
#endif
}
