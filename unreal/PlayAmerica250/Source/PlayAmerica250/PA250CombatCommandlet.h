// PA250CombatCommandlet.h — headless combat unit tests (avoids Slate game path crashes).
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PA250CombatCommandlet.generated.h"

UCLASS()
class UPA250CombatCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
