// PA250VolleyCoordinator.h — squad volleys, callouts, suppression.
#pragma once

#include "CoreMinimal.h"
#include "FormationQuery.h"
#include "PA250CombatProfiles.h"
#include "PA250Morale.h"
#include "UObject/Object.h"
#include "PA250VolleyCoordinator.generated.h"

class ARFCharacter;
class APA250BattleGameMode;
class UMusketTuning;
class UWorld;

UENUM()
enum class EVolleyCall : uint8
{
	None,
	MakeReady,
	Present,
	Fire
};

UCLASS()
class PLAYAMERICA250_API UPA250VolleyCoordinator : public UObject
{
	GENERATED_BODY()

public:
	void Init(APA250BattleGameMode* InGM);
	void Tick(float DeltaSeconds);
	void NotifyShot(ARFCharacter* Shooter, const FVector& Dir, bool bHit);

	FString GetCallout() const;
	int32 GetVolleyCount() const { return VolleyCount; }
	float GetAverageHits() const { return VolleyCount > 0 ? (float)TotalHits / VolleyCount : 0.f; }
	int32 GetLineEngagements() const { return LineEngagements; }
	int32 GetEngagements() const { return Engagements; }
	void PrintSpacingReport() const;
	bool EvaluateSpacing(float& OutMedianNN, float& OutCrowdedFrac, int32& OutClustersBrit, int32& OutClustersCont) const;

	/** Live / peak members for a squad id (1 when unknown). Drives the bot cover rule. */
	float GetSquadStrength(int32 SquadId) const;
	/** Bot shot ranges: buckets <20, 20-40, 40-80, 80-120, >=120 m. */
	void RecordEngagement(float RangeCm);
	const TArray<int32>& GetEngagementHistogram() const { return EngagementHistogram; }
	int32 GetEngagementSamples() const;
	/** Fraction of recorded bot shots inside the 40-80 m engage band (0 when no samples). */
	float GetMidRangeFraction() const;
	static int32 EngagementBucket(float RangeCm);
	/** Squads on the same post prefer sectors three octants apart (>= 15 m between centers). */
	static int32 SectorForSquad(int32 SquadId) { return (SquadId * 3) % 8; }

	/** Pure: formation spots for N men, X = lateral (right +), Y = distance behind the front rank.
	 *  Fewer than 4 men stand in one rank; otherwise two (or more) staggered ranks of <= MaxPerRank. */
	static void FormationOffsets(int32 N, float FileCm, float RankCm, int32 MaxPerRank, TArray<FVector2D>& Out);

	/** Squads are bots only: the player's character is never ordered by morale or tactics. */
	static bool ShouldJoinSquad(const ARFCharacter* C);
	/** Pick a wall segment for a skirmish squad near PostLoc (forward toward EnemyRear preferred), at
	 *  least MinApartCm from Taken; Out = the friendly side of it. Uses the cached PA250Cover anchors. */
	bool PickCoverLine(const FVector& PostLoc, const FVector& EnemyRear, const FVector& OwnRear, float SearchCm,
		float MinApartCm, const TArray<FVector>& Taken, FVector& Out) const;
	void SetCoverAnchorsForTest(const TArray<FVector>& In) { CoverAnchors = In; bCoverAnchorsBuilt = true; }
	/** Extra AI reload stress from the squad's morale state (0 when unknown / steady). */
	float GetSquadMoraleStress(int32 SquadId) const;
	EPA250Morale GetSquadMorale(int32 SquadId) const;
	/** {"british":{"Shaken":n,...},...} morale transitions so far, for battle_result.json. */
	FString MoraleTraceJson() const;

	/** Pure: enough safe-ready men, or max wait elapsed with at least one safe shooter. */
	static bool VolleyReadyToCall(int32 SafeReady, int32 LiveNum, float ReadyFraction, float NotReadyTime, float MaxWaitSeconds);
	/** Pure: seconds after "Fire!" that one man's piece goes off. OrderFromRight is his place among the
	 *  shooters counted from the right of the line; RollingFileSeconds > 0 makes the fire run down the
	 *  line, and JitterSeconds * Rand01 is the raggedness on top. */
	static float VolleyShotDelay(int32 OrderFromRight, float RollingFileSeconds, float JitterSeconds, float Rand01);
	/** Pure: pending discharge may fire only while the squad epoch still matches and its target lives. */
	static bool ShouldDischargePending(int32 SquadEpoch, int32 EntryEpoch, bool bAlive, bool bCanFire, bool bLaneClear, bool bTargetAlive);
	/** Muzzle-to-aim clear of world blocks and living same-faction soldiers. */
	static bool HasClearFiringLane(UWorld* World, const ARFCharacter* From, const ARFCharacter* Target, const FVector& AimAt);

private:
	void RefreshFormations();
	void TickSquads(float DeltaSeconds);
	void AssignSquadSlots();

	TWeakObjectPtr<APA250BattleGameMode> GameMode;
	TMap<FName, TArray<FVolleyShot>> RecentShots;
	float ShotClock = 0.f;
	FString Callout;
	float CalloutTimer = 0.f;
	int32 VolleyCount = 0;
	int32 TotalHits = 0;
	int32 LineEngagements = 0;
	int32 Engagements = 0;

	struct FPendingVolleyShot
	{
		TWeakObjectPtr<ARFCharacter> Shooter;
		TWeakObjectPtr<ARFCharacter> Target;
		FVector AimPoint = FVector::ZeroVector;
		float FireAtTime = 0.f;
		int32 Epoch = 0;
	};

	struct FSquad
	{
		int32 Id = -1;
		int32 Peak = 0;
		FName Faction;
		TArray<TWeakObjectPtr<ARFCharacter>> Members;
		EVolleyCall Stage = EVolleyCall::None;
		float StageTimer = 0.f;
		// Formation
		bool bAnchored = false;
		FVector Anchor = FVector::ZeroVector;
		FVector Facing = FVector::ForwardVector;
		bool bEngaged = false;
		float LastContactTime = -1000.f;
		float NotReadyTime = 0.f;
		bool bHasObjective = false;
		FVector ObjectivePoint = FVector::ZeroVector;
		TWeakObjectPtr<ARFCharacter> Contact;
		/** Member order along the line; re-sorted when the line turns so men don't cross. */
		TArray<TWeakObjectPtr<ARFCharacter>> FileOrder;
		// Training / role (all members share them) and morale
		FPA250TrainingProfile Training;
		FPA250TacticalRole Role;
		FPA250MoraleState Morale;
		float RecentLoss = 0.f;
		int32 LastLive = -1;
		float LastWithdrawTime = -1000.f;
		FVector WithdrawStart = FVector::ZeroVector;
		FVector RallyPoint = FVector::ZeroVector;
		// Cover (roles that prefer it): per file, a firing position near the spot, refreshed periodically
		TArray<FVector> CoverSpot;
		TArray<bool> bHasCover;
		float CoverQueryTimer = 0.f;
		float CoveredFraction = 0.f;
		bool bPressing = false;
		bool bCharging = false;
		float ChargeStartTime = -1000.f;
		float LastChargeEndTime = -1000.f;
		/** Bumped to invalidate delayed volley shots after cancel. */
		int32 VolleyEpoch = 0;
		/** Volleys fired since this contact began: the first is fired together, later ones may roll. */
		int32 VolleysThisFight = 0;
		TArray<FPendingVolleyShot> PendingShots;
		int32 DebugLoaded = 0;
		int32 DebugFormedReady = 0;
		int32 DebugSafeReady = 0;
		FString DebugReason = TEXT("idle");
	};
	void TickFormation(FSquad& Sq, const TArray<ARFCharacter*>& Live, float DeltaSeconds);
	void SortFiles(FSquad& Sq, const TArray<ARFCharacter*>& Live);
	bool IsNearViewer(const FVector& Where) const;
	void AnnounceMorale(const FSquad& Sq, EPA250Morale Before);
	/** Show a callout and play the matching shouted line (voice of VoiceFaction) at Where. */
	void Shout(FName VoiceFaction, const TCHAR* Key, const TCHAR* Text, const FVector& Where, float Seconds = 2.f);
	/** News about the enemy, shouted by the player's side close to the camera. */
	void ShoutNearListener(const TCHAR* Key, const TCHAR* Text);
	void CancelVolleyOrders(FSquad& Sq, const TCHAR* Reason);
	/** The one volley target for a shooter: the squad contact, never a per-man nearest enemy. */
	ARFCharacter* ResolveVolleyTarget(const FSquad& Sq, const ARFCharacter* Shooter, float EngageMaxCm) const;
	void ScheduleVolley(FSquad& Sq, const TArray<ARFCharacter*>& EligibleShooters);
	void TickPendingVolley(FSquad& Sq);
	void DischargeOne(ARFCharacter* Shooter, ARFCharacter* Enemy, const FVector& AimAt);
	TArray<FSquad> Squads;
	float SquadRebuildTimer = 0.f;
	int32 NextSquadId = 0;
	TMap<FString, int32> MoraleTransitions;
	TArray<FVector> CoverAnchors;          // tagged wall segments (PA250Cover) in the level
	bool bCoverAnchorsBuilt = false;
	TArray<int32> EngagementHistogram = {0, 0, 0, 0, 0};

	void RebuildSquads(int32 MinSize, int32 MaxSize);
	bool HasLineOfSight(const ARFCharacter* From, const ARFCharacter* To) const;
};
