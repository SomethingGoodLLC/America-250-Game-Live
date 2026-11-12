// PA250SpawnScreen.h — class + post picker. Shown before the first shot when the player's army has more
// than one soldier class (Deploy re-kits him where he stands) and after each death (Deploy respawns him).
// The classes offered are the ones the campaign army actually holds. Skipped in autoplay.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250SpawnScreen.generated.h"

class APA250BattleGameMode;
class APA250ControlPoint;
class UTexture2D;

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
	/** What the Deploy button does. Public so scripted probes can press it. */
	void Confirm();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** Card images (/Game/PA250/UI/Classes/T_Class_<faction>_<class>): kept alive for the brushes. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> PortraitTextures;
	TArray<TSharedPtr<struct FSlateBrush>> PortraitBrushes;
};
