// PA250MapTooltip.h — small hover tooltip for campaign map towns and armies (game-rendered text).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250MapTooltip.generated.h"

class SBorder;
class STextBlock;

/**
 * Parchment tooltip placed beside the cursor. It never takes input (hit-test invisible), flips to
 * stay inside the viewport, and is offset from the cursor so it never covers what is hovered.
 * Content comes from APA250ProvinceMapActor::DescribeHover (read-only sim state).
 */
UCLASS()
class PLAYAMERICA250_API UPA250MapTooltip : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show Text with its top-left near CursorPx (viewport pixels), kept inside ViewportPx. */
	void ShowAt(const FString& Text, FVector2D CursorPx, FVector2D ViewportPx);
	/** Show beside AvoidPx (screen box of what is hovered) so the tooltip never covers it. */
	void ShowBeside(const FString& Text, const FBox2D& AvoidPx, FVector2D CursorPx, FVector2D ViewportPx);
	void HideTip();
	bool IsShowing() const { return bShowing; }

	/** Gap between cursor and tooltip, in viewport pixels. */
	static constexpr float CursorOffsetPx = 18.f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SBorder> Panel;
	TSharedPtr<STextBlock> Label;
	FString Current;
	bool bShowing = false;
};
