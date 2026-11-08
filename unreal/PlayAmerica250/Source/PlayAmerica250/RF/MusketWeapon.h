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

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartReload();

	EActiveReloadHit CommitReload();

	void SetReloadHeld(bool bHeld) { bReloadHeld = bHeld; }

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
	/** Max barrel pitch (deg) up or down; the soldier has no aiming body pose to go further. */
	static constexpr float MaxBarrelPitch = 45.f;
	USceneComponent* GetMuzzleLocation() const { return MuzzleLocation; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UMusketTuning> Tuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UPA250WeaponTuning> WeaponTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	EPA250ReloadPreset ActivePreset = EPA250ReloadPreset::Authentic;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	EMusketState State = EMusketState::Loaded;

	void SetState(EMusketState NewState);
	void SyncTuningToSequencer();
	void SyncStateFromSequencer();

	FActiveReloadSequencer Sequencer;
	bool bReloadHeld = false;
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
