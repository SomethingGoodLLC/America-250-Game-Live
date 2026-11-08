#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ColoniesImportLibrary.generated.h"
UCLASS()
class UColoniesImportLibrary : public UBlueprintFunctionLibrary {
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable, Category="Colonies")
 static UObject* ImportColonyMap(const FString& AssetPath, UTexture2D* Texture, const FString& LookupPath, const FString& DataPath);
};
