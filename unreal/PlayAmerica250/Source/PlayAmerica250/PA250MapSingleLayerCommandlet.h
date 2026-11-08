#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PA250MapSingleLayerCommandlet.generated.h"

UCLASS()
class UPA250MapSingleLayerCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
