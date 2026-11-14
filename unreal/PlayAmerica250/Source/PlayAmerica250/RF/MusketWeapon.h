// MusketWeapon.h — single-shot muzzle-loader with active reload and spread.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ActiveReloadSequencer.h"
#include "MusketTuning.h"
#include "PA250WeaponTuning.h"
#include "PA250AIReload.h"
#include "MusketWeapon.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class USceneComponent;
class USoundBase;
class AMusketProjectile;
class UPA250WeaponTuning;

UENUM(BlueprintType)
enum class EMusketState : uint8
{
	Loaded,
	Reloading,
	Jammed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMusketStateChanged, EMusketState, NewState);

UCLASS()
class PLAYAMERICA250_API AMusketWeapon : public AActor
{
	GENERATED_BODY()

public:
	AMusketWeapon();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	bool Fire(FVector Direction = FVector::ZeroVector, bool bIsAiming = false, float OwnerSpeed = 0.f, float AccuracyScale = 1.f);

	/** Flash in the pan: the priming burns, the charge does not. No ball; the piece must be loaded again. */
	bool Misfire();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartReload();

	EActiveReloadHit CommitReload();

	void SetReloadHeld(bool bHeld) { bReloadHeld = bHeld; }

	/** Set by the owner every frame (the player at a run): the drill stands still, a press does nothing, and it
	 *  carries on from the same point when the pause lifts. */
	void SetReloadPaused(bool bPaused) { bReloadPaused = bPaused; }
	bool IsReloadPaused() const { return bReloadPaused && State != EMusketState::Loaded; }

	void TickBotReload(float Skill01, FRandomStream& Rng);

	/** AI only (bots call it every frame): presses reload when the training plan says so, through the
	 *  same TryCommit the player uses. The player's reload never goes through here. */
	EActiveReloadHit TickAIReload(const FPA250AIReloadParams& Params, const FPA250AIReloadConditions& Conditions, FRandomStream& Rng);

	/** Seconds the most recent reload took (fire -> loaded), and how many reloads finished. */
	float GetLastReloadSeconds() const { return LastReloadSeconds; }
	int32 GetReloadsCompleted() const { return ReloadsCompleted; }
	int32 GetLastReloadFumbles() const { return LastReloadFumbles; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool CanFire() const { return State == EMusketState::Loaded && !Sequencer.IsJammed(); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsJammed() const { return State == EMusketState::Jammed || Sequencer.IsJammed(); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const { return State == EMusketState::Reloading || Sequencer.IsReloading(); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EMusketState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetReloadProgress() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	FString GetReloadPhaseLabel() const { return Sequencer.PhaseLabel(); }

	float GetSweetStart() const { return Sequencer.GetSweetStart(); }
	float GetSweetEnd() const { return Sequencer.GetSweetEnd(); }
	float GetPerfectStart() const { return Sequencer.GetPerfectStart(); }
	float GetPerfectEnd() const { return Sequencer.GetPerfectEnd(); }

	const FActiveReloadStats& GetReloadStats() const { return Sequencer.GetStats(); }
	FActiveReloadSequencer& GetSequencer() { return Sequencer; }

	const FMusketTuningValues& GetTuning() const { return Tuning ? Tuning->Values : DefaultTuning; }
	float GetProjectileSpeed() const;
	/** World gravity x the projectile's gravity scale (negative, cm/s^2). */
	float GetProjectileGravityZ() const;
	/** Pure: low-arc launch direction from Muzzle that lands a ball of Speed on Target under GravityZ.
	 *  Returns false (OutDir = straight line) when the target is out of ballistic reach. */
	static bool SolveLaunchDirection(const FVector& Muzzle, const FVector& Target, float Speed, float GravityZ, FVector& OutDir);
	void ApplyWeaponTuning();
	void SetReloadPreset(EPA250ReloadPreset Preset);

	/** Pure: map an Arcade/Authentic preset onto the 3-phase sequencer so that
	 *  untouched = BaseSeconds, all phases committed at the window center = PerfectSeconds,
	 *  and an early miss on every phase = FailedSeconds. */
	static FActiveReloadTuningPlain PresetToSequencer(const FPA250ReloadPresetValues& Preset);

	UStaticMeshComponent* GetMusketMesh() const { return MusketMesh; }
	/** Skeletal Horizon musket that plays the pack's Firing/Reloading animations (null if the pack is missing). */
	USkeletalMeshComponent* GetAnimatedMesh() const { return AnimatedMesh; }
	UAnimSequence* GetFireAnim() const { return FireAnim; }
	UAnimSequence* GetReloadAnim() const { return ReloadAnim; }
	/** Visual only: aim the barrel along the owner's aim and pose the lock/ramrod. Called from Tick. */
	void UpdateVisuals(float DeltaSeconds);
	/** Visual only: publishes the left-hand-on-stock target; the body anim instance solves the arm. */
	void PoseSupportHand();
	/** Pure: 0..1 position in the pack's reload animation for a sequencer phase and its progress.
	 *  Returns a negative value for phases that hold the current pose (miss delay, jam). */
	static float ReloadAnimAlpha(EActiveReloadPhase Phase, float PhaseProgress);
	/** Barrel pitch limit (deg), up or down. Kept near the view limit so a high aim still raises the musket. */
	static constexpr float MaxBarrelPitch = 85.f;
	/** How far the barrel turns off the body's facing (deg) to follow the aim. Less to his right: the left
	 *  hand has to reach across the body to hold it there. */
	static constexpr float MaxCarryYawLeft = 60.f;
	static constexpr float MaxCarryYawRight = 25.f;
	USceneComponent* GetMuzzleLocation() const { return MuzzleLocation; }
	/** Visual only: show or hide the pack's socket bayonet on the muzzle (created on first use). */
	void SetBayonetFixed(bool bFixed);
	bool IsBayonetFixed() const { return bBayonetFixed; }
	/** Visual only: 0..1 how far the musket leaves the aim line for a body-driven hold (the reload drill),
	 *  and the world rotation it takes there. Set by the melee component. */
	float BodyPoseAlpha = 0.f;
	FQuat BodyPoseRotation = FQuat::Identity;
	/** Visual only: extra barrel pitch (deg, + = muzzle up) on top of the aim, set by the melee component (butt-stroke). */
	float MeleePitchOffset = 0.f;
	/** Visual only: extra barrel yaw (deg, + = muzzle to the right), for the guard carried across the body. */
	float MeleeYawOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UMusketTuning> Tuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UPA250WeaponTuning> WeaponTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	EPA250ReloadPreset ActivePreset = EPA250ReloadPreset::Authentic;

	// ---- Soldier class kit (PA250UnitClasses), set by the battle game mode. 1 = the plain musket. ----
	/** Aim cone multiplier of this piece (a rifle shoots straighter). */
	float ClassSpreadScale = 1.f;
	/** Stretches the damage falloff and maximum range of its ball. */
	float ClassRangeScale = 1.f;
	/** Reload time multiplier, player and AI alike: every step of the drill runs this much slower
	 *  (> 1) or faster (< 1). The class's own scale times any officer bonus. */
	float ReloadTimeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FMusketTuningValues DefaultTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<AMusketProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<USoundBase> FireSound;

	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnMusketStateChanged OnStateChanged;

protected:
	void SpawnMuzzleSmoke(const FVector& Location, const FRotator& Rotation) const;
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UStaticMeshComponent> MusketMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<USkeletalMeshComponent> AnimatedMesh;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FireAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ReloadAnim;
	float FireAnimRemaining = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USceneComponent> MuzzleLocation;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BayonetMesh;
	bool bBayonetFixed = false;
	bool bBayonetTried = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	EMusketState State = EMusketState::Loaded;

	void SetState(EMusketState NewState);
	void SyncTuningToSequencer();
	void SyncStateFromSequencer();

	FActiveReloadSequencer Sequencer;
	bool bReloadHeld = false;
	bool bReloadPaused = false;
	float BotCommitCooldown = 0.f;
	FPA250AIReloadPlanner AIPlanner;
	EActiveReloadPhase LastSoundPhase = EActiveReloadPhase::Idle;
	void NoteReloadSounds();
	float CurrentReloadSeconds = 0.f;
	float LastReloadSeconds = 0.f;
	int32 ReloadsCompleted = 0;
	int32 FumblesAtReloadStart = 0;
	int32 LastReloadFumbles = 0;
};
