// PA250BotTuning.h — squad spacing, engage distances, charge / cover thresholds.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PA250BotTuning.generated.h"

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250BotTuningValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	int32 SquadMinSize = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	int32 SquadMaxSize = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	float FileSpacingCm = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	float MinSquadCenterSeparationCm = 1500.f;

	/** Back-to-front distance between the two ranks of a squad line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float RankDepthCm = 200.f;

	/** Widest a rank gets; bigger squads stack into more ranks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	int32 MaxFilesPerRank = 6;

	/** How fast the formation anchor marches (bots walk ~380). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float MarchSpeedCm = 260.f;

	/** A man is "in position" within this distance of his file spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float FormationArriveCm = 120.f;

	/** The march pauses while men lag more than this behind their spots on average. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float FormationMaxLagCm = 500.f;

	/** Share of the squad that must be loaded and in position before "Make ready!". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float VolleyReadyFraction = 0.7f;

	/** After this long engaged without forming up, volley with whoever is loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float VolleyMaxWaitSeconds = 6.f;

	/** Max stagger across a squad volley (seconds). 0 = all fire on the same frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation", meta = (ClampMin = "0"))
	float VolleyFireJitterSeconds = 0.1f;

	/** Seconds without contact before a halted line resumes its march. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float DisengageSeconds = 4.f;

	/** Squads order a bayonet charge on an enemy closer than this (see PA250ShouldOrderCharge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeOrderRangeCm = 3500.f;

	/** A squad charge gives up after this long if it hasn't closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeOrderMaxSeconds = 12.f;

	/** Rest between a squad's charges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	float ChargeOrderCooldownSeconds = 10.f;

	// ---- Militia muster (GAME INVENTION, after 19 April 1775: companies kept arriving all day) ----
	/** A side's militia start partly on the field and the rest march in, in companies, over the battle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muster")
	bool bMilitiaMuster = true;

	/** Share of the side's militia places on the field at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muster", meta = (ClampMin = "0", ClampMax = "1"))
	float MusterInitialFraction = 0.5f;

	/** Seconds between arriving companies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muster")
	float MusterWaveSeconds = 15.f;

	/** Men per arriving company. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muster")
	int32 MusterWaveSize = 4;

	/** Militia on the field can grow to this multiple of their starting share (never beyond the side's tickets). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muster")
	float MusterMaxAliveScale = 2.0f;

	/** Skirmish roles take position behind a tagged wall (PA250Cover) within this distance of their post. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muster")
	float CoverLineSearchCm = 5000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots")
	float SlotRingMinCm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots")
	float SlotRingMaxCm = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots")
	int32 SlotsPerPost = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
	float SeparationWeight = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
	float CrowdAgentRadiusCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float EngageMinCm = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float EngageMaxCm = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float ReactionMinSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float ReactionMaxSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float VolleyWindowSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float ChargeRangeCm = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float ChargeVsReloadRangeCm = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float CoverSquadStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fight")
	float AveragePlayerReloadSkill = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacingTest")
	float MedianNearestNeighborMinCm = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacingTest")
	float CrowdedNeighborRadiusCm = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacingTest")
	int32 CrowdedNeighborCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacingTest")
	float MaxCrowdedFraction = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpacingTest")
	int32 MinClustersPerTeam = 3;
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UPA250BotTuning : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bot")
	FPA250BotTuningValues Values;

	static UPA250BotTuning* MakeRuntimeDefaults();
};
