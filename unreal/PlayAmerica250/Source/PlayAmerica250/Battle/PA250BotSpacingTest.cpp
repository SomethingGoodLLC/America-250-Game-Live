#include "PA250BotSpacingTest.h"
#include "PA250BattleGameMode.h"
#include "PA250VolleyCoordinator.h"

APA250BotSpacingTest::APA250BotSpacingTest()
{
	PrimaryActorTick.bCanEverTick = true;
}

bool APA250BotSpacingTest::EvaluateSnapshot(
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
	FString& OutReason)
{
	if (MedianNN < MedianMin)
	{
		OutReason = FString::Printf(TEXT("median NN %.1f < %.1f"), MedianNN, MedianMin);
		return false;
	}
	if (CrowdedFraction > MaxCrowded)
	{
		OutReason = FString::Printf(TEXT("crowded frac %.2f > %.2f"), CrowdedFraction, MaxCrowded);
		return false;
	}
	if (ClustersA < MinClusters || ClustersB < MinClusters)
	{
		OutReason = FString::Printf(TEXT("clusters %d/%d < %d"), ClustersA, ClustersB, MinClusters);
		return false;
	}
	if (Kills < MinKillsIn)
	{
		OutReason = FString::Printf(TEXT("kills %d < %d"), Kills, MinKillsIn);
		return false;
	}
	if (MidRangeShotFraction < MinMidRange)
	{
		OutReason = FString::Printf(TEXT("mid-range shot frac %.2f < %.2f"), MidRangeShotFraction, MinMidRange);
		return false;
	}
	OutReason = TEXT("ok");
	return true;
}

void APA250BotSpacingTest::StartTest()
{
	Super::StartTest();
	Elapsed = 0.f;
	KillCount = 0;
	MidRangeShots = 0;
	TotalShots = 0;
}

void APA250BotSpacingTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsRunning()) return;
	Elapsed += DeltaSeconds;
	if (Elapsed < DurationSeconds) return;

	float Median = 0.f, Crowded = 0.f;
	int32 CB = 0, CC = 0;
	float MidFrac = 0.f;
	int32 Samples = 0;
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		if (UPA250VolleyCoordinator* V = GM->GetVolleys())
		{
			V->EvaluateSpacing(Median, Crowded, CB, CC);
			MidFrac = V->GetMidRangeFraction();
			Samples = V->GetEngagementSamples();
			V->PrintSpacingReport();
		}
		KillCount = GM->GetTotalDeaths();
	}
	TotalShots = Samples;
	MidRangeShots = FMath::RoundToInt(MidFrac * Samples);
	FString Reason;
	if (EvaluateSnapshot(Median, Crowded, CB, CC, KillCount, MidFrac,
		MedianNearestNeighborMinCm, MaxCrowdedFraction, MinClustersPerTeam, MinKills, MinMidRangeShotFraction, Reason))
	{
		FinishTest(EFunctionalTestResult::Succeeded, Reason);
	}
	else
	{
		FinishTest(EFunctionalTestResult::Failed, Reason);
	}
}
