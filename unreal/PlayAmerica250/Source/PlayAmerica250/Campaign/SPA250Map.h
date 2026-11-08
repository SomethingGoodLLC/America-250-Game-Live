// SPA250Map.h — LEGACY Slate map canvas (retired M10-1). Left unreferenced; safe to delete with assets.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/** Retired. Do not instantiate. Map drawing lives on APA250ProvinceMapActor. */
class SPA250Map : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPA250Map) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
						  FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
						  bool bParentEnabled) const override
	{
		return LayerId;
	}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }
};
