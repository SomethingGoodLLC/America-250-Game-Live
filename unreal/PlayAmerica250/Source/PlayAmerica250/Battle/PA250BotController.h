// PA250BotController.h — code-driven bot brain: capture posts, defend under attack, engage, reload.
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
	HoldObjective,
	Defend
};

UCLASS()
class PLAYAMERICA250_API APA250BotController : public AAIController
{
	GENERATED_BODY()

public:
	APA250BotController();

	UPROPERTY(EditAnywhere, Category = "Bot")
	float AimErrorDegrees = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float SightRange = 6000.f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float EngageRange = 3500.f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float ReloadSkill = 0.6f;

	UPROPERTY(VisibleAnywhere, Category = "Bot")
	EBotState State = EBotState::Advance;

	void SetPosts(const TArray<TWeakObjectPtr<APA250ControlPoint>>& InPosts) { Posts = InPosts; }
	void SetObjective(APA250ControlPoint* InObjective);

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	ARFCharacter* FindNearestEnemy(const ARFCharacter* Me, float& OutDist) const;
	bool HasLineOfSight(const ARFCharacter* Me, const ARFCharacter* Enemy) const;
	void MoveToward(const ARFCharacter* Me, const FVector& Dest, float Acceptance);
	APA250ControlPoint* PickGoal(const ARFCharacter* Me) const;

	TArray<TWeakObjectPtr<APA250ControlPoint>> Posts;
	TWeakObjectPtr<APA250ControlPoint> Objective;
	float ThinkTimer = 0.f;
};
