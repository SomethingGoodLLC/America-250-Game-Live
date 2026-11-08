#pragma once
#include "Commandlets/Commandlet.h"
#include "PA250MapPickingCommandlet.generated.h"

UCLASS()
class UPA250MapPickingCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
