#pragma once
#include "Commandlets/Commandlet.h"
#include "PA250VisualsCommandlet.generated.h"

UCLASS()
class UPA250VisualsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UPA250VisualsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
