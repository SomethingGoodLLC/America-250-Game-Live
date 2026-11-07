// SPA250Map.h — Slate canvas for the Boston campaign map (province tint, borders, armies, markers).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"

class UPA250Subsystem;
class UTexture2D;

class SPA250Map : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPA250Map) {}
		SLATE_ARGUMENT(UPA250Subsystem*, Subsystem)
		SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	                      bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(1000.0, 700.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	// ---- Input logic, callable without a mouse (used by the autoplay/automation paths) ----
	/** Province id containing a map-space point, or empty. Point-in-polygon over the sim's province outlines. */
	FString ProvinceAt(const FVector2D& MapPoint) const;
	/** Left-click semantics: select an army, or order the selected army to an adjacent province. Returns a message. */
	FString HandleProvinceClick(const FString& ProvinceId);
	void ClearSelection();

	FString SelectedArmyId;
	FString SelectedProvinceId;
	FString LastMessage;

	/** Marks the ownership tint texture stale (call after any state change). */
	void InvalidateOverlay() { bOverlayDirty = true; }

private:
	static bool PointInPolygon(const TArray<FVector2D>& Poly, const FVector2D& P);
	void RebuildOverlay() const;
	void EnsureMapTexture() const;
	void ComputeTransform(const FGeometry& G, float& OutScale, FVector2D& OutOffset) const;
	FVector2D ToScreen(const FVector2D& MapPt, float Scale, const FVector2D& Offset) const { return Offset + MapPt * Scale; }

	TWeakObjectPtr<UPA250Subsystem> Subsystem;
	FSimpleDelegate OnSelectionChanged;

	mutable TStrongObjectPtr<UTexture2D> MapTexture;
	mutable TStrongObjectPtr<UTexture2D> OverlayTexture;
	mutable FSlateBrush MapBrush;
	mutable FSlateBrush OverlayBrush;
	mutable bool bOverlayDirty = true;
	mutable bool bMapTried = false;
	mutable FString OverlaySignature;
	mutable float LastScale = 1.f;
	mutable FVector2D LastOffset = FVector2D::ZeroVector;
};
