// PA250SpawnScreen.h — class + post picker shown on player death (skipped in autoplay).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250SpawnScreen.generated.h"

class APA250BattleGameMode;
class APA250ControlPoint;

UCLASS()
class PLAYAMERICA250_API UPA250SpawnScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<APA250BattleGameMode> GameMode;
	TWeakObjectPtr<APlayerController> OwnerPC;

	FName SelectedClass = TEXT("line_infantry");
	TWeakObjectPtr<APA250ControlPoint> SelectedPost;

	void OpenFor(APlayerController* PC, APA250BattleGameMode* GM);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Confirm();
};
