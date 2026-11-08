#pragma once
#include "Commandlets/Commandlet.h"
#include "PA250ClockCommandlet.generated.h"

UCLASS()
class UPA250ClockCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
