// PA250MapWidget.h — campaign map + HUD. Painting and click selection live in SPA250Map.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250MapWidget.generated.h"

class SPA250Map;
class UPA250Subsystem;

UCLASS()
class PLAYAMERICA250_API UPA250MapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- the same actions the buttons trigger (usable from scripts/autoplay) ----
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void PressEndTurn();
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void PressParley();
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void PressFight();
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void PressSave();
	UFUNCTION(BlueprintCallable, Category = "PA250")
	void PressLoad();

	/** Click by province id, exactly as a mouse click would (point-in-polygon already resolved). */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString ClickProvince(const FString& ProvinceId);

	TSharedPtr<SPA250Map> GetMap() const { return MapWidget; }
	FString GetMessage() const { return Message; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	UPA250Subsystem* Sub() const;
	FText HeaderText() const;
	FText StatusText() const;
	FText BattleText() const;
	void SetMessage(const FString& Msg) { Message = Msg; }

	TSharedPtr<SPA250Map> MapWidget;
	FString Message;
};
