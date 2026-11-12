#pragma once
#include "Commandlets/Commandlet.h"
#include "PA250MountedWeaponsCommandlet.generated.h"
UCLASS()
class UPA250MountedWeaponsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
