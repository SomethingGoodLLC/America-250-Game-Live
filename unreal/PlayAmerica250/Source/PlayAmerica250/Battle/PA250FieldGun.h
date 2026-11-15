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
/** Why a chosen load will not fire at this range, or null when it will. Grape keeps a little
 *  grace past its range (1.25) so a mark just beyond the ring still goes. */
inline const TCHAR* RefuseShot(EPA250Shot Shot, float RangeCm, const pa250::BattleRules& R)
{
	if (Shot == EPA250Shot::Grape && RangeCm > R.cannon_grape_range_m * 100.f * 1.25f)
	{
		return TEXT("grape too far — switch load");
	}
	if (Shot == EPA250Shot::Shell && RangeCm > R.cannon_shell_range_m * 100.f)
	{
		return TEXT("shell too far — switch load");
	}
	if (Shot == EPA250Shot::RoundShot && RangeCm > R.cannon_round_range_m * 100.f)
	{
		return TEXT("round shot too far");
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
	case EPA250Shot::Grape: return TEXT("grape");
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
/** The drill's times as an active-reload preset: a step left alone completes in its own time. */
inline FActiveReloadTuningPlain DrillPreset(const pa250::BattleRules& R)
{
	FActiveReloadTuningPlain T;
	T.PhasePrimePan = R.cannon_sponge_load_seconds;
	T.PhaseRamCharge = R.cannon_ram_seconds;
	T.PhaseShoulderAim = R.cannon_prime_lay_seconds;
	T.bTimeoutCompletesPhase = true;
	T.bMissResumesProgress = true;
	T.bIgnoreRepeatMiss = true;
	T.JamChanceOnMiss = 0.f;
	return T;
}
/** The words for the drill's steps. */
inline const TCHAR* DrillWords(EActiveReloadPhase Phase)
{
	switch (Phase)
	{
	case EActiveReloadPhase::PrimePan: return TEXT("Sponge and load");
	case EActiveReloadPhase::RamCharge: return TEXT("Ram home");
	case EActiveReloadPhase::ShoulderAim: return TEXT("Prime and lay");
	case EActiveReloadPhase::MissDelay: return TEXT("Fumbled");
	default: return TEXT("Loaded");
	}
}
/** How far the shot falls over a range (cm), so the piece is laid that much above its mark. */
inline float RoundShotDrop(float RangeCm, float SpeedCm, float GravityCm = 490.f)
{
	const float T = SpeedCm > 1.f ? RangeCm / SpeedCm : 0.f;
	return 0.5f * GravityCm * T * T;
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
	/** Own living men within the crew radius (never more than the crew posts), and how many of them are gunners. */
	int32 GetCrew() const { return CrewNow; }
	int32 GetTrainedCrew() const { return TrainedNow; }
	/** One line for the HUD of a man serving it. The first line is the load chip. */
	FString StatusLine() const;
	/** "LOAD: GRAPE" and the like. */
	FString LoadChip() const;
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
	/** Lay onto a ground mark. The actor stays level; the angle is ballistic compensation. */
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
	/** Where the piece is laid (ground aim mark), or ZeroVector when not serving. */
	FVector GetAimMark() const { return AimMarkWorld; }
	/** The player has the crew run the gun up after him (he walks ahead of it), or stand it again. */
	void SetFollowPlayer(bool bFollow) { bFollowPlayer = bFollow; }
	bool IsFollowingPlayer() const { return bFollowPlayer && PlayerGunner.IsValid(); }
	EActiveReloadHit PlayerCommit();
	int32 GetShotsFired() const { return ShotsFired; }
	/** Debug matchups: the gun fires at once whatever its drill says. */
	void ForceLoaded() { bLoaded = true; }

private:
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
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ball;
	FVector Velocity = FVector::ZeroVector;
	FName Faction;
	TWeakObjectPtr<ARFCharacter> Gunner;
	TWeakObjectPtr<const AActor> Gun;
	TSet<TWeakObjectPtr<AActor>> Struck;      // men it has gone through
	TSet<TWeakObjectPtr<AActor>> KnockedDown; // men knocked off their feet beside its path
	TSet<int32> SquadsShaken;
	int32 MenLeft = 0;
	float Travelled = 0.f;
};

/** An exploding shell: flies like round shot, bursts when travel reaches its fuse (the aim range). */
UCLASS()
class PLAYAMERICA250_API APA250CannonShell : public AActor
{
	GENERATED_BODY()

public:
	APA250CannonShell();
	virtual void Tick(float DeltaSeconds) override;
	void Launch(const FVector& InVelocity, float FuseCm, FName InFaction, ARFCharacter* InGunner, const AActor* InGun);

private:
	void Burst(const FVector& At);
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shell;
	FVector Velocity = FVector::ZeroVector;
	FName Faction;
	TWeakObjectPtr<ARFCharacter> Gunner;
	TWeakObjectPtr<const AActor> Gun;
	float FuseCm = 5000.f;
	float Travelled = 0.f;
};
