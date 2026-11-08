// PA250BattleHUD.h — tickets, CP pips, reticle, hitmarker, kill feed, reload bar.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PA250BattleHUD.generated.h"

class APA250BattleGameMode;

UCLASS()
class PLAYAMERICA250_API UPA250BattleHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<APA250BattleGameMode> GameMode;

	void ShowHitmarker(bool bKill);
	bool IsReticleHostile() const;
	bool IsReticleBlocked() const;
	float GetReticleSpreadPx() const;
	float GetHitmarkerTimer() const { return HitmarkerTimer; }
	bool IsHitmarkerKill() const { return bHitmarkerKill; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** Objective markers over each post (letter, holder colour, capture bar, ATTACK/DEFEND, distance)
	 *  and the letter boxes under the score bar. */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

public:
	/** Pure: the order shown over a post for a player of faction Me ("ATTACK", "DEFEND", "CAPTURING" or ""). */
	static FString PostOrder(FName Me, FName Holder, FName Capturer, float Progress, bool bContested);
	static TCHAR PostLetter(int32 Index) { return TCHAR('A' + Index); }

private:
	float HitmarkerTimer = 0.f;
	bool bHitmarkerKill = false;
};
