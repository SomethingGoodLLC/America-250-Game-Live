// PA250ControlPoint.h — capturable conquest post (Green / Meetinghouse / Buckman).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250ControlPoint.generated.h"

class USphereComponent;
class UStaticMeshComponent;

UCLASS()
class PLAYAMERICA250_API APA250ControlPoint : public AActor
{
	GENERATED_BODY()

public:
	APA250ControlPoint();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName PostId = TEXT("green");

	/** Faction that owns the post at BeginPlay, or NAME_None for neutral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName InitialOwner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float Radius = 1200.f;

	/** Seconds of sole presence (one attacker) to complete one ownership step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CaptureSeconds = 3.5f;

	/** Extra rate per additional attacker: rate = 1 + (n-1)*scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CapturePerAttackerScale = 0.35f;

	/** Multiplier applied while any overlapping soldier is suppressed (volley). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CaptureRateScale = 1.f;

	UFUNCTION(BlueprintPure, Category = "Objective")
	FName GetHolder() const { return Holder; }

	UFUNCTION(BlueprintPure, Category = "Objective")
	FName GetCapturer() const { return Capturer; }

	/** 0..1 progress toward the next ownership step (to neutral or to capturer). */
	UFUNCTION(BlueprintPure, Category = "Objective")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsContested() const { return bContested; }

	int32 GetPresence(FName Faction) const { const int32* N = Presence.Find(Faction); return N ? *N : 0; }

	/** True if this post flipped ownership since the last call (clears the flag). */
	bool ConsumeFlipFlag()
	{
		const bool B = bFlippedSinceLastCheck;
		bFlippedSinceLastCheck = false;
		return B;
	}

	void ApplyTuning(float InRadius, float InCaptureSeconds, float InPerAttackerScale);

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Zone;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Disc;

private:
	void UpdateColor();
	void SetHolder(FName NewHolder);

	FName Holder = NAME_None;
	FName Capturer = NAME_None;
	float CaptureProgress = 0.f;
	bool bContested = false;
	bool bFlippedSinceLastCheck = false;
	TMap<FName, int32> Presence;
};
