// PA250FieldGun.h — a field gun worked by a crew through a timed drill, and its shot.
// The drill is the musket's active reload (FActiveReloadSequencer) with the gun's own times; the
// crew's numbers and training set how fast it runs. Round shot knocks men down and batters field
// works at range; grape is a short-range blast of musket balls; shell is a timed fuse that bursts
// at the mark. A gun whose crew is gone and which the enemy stands over changes hands.
// GAMEPLAY SETTINGS: battle_rules.json "cannon".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ActiveReloadSequencer.h"
#include "pa250/types.hpp"
#include "PA250FieldGun.generated.h"

class ARFCharacter;
class UStaticMeshComponent;

UENUM()
enum class EPA250Shot : uint8
{
	RoundShot,
	Grape,
	Shell
};

/** Pure gunnery rules (checked without a world). */
namespace PA250Gunnery
{
/** Crew auto-pick: grape inside its range, else round shot out to its range. Shell only in the band
 *  past the round shot, and only when the shell outranges it (a howitzer's reach). The shipped
 *  numbers keep shell inside the round, so the crew never loads shell. */
inline bool ChooseShot(float RangeCm, const pa250::BattleRules& R, EPA250Shot& OutShot)
{
	if (RangeCm <= R.cannon_grape_range_m * 100.f)
	{
		OutShot = EPA250Shot::Grape;
		return true;
	}
	if (RangeCm <= R.cannon_round_range_m * 100.f)
	{
		OutShot = EPA250Shot::RoundShot;
		return true;
	}
	if (R.cannon_shell_range_m > R.cannon_round_range_m + 0.5f && RangeCm <= R.cannon_shell_range_m * 100.f)
	{
		OutShot = EPA250Shot::Shell;
		return true;
	}
	return false;
}
/** The data's word for a load (battle_rules.json cannon.gun_types). Grape is the canister load. */
inline const char* ShotKey(EPA250Shot Shot)
{
	return Shot == EPA250Shot::Shell ? "shell" : Shot == EPA250Shot::Grape ? "canister" : "round_shot";
}
/** The kind of piece a gun is when nothing says otherwise (cannon.default_gun_type). */
inline FString DefaultGunType(const pa250::BattleRules& R)
{
	return R.cannon_default_gun_type.empty() ? FString(TEXT("six_pounder")) : FString(UTF8_TO_TCHAR(R.cannon_default_gun_type.c_str()));
}
/** Whether this kind of piece fires the load (cannon.gun_types). A kind the data does not list
 *  fires round shot and canister only, so no unlisted piece ever throws shell. */
inline bool ShotAllowedForGun(const FString& GunType, EPA250Shot Shot, const pa250::BattleRules& R)
{
	const FString Type = GunType.IsEmpty() ? DefaultGunType(R) : GunType;
	const auto It = R.cannon_gun_types.find(std::string(TCHAR_TO_UTF8(*Type.ToLower())));
	if (It == R.cannon_gun_types.end()) return Shot != EPA250Shot::Shell;
	for (const std::string& K : It->second)
	{
		if (K == ShotKey(Shot)) return true;
	}
	return false;
}
/** Why a chosen load will not fire at this range, or null when it will. Grape keeps a little
 *  grace past its range (1.25) so a mark just beyond the ring still goes. */
inline const TCHAR* RefuseShot(EPA250Shot Shot, float RangeCm, const pa250::BattleRules& R, const FString& GunType = FString())
{
	// Range first (the existing messages), then what this kind of piece fires at all.
	if (Shot == EPA250Shot::Grape && RangeCm > R.cannon_grape_range_m * 100.f * 1.25f)
	{
		return TEXT("canister too far — switch load");
	}
	if (Shot == EPA250Shot::Shell && RangeCm > R.cannon_shell_range_m * 100.f)
	{
		return TEXT("shell too far — switch load");
	}
	if (Shot == EPA250Shot::RoundShot && RangeCm > R.cannon_round_range_m * 100.f)
	{
		return TEXT("round shot too far");
	}
	if (!ShotAllowedForGun(GunType, Shot, R))
	{
		return Shot == EPA250Shot::Shell ? TEXT("this piece fires no shell") : TEXT("this piece fires no such load");
	}
	return nullptr;
}
/** Elevation in degrees that puts a shot onto a mark: DropCm is the fall over HorizCm, HeightCm is
 *  the mark above the muzzle. The one-piece mesh stays level; this angle is aim compensation. */
inline float BallisticElevationDeg(float HorizCm, float DropCm, float HeightCm)
{
	if (HorizCm < 1.f) return 0.f;
	return FMath::RadiansToDegrees(FMath::Atan2(HeightCm + DropCm, HorizCm));
}
/** Words for the load on the HUD. */
inline const TCHAR* ShotWords(EPA250Shot Shot)
{
	switch (Shot)
	{
	case EPA250Shot::Grape: return TEXT("canister"); // enum key stays grape; player-facing label
	case EPA250Shot::Shell: return TEXT("shell");
	default: return TEXT("round shot");
	}
}
/** How fast the drill runs (1 = a full trained crew). Too few men cannot work the gun at all; a
 *  short crew is slower in proportion; men who are not gunners are slower by the untrained scale. */
inline float DrillSpeed(int32 Crew, int32 Trained, const pa250::BattleRules& R)
{
	if (Crew < R.cannon_min_crew) return 0.f;
	const int32 Men = FMath::Min(Crew, R.cannon_crew_men);
	const float Manning = float(Men) / float(FMath::Max(1, R.cannon_crew_men));
	const float TrainedShare = FMath::Clamp(float(FMath::Min(Trained, Men)) / float(Men), 0.f, 1.f);
	const float Skill = 1.f / FMath::Lerp(FMath::Max(1.f, R.cannon_untrained_scale), 1.f, TrainedShare);
	return Manning * Skill;
}
/** The drill's pace at the gun: DrillSpeed, and a crew short of full (at least min_crew) is slower
 *  again by cannon.missing_roles_scale (its missing roles are covered by men doing two jobs). */
inline float CrewDrillSpeed(int32 Crew, int32 Trained, const pa250::BattleRules& R)
{
	float Speed = DrillSpeed(Crew, Trained, R);
	if (Crew > 0 && Crew < R.cannon_crew_men) Speed /= FMath::Max(1.f, R.cannon_missing_roles_scale);
	return Speed;
}
/** The six drill steps, in the order the drill runs them. The HUD's labels (DrillPreset), the crew's
 *  clips (APA250FieldGun::CrewStepClip), the chest (Load) and the premature discharge (Swab) all key
 *  on these, never on bare numbers, so a reorder cannot leave one of them behind. */
enum class EPA250GunStep : uint8 { Swab = 0, Load = 1, Ram = 2, RunOut = 3, Prime = 4, Fire = 5, Count = 6 };
inline const TCHAR* GunStepLabel(EPA250GunStep Step)
{
	switch (Step)
	{
	case EPA250GunStep::Swab: return TEXT("Swab");
	case EPA250GunStep::Load: return TEXT("Load");
	case EPA250GunStep::Ram: return TEXT("Ram");
	case EPA250GunStep::RunOut: return TEXT("Run up");
	case EPA250GunStep::Prime: return TEXT("Prime");
	case EPA250GunStep::Fire: return TEXT("Fire");
	default: return TEXT("");
	}
}
inline bool IsGunStep(int32 StepIndex, EPA250GunStep Step) { return StepIndex == static_cast<int32>(Step); }
/** Six-step gun drill: Swab, Load, Ram, Run out ("Run up"), Prime, Fire. */
inline FActiveReloadTuningPlain DrillPreset(const pa250::BattleRules& R)
{
	FActiveReloadTuningPlain T;
	// Legacy three-phase fields kept as fallbacks; CustomStepSeconds drives the gun.
	T.PhasePrimePan = R.cannon_sponge_load_seconds;
	T.PhaseRamCharge = R.cannon_ram_seconds;
	T.PhaseShoulderAim = R.cannon_prime_lay_seconds;
	const float RunOut = R.cannon_run_out_seconds > 0.f ? R.cannon_run_out_seconds : 3.f;
	// Neutral target 22 s: scale the catalog times and add RunOut + Fire.
	const float Catalog = FMath::Max(0.1f,
		R.cannon_sponge_load_seconds + R.cannon_ram_seconds + R.cannon_prime_lay_seconds + RunOut);
	constexpr float NeutralTarget = 22.f;
	constexpr float FireStep = 2.0f;
	const float Scale = (NeutralTarget - FireStep) / Catalog;
	T.CustomStepSeconds = {
		R.cannon_sponge_load_seconds * 0.45f * Scale, // Swab (sponge)
		R.cannon_sponge_load_seconds * 0.55f * Scale, // Load
		R.cannon_ram_seconds * Scale,                 // Ram
		RunOut * Scale,                               // RunOut / run up
		R.cannon_prime_lay_seconds * Scale,           // Prime
		FireStep                                      // Fire
	};
	T.CustomStepLabels.Reset();
	for (int32 i = 0; i < static_cast<int32>(EPA250GunStep::Count); ++i) T.CustomStepLabels.Add(GunStepLabel(static_cast<EPA250GunStep>(i)));
	T.bTimeoutCompletesPhase = true;
	T.bMissResumesProgress = true;
	T.bIgnoreRepeatMiss = true;
	T.JamChanceOnMiss = 0.f;
	// A commit finishes its step immediately. The window itself saves time; applying
	// 15/22 again to later phases made a perfect drill take about nine seconds.
	T.WindowCenter = 15.f / 22.f;
	T.PerfectReloadScale = 1.f;
	return T;
}
/** The words for the drill's steps (3-phase fallback labels). */
inline const TCHAR* DrillWords(EActiveReloadPhase Phase)
{
	switch (Phase)
	{
	case EActiveReloadPhase::PrimePan: return TEXT("Sponge");
	case EActiveReloadPhase::RamCharge: return TEXT("Ram");
	case EActiveReloadPhase::ShoulderAim: return TEXT("Prime and lay");
	case EActiveReloadPhase::MissDelay: return TEXT("Fumbled");
	default: return TEXT("Loaded");
	}
}
/** World gravity for round shot and shell (cm/s^2). Lay and flight both use this.
 *  980 is the engine's gravity. The old flight used 490 ("drops a little"); that
 *  half-gravity stayed in the lay formula after flight moved to 980, so the piece
 *  was laid for half the drop the ball actually made. */
inline constexpr float ShotGravityCm = 980.f;
/** Quadratic drag: acceleration in m/s^2 is ShotDragK * v(m/s)^2. About an iron 6 lb ball.
 *  Applied with speed in m/s. A coefficient written against cm/s and then multiplied
 *  by 100 stops the shot in one tick. */
inline constexpr float ShotDragK = 0.00074f;

/** Drag then gravity. GravityCm and DragK come from battle_rules (the defaults match the data). */
inline void ApplyShotAccel(FVector& Vel, float Dt, float GravityCm = ShotGravityCm, float DragK = ShotDragK)
{
	const float Speed = Vel.Size();
	if (Speed > 1.f && DragK > 0.f)
	{
		const float Mps = Speed / 100.f;
		const float DragCm = DragK * Mps * Mps * 100.f;
		Vel -= Vel.GetSafeNormal() * FMath::Min(Speed, DragCm * Dt);
	}
	Vel.Z -= GravityCm * Dt;
}

/** One flight step: the same accel the actors use, then move. */
inline void IntegrateShot(FVector& Pos, FVector& Vel, float Dt, float GravityCm = ShotGravityCm, float DragK = ShotDragK)
{
	ApplyShotAccel(Vel, Dt, GravityCm, DragK);
	Pos += Vel * Dt;
}

/** Vacuum drop over a level range (cm): ½ g t² with t = range/speed. The piece is laid
 *  by SolveElevationDeg, which integrates drag as well as this gravity. */
inline float RoundShotDrop(float RangeCm, float SpeedCm, float GravityCm = ShotGravityCm)
{
	const float T = SpeedCm > 1.f ? RangeCm / SpeedCm : 0.f;
	return 0.5f * GravityCm * T * T;
}

/** Height (cm) of a shot fired at PitchDeg once it has covered HorizCm. */
inline float ShotArrivalZ(float PitchDeg, float HorizCm, float SpeedCm, float GravityCm = ShotGravityCm, float DragK = ShotDragK)
{
	if (HorizCm < 1.f || SpeedCm < 1.f) return 0.f;
	const float Rad = FMath::DegreesToRadians(PitchDeg);
	FVector Pos = FVector::ZeroVector;
	FVector Vel(FMath::Cos(Rad) * SpeedCm, 0.f, FMath::Sin(Rad) * SpeedCm);
	float Guard = 0.f;
	while (Pos.X < HorizCm && Guard < 12.f && Vel.X > 50.f)
	{
		const FVector Before = Pos;
		IntegrateShot(Pos, Vel, 0.01f, GravityCm, DragK);
		if (Pos.X >= HorizCm)
			return FMath::Lerp(Before.Z, Pos.Z, (HorizCm - Before.X) / (Pos.X - Before.X));
		Guard += 0.01f;
	}
	return Pos.Z;
}

/** Pitch (degrees) so a shot fired at SpeedCm is at HeightCm when it has covered HorizCm.
 *  Binary search over the elevation band, using IntegrateShot (gravity + drag). */
inline float SolveElevationDeg(float HorizCm, float SpeedCm, float HeightCm, float MinDeg, float MaxDeg,
	float GravityCm = ShotGravityCm, float DragK = ShotDragK)
{
	float Lo = MinDeg;
	float Hi = MaxDeg;
	if (HorizCm < 1.f || SpeedCm < 1.f) return 0.f;
	for (int32 i = 0; i < 14; ++i)
	{
		const float Mid = 0.5f * (Lo + Hi);
		const float Z = ShotArrivalZ(Mid, HorizCm, SpeedCm, GravityCm, DragK);
		if (Z > HeightCm) Hi = Mid;
		else Lo = Mid;
	}
	return 0.5f * (Lo + Hi);
}

/** The quoin notch whose shot lands closest to HeightCm. Nearest-angle is not nearest-impact:
 *  six steps across this band miss a 300 m mark by several metres; the data uses a finer quoin. */
inline float BestQuoinElevationDeg(float HorizCm, float SpeedCm, float HeightCm, float MinDeg, float MaxDeg,
	int32 Steps, int32& OutNotch, float GravityCm = ShotGravityCm, float DragK = ShotDragK)
{
	Steps = FMath::Max(2, Steps);
	const float Span = FMath::Max(0.1f, MaxDeg - MinDeg);
	float BestErr = TNumericLimits<float>::Max();
	float BestPitch = MinDeg;
	OutNotch = 0;
	const float Solved = SolveElevationDeg(HorizCm, SpeedCm, HeightCm, MinDeg, MaxDeg, GravityCm, DragK);
	const int32 Lower = FMath::Clamp(FMath::FloorToInt((Solved - MinDeg) / Span * (Steps - 1)), 0, Steps - 1);
	// The low trajectory is monotonic in this elevation band. Only the two notches
	// bracketing its solution can be nearest; avoid integrating every notch each frame.
	for (int32 i = Lower; i <= FMath::Min(Lower + 1, Steps - 1); ++i)
	{
		const float Pitch = MinDeg + Span * (float(i) / float(Steps - 1));
		const float Err = FMath::Abs(ShotArrivalZ(Pitch, HorizCm, SpeedCm, GravityCm, DragK) - HeightCm);
		if (Err < BestErr)
		{
			BestErr = Err;
			BestPitch = Pitch;
			OutNotch = i;
		}
	}
	return BestPitch;
}

/** Canister damage scale from how far the pellet has flown, not from the aim mark.
 *  Full until FallStartCm, then down to Floor at FallEndCm. */
inline float CanisterDamageScale(float DistCm, float FallStartCm, float FallEndCm, float Floor = 0.25f)
{
	if (FallEndCm <= FallStartCm || DistCm <= FallStartCm) return 1.f;
	return FMath::Lerp(1.f, FMath::Clamp(Floor, 0.f, 1.f),
		FMath::Clamp((DistCm - FallStartCm) / (FallEndCm - FallStartCm), 0.f, 1.f));
}
/** Grazing angle is measured from the surface, rather than its normal. */
inline float GrazeAngleDeg(const FVector& Velocity, const FVector& Normal)
{
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(
		FMath::Abs(FVector::DotProduct(Velocity.GetSafeNormal(), Normal.GetSafeNormal())), 0.f, 1.f)));
}
/** A gun is overrun when none of its own side stands by it and an enemy does. */
inline bool IsOverrun(int32 OwnMenNear, int32 EnemiesNear) { return OwnMenNear <= 0 && EnemiesNear > 0; }
/** A crew can push its gun when enough of them are at it and it is not being loaded. */
inline bool CanPush(int32 Crew, bool bLoading, const pa250::BattleRules& R) { return Crew >= R.cannon_min_crew && !bLoading; }
}

UCLASS()
class PLAYAMERICA250_API APA250FieldGun : public AActor
{
	GENERATED_BODY()

public:
	APA250FieldGun();
	virtual void Tick(float DeltaSeconds) override;

	/** Stand the gun on the field for a side, loaded with round shot. */
	void Place(FName InFaction);
	FName GetFaction() const { return Faction; }
	FName GetOriginalFaction() const { return OriginalFaction; }
	bool IsTaken() const { return Faction != OriginalFaction; }
	bool IsLoaded() const { return bLoaded; }
	/** Gunners at the piece (a claimed post, or an artilleryman inside the crew radius).
	 *  Line infantry standing nearby are not crew. Never more than the crew posts. */
	int32 GetCrew() const { return CrewNow; }
	int32 GetTrainedCrew() const { return TrainedNow; }
	/** One line for the HUD of a man serving it. The first line is the load chip. */
	FString StatusLine() const;
	/** "LOAD: CANISTER" (grape enum) and the like. */
	FString LoadChip() const;
	/** Drill sequencer for the bottom-center reload widget while the player serves. */
	const FActiveReloadSequencer& GetDrill() const { return Drill; }
	bool IsDrillActive() const { return !bLoaded && Drill.IsReloading(); }
	/** Quoin notch index (0 = lowest elevation). */
	int32 GetQuoinNotch() const { return QuoinNotch; }
	float GetBarrelHeat() const { return BarrelHeat; }
	FVector GetMuzzle() const;
	/** Where the crew stand: two by the wheels, two by the trail. */
	FVector GetCrewPost(int32 Index) const;
	/** A bot gunner asks for a place; INDEX_NONE when the posts are full. He keeps it while he lives. */
	int32 ClaimPost(ARFCharacter* Gunner);
	/** Take a post even if full (displaces a bot). Used when the player deploys as gunner. */
	int32 ForceClaimPost(ARFCharacter* Gunner);
	/** The post he already has at this gun, or INDEX_NONE. */
	int32 PostOf(const ARFCharacter* Gunner) const;
	bool HasFreePost() const;
	/** The first empty crew post, or INDEX_NONE. */
	int32 FreePostIndex() const;
	/** Count the men at the piece now (Fire and a deploy do not wait for the next tick). */
	void RecountCrew();
	/** Lay onto a ground mark. The actor stays level. The shot is the quoin notch nearest
	 *  the drag-aware ballistic pitch. */
	float LayTo(const FVector& AimAt);
	float GetLayPitch() const { return LayPitch; }
	/** Probe and checks: fire the chosen load at a point. Recounts the crew first. */
	bool DischargeAt(const FVector& AimPoint, ARFCharacter* Gunner);
	/** The player serves the gun: it lays where he looks and fires on his trigger; his reload key works the drill. */
	void SetPlayerGunner(ARFCharacter* Player);
	ARFCharacter* GetPlayerGunner() const { return PlayerGunner.Get(); }
	bool PlayerFire(const FVector& AimPoint);
	/** Cycle grape → round shot → shell → grape while the player serves. */
	void CycleShot(int32 Delta);
	EPA250Shot GetSelectedShot() const { return SelectedShot; }
	/** The kind of piece (a key of battle_rules.json cannon.gun_types); empty means default_gun_type. */
	FString GetGunType() const;
	/** Makes this piece another kind (tests, a battle with a howitzer). A load it no longer fires is dropped. */
	void SetGunType(const FString& Type);
	/** Where the piece is laid (ground aim mark), or ZeroVector when not serving. */
	FVector GetAimMark() const { return AimMarkWorld; }
	/** The player has the crew run the gun up after him (he walks ahead of it), or stand it again. */
	void SetFollowPlayer(bool bFollow) { bFollowPlayer = bFollow; }
	bool IsFollowingPlayer() const { return bFollowPlayer && PlayerGunner.IsValid(); }
	EActiveReloadHit PlayerCommit();
	int32 GetShotsFired() const { return ShotsFired; }
	/** Debug matchups: the gun fires at once whatever its drill says. */
	void ForceLoaded() { bLoaded = true; }
	APA250AmmoChest* GetChest() const { return Chest.Get(); }
	void SetChest(APA250AmmoChest* InChest) { Chest = InChest; }
	/** True when the Load step cannot run: the chest holds no round, or it has blown up. */
	bool IsChestEmpty() const;
	bool IsChestBlown() const;
	/** The loader has taken this drill's round from the chest (it is not yet rammed home). */
	bool HasRoundInHand() const { return bRoundInHand; }
	/** Rounds this piece holds that the magazine has not been charged for yet: those in its chest
	 *  and the one in the loader's hands or in the barrel. A round is charged when it is fired. */
	int32 RoundsHeld() const;
	/** Pure: the registry clip a post's man shows at a drill step (an EPA250GunStep index; posts are
	 *  0 sponger, 1 loader, 2 ventsman, 3 captain). */
	static FName CrewStepClip(int32 Post, int32 Step, bool bDrilling);
	static FName CrewIdleClip(int32 Post);
	/** Premature discharge (M6): the sponger first (55% WIA / 45% KIA), then the muzzle blast for the rest. */
	void DischargePremature(FRandomStream& Hot);
	/** Tests: an empty barrel and a drill that has not begun. */
	void ForceEmptyForTest() { bLoaded = false; Drill.Reset(); DrillStepHeard = -1; bRoundInHand = false; }

private:
	/** Eases each bot of the crew onto his drill station and shows the step's clip on him. */
	void PoseCrew(float DeltaSeconds);
	void FitMesh();
	void CountMen(int32& OutOwn, int32& OutTrained, int32& OutEnemies) const;
	/** The crew's own judgement: the nearest enemy it can see, or the work he shelters behind. */
	bool PickTarget(FVector& OutPoint, AActor*& OutActor) const;
	bool Fire(const FVector& AimPoint, ARFCharacter* Gunner, bool bPlayerChose);
	void ChangeHands(FName NewFaction);
	void UpdateAimMark(const FVector& AimAt);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> AimMark;
	FVector MuzzleLocal = FVector(130.f, 0.f, 95.f);  // actor space; refitted from the mesh
	FName Faction;
	FName OriginalFaction;
	bool bLoaded = true;
	FActiveReloadSequencer Drill;
	TArray<TWeakObjectPtr<ARFCharacter>> Posts;
	TWeakObjectPtr<ARFCharacter> PlayerGunner;
	EPA250Shot SelectedShot = EPA250Shot::RoundShot;
	FString GunType; // empty: battle_rules cannon.default_gun_type
	bool bPlayerChoseShot = false;
	float LayPitch = 0.f;
	FVector AimMarkWorld = FVector::ZeroVector;
	int32 CrewNow = 0;
	int32 TrainedNow = 0;
	float OverrunSeconds = 0.f;
	float IdleSeconds = 0.f;
	float CountTimer = 0.f;
	int32 OwnNear = 0;
	int32 EnemiesNear = 0;
	int32 ShotsFired = 0;
	bool bLoggedWholePiece = false;
	bool bLoggedLay = false;
	bool bFollowPlayer = false;
	/** Artillery pass M6: quoin notches, barrel heat, recoil settle. */
	int32 QuoinNotch = 2;
	/** Last drill step that has already had its ramrod cue, or -1 before the drill starts. */
	int32 DrillStepHeard = -1;
	/** Seconds before a short crew is ordered off the piece again. */
	float CrewAbandonHold = 0.f;
	float BarrelHeat = 0.f;
	float RecoilLeft = 0.f;
	float SettleWobble = 0.f;
	FVector HolsterRestore = FVector::ZeroVector;
	TWeakObjectPtr<APA250AmmoChest> Chest;
	/** The Load step has its round (taken from the chest when the step began). */
	bool bRoundInHand = false;
	/** Crew clips this gun has shown (one log line each). */
	TSet<FName> CrewClipsLogged;
};

/** Ammunition chest beside a field gun: holds ready rounds. Load takes one; Fire spends through
 *  NotifyGunFired. Refills near a friendly command post. A hit may detonate (FPA250Blast Chest). */
UCLASS()
class PLAYAMERICA250_API APA250AmmoChest : public AActor
{
	GENERATED_BODY()

public:
	APA250AmmoChest();
	virtual void Tick(float DeltaSeconds) override;

	void AttachToGun(APA250FieldGun* Gun);
	APA250FieldGun* GetGun() const { return Gun.Get(); }
	int32 ReadyRounds() const { return Ready; }
	int32 Capacity() const { return Cap; }
	bool TakeOneForLoad();
	/** Seeded detonation roll after a round-shot or shell strike. Returns true if it blew.
	 *  ByFaction / By are the side and gunner whose shot struck it: they own the casualties. */
	bool TryDetonateFromHit(bool bShell, uint32 Seed, FName ByFaction = NAME_None, ARFCharacter* By = nullptr);
	/** How many strikes have reached the detonation roll. */
	int32 HitRolls() const { return Rolls; }
	int32 RoundsLostOnDetonation() const { return LostOnDetonation; }
	bool IsDetonated() const { return bDetonated; }

private:
	void Detonate(FName ByFaction, ARFCharacter* By);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Crate;
	TWeakObjectPtr<APA250FieldGun> Gun;
	int32 Ready = 0;
	int32 Cap = 6;
	float RefillAccum = 0.f;
	bool bDetonated = false;
	int32 LostOnDetonation = 0;
	int32 Rolls = 0;
};

/** A round shot in flight: goes through a few men, knocks down those beside its path, batters a
 *  field work it strikes, and shakes the squads it ploughs through. */
UCLASS()
class PLAYAMERICA250_API APA250CannonBall : public AActor
{
	GENERATED_BODY()

public:
	APA250CannonBall();
	virtual void Tick(float DeltaSeconds) override;
	void Launch(const FVector& InVelocity, FName InFaction, ARFCharacter* InGunner, const AActor* InGun);

private:
	void AdvanceFlight(float DeltaSeconds);
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ball;
	FVector Velocity = FVector::ZeroVector;
	FName Faction;
	TWeakObjectPtr<ARFCharacter> Gunner;
	TWeakObjectPtr<const AActor> Gun;
	TSet<TWeakObjectPtr<AActor>> Struck;      // men it has gone through
	TSet<TWeakObjectPtr<AActor>> KnockedDown; // men knocked off their feet beside its path
	TSet<int32> SquadsShaken;
	TSet<TWeakObjectPtr<AActor>> ChestsRolled; // one detonation roll per chest per shot
	int32 MenLeft = 0;
	float Travelled = 0.f;
};

/** An exploding shell: flies like round shot, bursts when travel reaches its fuse (the aim range).
 *  A dud (cannon.shell_dud_chance, rolled at the gun) flies the same way and lands without bursting. */
UCLASS()
class PLAYAMERICA250_API APA250CannonShell : public AActor
{
	GENERATED_BODY()

public:
	APA250CannonShell();
	virtual void Tick(float DeltaSeconds) override;
	void Launch(const FVector& InVelocity, float FuseCm, FName InFaction, ARFCharacter* InGunner, const AActor* InGun, bool bInDud = false);
	bool IsDud() const { return bDud; }
	/** Tests: advance the flight by hand in a world that has not begun play. */
	void AdvanceForTest(float DeltaSeconds) { AdvanceFlight(DeltaSeconds); }

private:
	void AdvanceFlight(float DeltaSeconds);
	void Burst(const FVector& At);
	/** A dud comes to earth: the thud, no blast, no shake. */
	void LandDud(const FVector& At);
	bool bDud = false;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shell;
	FVector Velocity = FVector::ZeroVector;
	FName Faction;
	TWeakObjectPtr<ARFCharacter> Gunner;
	TWeakObjectPtr<const AActor> Gun;
	float FuseCm = 5000.f;
	float Travelled = 0.f;
	TSet<TWeakObjectPtr<AActor>> ChestsRolled; // one detonation roll per chest per shot
};
