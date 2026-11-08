// PA250BotSpacingTest.h — AFunctionalTest criteria for FTEST_BotSpacing (also runnable headless via Evaluate).
#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "PA250BotSpacingTest.generated.h"

UCLASS()
class PLAYAMERICA250_API APA250BotSpacingTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	APA250BotSpacingTest();

	UPROPERTY(EditAnywhere, Category = "PA250")
	float DurationSeconds = 90.f;

	UPROPERTY(EditAnywhere, Category = "PA250")
	float MedianNearestNeighborMinCm = 200.f;

	UPROPERTY(EditAnywhere, Category = "PA250")
	float MaxCrowdedFraction = 0.10f;

	UPROPERTY(EditAnywhere, Category = "PA250")
	int32 MinClustersPerTeam = 3;

	UPROPERTY(EditAnywhere, Category = "PA250")
	int32 MinKills = 5;

	UPROPERTY(EditAnywhere, Category = "PA250")
	float MinMidRangeShotFraction = 0.70f;

	/** Pure evaluation used by the functional test and the battle commandlet. */
	static bool EvaluateSnapshot(
		float MedianNN,
		float CrowdedFraction,
		int32 ClustersA,
		int32 ClustersB,
		int32 Kills,
		float MidRangeShotFraction,
		float MedianMin,
		float MaxCrowded,
		int32 MinClusters,
		int32 MinKillsIn,
		float MinMidRange,
		FString& OutReason);

protected:
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	float Elapsed = 0.f;
	int32 KillCount = 0;
	int32 MidRangeShots = 0;
	int32 TotalShots = 0;
};
