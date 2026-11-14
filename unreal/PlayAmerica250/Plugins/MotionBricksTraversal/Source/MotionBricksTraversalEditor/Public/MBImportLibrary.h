#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MBImportLibrary.generated.h"
class UMBGeneratedClip;
class USkeletalMesh;

UCLASS()
class MOTIONBRICKSTRAVERSALEDITOR_API UMBImportLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    /** Import only to the reserved evaluation directory. Existing assets are refused. */
    UFUNCTION(BlueprintCallable, Category="MotionBricks Evaluation")
    static UMBGeneratedClip* ImportGeneratedMotion(const FString& Filename, USkeletalMesh* Mesh);
};
