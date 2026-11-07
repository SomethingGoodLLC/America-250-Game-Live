// PA250BotController.h — code-driven bot brain: advance to the objective, engage the nearest enemy, reload.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "PA250BotController.generated.h"

class ARFCharacter;
class APA250ControlPoint;

UENUM()
enum class EBotState : uint8
{
	Advance,
	Engage,
	Reload,
	HoldObjective
};

UCLASS()
class PLAYAMERICA250_API APA250BotController : public AAIController
{
	GENERATED_BODY()

public:
	APA250BotController();

	/** Aim error cone half-angle in degrees. */
	UPROPERTY(EditAnywhere, Category = "Bot")
	float AimErrorDegrees = 2.5f;

	/** Enemies farther than this are ignored. */
	UPROPERTY(EditAnywhere, Category = "Bot")
	float SightRange = 6000.f;

	/** Fire when the nearest visible enemy is within this range. */
	UPROPERTY(EditAnywhere, Category = "Bot")
	float EngageRange = 3500.f;

	UPROPERTY(VisibleAnywhere, Category = "Bot")
	EBotState State = EBotState::Advance;

	void SetObjective(APA250ControlPoint* InObjective) { Objective = InObjective; }

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	ARFCharacter* FindNearestEnemy(const ARFCharacter* Me, float& OutDist) const;
	bool HasLineOfSight(const ARFCharacter* Me, const ARFCharacter* Enemy) const;
	void MoveToward(const ARFCharacter* Me, const FVector& Dest, float Acceptance);

	TWeakObjectPtr<APA250ControlPoint> Objective;
	float ThinkTimer = 0.f;
};
