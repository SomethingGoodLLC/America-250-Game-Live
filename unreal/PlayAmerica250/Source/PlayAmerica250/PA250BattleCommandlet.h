// PA250BattleCommandlet.h — headless conquest smoke (commandlet path; -game currently SIGBUS on this Mac).
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PA250BattleCommandlet.generated.h"

UCLASS()
class UPA250BattleCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
