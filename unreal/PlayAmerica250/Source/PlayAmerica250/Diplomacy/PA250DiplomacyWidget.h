// PA250DiplomacyWidget.h — overlay: envoy header, transcript, text input, terms panel, Accept / Reject / Leave.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250DiplomacyWidget.generated.h"

class APA250DiplomacyGameMode;
class SEditableTextBox;

UCLASS()
class PLAYAMERICA250_API UPA250DiplomacyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<APA250DiplomacyGameMode> GameMode;

	void SubmitText(const FString& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SEditableTextBox> Input;
};
