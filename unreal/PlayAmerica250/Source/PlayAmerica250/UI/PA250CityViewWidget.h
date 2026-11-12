// PA250CityViewWidget.h — town panel in the manner of a grand-strategy province view. Display + input only.
//
// The one window for a selected town. A town the player holds shows its stats and three tabs
// (Buildings, Forces, Magazine); any other town shows only what the player may know of it.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "PA250CityViewWidget.generated.h"

class SBox;
class SWidget;
class UTexture2D;
class UPA250MapWidget;
struct FPA250CityViewStyle;
struct FPA250CitySnapshot;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPA250CityViewClosed);

UCLASS()
class PLAYAMERICA250_API UPA250CityViewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void OpenForProvince(const FString& InProvinceId);
	const FString& GetProvinceId() const { return ProvinceId; }

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void Refresh();

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void CloseCityView();

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void RequestBuild(const FString& BuildingId);

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void RequestDemolish(const FString& BuildingId);

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void RequestCancel(const FString& BuildingId);

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void RequestRecruit(const FString& UnitId);

	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void RequestDismissMuster();

	/** 0 Buildings, 1 Forces, 2 Magazine. */
	UFUNCTION(BlueprintCallable, Category = "PA250|CityView")
	void SelectTab(int32 Tab);

	/** The map HUD: owner of the army selection and of the magazine / battle actions. */
	void SetHud(UPA250MapWidget* InHud);

	/** Re-reads the sim when the HUD epoch has moved. Driven by a core ticker, not a widget tick:
	 *  widget ticks run inside Slate's paint pass, after layout, and replacing the panel's content
	 *  there draws one garbled frame (everything at zero size) each time the content changes. */
	void RefreshIfStale();

	/** Select a plot by building id, or "+" for the build list (plot clicks; also -PA250CityProbe). */
	void SelectPlot(const FString& Id);

	/** True when a viewport-pixel position is on the panel. The rest of the screen stays map. */
	bool IsViewportPointOverPanel(FVector2D ViewportPx) const;

	UPROPERTY(BlueprintAssignable, Category = "PA250|CityView")
	FPA250CityViewClosed OnClosed;

	/** Header painting. Defaults to /Game/PA250/UI/CityView/T_TownHeader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PA250|CityView")
	TSoftObjectPtr<UTexture2D> TownHeaderTexture;

	/** Building id -> plot picture. A building without one shows its initial on a plain plot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PA250|CityView")
	TMap<FString, TSoftObjectPtr<UTexture2D>> BuildingIcons;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	void EnsureStyle();
	void RebuildDynamic();
	TSharedRef<SWidget> BuildBody();
	TSharedRef<SWidget> BuildEnemyView();
	TSharedRef<SWidget> BuildBuildingsTab();
	TSharedRef<SWidget> BuildForcesTab();
	TSharedRef<SWidget> BuildMagazineTab();
	TSharedRef<SWidget> BuildForceList();
	TSharedRef<SWidget> BuildPlots();
	TSharedRef<SWidget> BuildDetail();
	TSharedRef<SWidget> BuildBuildList();
	void SetStatus(const FString& Text) { StatusLine = Text; }

	FString ProvinceId;
	/** Building id of the selected plot, "+" for the build list, empty for none. */
	FString SelectedPlot;
	FString StatusLine;
	bool bDemolishArmed = false;
	int32 ActiveTab = 0;
	TWeakObjectPtr<UPA250MapWidget> Hud;
	int32 LastHudEpoch = -1;
	/** What the plots and detail pane were last built from; they are rebuilt only when it changes. */
	FString LastLayoutSignature;

	TSharedPtr<FPA250CityViewStyle> Style;
	TSharedPtr<FPA250CitySnapshot> Snap;
	TSharedPtr<SWidget> PanelWidget;
	TSharedPtr<SBox> BodyBox;
	FTSTicker::FDelegateHandle RefreshTicker;

	/** Keeps the textures behind the Slate brushes alive. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;
};
