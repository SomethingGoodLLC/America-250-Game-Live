#include "UI/PA250CityViewWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Campaign/PA250MapWidget.h"
#include "Campaign/PA250Subsystem.h"
#include "pa250/sim.hpp"
#include "UI/PA250HudStyle.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

// One building as the panel shows it: a filled plot, a plot under construction, or a build option.
struct FPA250CityBuilding
{
	FString Id;
	FString Name;
	FString CampaignEffect;
	FString BattleEffect;
	FString Description;
	FString Reason;         // why a build/upgrade cannot start (empty when it can)
	int32 Level = 0;        // completed level; for a job, the level it will reach
	int32 MaxLevel = 1;
	int32 Cost = 0;         // build cost (option) or upgrade cost (filled)
	int32 Days = 1;
	int32 Salvage = 0;      // gold from demolishing
	int32 PaidGold = 0;     // returned in full when a job is cancelled
	int32 DaysRemaining = 0;
	float Progress = 0.f;
	bool bConstructing = false;
	bool bCanUpgrade = false; // below max level
	bool bAvailable = false;  // the sim would accept the build/upgrade now
};

// "17 militia", "1 officer", "4 regulars": the campaign's unit classes in plain words.
static FString UnitLabel(const FString& UnitClass, int32 Count)
{
	const bool bOne = Count == 1;
	if (UnitClass == TEXT("line_infantry")) return bOne ? TEXT("regular") : TEXT("regulars");
	if (UnitClass == TEXT("militia")) return TEXT("militia");
	if (UnitClass == TEXT("rifleman")) return bOne ? TEXT("rifleman") : TEXT("riflemen");
	if (UnitClass == TEXT("grenadier")) return bOne ? TEXT("grenadier") : TEXT("grenadiers");
	if (UnitClass == TEXT("dragoon")) return bOne ? TEXT("dragoon") : TEXT("dragoons");
	if (UnitClass == TEXT("light_infantry")) return TEXT("light infantry");
	if (UnitClass == TEXT("officer")) return bOne ? TEXT("officer") : TEXT("officers");
	if (UnitClass.IsEmpty()) return bOne ? TEXT("man") : TEXT("men");
	return UnitClass.Replace(TEXT("_"), TEXT(" "));
}

struct FPA250CityForce
{
	FString ArmyId;
	FString Name;
	FString Composition; // "18 militia · 4 regulars"
	bool bYours = false;
	bool bMarching = false;
	int32 Fit = 0;
	int32 Sick = 0;
	int32 Wounded = 0;
	int32 Morale = 0;
	int32 Readiness = 0;
};

// A company the town can raise.
struct FPA250CityRecruit
{
	FString Id;
	FString Name;
	FString Requires;
	FString Description;
	FString Reason;
	FString UnitClass; // Army.units key the men join as
	FString Role;      // what they do in battle (from the sim's catalog)
	int32 Men = 0;
	int32 Gold = 0;
	int32 Muskets = 0;
	int32 Provisions = 0;
	int32 Days = 1;
	bool bAvailable = false;
};

struct FPA250CitySnapshot
{
	bool bOk = false;
	bool bYours = false;
	bool bBattlePending = false;
	FString Name;
	FString SizeLabel;
	FString Owner;
	int32 Slots = 0;
	int32 Occupied = 0;
	int32 Gold = 0;
	int32 Support = 0;
	int32 Defense = 0;
	int32 Development = 0;
	int32 Supply = 0;
	int32 Powder = 0;
	int32 Cartridges = 0;
	int32 Muskets = 0;
	int32 Provisions = 0;
	int32 Bayonets = 0;
	int32 Lead = 0;
	int32 Paper = 0;
	bool bBattleHere = false;
	bool bEnemyInterest = false;
	bool bSuppliedBySea = false;
	int32 GarrisonUnpaidMonths = 0;
	int32 ManpowerCap = 0;
	int32 ManpowerUsed = 0;
	bool bMustering = false;
	FString MusterName;
	int32 MusterMen = 0;
	int32 MusterDays = 0;
	int32 MusterGold = 0;
	float MusterProgress = 0.f;
	TArray<FPA250CityForce> Forces;
	TArray<FPA250CityRecruit> Recruits;
	TArray<FPA250CityBuilding> Plots;   // under construction first, then completed
	TArray<FPA250CityBuilding> Options; // new buildings only; upgrades live on their plot

	const FPA250CityBuilding* FindPlot(const FString& Id) const
	{
		return Plots.FindByPredicate([&Id](const FPA250CityBuilding& B) { return B.Id == Id; });
	}
};

namespace
{
const FLinearColor KInk = FPA250HudStyle::SRGB(0.085f, 0.068f, 0.050f, 0.96f);
const FLinearColor KWood = FPA250HudStyle::SRGB(0.125f, 0.098f, 0.072f, 1.f);
const FLinearColor KWoodLight = FPA250HudStyle::SRGB(0.200f, 0.155f, 0.110f, 1.f);
const FLinearColor KWoodHover = FPA250HudStyle::SRGB(0.270f, 0.210f, 0.140f, 1.f);
const FLinearColor KCream = FPA250HudStyle::SRGB(0.93f, 0.87f, 0.70f, 1.f);
const FLinearColor KCreamDim = FPA250HudStyle::SRGB(0.70f, 0.64f, 0.50f, 1.f);
const FLinearColor KGold = FPA250HudStyle::SRGB(0.85f, 0.65f, 0.25f, 1.f);
const FLinearColor KGoldDim = FPA250HudStyle::SRGB(0.50f, 0.38f, 0.16f, 1.f);
const FLinearColor KRed = FPA250HudStyle::SRGB(0.80f, 0.36f, 0.28f, 1.f);
const FLinearColor KGreen = FPA250HudStyle::SRGB(0.47f, 0.62f, 0.33f, 1.f);

constexpr float KPanelWidth = 472.f;
constexpr float KPlotSize = 102.f;
// Panel width less its frame and padding. The plot grid wraps at this fixed width: a wrap box told
// to use its allotted size reports one plot per row on the first layout pass and the panel grows
// to several times its height.
constexpr float KContentWidth = KPanelWidth - 28.f;
// Text in the scrolling detail pane wraps at fixed widths: auto-wrap measures its height before it
// knows its width there, and the lines below the first are clipped.
constexpr float KDetailTextWidth = KContentWidth - 36.f;
constexpr float KListTextWidth = KDetailTextWidth - 54.f - 108.f;
constexpr float KBottomStripPx = 48.f; // PA250MapWidget::BottomStripHeightPx

FSlateFontInfo Font(const TCHAR* Face, int32 Size) { return FPA250HudStyle::Font(Face, Size); }

/** Sim error codes ("code" or "code: detail") as a sentence for the status line. */
FString FriendlyError(const FString& Err)
{
	if (Err.StartsWith(TEXT("battle_pending"))) return TEXT("Not while a battle is pending.");
	if (Err.StartsWith(TEXT("not_your_province"))) return TEXT("You do not hold this town.");
	if (Err.StartsWith(TEXT("under_construction"))) return TEXT("Cancel the work in progress first.");
	if (Err.StartsWith(TEXT("no_construction"))) return TEXT("Nothing is being built there.");
	if (Err.StartsWith(TEXT("not_present"))) return TEXT("That building is not here.");
	if (Err.StartsWith(TEXT("not_in_campaign"))) return TEXT("Not available right now.");
	if (Err.StartsWith(TEXT("no_muster"))) return TEXT("No company is mustering here.");
	return Err;
}

FString Days(int32 N) { return FString::Printf(TEXT("%d day%s"), N, N == 1 ? TEXT("") : TEXT("s")); }

FPA250CityBuilding ReadBuilding(const TSharedPtr<FJsonObject>& Obj)
{
	FPA250CityBuilding B;
	B.Id = Obj->GetStringField(TEXT("building_id"));
	B.Name = Obj->GetStringField(TEXT("name"));
	Obj->TryGetStringField(TEXT("campaign_effect"), B.CampaignEffect);
	Obj->TryGetStringField(TEXT("battle_effect"), B.BattleEffect);
	Obj->TryGetStringField(TEXT("description"), B.Description);
	return B;
}
} // namespace

struct FPA250CityViewStyle
{
	FSlateRoundedBoxBrush Panel{KInk, 6.f, KGoldDim, 1.5f};
	FSlateRoundedBoxBrush Card{KWood, 4.f, FPA250HudStyle::SRGB(0.f, 0.f, 0.f, 0.5f), 1.f};
	FSlateRoundedBoxBrush Plot{KWoodLight, 4.f, KGoldDim, 1.f};
	FSlateRoundedBoxBrush PlotHover{KWoodHover, 4.f, KGold, 1.f};
	FSlateRoundedBoxBrush PlotSelected{KWoodHover, 4.f, KGold, 2.5f};
	FSlateRoundedBoxBrush PlotEmpty{FPA250HudStyle::SRGB(0.07f, 0.055f, 0.04f, 1.f), 4.f, FPA250HudStyle::SRGB(0.30f, 0.24f, 0.15f, 1.f), 1.f};
	FSlateRoundedBoxBrush Tab{FPA250HudStyle::SRGB(0.10f, 0.08f, 0.06f, 1.f), 3.f, FPA250HudStyle::SRGB(0.30f, 0.24f, 0.15f, 1.f), 1.f};
	FSlateRoundedBoxBrush TabHover{FPA250HudStyle::SRGB(0.16f, 0.13f, 0.09f, 1.f), 3.f, KGoldDim, 1.f};
	FSlateRoundedBoxBrush TabActive{FPA250HudStyle::SRGB(0.105f, 0.165f, 0.290f, 1.f), 3.f, KGold, 1.5f};
	FSlateRoundedBoxBrush Btn{FPA250HudStyle::SRGB(0.28f, 0.20f, 0.10f, 1.f), 3.f, KGoldDim, 1.f};
	FSlateRoundedBoxBrush BtnHover{FPA250HudStyle::SRGB(0.38f, 0.28f, 0.13f, 1.f), 3.f, KGold, 1.f};
	FSlateRoundedBoxBrush BtnPressed{FPA250HudStyle::SRGB(0.20f, 0.14f, 0.07f, 1.f), 3.f, KGold, 1.f};
	FSlateRoundedBoxBrush BtnDisabled{FPA250HudStyle::SRGB(0.12f, 0.10f, 0.08f, 1.f), 3.f, FPA250HudStyle::SRGB(0.22f, 0.19f, 0.15f, 1.f), 1.f};
	FSlateRoundedBoxBrush Danger{FPA250HudStyle::SRGB(0.30f, 0.11f, 0.08f, 1.f), 3.f, FPA250HudStyle::SRGB(0.55f, 0.22f, 0.16f, 1.f), 1.f};
	FSlateRoundedBoxBrush DangerHover{FPA250HudStyle::SRGB(0.44f, 0.15f, 0.10f, 1.f), 3.f, KRed, 1.f};
	FSlateColorBrush Shade{FPA250HudStyle::SRGB(0.f, 0.f, 0.f, 0.62f)};
	FSlateColorBrush Veil{FPA250HudStyle::SRGB(0.f, 0.f, 0.f, 0.45f)};
	FSlateColorBrush BarBack{FPA250HudStyle::SRGB(0.f, 0.f, 0.f, 0.7f)};
	FSlateColorBrush BarFill{KGold};
	FSlateColorBrush Rule{KGoldDim};
	FButtonStyle PlotButton;
	FButtonStyle PlotButtonSelected;
	FButtonStyle PlotButtonEmpty;
	FButtonStyle Action;
	FButtonStyle DangerAction;
	FButtonStyle TabButton;
	FButtonStyle TabButtonActive;
	FProgressBarStyle Bar;
	FSlateBrush Header;
	bool bHasHeader = false;
	TMap<FString, TSharedPtr<FSlateBrush>> Icons;

	FPA250CityViewStyle()
	{
		const FMargin None(0.f);
		PlotButton.SetNormal(Plot).SetHovered(PlotHover).SetPressed(PlotSelected).SetDisabled(Plot).SetNormalPadding(None).SetPressedPadding(None);
		PlotButtonSelected.SetNormal(PlotSelected).SetHovered(PlotSelected).SetPressed(PlotSelected).SetDisabled(PlotSelected).SetNormalPadding(None).SetPressedPadding(None);
		PlotButtonEmpty.SetNormal(PlotEmpty).SetHovered(PlotHover).SetPressed(PlotSelected).SetDisabled(PlotEmpty).SetNormalPadding(None).SetPressedPadding(None);
		Action.SetNormal(Btn).SetHovered(BtnHover).SetPressed(BtnPressed).SetDisabled(BtnDisabled).SetNormalPadding(None).SetPressedPadding(None);
		DangerAction.SetNormal(Danger).SetHovered(DangerHover).SetPressed(Danger).SetDisabled(BtnDisabled).SetNormalPadding(None).SetPressedPadding(None);
		TabButton.SetNormal(Tab).SetHovered(TabHover).SetPressed(TabActive).SetDisabled(Tab).SetNormalPadding(None).SetPressedPadding(None);
		TabButtonActive.SetNormal(TabActive).SetHovered(TabActive).SetPressed(TabActive).SetDisabled(TabActive).SetNormalPadding(None).SetPressedPadding(None);
		Bar.SetBackgroundImage(BarBack).SetFillImage(BarFill);
	}
};

void UPA250CityViewWidget::EnsureStyle()
{
	if (Style.IsValid()) return;
	Style = MakeShared<FPA250CityViewStyle>();
	Snap = MakeShared<FPA250CitySnapshot>();

	if (!TownHeaderTexture.ToSoftObjectPath().IsValid())
	{
		TownHeaderTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/PA250/UI/CityView/T_TownHeader.T_TownHeader")));
	}
	auto DefaultIcon = [this](const TCHAR* Id, const TCHAR* Path) {
		if (!BuildingIcons.Contains(Id)) BuildingIcons.Add(Id, TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path)));
	};
	// Fort and Meeting House have no artwork yet: they show an initial on a plain plot.
	DefaultIcon(TEXT("farm"), TEXT("/Game/PA250/UI/CityView/T_Icon_Farm.T_Icon_Farm"));
	DefaultIcon(TEXT("workshop"), TEXT("/Game/PA250/UI/CityView/T_Icon_Workshop.T_Icon_Workshop"));
	DefaultIcon(TEXT("depot"), TEXT("/Game/PA250/UI/CityView/T_Icon_Depot.T_Icon_Depot"));
	DefaultIcon(TEXT("militia_muster"), TEXT("/Game/PA250/UI/CityView/T_Icon_MilitiaMuster.T_Icon_MilitiaMuster"));
	// Soldier class thumbnails for the company cards (stills of the in-game soldiers), keyed class_<faction>_<class>.
	for (const TCHAR* Cls : {TEXT("british_line_infantry"), TEXT("british_grenadier"), TEXT("british_light_infantry"), TEXT("british_officer"),
			 TEXT("continental_line_infantry"), TEXT("continental_militia"), TEXT("continental_rifleman"), TEXT("continental_officer")})
	{
		const FString Key = FString(TEXT("class_")) + Cls;
		if (!BuildingIcons.Contains(Key))
		{
			BuildingIcons.Add(Key, TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(TEXT("/Game/PA250/UI/Classes/T_Class_%s.T_Class_%s"), Cls, Cls))));
		}
	}

	LoadedTextures.Reset();
	if (UTexture2D* Tex = TownHeaderTexture.LoadSynchronous())
	{
		LoadedTextures.Add(Tex);
		Style->Header.SetResourceObject(Tex);
		Style->Header.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
		Style->Header.DrawAs = ESlateBrushDrawType::Image;
		Style->bHasHeader = true;
	}
	for (const TPair<FString, TSoftObjectPtr<UTexture2D>>& Kv : BuildingIcons)
	{
		if (UTexture2D* Tex = Kv.Value.LoadSynchronous())
		{
			LoadedTextures.Add(Tex);
			TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Tex);
			Brush->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
			Brush->DrawAs = ESlateBrushDrawType::Image;
			Style->Icons.Add(Kv.Key, Brush);
		}
	}
}

void UPA250CityViewWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (RefreshTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RefreshTicker);
		RefreshTicker.Reset();
	}
	PanelWidget.Reset();
	BodyBox.Reset();
}

TSharedRef<SWidget> UPA250CityViewWidget::RebuildWidget()
{
	EnsureStyle();
	LastLayoutSignature.Reset();
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();

	auto Stat = [St](const TCHAR* Label, TFunction<FString()> Value, const TCHAR* Tip) {
		return SNew(SBorder)
			.BorderImage(&St->Card)
			.Padding(FMargin(8.f, 5.f))
			.ToolTipText(FText::FromString(Tip))
			[SNew(SVerticalBox) +
			 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 16)).ColorAndOpacity(KCream).Text_Lambda([Value]() {
					 return FText::FromString(Value());
				 })] +
			 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				 [SNew(STextBlock).Font(Font(TEXT("Regular"), 8)).ColorAndOpacity(KCreamDim).Text(FText::FromString(Label))]];
	};

	TSharedRef<SWidget> HeaderArt = St->bHasHeader
		? StaticCastSharedRef<SWidget>(SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[SNew(SImage).Image(&St->Header)])
		: StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(&St->Card));

	TSharedRef<SWidget> Header =
		SNew(SBox).HeightOverride(150.f).Clipping(EWidgetClipping::ClipToBounds)
		[SNew(SOverlay) +
		 SOverlay::Slot()[HeaderArt] +
		 SOverlay::Slot().VAlign(VAlign_Bottom)
			 [SNew(SBorder).BorderImage(&St->Shade).Padding(FMargin(14.f, 4.f))
			  [SNew(SVerticalBox) +
			   SVerticalBox::Slot().AutoHeight()
				   [SNew(STextBlock).Font(Font(TEXT("Bold"), 22)).ColorAndOpacity(KCream).Text_Lambda([S]() {
					   return FText::FromString(S->Name.ToUpper());
				   })] +
			   SVerticalBox::Slot().AutoHeight()
				   [SNew(STextBlock).Font(Font(TEXT("Regular"), 11)).ColorAndOpacity_Lambda([S]() {
					   return FSlateColor(S->bYours ? KGold : KRed);
				   }).Text_Lambda([S]() {
					   const FString Held = S->Owner.IsEmpty() ? TEXT("Not held")
											: S->bYours ? TEXT("Held by the Continentals — yours")
											: S->Owner == TEXT("british") ? TEXT("Held by the British")
											: TEXT("Held by ") + S->Owner;
					   return FText::FromString(S->SizeLabel + TEXT("  ·  ") + Held);
				   })]]] +
		 SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(6.f)
			 [SNew(SBox).WidthOverride(26.f).HeightOverride(26.f)
			  [SNew(SButton)
				   .ButtonStyle(&St->Action)
				   .IsFocusable(false)
				   .HAlign(HAlign_Center)
				   .VAlign(VAlign_Center)
				   .ToolTipText(FText::FromString(TEXT("Close (Esc)")))
				   .OnClicked_Lambda([Weak]() {
					   if (Weak.IsValid()) Weak->CloseCityView();
					   return FReply::Handled();
				   })
				   [SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(KCream).Text(FText::FromString(TEXT("X")))]]]];

	TSharedRef<SWidget> Body =
		SNew(SVerticalBox) +
		// Stat tiles: only for a town the player holds.
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SHorizontalBox).Visibility_Lambda([S]() { return S->bYours ? EVisibility::Visible : EVisibility::Collapsed; }) +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
				 [Stat(TEXT("DEVELOPMENT"), [S]() { return FString::FromInt(S->Development); },
					   TEXT("1 + the levels of Farm, Workshop, Depot and Fort."))] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
				 [Stat(TEXT("SUPPLY"), [S]() { return FString::FromInt(S->Supply); },
					   TEXT("Monthly supply and treasury income from this town. Farms raise it."))] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
				 [Stat(TEXT("DEFENCE"), [S]() { return FString::Printf(TEXT("+%d"), S->Defense); },
					   TEXT("Morale the Fort gives a defender who holds this town."))] +
			 SHorizontalBox::Slot().FillWidth(1.f)
				 [Stat(TEXT("MANPOWER"), [S]() { return FString::Printf(TEXT("%d"), FMath::Max(0, S->ManpowerCap - S->ManpowerUsed)); },
					   TEXT("Men this town can still raise this month. Grows with the town's size and with Support."))]] +
		// Tabs and their content, or the short view of a town held by someone else.
		SVerticalBox::Slot().AutoHeight()[SAssignNew(BodyBox, SBox)] +
		// Footer: last action and the treasury
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				 [SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(KCreamDim).WrapTextAt(KContentWidth - 150.f).Text_Lambda([Weak]() {
					 if (!Weak.IsValid()) return FText::GetEmpty();
					 const FString Dev = Weak->Hud.IsValid() ? Weak->Hud->DevText() : FString();
					 return FText::FromString(Dev.IsEmpty() ? Weak->StatusLine : Dev);
				 })] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(KGold).Text_Lambda([S]() {
					 return FText::FromString(FString::Printf(TEXT("Treasury %d gold"), S->Gold));
				 })]];

	// Lower left, clear of the Your-forces list above and the control strip below. Everything outside
	// the panel is not hit-testable, so the map stays clickable and another town can be picked.
	TSharedRef<SWidget> Root =
		SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible) +
		SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(10.f, 0.f, 0.f, KBottomStripPx + 10.f))
			[SAssignNew(PanelWidget, SBox).WidthOverride(KPanelWidth).Visibility_Lambda([Weak]() {
				UPA250Subsystem* Sub = Weak.IsValid() ? UPA250Subsystem::Get(Weak.Get()) : nullptr;
				// Staff letters and notices open on the left; the panel steps aside for them.
				return Sub && Sub->HasStaffEvent() ? EVisibility::Collapsed : EVisibility::Visible;
			})
			 [SNew(SBorder).BorderImage(&St->Panel).Padding(FMargin(2.f))
			  [SNew(SVerticalBox) +
			   SVerticalBox::Slot().AutoHeight()[Header] +
			   SVerticalBox::Slot().AutoHeight().Padding(FMargin(12.f, 10.f, 12.f, 10.f))[Body]]]];

	if (!RefreshTicker.IsValid())
	{
		RefreshTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float) {
			if (GetVisibility() != ESlateVisibility::Collapsed) RefreshIfStale();
			return true;
		}));
	}
	if (!ProvinceId.IsEmpty()) Refresh();
	return Root;
}

bool UPA250CityViewWidget::IsViewportPointOverPanel(FVector2D ViewportPx) const
{
	if (!PanelWidget.IsValid() || !PanelWidget->GetVisibility().IsVisible() || !GEngine || !GEngine->GameViewport) return false;
	const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget();
	if (!Viewport.IsValid()) return false;
	// Viewport pixels -> Slate absolute space, the inverse of how the viewport reports the cursor.
	const FGeometry& VG = Viewport->GetTickSpaceGeometry();
	const FVector2D Abs = VG.LocalToAbsolute(ViewportPx / FMath::Max(VG.Scale, KINDA_SMALL_NUMBER));
	return PanelWidget->GetTickSpaceGeometry().IsUnderLocation(Abs);
}

void UPA250CityViewWidget::OpenForProvince(const FString& InProvinceId)
{
	if (ProvinceId != InProvinceId)
	{
		SelectedPlot.Reset();
		StatusLine.Reset();
	}
	// Opened by picking an army: its town's Forces tab is what the player is after.
	if (Hud.IsValid() && !Hud->GetSelectedArmyId().IsEmpty()) ActiveTab = 1;
	ProvinceId = InProvinceId;
	bDemolishArmed = false;
	LastLayoutSignature.Reset();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Refresh();
}

void UPA250CityViewWidget::CloseCityView()
{
	if (GetVisibility() == ESlateVisibility::Collapsed) return;
	SetVisibility(ESlateVisibility::Collapsed);
	bDemolishArmed = false;
	OnClosed.Broadcast();
}

void UPA250CityViewWidget::RefreshIfStale()
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S) return;
	if (S->GetFlow() != EPA250Flow::Campaign)
	{
		CloseCityView();
		return;
	}
	if (S->GetHudEpoch() != LastHudEpoch) Refresh();
}

void UPA250CityViewWidget::SetHud(UPA250MapWidget* InHud) { Hud = InHud; }

void UPA250CityViewWidget::SelectTab(int32 Tab)
{
	ActiveTab = FMath::Clamp(Tab, 0, 2);
	bDemolishArmed = false;
	Refresh();
}

void UPA250CityViewWidget::SelectPlot(const FString& Id)
{
	ActiveTab = 0;
	SelectedPlot = SelectedPlot == Id ? FString() : Id;
	bDemolishArmed = false;
	Refresh();
}

void UPA250CityViewWidget::Refresh()
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S || ProvinceId.IsEmpty() || !Snap.IsValid()) return;
	LastHudEpoch = S->GetHudEpoch();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(S->GetCityViewJson(ProvinceId));
	FPA250CitySnapshot New;
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid() && Root->GetBoolField(TEXT("ok")))
	{
		New.bOk = true;
		New.bYours = Root->GetBoolField(TEXT("yours"));
		New.bBattlePending = Root->GetBoolField(TEXT("battle_pending"));
		New.Name = Root->GetStringField(TEXT("name"));
		New.SizeLabel = Root->GetStringField(TEXT("size_label"));
		New.Owner = Root->GetStringField(TEXT("owner"));
		New.Slots = Root->GetIntegerField(TEXT("slot_count"));
		New.Occupied = Root->GetIntegerField(TEXT("slots_occupied"));
		New.Gold = Root->GetIntegerField(TEXT("player_gold"));
		New.Support = Root->GetIntegerField(TEXT("local_support"));
		New.Defense = Root->GetIntegerField(TEXT("defense_bonus"));
		New.Development = Root->GetIntegerField(TEXT("development"));
		New.Supply = Root->GetIntegerField(TEXT("supply_value"));
		if (const TSharedPtr<FJsonObject>* Stores = nullptr; Root->TryGetObjectField(TEXT("stores"), Stores))
		{
			New.Powder = (*Stores)->GetIntegerField(TEXT("powder"));
			New.Cartridges = (*Stores)->GetIntegerField(TEXT("cartridges"));
			New.Muskets = (*Stores)->GetIntegerField(TEXT("muskets"));
			New.Provisions = (*Stores)->GetIntegerField(TEXT("provisions"));
			New.Bayonets = (*Stores)->GetIntegerField(TEXT("bayonets"));
			New.Lead = (*Stores)->GetIntegerField(TEXT("lead"));
			New.Paper = (*Stores)->GetIntegerField(TEXT("paper"));
		}
		New.bBattleHere = Root->GetBoolField(TEXT("battle_here"));
		New.bEnemyInterest = Root->GetBoolField(TEXT("enemy_interest"));
		New.bSuppliedBySea = Root->GetBoolField(TEXT("supplied_by_sea"));
		New.GarrisonUnpaidMonths = Root->GetIntegerField(TEXT("garrison_unpaid_months"));
		New.ManpowerCap = Root->GetIntegerField(TEXT("manpower_cap"));
		New.ManpowerUsed = Root->GetIntegerField(TEXT("manpower_used"));
		if (const TSharedPtr<FJsonObject>* Muster = nullptr; Root->TryGetObjectField(TEXT("mustering"), Muster))
		{
			New.bMustering = true;
			New.MusterName = (*Muster)->GetStringField(TEXT("name"));
			New.MusterMen = (*Muster)->GetIntegerField(TEXT("men"));
			New.MusterDays = (*Muster)->GetIntegerField(TEXT("days_remaining"));
			New.MusterGold = (*Muster)->GetIntegerField(TEXT("paid_gold"));
			New.MusterProgress = static_cast<float>((*Muster)->GetNumberField(TEXT("progress")));
		}
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("forces")))
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;
			FPA250CityForce F;
			F.ArmyId = Obj->GetStringField(TEXT("army_id"));
			F.Name = Obj->GetStringField(TEXT("name"));
			F.bYours = Obj->GetBoolField(TEXT("yours"));
			F.bMarching = Obj->GetBoolField(TEXT("marching"));
			if (F.bYours)
			{
				F.Fit = Obj->GetIntegerField(TEXT("fit"));
				F.Sick = Obj->GetIntegerField(TEXT("sick"));
				F.Wounded = Obj->GetIntegerField(TEXT("wounded"));
				F.Morale = Obj->GetIntegerField(TEXT("morale"));
				F.Readiness = Obj->GetIntegerField(TEXT("readiness"));
				if (const TSharedPtr<FJsonObject>* Units = nullptr; Obj->TryGetObjectField(TEXT("units"), Units))
				{
					for (const auto& Kv : (*Units)->Values)
					{
						const int32 N = static_cast<int32>(Kv.Value->AsNumber());
						if (N <= 0) continue;
						const FString Key(Kv.Key);
						const FString Label = UnitLabel(Key, N);
						F.Composition += (F.Composition.IsEmpty() ? TEXT("") : TEXT(" · ")) + FString::Printf(TEXT("%d %s"), N, *Label);
					}
				}
			}
			New.Forces.Add(MoveTemp(F));
		}
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("recruits")))
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;
			FPA250CityRecruit R;
			R.Id = Obj->GetStringField(TEXT("unit_id"));
			R.Name = Obj->GetStringField(TEXT("name"));
			R.Requires = Obj->GetStringField(TEXT("requires"));
			R.Description = Obj->GetStringField(TEXT("description"));
			R.Reason = Obj->GetStringField(TEXT("reason"));
			Obj->TryGetStringField(TEXT("unit_class"), R.UnitClass);
			Obj->TryGetStringField(TEXT("role"), R.Role);
			R.Men = Obj->GetIntegerField(TEXT("men"));
			R.Gold = Obj->GetIntegerField(TEXT("gold"));
			R.Muskets = Obj->GetIntegerField(TEXT("muskets"));
			R.Provisions = Obj->GetIntegerField(TEXT("provisions"));
			R.Days = Obj->GetIntegerField(TEXT("days"));
			R.bAvailable = Obj->GetBoolField(TEXT("available"));
			New.Recruits.Add(MoveTemp(R));
		}
		TSet<FString> Building;
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("constructing")))
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;
			FPA250CityBuilding B = ReadBuilding(Obj);
			B.bConstructing = true;
			B.Level = Obj->GetIntegerField(TEXT("target_level"));
			B.DaysRemaining = Obj->GetIntegerField(TEXT("days_remaining"));
			B.Progress = static_cast<float>(Obj->GetNumberField(TEXT("progress")));
			B.PaidGold = Obj->GetIntegerField(TEXT("paid_gold"));
			Building.Add(B.Id);
			New.Plots.Add(MoveTemp(B));
		}
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("filled")))
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;
			FPA250CityBuilding B = ReadBuilding(Obj);
			if (Building.Contains(B.Id)) continue; // an upgrade under way is shown once, as the job
			B.Level = Obj->GetIntegerField(TEXT("level"));
			B.MaxLevel = Obj->GetIntegerField(TEXT("max_level"));
			B.bCanUpgrade = Obj->GetBoolField(TEXT("can_upgrade"));
			B.bAvailable = Obj->GetBoolField(TEXT("upgrade_available"));
			B.Cost = Obj->GetIntegerField(TEXT("upgrade_cost"));
			B.Days = Obj->GetIntegerField(TEXT("upgrade_days"));
			B.Salvage = Obj->GetIntegerField(TEXT("demolish_refund"));
			if (B.bCanUpgrade && !B.bAvailable) B.Reason = Obj->GetStringField(TEXT("upgrade_preview"));
			New.Plots.Add(MoveTemp(B));
		}
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("options")))
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid() || Obj->GetBoolField(TEXT("is_upgrade"))) continue;
			FPA250CityBuilding B = ReadBuilding(Obj);
			if (Building.Contains(B.Id)) continue;
			B.Cost = Obj->GetIntegerField(TEXT("cost"));
			B.Days = Obj->GetIntegerField(TEXT("build_time_days"));
			B.bAvailable = Obj->GetBoolField(TEXT("available"));
			B.Reason = Obj->GetStringField(TEXT("reason"));
			New.Options.Add(MoveTemp(B));
		}
	}
	*Snap = MoveTemp(New);
	if (!Snap->bOk) return;
	if (SelectedPlot != TEXT("+") && !SelectedPlot.IsEmpty() && !Snap->FindPlot(SelectedPlot))
	{
		SelectedPlot.Reset();
		bDemolishArmed = false;
	}

	// HudEpoch moves every sim hour. Rebuilding buttons that often would drop a click whose press and
	// release straddle the rebuild, so the plots and detail pane are rebuilt only when what they show
	// changes. Texts and progress bars are bound and follow the snapshot on their own.
	FString Sig = FString::Printf(TEXT("%s|%d|%d|%d|%d|%s|%d|"), *ProvinceId, Snap->Slots, Snap->bYours ? 1 : 0,
								  Snap->bBattlePending ? 1 : 0, Snap->Gold, *SelectedPlot, bDemolishArmed ? 1 : 0);
	for (const FPA250CityBuilding& B : Snap->Plots)
	{
		Sig += FString::Printf(TEXT("%s:%d:%d:%d;"), *B.Id, B.Level, B.bConstructing ? 1 : 0, B.DaysRemaining);
	}
	Sig += FString::Printf(TEXT("|u%d%d"), Snap->GarrisonUnpaidMonths, Snap->bSuppliedBySea ? 1 : 0);
	Sig += FString::Printf(TEXT("|t%d|%d%d|m%d:%d:%d|s%d,%d,%d,%d,%d,%d,%d|%s|"), ActiveTab, Snap->bBattleHere ? 1 : 0,
						   Snap->bEnemyInterest ? 1 : 0, Snap->bMustering ? Snap->MusterDays : -1, Snap->ManpowerUsed,
						   Snap->ManpowerCap, Snap->Powder, Snap->Cartridges, Snap->Muskets, Snap->Bayonets, Snap->Provisions,
						   Snap->Lead, Snap->Paper, Hud.IsValid() ? *Hud->GetSelectedArmyId() : TEXT(""));
	for (const FPA250CityForce& F : Snap->Forces)
	{
		Sig += FString::Printf(TEXT("%s:%d:%d:%d:%d;"), *F.ArmyId, F.Fit, F.Morale, F.Readiness, F.bMarching ? 1 : 0);
	}
	if (Sig != LastLayoutSignature)
	{
		LastLayoutSignature = Sig;
		RebuildDynamic();
	}
}

void UPA250CityViewWidget::RebuildDynamic()
{
	if (BodyBox.IsValid()) BodyBox->SetContent(BuildBody());
}

namespace
{
TSharedRef<SWidget> SectionTitle(const FPA250CityViewStyle* St, const FString& Left, const FString& Right = FString())
{
	return SNew(SVerticalBox) +
		SVerticalBox::Slot().AutoHeight()
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().FillWidth(1.f)
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(KGold).Text(FText::FromString(Left))] +
			 SHorizontalBox::Slot().AutoWidth()
				 [SNew(STextBlock).Font(Font(TEXT("Regular"), 11)).ColorAndOpacity(KCreamDim).Text(FText::FromString(Right))]] +
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 6.f)[SNew(SBox).HeightOverride(1.f)[SNew(SImage).Image(&St->Rule)]];
}

TSharedRef<SWidget> BodyLine(const FString& Text, const FLinearColor& Color, const TCHAR* Face = TEXT("Regular"), int32 Size = 11)
{
	return SNew(STextBlock).Font(Font(Face, Size)).ColorAndOpacity(Color).WrapTextAt(KContentWidth).Text(FText::FromString(Text));
}

TSharedRef<SWidget> ActionButton(const FPA250CityViewStyle* St, const FString& Label, bool bDanger, bool bEnabled, const FString& Tip,
								 TFunction<void()> Fn)
{
	return SNew(SButton)
		.ButtonStyle(bDanger ? &St->DangerAction : &St->Action)
		.IsFocusable(false)
		.IsEnabled(bEnabled)
		.ContentPadding(FMargin(12.f, 5.f))
		.HAlign(HAlign_Center)
		.ToolTipText(FText::FromString(Tip))
		.OnClicked_Lambda([Fn]() {
			Fn();
			return FReply::Handled();
		})
		[SNew(STextBlock).Font(Font(TEXT("Bold"), 11)).ColorAndOpacity(bEnabled ? KCream : KCreamDim).Text(FText::FromString(Label))];
}
} // namespace

TSharedRef<SWidget> UPA250CityViewWidget::BuildBody()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// A battle waiting here outranks everything else in the panel.
	if (S->bBattleHere)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[ActionButton(St, TEXT("Armies in contact here  —  enter battle"), true, true, TEXT("Open the order of battle for the fight at this town."),
						  [Weak]() { if (Weak.IsValid() && Weak->Hud.IsValid()) Weak->Hud->PressEnterBattleAtSelection(); })];
	}
	if (!S->bYours)
	{
		Box->AddSlot().AutoHeight()[BuildEnemyView()];
		return Box;
	}

	const TCHAR* Tabs[] = {TEXT("Buildings"), TEXT("Forces"), TEXT("Magazine")};
	TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bActive = ActiveTab == i;
		Strip->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0.f : 4.f, 0.f, 0.f, 0.f)
			[SNew(SButton)
				 .ButtonStyle(bActive ? &St->TabButtonActive : &St->TabButton)
				 .IsFocusable(false)
				 .ContentPadding(FMargin(6.f, 4.f))
				 .HAlign(HAlign_Center)
				 .OnClicked_Lambda([Weak, i]() {
					 if (Weak.IsValid()) Weak->SelectTab(i);
					 return FReply::Handled();
				 })
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(bActive ? KGold : KCreamDim).Text(FText::FromString(Tabs[i]))]];
	}
	Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[Strip];
	Box->AddSlot().AutoHeight()
		[ActiveTab == 1 ? BuildForcesTab() : ActiveTab == 2 ? BuildMagazineTab() : BuildBuildingsTab()];
	return Box;
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildEnemyView()
{
	// A town someone else holds: only what the player may know. No plots, no stores, no stats.
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[SectionTitle(St, TEXT("FORCES REPORTED"))];
	Box->AddSlot().AutoHeight()[BuildForceList()];
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[SectionTitle(St, TEXT("DEFENCES"))];
	const FPA250CityBuilding* Fort = S->FindPlot(TEXT("fort"));
	Box->AddSlot().AutoHeight()
		[BodyLine(Fort && !Fort->bConstructing
					  ? FString::Printf(TEXT("Fortified (level %d): its defenders hold a post at the start and stand firmer (+%d morale)."), Fort->Level, S->Defense)
					  : FString(TEXT("No works reported.")),
				  KCream)];
	// The other side has accounts to settle too. What reaches the player is what deserters and
	// townsfolk would carry: whether the ships still come, and whether the pay has stopped.
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[SectionTitle(St, TEXT("SUPPLY AND PAY"))];
	Box->AddSlot().AutoHeight()
		[BodyLine(S->bSuppliedBySea ? TEXT("Supplied by sea: gold and provisions are landed here every month while the harbour is theirs.")
									: TEXT("No supply by sea reaches this town: it lives on its own levies and stores."),
				  KCream)];
	Box->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[BodyLine(S->GarrisonUnpaidMonths > 0
					  ? FString::Printf(TEXT("Deserters report the garrison unpaid for %d month%s: its morale is falling."), S->GarrisonUnpaidMonths,
										S->GarrisonUnpaidMonths == 1 ? TEXT("") : TEXT("s"))
					  : FString(TEXT("The garrison is reported paid. With gold to spare it raises a fresh company each month.")),
				  S->GarrisonUnpaidMonths > 0 ? KGreen : KCreamDim)];
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[SectionTitle(St, TEXT("REPORTS"))];
	Box->AddSlot().AutoHeight()
		[BodyLine(S->bBattleHere ? TEXT("Armies in contact — battle pending.")
				  : S->bEnemyInterest ? TEXT("The enemy is reported to be gathering here (unconfirmed).")
				  : TEXT("Nothing reported. Take the town to build and muster here."),
				  KCreamDim)];
	return Box;
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildForceList()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (S->Forces.Num() == 0)
	{
		Box->AddSlot().AutoHeight()[BodyLine(TEXT("No forces known to be here."), KCreamDim)];
		return Box;
	}
	const FString SelectedArmy = Hud.IsValid() ? Hud->GetSelectedArmyId() : FString();
	for (const FPA250CityForce& F : S->Forces)
	{
		const FString Id = F.ArmyId;
		const bool bSelected = F.bYours && Id == SelectedArmy;
		TSharedRef<SVerticalBox> Text = SNew(SVerticalBox) +
			SVerticalBox::Slot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(F.bYours ? KCream : KRed).Text(FText::FromString(F.Name))];
		if (F.bYours)
		{
			FString Line = FString::Printf(TEXT("%d fit"), F.Fit);
			if (!F.Composition.IsEmpty()) Line += TEXT("  (") + F.Composition + TEXT(")");
			if (F.Sick + F.Wounded > 0) Line += FString::Printf(TEXT("  ·  %d sick, %d wounded"), F.Sick, F.Wounded);
			Text->AddSlot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(KCream).WrapTextAt(KContentWidth - 130.f).Text(FText::FromString(Line))];
			Text->AddSlot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(KCreamDim).Text(FText::FromString(FString::Printf(
					 TEXT("Morale %d  ·  readiness %d  ·  %s"), F.Morale, F.Readiness, F.bMarching ? TEXT("on the march") : TEXT("holding"))))];
		}
		else
		{
			Text->AddSlot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(KCreamDim).Text(FText::FromString(TEXT("Strength not reported")))];
		}
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text];
		if (F.bYours)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[ActionButton(St, bSelected ? TEXT("Selected") : TEXT("Give orders"), false, !bSelected,
							  TEXT("Select this force, then click a town on the map to march there."),
							  [Weak, Id]() { if (Weak.IsValid() && Weak->Hud.IsValid()) Weak->Hud->PressSelectArmy(Id); })];
		}
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SNew(SBorder).BorderImage(bSelected ? &St->PlotSelected : &St->Card).Padding(FMargin(9.f, 6.f))[Row]];
	}
	return Box;
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildBuildingsTab()
{
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	return SNew(SVerticalBox) +
		SVerticalBox::Slot().AutoHeight()
			[SectionTitle(St, TEXT("BUILDINGS"), FString::Printf(TEXT("%d of %d plots used"), S->Occupied, S->Slots))] +
		SVerticalBox::Slot().AutoHeight()[BuildPlots()] +
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
			[SNew(SBorder).BorderImage(&St->Card).Padding(FMargin(10.f, 8.f))
			 [SNew(SBox).MaxDesiredHeight(236.f)[SNew(SScrollBox) + SScrollBox::Slot()[BuildDetail()]]]];
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildForcesTab()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[SectionTitle(St, TEXT("GARRISON"))];
	Box->AddSlot().AutoHeight()[BuildForceList()];

	const int32 Left = FMath::Max(0, S->ManpowerCap - S->ManpowerUsed);
	Box->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[SectionTitle(St, TEXT("RAISE TROOPS"), FString::Printf(TEXT("%d of %d men left this month"), Left, S->ManpowerCap))];
	Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[BodyLine(TEXT("Each company is a soldier class. In battle you may fight as any class your army holds."), KCreamDim, TEXT("Italic"), 10)];
	if (S->bMustering)
	{
		Box->AddSlot().AutoHeight()
			[SNew(SBorder).BorderImage(&St->Card).Padding(FMargin(10.f, 8.f))
			 [SNew(SVerticalBox) +
			  SVerticalBox::Slot().AutoHeight()
				  [BodyLine(FString::Printf(TEXT("%s mustering  —  %d men, %s remaining"), *S->MusterName, S->MusterMen, *Days(S->MusterDays)), KGold)] +
			  SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
				  [SNew(SBox).HeightOverride(8.f)
				   [SNew(SProgressBar).Style(&St->Bar).Percent_Lambda([S]() { return TOptional<float>(FMath::Clamp(S->MusterProgress, 0.f, 1.f)); })]] +
			  SVerticalBox::Slot().AutoHeight()
				  [BodyLine(TEXT("They join your force standing in this town, or form a company of their own."), KCreamDim, TEXT("Italic"), 10)] +
			  SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 6.f, 0.f, 0.f)
				  [ActionButton(St, FString::Printf(TEXT("Send them home  (+%d gold back)"), S->MusterGold), true, true,
								TEXT("The men disperse; purse, arms, rations and the town's manpower all return."),
								[Weak]() { if (Weak.IsValid()) Weak->RequestDismissMuster(); })]]];
	}
	else
	{
		for (const FPA250CityRecruit& R : S->Recruits)
		{
			const FString Id = R.Id;
			const FLinearColor TextColor = R.bAvailable ? KCream : KCreamDim;
			FString Cost = FString::Printf(TEXT("%d %s"), R.Men, *UnitLabel(R.UnitClass, R.Men));
			if (R.Muskets > 0) Cost += FString::Printf(TEXT("  ·  %d muskets from the magazine"), R.Muskets);
			if (R.Provisions > 0) Cost += FString::Printf(TEXT("  ·  %d provisions"), R.Provisions);
			TSharedRef<SVerticalBox> Text = SNew(SVerticalBox) +
				SVerticalBox::Slot().AutoHeight()
					[SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(TextColor).Text(FText::FromString(R.Name))] +
				SVerticalBox::Slot().AutoHeight()
					[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(TextColor).WrapTextAt(KContentWidth - 140.f).Text(FText::FromString(Cost))];
			if (!R.Role.IsEmpty())
			{
				// What this class does on the field: the same class is then selectable in battle.
				Text->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[SNew(STextBlock).Font(Font(TEXT("Italic"), 10)).ColorAndOpacity(R.bAvailable ? KGold : KCreamDim).WrapTextAt(KContentWidth - 212.f).Text(FText::FromString(R.Role))];
			}
			TSharedRef<SWidget> ClassThumb = SNullWidget::NullWidget;
			if (const TSharedPtr<FSlateBrush>* Thumb = St->Icons.Find(FString::Printf(TEXT("class_%s_%s"), *S->Owner, *R.UnitClass)))
			{
				ClassThumb = SNew(SBox).WidthOverride(64.f).HeightOverride(48.f)[SNew(SImage).Image(Thumb->Get()).ColorAndOpacity(R.bAvailable ? FLinearColor::White : FLinearColor(0.55f, 0.55f, 0.55f))];
			}
			if (!R.bAvailable && !R.Reason.IsEmpty())
			{
				Text->AddSlot().AutoHeight()
					[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(KRed).WrapTextAt(KContentWidth - 140.f).Text(FText::FromString(R.Reason))];
			}
			Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[SNew(SBorder).BorderImage(&St->Card).Padding(FMargin(9.f, 6.f)).ToolTipText(FText::FromString(R.Description))
				 [SNew(SHorizontalBox) +
				  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, ClassThumb == SNullWidget::NullWidget ? 0.f : 8.f, 0.f)[ClassThumb] +
				  SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text] +
				  SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
					  [SNew(SBox).MinDesiredWidth(96.f)
					   [SNew(SButton)
							.ButtonStyle(&St->Action)
							.IsFocusable(false)
							.IsEnabled(R.bAvailable)
							.ContentPadding(FMargin(8.f, 4.f))
							.HAlign(HAlign_Center)
							.ToolTipText(FText::FromString(R.bAvailable ? TEXT("Gold and stores are spent now; the men arrive when the muster is complete.") : *R.Reason))
							.OnClicked_Lambda([Weak, Id]() {
								if (Weak.IsValid()) Weak->RequestRecruit(Id);
								return FReply::Handled();
							})
							[SNew(SVerticalBox) +
							 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
								 [SNew(STextBlock).Font(Font(TEXT("Bold"), 11)).ColorAndOpacity(R.bAvailable ? KGold : KCreamDim)
									  .Text(FText::FromString(FString::Printf(TEXT("%d gold"), R.Gold)))] +
							 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
								 [SNew(STextBlock).Font(Font(TEXT("Regular"), 9)).ColorAndOpacity(TextColor).Text(FText::FromString(Days(R.Days)))]]]]]];
		}
	}
	// The last fight's return, where the forces are read.
	const FString Report = Hud.IsValid() ? Hud->BattleReportText().ToString() : FString();
	if (!Report.IsEmpty())
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[SectionTitle(St, TEXT("LAST ENGAGEMENT"))];
		Box->AddSlot().AutoHeight()
			[SNew(SBox).MaxDesiredHeight(120.f)[SNew(SScrollBox) + SScrollBox::Slot()[BodyLine(Report, KCreamDim, TEXT("Regular"), 10)]]];
	}
	return Box;
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildMagazineTab()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	auto Store = [St](const TCHAR* Label, int32 Value) {
		return SNew(SBorder).BorderImage(&St->Card).Padding(FMargin(6.f, 4.f))
			[SNew(SVerticalBox) +
			 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 15)).ColorAndOpacity(KCream).Text(FText::AsNumber(Value))] +
			 SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				 [SNew(STextBlock).Font(Font(TEXT("Regular"), 8)).ColorAndOpacity(KCreamDim).Text(FText::FromString(Label))]];
	};
	auto HudAction = [Weak, St](const FString& Label, const TCHAR* Tip, void (UPA250MapWidget::*Fn)()) {
		return ActionButton(St, Label, false, true, Tip, [Weak, Fn]() {
			if (Weak.IsValid() && Weak->Hud.IsValid())
			{
				(Weak->Hud.Get()->*Fn)();
				Weak->Refresh();
			}
		});
	};
	return SNew(SVerticalBox) +
		SVerticalBox::Slot().AutoHeight()[SectionTitle(St, TEXT("IN THIS TOWN'S MAGAZINE"))] +
		SVerticalBox::Slot().AutoHeight()
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[Store(TEXT("POWDER"), S->Powder)] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[Store(TEXT("CARTRIDGES"), S->Cartridges)] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[Store(TEXT("MUSKETS"), S->Muskets)] +
			 SHorizontalBox::Slot().FillWidth(1.f)[Store(TEXT("BAYONETS"), S->Bayonets)]] +
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[Store(TEXT("PROVISIONS"), S->Provisions)] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[Store(TEXT("LEAD"), S->Lead)] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)[Store(TEXT("PAPER"), S->Paper)] +
			 SHorizontalBox::Slot().FillWidth(1.f)] +
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[SectionTitle(St, TEXT("BUY FOR THIS TOWN"), TEXT("arrives after transit"))] +
		SVerticalBox::Slot().AutoHeight()
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)
				 [HudAction(*FString::Printf(TEXT("%d cartridges\n%d gold"), UPA250MapWidget::BuyCartridgesQty,
											 pa250::purchase_cost(0, UPA250MapWidget::BuyCartridgesQty, 0, 0, 0, 0, 0)),
							TEXT("Ready-made cartridges for whoever fights from this town."), &UPA250MapWidget::PressBuyCartridges)] +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)
				 [HudAction(*FString::Printf(TEXT("%d muskets\n%d gold"), UPA250MapWidget::BuyMusketsQty,
											 pa250::purchase_cost(0, 0, UPA250MapWidget::BuyMusketsQty, 0, 0, 0, 0)),
							TEXT("Arms for provincial regulars raised here (a company needs 4)."), &UPA250MapWidget::PressBuyMuskets)] +
			 SHorizontalBox::Slot().FillWidth(1.f)
				 [HudAction(*FString::Printf(TEXT("%d provisions\n%d gold"), UPA250MapWidget::BuyProvisionsQty,
											 pa250::purchase_cost(0, 0, 0, 0, UPA250MapWidget::BuyProvisionsQty, 0, 0)),
							TEXT("Rations: every company raised here draws on them."), &UPA250MapWidget::PressBuyProvisions)]] +
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[SectionTitle(St, TEXT("ORDERS"))] +
		SVerticalBox::Slot().AutoHeight()
			[SNew(SHorizontalBox) +
			 SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 5.f, 0.f)
				 [HudAction(TEXT("Make cartridges"), TEXT("Set men to rolling cartridges from this town's powder, lead and paper."), &UPA250MapWidget::PressMakeCartridges)] +
			 SHorizontalBox::Slot().FillWidth(1.f)
				 [HudAction(TEXT("Send stores to depot"), TEXT("Move stores from this town to the headquarters depot."), &UPA250MapWidget::PressTransferToDepot)]] +
		SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[BodyLine(TEXT("The result of each order is reported on the strip at the bottom of the screen."), KCreamDim, TEXT("Italic"), 10)];
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildPlots()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(KContentWidth).InnerSlotPadding(FVector2D(8.f, 8.f));

	for (const FPA250CityBuilding& B : S->Plots)
	{
		const FString Id = B.Id;
		const bool bSelected = SelectedPlot == Id;
		const TSharedPtr<FSlateBrush>* Icon = St->Icons.Find(Id);
		TSharedRef<SWidget> Picture = Icon
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon->Get()).ColorAndOpacity(
				  B.bConstructing ? FPA250HudStyle::SRGB(0.45f, 0.45f, 0.45f, 1.f) : FLinearColor::White))
			: StaticCastSharedRef<SWidget>(
				  SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 14.f))
				  [SNew(STextBlock).Font(Font(TEXT("Bold"), 34)).ColorAndOpacity(B.bConstructing ? KGoldDim : KGold)
					   .Text(FText::FromString(B.Name.Left(1)))]);

		FString Tip = B.Name;
		if (B.MaxLevel > 1 || B.Level > 1) Tip += FString::Printf(TEXT(" (level %d)"), B.Level);
		if (B.bConstructing) Tip += TEXT(" — under construction, ") + Days(B.DaysRemaining) + TEXT(" remaining");
		if (!B.CampaignEffect.IsEmpty()) Tip += TEXT("\n") + B.CampaignEffect;
		if (!B.BattleEffect.IsEmpty()) Tip += TEXT("\nBattle: ") + B.BattleEffect;

		TSharedRef<SOverlay> Face = SNew(SOverlay) +
			SOverlay::Slot().Padding(3.f)[Picture] +
			SOverlay::Slot().VAlign(VAlign_Bottom).Padding(3.f)
				[SNew(SBorder).BorderImage(&St->Shade).Padding(FMargin(3.f, 2.f)).HAlign(HAlign_Center)
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 9)).ColorAndOpacity(KCream).Text(FText::FromString(B.Name))]];
		if (B.MaxLevel > 1 && !B.bConstructing)
		{
			// One brass square per level reached, a dark one per level still to build. Drawn, not
			// typed: the serif face has no geometric-shape glyphs.
			TSharedRef<SHorizontalBox> LevelPips = SNew(SHorizontalBox);
			for (int32 L = 0; L < B.MaxLevel; ++L)
			{
				LevelPips->AddSlot().AutoWidth().Padding(1.f, 2.f)
					[SNew(SBox).WidthOverride(7.f).HeightOverride(7.f)
					 [SNew(SImage).Image(&St->BarFill).ColorAndOpacity(L < B.Level ? FLinearColor::White : FLinearColor(0.22f, 0.20f, 0.17f, 1.f))]];
			}
			Face->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(5.f)
				[SNew(SBorder).BorderImage(&St->Shade).Padding(FMargin(4.f, 1.f))
				 [LevelPips]];
		}
		if (B.bConstructing)
		{
			Face->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 16.f))
				[SNew(SBorder).BorderImage(&St->Shade).Padding(FMargin(6.f, 2.f))
				 [SNew(STextBlock).Font(Font(TEXT("Bold"), 11)).ColorAndOpacity(KCream).Text(FText::FromString(Days(B.DaysRemaining)))]];
			Face->AddSlot().VAlign(VAlign_Top).Padding(3.f)
				[SNew(SBox).HeightOverride(6.f)
				 [SNew(SProgressBar).Style(&St->Bar).Percent_Lambda([S, Id]() {
					 const FPA250CityBuilding* P = S->FindPlot(Id);
					 return TOptional<float>(P ? FMath::Clamp(P->Progress, 0.f, 1.f) : 0.f);
				 })]];
		}
		Wrap->AddSlot()
			[SNew(SBox).WidthOverride(KPlotSize).HeightOverride(KPlotSize)
			 [SNew(SButton)
				  .ButtonStyle(bSelected ? &St->PlotButtonSelected : &St->PlotButton)
				  .IsFocusable(false)
				  .ContentPadding(FMargin(0.f))
				  .ToolTipText(FText::FromString(Tip))
				  .OnClicked_Lambda([Weak, Id]() {
					  if (Weak.IsValid()) Weak->SelectPlot(Id);
					  return FReply::Handled();
				  })
				  [Face]]];
	}

	const int32 Empty = FMath::Max(0, S->Slots - S->Plots.Num());
	for (int32 i = 0; i < Empty; ++i)
	{
		const bool bSelected = i == 0 && SelectedPlot == TEXT("+"); // the next plot to be built on
		Wrap->AddSlot()
			[SNew(SBox).WidthOverride(KPlotSize).HeightOverride(KPlotSize)
			 [SNew(SButton)
				  .ButtonStyle(bSelected ? &St->PlotButtonSelected : &St->PlotButtonEmpty)
				  .IsFocusable(false)
				  .IsEnabled(S->bYours)
				  .ContentPadding(FMargin(0.f))
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Center)
				  .ToolTipText(FText::FromString(S->bYours ? TEXT("Empty plot — choose what to build here.")
														   : TEXT("Empty plot. You can build only in towns you hold.")))
				  .OnClicked_Lambda([Weak]() {
					  if (Weak.IsValid()) Weak->SelectPlot(TEXT("+"));
					  return FReply::Handled();
				  })
				  [SNew(SVerticalBox) +
				   SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					   [SNew(STextBlock).Font(Font(TEXT("Regular"), 30)).ColorAndOpacity(S->bYours ? KGoldDim : FPA250HudStyle::SRGB(0.25f, 0.21f, 0.16f, 1.f))
							.Text(FText::FromString(TEXT("+")))] +
				   SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					   [SNew(STextBlock).Font(Font(TEXT("Regular"), 9)).ColorAndOpacity(KCreamDim)
							.Text(FText::FromString(S->bYours ? TEXT("Empty plot") : TEXT("Empty")))]]]];
	}
	return Wrap;
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildDetail()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	auto Line = [](const FString& Text, const FLinearColor& Color, const TCHAR* Face = TEXT("Regular"), int32 Size = 11) {
		return SNew(STextBlock).Font(Font(Face, Size)).ColorAndOpacity(Color).WrapTextAt(KDetailTextWidth).Text(FText::FromString(Text));
	};
	auto Action = [St](const FString& Label, bool bDanger, bool bEnabled, const FString& Tip, TFunction<void()> Fn) {
		return SNew(SButton)
			.ButtonStyle(bDanger ? &St->DangerAction : &St->Action)
			.IsFocusable(false)
			.IsEnabled(bEnabled)
			.ContentPadding(FMargin(12.f, 6.f))
			.HAlign(HAlign_Center)
			.ToolTipText(FText::FromString(Tip))
			.OnClicked_Lambda([Fn]() {
				Fn();
				return FReply::Handled();
			})
			[SNew(STextBlock).Font(Font(TEXT("Bold"), 11)).ColorAndOpacity(bEnabled ? KCream : KCreamDim).Text(FText::FromString(Label))];
	};

	if (SelectedPlot == TEXT("+")) return BuildBuildList();

	const FPA250CityBuilding* B = SelectedPlot.IsEmpty() ? nullptr : S->FindPlot(SelectedPlot);
	if (!B)
	{
		return Line(S->bYours ? TEXT("Select a building to upgrade or demolish it, or an empty plot to build.")
							  : TEXT("You do not hold this town. Its buildings serve whoever holds it."),
					KCreamDim);
	}

	const FString Id = B->Id;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	FString Title = B->Name;
	if (B->MaxLevel > 1 && !B->bConstructing) Title += FString::Printf(TEXT("  —  level %d of %d"), B->Level, B->MaxLevel);
	Box->AddSlot().AutoHeight()[Line(Title, KGold, TEXT("Bold"), 14)];
	if (!B->Description.IsEmpty()) Box->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 4.f)[Line(B->Description, KCreamDim, TEXT("Italic"))];
	if (!B->CampaignEffect.IsEmpty()) Box->AddSlot().AutoHeight()[Line(TEXT("Campaign: ") + B->CampaignEffect, KCream)];
	if (!B->BattleEffect.IsEmpty()) Box->AddSlot().AutoHeight()[Line(TEXT("Battle: ") + B->BattleEffect, KCream)];

	if (B->bConstructing)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 3.f)
			[Line(FString::Printf(TEXT("Under construction%s — %s remaining"),
								  B->Level > 1 ? *FString::Printf(TEXT(" (to level %d)"), B->Level) : TEXT(""), *Days(B->DaysRemaining)),
				  KGold)];
		Box->AddSlot().AutoHeight()
			[SNew(SBox).HeightOverride(8.f)
			 [SNew(SProgressBar).Style(&St->Bar).Percent_Lambda([S, Id]() {
				 const FPA250CityBuilding* P = S->FindPlot(Id);
				 return TOptional<float>(P ? FMath::Clamp(P->Progress, 0.f, 1.f) : 0.f);
			 })]];
		if (S->bYours)
		{
			Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 8.f, 0.f, 0.f)
				[Action(FString::Printf(TEXT("Stop work  (+%d gold back)"), B->PaidGold), true, true,
						TEXT("Nothing has been spent yet beyond the purse set aside: stopping returns it in full."),
						[Weak, Id]() { if (Weak.IsValid()) Weak->RequestCancel(Id); })];
		}
		return Box;
	}
	if (!S->bYours) return Box;

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (B->bCanUpgrade)
	{
		Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[Action(FString::Printf(TEXT("Upgrade to level %d  —  %d gold, %s"), B->Level + 1, B->Cost, *Days(B->Days)), false,
					B->bAvailable, B->bAvailable ? TEXT("Gold is spent now; the level rises when the work is done.") : B->Reason,
					[Weak, Id]() { if (Weak.IsValid()) Weak->RequestBuild(Id); })];
	}
	Row->AddSlot().AutoWidth()
		[Action(bDemolishArmed ? FString::Printf(TEXT("Confirm: pull it down  (+%d gold salvage)"), B->Salvage) : FString(TEXT("Demolish")),
				true, !S->bBattlePending,
				FString::Printf(TEXT("Pulls the building down for good and frees its plot. Only salvage comes back: +%d gold "
									 "for timber, nails and brick. A second click confirms."),
								B->Salvage),
				[Weak, Id]() {
					if (!Weak.IsValid()) return;
					if (!Weak->bDemolishArmed)
					{
						Weak->bDemolishArmed = true;
						Weak->Refresh();
						return;
					}
					Weak->RequestDemolish(Id);
				})];
	Box->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[Row];
	if (!B->bCanUpgrade && B->MaxLevel > 1) Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[Line(TEXT("Fully developed."), KGreen)];
	if (B->bCanUpgrade && !B->bAvailable && !B->Reason.IsEmpty())
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[Line(B->Reason, KRed, TEXT("Regular"), 10)];
	}
	return Box;
}

TSharedRef<SWidget> UPA250CityViewWidget::BuildBuildList()
{
	const TWeakObjectPtr<UPA250CityViewWidget> Weak(this);
	const TSharedPtr<FPA250CitySnapshot> S = Snap;
	const FPA250CityViewStyle* St = Style.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(KGold).Text(FText::FromString(TEXT("Build on this plot")))];
	if (S->Options.Num() == 0)
	{
		Box->AddSlot().AutoHeight()
			[SNew(STextBlock).Font(Font(TEXT("Regular"), 11)).ColorAndOpacity(KCreamDim).WrapTextAt(KListTextWidth)
				 .Text(FText::FromString(TEXT("Everything this town can hold is built or under way.")))];
		return Box;
	}
	for (const FPA250CityBuilding& B : S->Options)
	{
		const FString Id = B.Id;
		const TSharedPtr<FSlateBrush>* Icon = St->Icons.Find(Id);
		const FLinearColor TextColor = B.bAvailable ? KCream : KCreamDim;
		TSharedRef<SWidget> Picture = Icon
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon->Get()).ColorAndOpacity(
				  B.bAvailable ? FLinearColor::White : FPA250HudStyle::SRGB(0.4f, 0.4f, 0.4f, 1.f)))
			: StaticCastSharedRef<SWidget>(
				  SNew(SBorder).BorderImage(&St->PlotEmpty).HAlign(HAlign_Center).VAlign(VAlign_Center)
				  [SNew(STextBlock).Font(Font(TEXT("Bold"), 20)).ColorAndOpacity(B.bAvailable ? KGold : KGoldDim)
					   .Text(FText::FromString(B.Name.Left(1)))]);
		TSharedRef<SVerticalBox> Text = SNew(SVerticalBox) +
			SVerticalBox::Slot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Bold"), 12)).ColorAndOpacity(TextColor).Text(FText::FromString(B.Name))] +
			SVerticalBox::Slot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(TextColor).WrapTextAt(KListTextWidth)
					 .Text(FText::FromString(B.CampaignEffect))];
		if (!B.BattleEffect.IsEmpty())
		{
			Text->AddSlot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(TextColor).WrapTextAt(KListTextWidth)
					 .Text(FText::FromString(TEXT("Battle: ") + B.BattleEffect))];
		}
		if (!B.bAvailable && !B.Reason.IsEmpty())
		{
			Text->AddSlot().AutoHeight()
				[SNew(STextBlock).Font(Font(TEXT("Regular"), 10)).ColorAndOpacity(KRed).WrapTextAt(KListTextWidth).Text(FText::FromString(B.Reason))];
		}
		Box->AddSlot().AutoHeight().Padding(0.f, 3.f)
			[SNew(SHorizontalBox).ToolTipText(FText::FromString(B.Description)) +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 8.f, 0.f)
				 [SNew(SBox).WidthOverride(46.f).HeightOverride(46.f)[Picture]] +
			 SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text] +
			 SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				 [SNew(SBox).MinDesiredWidth(96.f)
				  [SNew(SButton)
					   .ButtonStyle(&St->Action)
					   .IsFocusable(false)
					   .IsEnabled(B.bAvailable)
					   .ContentPadding(FMargin(8.f, 4.f))
					   .HAlign(HAlign_Center)
					   .ToolTipText(FText::FromString(B.bAvailable ? TEXT("Gold is spent now; the building stands when the work is done.") : *B.Reason))
					   .OnClicked_Lambda([Weak, Id]() {
						   if (Weak.IsValid()) Weak->RequestBuild(Id);
						   return FReply::Handled();
					   })
					   [SNew(SVerticalBox) +
						SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
							[SNew(STextBlock).Font(Font(TEXT("Bold"), 11)).ColorAndOpacity(B.bAvailable ? KGold : KCreamDim)
								 .Text(FText::FromString(FString::Printf(TEXT("%d gold"), B.Cost)))] +
						SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
							[SNew(STextBlock).Font(Font(TEXT("Regular"), 9)).ColorAndOpacity(TextColor).Text(FText::FromString(Days(B.Days)))]]]]];
	}
	return Box;
}

void UPA250CityViewWidget::RequestBuild(const FString& BuildingId)
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S) return;
	FString Err;
	if (!S->StartConstruction(ProvinceId, BuildingId, Err))
	{
		// The sim's own sentence when it has one (no plot, not enough gold); controller-level
		// refusals (battle pending, not in campaign) have no preview and use the code table.
		const FString Preview = S->GetBuildingPreview(ProvinceId, BuildingId);
		SetStatus(Preview.StartsWith(TEXT("Unavailable")) ? Preview : FriendlyError(Err));
		return;
	}
	SetStatus(S->GetLastResourceChangeReason());
	SelectedPlot = BuildingId; // follow the new work on its plot
	bDemolishArmed = false;
	Refresh();
}

void UPA250CityViewWidget::RequestDemolish(const FString& BuildingId)
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S) return;
	FString Err;
	bDemolishArmed = false;
	if (S->DemolishBuilding(ProvinceId, BuildingId, Err))
	{
		SetStatus(S->GetLastResourceChangeReason());
		SelectedPlot.Reset();
	}
	else
	{
		SetStatus(FriendlyError(Err));
	}
	Refresh();
}

void UPA250CityViewWidget::RequestRecruit(const FString& UnitId)
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S) return;
	FString Err;
	if (S->StartRecruitment(ProvinceId, UnitId, Err))
	{
		SetStatus(S->GetLastResourceChangeReason());
	}
	else
	{
		const FString Preview = S->GetRecruitmentPreview(ProvinceId, UnitId);
		SetStatus(Preview.StartsWith(TEXT("Unavailable")) ? Preview : FriendlyError(Err));
	}
	Refresh();
}

void UPA250CityViewWidget::RequestDismissMuster()
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S) return;
	FString Err;
	SetStatus(S->CancelRecruitment(ProvinceId, Err) ? S->GetLastResourceChangeReason() : FriendlyError(Err));
	Refresh();
}

void UPA250CityViewWidget::RequestCancel(const FString& BuildingId)
{
	UPA250Subsystem* S = UPA250Subsystem::Get(this);
	if (!S) return;
	FString Err;
	if (S->CancelConstruction(ProvinceId, BuildingId, Err))
	{
		SetStatus(S->GetLastResourceChangeReason());
	}
	else
	{
		SetStatus(FriendlyError(Err));
	}
	Refresh();
}
