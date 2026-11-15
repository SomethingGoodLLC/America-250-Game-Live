// PA250HeroScreen.h — the takeover screen (M5): who the general is, the kit he fights with, and what
// his fall costs the campaign, before the player takes him into the fight. Opened with Y once the
// player's score has earned him; "Take command" hands him over, "Not now" closes it.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250HeroScreen.generated.h"

class APA250BattleGameMode;

UCLASS()
class PLAYAMERICA250_API UPA250HeroScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<APA250BattleGameMode> GameMode;
	TWeakObjectPtr<APlayerController> OwnerPC;

	void OpenFor(APlayerController* PC, APA250BattleGameMode* GM);
	/** What the two buttons do. Public so scripted probes can press them. */
	void Accept();
	void Decline();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
