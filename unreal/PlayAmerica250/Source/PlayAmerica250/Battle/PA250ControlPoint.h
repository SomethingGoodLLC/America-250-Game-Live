// PA250ControlPoint.h — the single capture objective on the Green. Server-side presence/hold timers.
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
	float Radius = 1200.f;

	/** Seconds of uncontested presence to flip the holder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CaptureSeconds = 8.f;

	/** Current holder faction, or NAME_None. */
	UFUNCTION(BlueprintPure, Category = "Objective")
	FName GetHolder() const { return Holder; }

	/** Continuous seconds the current holder has kept the point. */
	UFUNCTION(BlueprintPure, Category = "Objective")
	float GetHoldSeconds() const { return HoldSeconds; }

	/** Alive soldiers per faction currently on the point. */
	int32 GetPresence(FName Faction) const { const int32* N = Presence.Find(Faction); return N ? *N : 0; }

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Zone;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Disc;

private:
	void UpdateColor();

	FName Holder = NAME_None;
	FName Capturer = NAME_None;
	float CaptureProgress = 0.f;
	float HoldSeconds = 0.f;
	TMap<FName, int32> Presence;
};
