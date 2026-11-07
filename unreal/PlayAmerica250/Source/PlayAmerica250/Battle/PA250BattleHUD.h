// PA250BattleHUD.h — tickets, objective, timer, reload bar. Reads APA250BattleGameMode.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250BattleHUD.generated.h"

class APA250BattleGameMode;

UCLASS()
class PLAYAMERICA250_API UPA250BattleHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<APA250BattleGameMode> GameMode;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
