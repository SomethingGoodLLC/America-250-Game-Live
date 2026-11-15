// PA250VolleyCoordinator.h — squad volleys, callouts, suppression.
#pragma once

#include "CoreMinimal.h"
#include "PA250Cover.h"
#include "FormationQuery.h"
#include "PA250CombatProfiles.h"
#include "PA250Morale.h"
#include "UObject/Object.h"
#include "PA250VolleyCoordinator.generated.h"

class ARFCharacter;
class APA250BattleGameMode;
class APA250ControlPoint;
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

/** What a leader (the player; an officer for every squad in his aura) has told a squad to do. */
UENUM()
enum class EPA250SquadOrder : uint8
{
	None,       // the squad's own judgement (bots without a leader)
	FollowMe,   // form on the leader and go where he goes
	FormLine,   // form line at the point given, facing as he faced
	Hold,       // keep this ground
	TakeCover,  // each man to the nearest covered firing place by the point given
	Charge,     // bayonets, at the enemy he aims at
	FallBack    // to the nearest post the side holds, to fill the cartridge boxes
};

/** Pure helpers for a led squad (checked without a world). */
namespace PA250Squad
{
/** Words for the HUD and the notice. */
PLAYAMERICA250_API const TCHAR* OrderWords(EPA250SquadOrder Order);
/** The shouted line for an order ("follow_me", "charge_bayonets", ...), or null when it has none. */
PLAYAMERICA250_API const TCHAR* OrderVoiceKey(EPA250SquadOrder Order);
/** Places for a file that follows its leader: one rank at his shoulders, half the men to each side
 *  of him (none where he stands), SpacingCm apart, left to right. X is to his right; Y (behind) is 0. */
inline void AbreastOffsets(int32 N, float SpacingCm, TArray<FVector2D>& Out)
{
	Out.Reset();
	const int32 Left = N / 2;
	for (int32 i = Left; i >= 1; --i) Out.Add(FVector2D(-i * SpacingCm, 0.f));
	for (int32 i = 1; i <= N - Left; ++i) Out.Add(FVector2D(i * SpacingCm, 0.f));
}
/** Whether a following line turns to its leader's new facing: always while he moves; standing still,
 *  only once he has turned more than TurnDegrees from the line (so looking about does not wheel it). */
inline bool FollowTurns(const FVector& LineFacing, const FVector& LeaderFacing, bool bLeaderMoving, float TurnDegrees)
{
	return bLeaderMoving || FVector::DotProduct(LineFacing.GetSafeNormal2D(), LeaderFacing.GetSafeNormal2D()) < FMath::Cos(FMath::DegreesToRadians(TurnDegrees));
}
/** The drummer's beating for an order (SFX bank "drum_call_<name>"), told apart by ear. */
PLAYAMERICA250_API const TCHAR* OrderDrumCall(EPA250SquadOrder Order);
/** Where a led squad's line stands: behind the leader (FollowMe), on the point given (FormLine, Hold,
 *  TakeCover), else where it stands now. */
PLAYAMERICA250_API FVector LedAnchor(EPA250SquadOrder Order, const FVector& LeaderLocation, const FVector& LeaderFacing,
	const FVector& OrderPoint, const FVector& CurrentAnchor, float FollowBehindCm);
/** The volley of a led line. While the leader holds his aim the line makes ready, then presents and
 *  waits there; his shot (bLeaderFired, only once the line is presented) fires it; lowering his aim
 *  recovers arms. StageTimer counts the make-ready down. Returns the next stage; bOutFire is true on
 *  the one call in which the line should fire. */
PLAYAMERICA250_API EVolleyCall LedVolleyNext(EVolleyCall Stage, bool bLeaderAiming, bool bLeaderFired, bool bShotsPending,
	bool bAnyLoaded, float StageTimer, bool& bOutFire);
/** One candidate for the leader's file. */
struct FCandidate
{
	int32 Index = 0;
	bool bSameClass = false;
	float DistCm = 0.f;
};
/** The file: the nearest men of the leader's class, up to MaxMen; if that leaves it under MinMen it
 *  is made up to MinMen with the nearest other men. Returns candidate indices in the order chosen. */
PLAYAMERICA250_API TArray<int32> PickFile(const TArray<FCandidate>& Candidates, int32 MinMen, int32 MaxMen);
}

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
	/** Works were raised or battered down: read the level's cover again at the next assignment. */
	void RefreshCoverAnchors() { bCoverAnchorsBuilt = false; }
	/** Pure: the cover line for a squad at PostLoc among Anchors (toward the enemy preferred, at least
	 *  MinApartCm from Taken); Out is the friendly side of it. */
	static bool PickLine(const TArray<FVector>& Anchors, const FVector& PostLoc, const FVector& EnemyRear, const FVector& OwnRear,
		float SearchCm, float MinApartCm, const TArray<FVector>& Taken, FVector& Out);
	/** Extra AI reload stress from the squad's morale state (0 when unknown / steady). */
	float GetSquadMoraleStress(int32 SquadId) const;
	EPA250Morale GetSquadMorale(int32 SquadId) const;
	/** {"british":{"Shaken":n,...},...} morale transitions so far, for battle_result.json. */
	FString MoraleTraceJson() const;
	// ---- Class verbs (PA250UnitClasses; values in unit_classes.json) ----
	/** Rifleman: an officer killed leaves the squad he stood with shaken. True when a squad was shaken. */
	bool ShakeSquadOf(const ARFCharacter* Officer);
	/** Officer: the nearest routing squad of the side within RadiusCm of Where starts to rally. */
	bool RallyNearestRouting(FName Faction, const FVector& Where, float RadiusCm);
	/** Officer / regular / grenadier: the nearest line squad of the side within RadiusCm of Where
	 *  volleys now (it has a contact beyond charge range and loaded muskets) or charges (contact in
	 *  charge range). Returns "volley", "charge" or an empty string when no squad could obey. */
	FString OrderNearestLine(FName Faction, const FVector& Where, float RadiusCm, bool bAllowVolley, bool bAllowCharge);
	// ---- The played general's verbs (M5; every value is the persons data's hero block) ----
	/** Every routing squad of the side within RadiusCm of Where starts to rally. Returns how many. */
	int32 RallyAllRouting(FName Faction, const FVector& Where, float RadiusCm);
	/** hold_fast: for Seconds, fire and losses weigh Scale times as much on the side's squads within
	 *  RadiusCm of Where. Returns the squads it reached (the same for the three below). */
	int32 HeroHoldFast(FName Faction, const FVector& Where, float RadiusCm, float Seconds, float Scale);
	/** charge: every line within RadiusCm that can obey (steady or shaken, an enemy in front, inside
	 *  charge range) goes in with the bayonet; for Seconds the blows of every squad within RadiusCm
	 *  are Scale times as heavy. OutCharging: how many went in. */
	int32 HeroCharge(FName Faction, const FVector& Where, float RadiusCm, float Seconds, float Scale, int32& OutCharging);
	/** steady: the squads within RadiusCm shake off Share of what they have suffered; a shaken one stands steady again. */
	int32 HeroSteady(FName Faction, const FVector& Where, float RadiusCm, float Share);
	/** quick_step: for Seconds the men of the squads within RadiusCm move Scale times as fast. */
	int32 HeroQuickStep(FName Faction, const FVector& Where, float RadiusCm, float Seconds, float Scale);
	/** A man's pace under quick_step (1 when his squad has none). */
	float SpeedScaleFor(const ARFCharacter* Soldier) const;
	/** An event's shout (battle_feel.json intensity.shouts: charge, rout, officer_down, colours_taken),
	 *  unless the same event was shouted for that side inside its cooldown. True when it was given. */
	bool ShoutEvent(FName Event, FName Faction, const FVector& Where);
	int32 ShoutEventCount(FName Event) const { return ShoutEventCounts.FindRef(Event); }
	/** The share of a side's squads (with a man standing) that are steady: 1 when it has none. */
	float SteadyShare(FName Faction) const;
	/** A side's squads that are routing now. */
	int32 CountRouting(FName Faction) const;
	/** Is this man's squad routing (a dragoon may take him)? */
	bool IsRouting(const ARFCharacter* Soldier) const;
	/** How often a verb or morale change has happened ("british.VolleyShock", "continental.Rallied"...). */
	int32 CountOf(const FString& Key) const { return MoraleTransitions.FindRef(Key); }
	/** Debug matchups: squads keep the ground they stand on (no march, no cover lines). */
	bool bHoldGround = false;

	// ---- A led file (the player's; values in unit_classes.json verbs.squad) ----
	/** Give Leader a file of his own: the nearest men of his class on the field (made up with others
	 *  if they are too few), taken out of the squads they stood in. Idempotent: a leader who already
	 *  has a file of his class with a man standing keeps it. Returns the men in the file. */
	int32 EnsureLeaderFile(ARFCharacter* Leader);
	/** The leader's order to his file; an officer's order also reaches every friendly squad whose line
	 *  is within his order radius (they obey for the order seconds, then judge for themselves again).
	 *  AimPoint is where he is looking. Returns the squads that took the order. */
	int32 GiveOrder(ARFCharacter* Leader, EPA250SquadOrder Order, const FVector& AimPoint);
	/** Every frame: is the leader holding his aim, and on what point. */
	void UpdateLeader(ARFCharacter* Leader, bool bAiming, const FVector& AimPoint);
	/** The leader's own shot. A presented line fires with him. True when a volley was fired. */
	bool LeaderFired(ARFCharacter* Leader);
	/** The file's state for the HUD. False when Leader has no file. */
	bool GetFileStatus(const ARFCharacter* Leader, int32& OutMen, int32& OutLoaded, int32& OutPresented, EPA250SquadOrder& OutOrder, int32* OutWithLeader = nullptr) const;
	/** The living men of the leader's file (for probes and tests). */
	TArray<ARFCharacter*> GetFileMembers(const ARFCharacter* Leader) const;
	/** The man of his file nearest the fallen leader (the player steps into him), or null. */
	ARFCharacter* PickSuccessor(const ARFCharacter* FallenLeader) const;
	/** Successor (a man of the file) becomes its leader. */
	void TransferLeadership(const ARFCharacter* OldLeader, ARFCharacter* Successor);
	bool IsLeader(const ARFCharacter* C) const;
	// ---- Support roles (values in unit_classes.json verbs.ensign / musician) ----
	/** Where a side's colours fly (carried, not fallen). Squads near them rally sooner. */
	void SetColours(FName Faction, bool bFlying, const FVector& Where);
	/** Battlefield callout + shouted VO (general falls, etc.). */
	void CallOut(FName VoiceFaction, const TCHAR* Key, const TCHAR* Text, const FVector& Where, float Seconds = 2.f)
	{
		Shout(VoiceFaction, Key, Text, Where, Seconds);
	}
	void CallOutNearListener(const TCHAR* Key, const TCHAR* Text) { ShoutNearListener(Key, Text); }
	/** A morale check on a whole side: its colours fell or were taken, or it is under cannon fire. */
	void PressSide(FName Faction, float Pressure, float Seconds, const TCHAR* CountAs);
	/** The same on one squad (round shot ploughing through it). */
	void PressSquad(int32 SquadId, float Pressure, float Seconds, FName ByFaction, const TCHAR* CountAs);

	/** A side's squads with a man still standing, and how many of them are withdrawing or routing
	 *  (the army_broke ending, see PA250EndRules::ArmyIsBreaking). */
	void CountSquads(FName Faction, int32& OutSquads, int32& OutBroken) const;

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
		/** A led volley at a point with no man on it: fires at AimPoint without a target. */
		bool bAtPoint = false;
	};

	struct FSquad
	{
		int32 Id = -1;
		int32 Peak = 0;
		FName Faction;
		/** One kind of soldier a squad (officers attach to any): its class decides band and verb. */
		FName UnitClass;
		float FireMaxCm = 8000.f;   // opens fire inside this
		float PreferMinCm = 4000.f; // closes to this
		float ShockUntil = -1000.f; // a volley's shock on this squad, fading out
		float ShockPressure = 0.f;
		float ShockSpan = 6.f;      // the seconds that shock fades over
		bool bRoutCheckDone = false; // this charge has already forced its check
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
		/** The current withdrawal is the role's drill (exposed in the open, or evading a charge),
		 *  not one forced by numbers: it does not count toward army_broke (PA250SquadIsBroken). */
		bool bDrillWithdrawal = false;
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
		/** This squad defends a post from behind its field works: it keeps to them (no advance, no
		 *  pursuit) and every man looks for a place behind a bank, whatever his drill. */
		bool bAtWorks = false;
		/** The post it is ordered to is its own side's: it is holding ground, not going to take it. */
		bool bHoldsPost = false;
		/** Following its leader at his shoulders (one rank, half to each side of him). */
		bool bAbreast = false;
		/** The post whose works it mans. While that post is its side's and a work still stands by it,
		 *  the squad stays there instead of going out after a post the enemy has taken (a garrison
		 *  that sallied from its works was shot down in the open: works matchup, 2026-11-14). */
		TWeakObjectPtr<APA250ControlPoint> WorksPost;
		// The played general's verbs (M5), each until its time:
		float HeroPressureScale = 1.f;   // hold_fast: fire and losses weigh this much
		float HeroPressureUntil = -1000.f;
		float HeroBlowScale = 1.f;       // charge: the men's blows are this much heavier
		float HeroBlowUntil = -1000.f;
		float HeroSpeedScale = 1.f;      // quick_step: the men's pace
		float HeroSpeedUntil = -1000.f;
		// A led squad: who leads it, what he ordered and where, and where he is aiming.
		TWeakObjectPtr<ARFCharacter> Leader;
		EPA250SquadOrder Order = EPA250SquadOrder::None;
		/** An officer's order to a squad that is not his own file lapses at this time (< 0: no limit). */
		float OrderUntil = -1.f;
		/** The officer whose order this squad is obeying (not its own leader). */
		TWeakObjectPtr<ARFCharacter> OrderedBy;
		FVector OrderPoint = FVector::ZeroVector;
		FVector OrderFacing = FVector::ForwardVector;
		bool bLeaderAiming = false;
		bool bLeaderFired = false;
		FVector LeaderAimPoint = FVector::ZeroVector;
		int32 DebugPresented = 0;
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
	FSquad* FindSquad(int32 SquadId);
	/** Where a squad's living men are (their mean place). False when none stands. */
	static bool SquadCentre(const FSquad& Sq, FVector& OutCentre);
	/** Regular's verb: a volley of enough muskets shakes the squad it was fired on. */
	void ApplyVolleyShock(int32 TargetSquadId, FName ByFaction);
	/** Grenadier's verb: a charge that reaches a squad forces its rout check. */
	void ForceChargeRout(int32 TargetSquadId, FName ByFaction);
	void StartCharge(FSquad& Sq, float Now);
	/** The one volley target for a shooter: the squad contact, never a per-man nearest enemy. */
	ARFCharacter* ResolveVolleyTarget(const FSquad& Sq, const ARFCharacter* Shooter, float EngageMaxCm) const;
	void ScheduleVolley(FSquad& Sq, const TArray<ARFCharacter*>& EligibleShooters);
	/** The leader's volley: every presented, loaded man fires at the enemy by the aim point, or at
	 *  the point itself when nobody stands there. */
	void ScheduleLedVolley(FSquad& Sq, const TArray<ARFCharacter*>& Presented);
	FSquad* FindLedSquad(const ARFCharacter* Leader);
	const FSquad* FindLedSquad(const ARFCharacter* Leader) const;
	/** The living enemy nearest a point (within RadiusCm), for the leader's aim. */
	ARFCharacter* EnemyNear(FName Faction, const FVector& Point, float RadiusCm) const;
	void SetOrder(FSquad& Sq, EPA250SquadOrder Order, const ARFCharacter* Giver, const FVector& AimPoint, float Until);
	void TickPendingVolley(FSquad& Sq);
	void DischargeOne(ARFCharacter* Shooter, ARFCharacter* Enemy, const FVector& AimAt);
	TArray<FSquad> Squads;
	float SquadRebuildTimer = 0.f;
	int32 NextSquadId = 0;
	TMap<FString, int32> MoraleTransitions;
	TMap<FString, float> LastShoutEventAt;   // "<event>.<faction>"
	TMap<FName, int32> ShoutEventCounts;
	TArray<FVector> CoverAnchors;          // tagged wall segments (PA250Cover) in the level
	TArray<FVector> WorkAnchors;           // standing field works (PA250FieldWork that give cover)
	TMap<FName, FVector> ColoursFlying;    // a side's colours while they are carried
	bool bCoverAnchorsBuilt = false;
	/** Places along every tagged wall and standing work (battle_rules.json cover), and who has each. */
	TArray<PA250Cover::FWallSlot> CoverSlots;
	TMap<int32, TWeakObjectPtr<ARFCharacter>> CoverSlotOwner;
public:
	/** Trace only: what became of each man's search for a covered place. */
	struct FCoverSearch
	{
		int32 Men = 0;            // men whose squad sought cover at a query
		int32 Skipped = 0;        // not searched (no contact yet and before_contact off, or formation not kept)
		int32 NoSlotNear = 0;     // no free wall place within search_cm on the side away from the threat
		int32 SlotOffNav = 0;     // a place was near but not on walkable ground
		int32 SlotNotCover = 0;   // a place was near but the trace said it gives no cover with a clear shot
		int32 WallPlaces = 0;     // given a place along a wall
		int32 RingPlaces = 0;     // given a place by the old ring search
		int32 SquadQueries = 0;   // squad queries that sought cover
		int32 SquadQueriesNot = 0; // squad queries of squads that do not seek cover
	};
	const FCoverSearch& GetCoverSearch() const { return CoverSearch; }
	int32 NumCoverSlots() const { return CoverSlots.Num(); }
private:
	FCoverSearch CoverSearch;
	/** Do this squad's men use the walls: skirmishers always; volley squads by cover.line_troops. */
	bool SquadUsesWalls(const FSquad& Sq) const;
	TArray<int32> EngagementHistogram = {0, 0, 0, 0, 0};

	void RebuildSquads(int32 MinSize, int32 MaxSize);
	bool HasLineOfSight(const ARFCharacter* From, const ARFCharacter* To) const;
};
