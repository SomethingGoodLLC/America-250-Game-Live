// PA250MapWidget.h — campaign HUD strips + battle broadside only (no legacy Slate map canvas).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250ProvinceMapActor.h"
#include "PA250MapWidget.generated.h"

class UPA250Subsystem;

UCLASS()
class PLAYAMERICA250_API UPA250MapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
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

	/** Select / order by province id (used by autoplay and map-actor clicks). */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString ClickProvince(const FString& ProvinceId);

	void ClearSelection();
	void SetStatusMessage(const FString& Msg);

	FString GetSelectedArmyId() const { return SelectedArmyId; }
	FString GetMessage() const { return Message; }
	FString GetLastMessage() const { return LastMessage; }

	/** True only if a legacy SPA250Map canvas were still mounted (always false after M10-1). */
	bool HasLegacyMapCanvas() const { return false; }

	/** Screen-space rects of the top/bottom HUD strips at the current viewport size (for framing tests). */
	FSlateRect GetTopStripScreenRect() const;
	FSlateRect GetBottomStripScreenRect() const;
	static constexpr float TopStripHeightPx = 44.f;
	static constexpr float BottomStripHeightPx = 52.f;

	void SetMapActor(APA250ProvinceMapActor* InActor) { MapActor = InActor; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeSupportsKeyboardFocus() const override { return true; }

private:
	UPA250Subsystem* Sub() const;
	FText HeaderText() const;
	FText StatusText() const;
	FText BattleText() const;
	void NotifyMapSelectionChanged();

	FString Message;
	FString LastMessage;
	FString SelectedArmyId;
	FString SelectedProvinceId;
	TWeakObjectPtr<APA250ProvinceMapActor> MapActor;
};
