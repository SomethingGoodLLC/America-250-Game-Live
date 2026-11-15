// PA250MapWidget.h — campaign HUD strips + location panel + battle broadside (no legacy Slate map canvas).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250ProvinceMapActor.h"
#include "UI/PA250CampaignArcPanels.h"
#include "Styling/SlateBrush.h"
#include "PA250MapWidget.generated.h"

class UPA250Subsystem;
class UPA250CityViewWidget;

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
	void PressLogistics(const FString& Choice);
	/** Last desk-news serial shown on the message line (-1 until the first look). */
	int32 SeenDispatchSerial = -1;
	void PressCommander(const FString& Action);
	void PressBuyPowder();
	void PressBuyCartridges();
	void PressBuyMuskets();
	void PressBuyProvisions();
	/** Order sizes of the three purchase buttons (the town panel prices them from the sim). */
	static constexpr int32 BuyPowderQty = 5;
	static constexpr int32 BuyCartridgesQty = 20;
	static constexpr int32 BuyMusketsQty = 4;
	static constexpr int32 BuyProvisionsQty = 6;
	void PressTransferToDepot();
	void PressMakeCartridges();
	void PressResetOverview();
	void PressFocusSelected();
	void PressLedgerToggle();
	bool IsLedgerOpen() const { return bShowLedger; }
	void PressTreasuryToggle();
	bool IsTreasuryOpen() const { return bShowTreasury; }
	void PressBorrow();
	void PressRepay();
	// Campaign arc: the mission tracker's actions and the end screen's.
	void PressSendForGuns();
	void PressHaulGuns();
	void PressLeaveGuns();
	void PressReenlist();
	void PressNewCampaign();
	void PressQuitGame();
	// Campaign pass: the people panel, the floor of Congress (or Whitehall), the decision screen.
	// The battle story (UI/PA250BattleStory): "To arms!" opens it; it stays while the battle level
	// loads, offers Continue when the field is ready, and the field then fades in.
	bool IsStoryUp() const { return bStoryUp; }
	bool IsStoryReady() const { return bStoryUp && bStoryReady; }
	/** Leave the story for the field (the Continue button; autoplay and probes call it too). */
	void ContinueToBattle();
	void PressPeopleToggle();
	void PressFloorToggle();
	bool IsPeopleOpen() const { return bShowPeople; }
	bool IsFloorOpen() const { return bShowFloor; }
	/** Issue a campaign order from a panel button; the status line carries the result or the refusal. */
	void IssueOrder(const FString& OrderJson);
	/** Halt the selected army (button on the army panel, key H): an order command. */
	void PressHalt();
	/** Pure: what the status line says when a halt is ordered. HoursIntoLeg > 0: the army turns about. */
	static FString HaltMessage(const FString& ArmyName, const FString& TownName, int32 HoursIntoLeg);
	bool CanHaltSelected() const;
	/** The mission card: shown once when a mission of the player's ends (after any battle report), until Continue. */
	bool IsMissionCardShown() const;
	PA250ArcPanels::FMissionCard MissionCardNow() const;
	void PressMissionCardContinue();
	void PressSelectArmy(const FString& ArmyId);
	void PressSelectArmyAtSelection();
	void PressAboutToggle();
	void PressDevToggle();
	/** Mission tracker: fold it down to its one-line heading (so it stops covering what is under it), or open it again. */
	void PressTrackerToggle();
	bool IsTrackerMinimized() const { return bTrackerMinimized; }
	/** The HUD's width in Slate units (the UI is scaled by the window's height, so a 16:10 window
	 *  is narrower in units than a 16:9 one of any size). */
	float HudWidthUnits() const;
	/** For the probes: each piece of the top bar and the strip under it with the width it was given
	 *  against the width it wants; bOutFits is false if any shown piece is squeezed or off the screen. */
	FString TopBarReport(bool& bOutFits) const;
	void PressEnterBattleAtSelection();

	/** True when a screen point falls on a HUD strip/panel (clicks there must not reach the map). */
	bool IsPointOverHud(FVector2D ScreenPos) const;
	/** Pure: the correspondence panel waits while a battle card is up (the pre-battle card of a pending
	 *  battle, or an after-battle card not yet read). The letter is still there when the card is put away. */
	static bool StaffPanelVisible(bool bStaffOpen, bool bBattlePending, bool bUnseenBattleReport)
	{
		return bStaffOpen && !bBattlePending && !bUnseenBattleReport;
	}
	/** Pure: what the status line says when "Send stores to depot" is refused (the sim's error code in words). */
	static FString SendStoresRefusal(const FString& ErrorCode);
	bool IsAboutOpen() const { return bShowAbout; }
	bool IsDevOpen() const { return bShowDev; }
	bool IsCityViewOpen() const;
	/** Read by the town panel, which is the one window for a selected town. */
	FText BattleReportText() const;
	bool CanEnterBattleAtSelection() const;
	FString DevText() const;
	static constexpr int32 MaxForceRows = 6;
	static constexpr float TownsPanelWidth = 250.f;
	static constexpr float TreasuryPanelWidth = 460.f;
	/** The selected army's panel: bottom left, to the right of the town panel. */
	static constexpr float ArmyPanelLeft = 492.f;  // Slate units: the town panel is 472 wide
	static constexpr float ArmyPanelWidth = 470.f;
	/** Towns list (right): one line per town the player holds, with what is under way there. */
	FText TownRowStatus(const FString& ProvinceId) const;
	bool IsTownHeld(const FString& ProvinceId) const;
	int32 HeldTownCount() const;
	UPA250CityViewWidget* GetCityView() const { return CityView; }

	/** Inspect a province: select it, and a friendly army standing there. Never marches. */
	UFUNCTION(BlueprintCallable, Category = "PA250")
	FString ClickProvince(const FString& ProvinceId);
	/** Left click: inspect the town and open its panel. */
	FString HandleTownClicked(const FString& ProvinceId);
	/** Right click: march the selected army to this town, or clear the selection when none is selected. */
	FString HandleTownOrdered(const FString& ProvinceId);
	/** Queue a march for the selected army. Same confirmation text the old left-click order used. */
	FString OrderProvince(const FString& ProvinceId);
	/** Show the docked town panel for a province (town click or View Town). */
	void OpenCityView(const FString& ProvinceId);
	void CloseCityView();

	UFUNCTION()
	void ClearSelection();
	void SetStatusMessage(const FString& Msg);

	FString GetSelectedArmyId() const { return SelectedArmyId; }
	FString GetSelectedProvinceId() const { return SelectedProvinceId; }
	FString GetMessage() const { return Message; }
	FString GetLastMessage() const { return LastMessage; }
	/** Arrow-key pan direction: X east (+) / west (-), Y south (+) / north (-). */
	FVector2D GetArrowPanAxis() const
	{
		return FVector2D(((HeldArrows >> 1) & 1) - (HeldArrows & 1), ((HeldArrows >> 3) & 1) - ((HeldArrows >> 2) & 1));
	}

	/** True only if a legacy SPA250Map canvas were still mounted (always false after M10-1). */
	bool HasLegacyMapCanvas() const { return false; }

	/** Screen-space rects of the top/bottom HUD strips at the current viewport size (for framing tests). */
	FSlateRect GetTopStripScreenRect() const;
	FSlateRect GetBottomStripScreenRect() const;
	static constexpr float TopStripHeightPx = 72.f;
	static constexpr float BottomStripHeightPx = 48.f;

	void SetMapActor(APA250ProvinceMapActor* InActor) { MapActor = InActor; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual bool NativeSupportsKeyboardFocus() const override { return true; }

private:
	UPA250Subsystem* Sub() const;
	FText HeaderText() const;
	FText DateText() const;
	FText TreasuryText() const;
	FText StoresText() const;
	FText SupportText() const;
	FText ScoreboardText() const;
	FText AboutText() const;
	FText DisclaimerText() const;
	FText StatusText() const;
	FText BattleText() const;
	FText StaffPanelText() const;
	FText SelectedArmyBannerText() const;
	FText ArmyButtonText(int32 Index) const;
	bool GetPlayerArmyId(int32 Index, FString& OutId) const;
	FText LedgerText() const;
	FText PendingText() const;
	FText AftermathTip(const FString& Choice) const;
	void NotifyMapSelectionChanged();

	FString Message;
	FString LastMessage;
	FString SelectedArmyId;
	FString SelectedProvinceId;
	bool bShowAbout = false;
	bool bShowTreasury = false;
	/** Treasury window text, rebuilt from the sim's treasury_json when the HUD epoch moves. */
	void RefreshTreasuryCache() const;
	mutable int32 TreasuryEpoch = -1;
	mutable FString TreasuryLabels;
	mutable FString TreasuryAmounts;
	mutable FString TreasuryNote;
	mutable bool bCanBorrow = false;
	mutable bool bCanRepay = false;
	mutable int32 LoanStep = 0;
	mutable int32 LoanInterestPct = 0;
	/** Held arrow keys (bit 0 left, 1 right, 2 up, 3 down); the controller pans while held. */
	uint8 HeldArrows = 0;
	bool bShowDev = false;
	bool bShowLedger = false;
	/** Last HudEpoch painted. Slate does not subscribe to the epoch; NativeTick invalidates when it changes. */
	int32 PaintedHudEpoch = -1;
	/** The pre-battle and after-battle cards (UI/PA250BattleCards): rebuilt when what they show changes. */
	TSharedPtr<class SBox> ArmyPanelBox;
	/** The top bar's pieces, kept so a probe can measure them (TopBarReport). */
	TArray<TPair<FString, TSharedPtr<class SWidget>>> BarPieces;
	bool bShowPeople = false;
	bool bShowFloor = false;
	TSharedPtr<class SBox> PeopleBox;
	TSharedPtr<class SBox> FloorBox;
	TSharedPtr<class SBox> DecisionBox;
	TSharedPtr<class SBox> StoryBox;
	bool bStoryUp = false;
	bool bStoryReady = false;   // the battle level is in memory
	bool bStoryLeaving = false;
	double StoryStartSeconds = 0.0;
	FString StoryLevelPackage;
	/** Shows the story for the battle just begun and starts loading its level behind it. */
	void StartBattleStory();
	FString PeopleKey;
	FString FloorKey;
	FString DecisionKey;
	/** Rebuilds the people panel, the floor and the decision screen when what they show changes. */
	void RefreshPassPanels();
	/** The painting behind a decision (a placeholder wash until the picture is supplied). */
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> DecisionTexture;
	FSlateBrush DecisionBrush;
	/** True when a viewport pixel lies on a widget that is on screen (the town panel's own test). */
	static bool IsViewportPointOver(const TSharedPtr<class SWidget>& Widget, FVector2D ViewportPx);
	TSharedPtr<class SBox> PreBattleBox;
	TSharedPtr<class SBox> AfterBattleBox;
	FString PreBattleKey;
	FString AfterBattleKey;
	void RefreshBattleCards();
	/** The mission tracker's text (UI/PA250CampaignArcPanels), rebuilt when the HUD epoch or the selected army changes. */
	void RefreshTrackerCache() const;
	mutable TSharedPtr<struct FPA250TrackerCache> TrackerCache;
	mutable int32 TrackerEpoch = -1;
	mutable FString TrackerArmy;
	/** Folded to one line by the player (Hide / Show). Kept for the session, not in the save. */
	bool bTrackerMinimized = false;
	/** The tracker is on screen: a mission or notice to show, no battle pending, no report up, campaign not over. */
	bool IsTrackerShown() const;
	TWeakObjectPtr<APA250ProvinceMapActor> MapActor;
	UPROPERTY()
	TObjectPtr<UPA250CityViewWidget> CityView;
	/** Soft dark edge drawn over the map (generated once; see RebuildWidget). */
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> VignetteTexture;
	FSlateBrush VignetteBrush;
	UPROPERTY(EditAnywhere, Category = "PA250|CityView")
	TSubclassOf<UPA250CityViewWidget> CityViewClass;
};
