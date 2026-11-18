// RFCharacter.h — third-person musket infantryman. Fires an AMusketWeapon (projectile); hitscan is retired.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "ActiveReloadSequencer.h"
#include "PA250InputBuffer.h"
#include "RFCharacter.generated.h"

class APA250MountedSoldier;
class UCameraComponent;
class UAnimSequence;
class USpringArmComponent;
class UInputMappingContext;
class UInputAction;
class URFHealthComponent;
class AMusketWeapon;
class UStaticMeshComponent;
class UPA250AimComponent;
class UPA250MeleeComponent;
class UPA250TraversalComponent;
class UMotionWarpingComponent;
class UWidgetComponent;
class UPA250WeaponTuning;
class UPA250FactionVisuals;

UCLASS()
class PLAYAMERICA250_API ARFCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARFCharacter();

	/** Local playable horse prototype: H mounts or dismounts. */
	UFUNCTION(Exec, BlueprintCallable, Category="Mounted")
	void ToggleMounted();
    AMusketWeapon* GetEquippedFirearm() const;
	/** The played general's arms (M5): a sword in hand and a pistol drawn while he aims, in place of
	 *  the musket. Prototype shapes, as the mounted rider's, until period models are supplied. The
	 *  numbers are the persons data's. Off: his musket again. */
	void SetHeroArms(bool bOn, float SwordDamage, float SwordReachCm, float SwordRecoverSeconds, float PistolDamage, float PistolRangeM, float PistolReloadSeconds);
	bool HasHeroArms() const { return bHeroArms; }
	/** Debug probes: hold or let go the aim as the player's key would. */
	void ProbeAim(bool bOn) { if (bOn) StartAim(); else StopAim(); }
    void SelectMountedSword();
    void SelectMountedRifle();
    void SelectMountedPistol();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool IsMounted() const { return MountedVisual != nullptr; }
	APA250MountedSoldier* GetMountedVisual() const { return MountedVisual; }

	// Riding (Mounted/PA250MountedRiding.cpp). Shift urges the horse up a gait, Ctrl reins it down.
	UFUNCTION(Exec, BlueprintCallable, Category="Mounted")
	void MountedGaitUp();
	UFUNCTION(Exec, BlueprintCallable, Category="Mounted")
	void MountedGaitDown();
	/** Probes: one sabre cut from the saddle (selects the sword). */
	UFUNCTION(Exec, BlueprintCallable, Category="Mounted")
	void MountedSlash() { OnMeleePressed(); }
	/** 0 walk, 1 trot, 2 canter, 3 gallop (EPA250Gait). */
	uint8 GetMountedGait() const { return MountedGait; }
	/** GAME INVENTION: extra aim cone from a moving saddle, 0 standing to 4 deg at a full gallop. */
	static float MountedFireSwayDegrees(float GroundSpeed);
	/** GAME INVENTION: a sabre cut from the saddle, 55 standing to 120 at a full gallop. */
	static float MountedSwordDamage(float GroundSpeed);
	/** Called by the horse's sword sweep for each man the blade passes through. Returns the damage dealt (0 if spared). */
	float OnMountedSwordHit(ARFCharacter* Victim, const FVector& Where);
	/** The horse is down: it falls with its momentum and the rider is thrown clear (ragdoll, then up). */
	void OnHorseKilled();
	/** Debug / probes: kill the horse under the player. */
	UFUNCTION(Exec, BlueprintCallable, Category="Mounted")
	void KillHorse();
	bool IsThrownFromHorse() const { return bThrownFromHorse; }

	// Dragoons (GAME INVENTION for 1775): any soldier can be put on a horse; a dragoon keeps his own.
	/** Into the saddle: his own picketed horse (Existing) or a new one. bForce skips the on-foot checks (spawn). */
	bool MountHorse(APA250MountedSoldier* Existing, bool bForce);
	/** Down from the saddle, leaving the horse picketed where it stands (it stays his horse). */
	void PicketHorse();
	/** Bots: 0 walk .. 3 gallop. */
	void SetMountedGait(uint8 Gait);
	/** A dragoon's own horse (ridden or picketed; may be dead). */
	APA250MountedSoldier* GetOwnHorse() const { return OwnHorse; }
	bool KeepsOwnHorse() const { return UnitClass == TEXT("dragoon"); }
	/** Not a dragoon any more (re-kitted): his horse is led away. */
	void ReleaseOwnHorse();
	/** How close (cm) a dragoon must be to his picketed horse to mount it. */
	static constexpr float RemountReachCm = 300.f;
	bool IsRecoveringFromThrow() const { return bThrownFromHorse && ThrowRecoverAlpha < 1.f && ThrowRecoverAlpha >= 0.f; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	FName FactionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	FName UnitClass = TEXT("line_infantry");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	TObjectPtr<URFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Team")
	TObjectPtr<UStaticMeshComponent> TeamMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UPA250AimComponent> AimComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UPA250MeleeComponent> MeleeComponent;

	/** Automatic traversal (M2): the engine fits the vault clips' root motion to the obstacle. */
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	TObjectPtr<UMotionWarpingComponent> MotionWarping;
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	TObjectPtr<UPA250TraversalComponent> Traversal;
	UPA250TraversalComponent* GetTraversal() const { return Traversal; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Team")
	TObjectPtr<UStaticMeshComponent> HatMesh;
	/** Class headgear and feathers on the period soldier (bearskin, cap, round hat, officer's feather). Visual only. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ClassHatPieces;
	/** The soldier drawn from the pack's separate part meshes (all but the cocked hat), following the main
	 *  mesh's pose: used by classes with their own headgear. Empty for everyone else. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> BodyParts;
	bool bBodyPartsFailed = false;
	FVector BaseMeshScale = FVector::ZeroVector;
	/** Height and headgear of the soldier's class (PA250UnitClasses look fields). */
	void ApplyClassLook();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Team")
	TObjectPtr<UWidgetComponent> Nameplate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TSubclassOf<AMusketWeapon> MusketClass;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AMusketWeapon> Musket;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TObjectPtr<UPA250WeaponTuning> WeaponTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	TObjectPtr<UPA250FactionVisuals> FactionVisuals;

	/** Squad index assigned by volley/squad coordinator (-1 = none). */
	int32 SquadId = -1;
	/** In a boat on the way in (APA250RowBoat): he neither moves nor fires until he is ashore. */
	bool bEmbarked = false;
	/** Knocked off his feet (a round shot passing close): a loose body thrown along Push (cm/s), who
	 *  gets up again as a thrown rider does. False when he cannot be (dead, mounted, in a boat, down already). */
	bool KnockDown(const FVector& Push);
	bool IsKnockedDown() const { return bThrownFromHorse; }
	/** The registry clip he gets up with when his loose body is next stood up (a blast knockdown
	 *  knows how it left him lying). NAME_None: the body is only blended up to standing. */
	void SetGetUpClip(FName Clip) { GetUpClip = Clip; }
	FName GetGetUpClip() const { return GetUpClip; }
	/** First person: the camera at his eyes. Z toggles it; third person is the default. */
	void ToggleFirstPerson();
	bool IsFirstPerson() const { return bFirstPerson; }
	/** Where the first-person camera sits from the capsule's centre, in view space (cm). */
	static FVector FirstPersonOffset() { return FVector(26.f, 0.f, 68.f); }

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool FireMusket();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool FireMusketAt(FVector TargetLocation, bool bCompensateDrop = true);

	/** Plant the feet: no walking for Seconds (firing, butt-stroke stagger). Aiming still turns. */
	void LockMovement(float Seconds);
	bool IsMovementLocked() const { return MovementLockTimer > 0.f; }
	float GetMovementLockRemaining() const { return MovementLockTimer; }

	/** Last reload press result, for the HUD flash (None when it has faded). */
	EActiveReloadHit GetReloadFeedback() const { return ReloadFeedbackTimer > 0.f ? LastReloadHit : EActiveReloadHit::None; }
	bool IsSprinting() const { return bSprinting; }
	/** GAMEPLAY SETTING (user decision 2026-11-14): the player loads standing or at his ordinary pace, never at a
	 *  run. Pure: true when a player on foot moves faster than his ordinary pace. Bots and riders are not held to it. */
	static bool ReloadBlockedByMovement(bool bPlayer, bool bMounted, float Speed2D, float OrdinaryPace)
	{
		return bPlayer && !bMounted && Speed2D > OrdinaryPace + 30.f;
	}
	bool IsReloadBlockedByMovement() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintCallable, Category = "Team")
	void SetTeam(FName InFaction, FName InUnitClass);

	UFUNCTION(BlueprintCallable, Category = "Team")
	void ApplyFactionVisuals();
	/** Stable for the life of one man, including the soldier who replaces him when he falls. */
	void SetClothingSeed(uint32 Seed);
	uint32 GetClothingSeed() const { return bClothingSeedSet ? ClothingSeed : 0; }
	bool IsUsingPeriodSoldier() const { return bUsesPeriodSoldier; }
	/** The battle's tier pick (battle_feel.json animation): motion matching for this soldier, or the cheap path. */
	void SetAnimFullQuality(bool bFull) { bAnimFullQuality = bFull; }
	bool IsAnimFullQuality() const { return bAnimFullQuality; }

	/** Pure: uniforms tell the sides apart, so a label only appears close up - an enemy inside 15 m and in
	 *  sight, a squadmate inside 25 m, any other friend inside 12 m. */
	static bool ShouldShowNameplate(bool bEnemy, bool bSquadmate, float DistanceCm, bool bHasLOS);
	/** Pure: what the label says ("Regular", "Provincial", "Militia"), not the raw faction id. */
	static FText NameplateLabel(FName Faction, FName InUnitClass);
	/** Pure: how hard the view closes in (0..1) from wounds, being under fire and a fresh hit. */
	static float ViewStress(float HealthFraction, bool bSuppressed, float HitPulse);

	UFUNCTION(BlueprintCallable, Category = "Team")
	void UpdateNameplate(FName ViewerFaction, bool bSquadmate, float DistanceCm, bool bHasLOS);

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> IMC_ThirdPerson;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Move;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Look;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Jump;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Sprint;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Aim;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Fire;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Reload;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_ShoulderSwap;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Melee;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_ButtStroke;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Charge;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Crouch;

	/** Crouched capsule half-height: 50 = 1.0 m tall, under a 1.1 m stone wall. */
	static constexpr float CrouchedHalfHeightCm = 50.f;
	/** Duck behind cover (or rise). Firing always rises first. */
	void SetDucking(bool bDuck);
	/** The player behind a wall, fence or work: he kneels by himself while he loads and rises when he
	 *  presents (battle_rules.json cover.snap_cm). True while that stance has him down. */
	bool IsAutoCovered() const { return bAutoDucked; }
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_SensUp;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_SensDown;

	/** Look sensitivity multiplier (pa250.LookSensitivity); [ / ] change it in game. */
	static float GetLookSensitivity();
	/** Look speed while aiming down sights, relative to hip. */
	UPROPERTY(EditAnywhere, Category = "Input")
	float AdsLookScale = 0.6f;
	/** Horizontal look relative to vertical. Trackpads and mice travel further sideways, so 1:1 turned too fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float YawLookScale = 0.6f;
	/** HUD shows the new sensitivity briefly after [ or ]. */
	bool ShowSensitivityToast() const { return SensitivityToastTimer > 0.f; }

	float FormationAccuracyScale = 1.f;
	/** Officer nearby (battle game mode, twice a second): aim cone multiplier and the flag for the HUD. */
	float OfficerAccuracyScale = 1.f;
	bool bNearOfficer = false;
	/** AI only (set from the training profile): chance a shot flashes in the pan. 0 for the player. */
	float MisfireChance = 0.f;
	/** 0..1, published by the bot controller; raises the misfire chance. */
	float MisfireStress = 0.f;
	int32 Misfires = 0;
	/** Trigger pulls on his musket this battle: the index of his musket-feel roll (PA250MusketFeel). */
	int32 TriggerPulls = 0;
	/** True once after a wound he survived (the bot loses his aim). */
	bool ConsumeFlinch() { const bool b = bFlinched; bFlinched = false; return b; }
	/** How long the fallen stay on the field (pa250.CorpseSeconds) and how many at once (pa250.MaxCorpses). */
	static float GetCorpseSeconds();
	static int32 GetMaxCorpses();
	/** The fallen body stops simulating once it has settled (intensity.corpses.freeze_after_seconds). */
	void FreezeCorpse();
	/** The look budget (battle_look.json soldier_lod): whether he casts a shadow now, and whether the engine may animate him at a lower rate when far or off screen. */
	void SetLookBudget(bool bCastShadow, bool bUpdateRateOptimisation);
	int8 LookShadow = -1;
	bool IsCorpseFrozen() const { return bCorpseFrozen; }
	bool bCorpseFrozen = false;
	float SuppressionSwayDegrees = 0.f;
	float SuppressionTimer = 0.f;
	float SuppressionLevel = 0.f;
	bool bInLine = false;
	int32 LineSize = 0;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAiming() const { return bAiming; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInLine() const { return bInLine; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetLineSize() const { return LineSize; }

	UFUNCTION(BlueprintPure, Category = "Camera")
	float GetShoulderSign() const { return ShoulderSign; }

	void ApplySuppression(float Seconds, float SwayDegrees);
	/** The player's suppression (battle_feel.intensity.suppression): rises by Amount (near misses,
	 *  strikes close by), clamped to its max; it blurs his view and sways his aim, and falls away. */
	void AddSuppression(float Amount);
	/** Lets suppression fall for DeltaTime (every tick for the player; tests call it). */
	void TickSuppression(float DeltaTime);
	float GetSuppressionLevel() const { return SuppressionLevel; }
	/** The aim sway his suppression adds now, in degrees (level x aim_sway_deg). */
	float SuppressionSwayNow() const;
	void NotifyFired();
	void ApplyCameraKick(float PitchDeg, float YawDeg);
	/** Momentary FOV change (deg, - = punch in); the camera blend eases it back. */
	void PunchFOV(float Degrees) { CurrentFOV = FMath::Clamp(CurrentFOV + Degrees, 30.f, 110.f); }
	float GetCurrentFOV() const { return CurrentFOV; }
	/** Extra FOV while a bayonet charge is running. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float ChargeFOVBoost = 7.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float DefaultFOV = 80.f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	float AimedFOV = 55.f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	float HipArmLength = 220.f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	float AdsArmLength = 120.f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector HipSocketOffset = FVector(0.f, 60.f, 70.f);
	UPROPERTY(EditAnywhere, Category = "Camera")
	FVector AdsSocketOffset = FVector(0.f, 45.f, 70.f);
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraBlendSeconds = 0.15f;
	UPROPERTY(EditAnywhere, Category = "Camera")
	float FaceControlYawHoldSeconds = 1.f;
	/** Ordinary pace, cm/s. A walk: inside battle_feel.json's walk band (at or under walk_max, 300).
	 *  450 is that file's run, so holding a direction used to play a run and Shift a sprint, and
	 *  there was no walk. Shift is still the sprint. */
	UPROPERTY(EditAnywhere, Category = "Movement")
	float WalkSpeed = 220.f;
	UPROPERTY(EditAnywhere, Category = "Movement")
	float SprintSpeed = 650.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Destroyed() override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void BuildDefaultInput();
	void SpawnMusket();
	void ApplyTeamMarker();
	void ApplyCameraPose(float DeltaTime);
	void UpdateFacingMode();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void StartAim();
	void StopAim();
	void OnFirePressed();
	void OnReloadStarted();
	/** Jump key: over the obstacle before him if there is one he can cross (at any speed), else a jump. */
	void OnJumpPressed();
	/** M3: presses that could not be acted on at once, acted on when they can be (battle_feel.json input). */
	void TickInputBuffer();
	/** M3: the player's movement and camera values from battle_feel.json movement; a bot keeps the class's own. */
	void ApplyMovementFeel(bool bPlayer);
	bool TryFireNow();
	bool IsOverObstacle() const;
	FPA250InputBuffer InputBuffer;
	bool bPlayerFeelApplied = false;
	void OnReloadCompleted();
	void OnShoulderSwap();
	void OnMeleePressed();
	void OnButtStrokePressed();
	void OnSensitivityUp();
	void OnChargePressed();
	void OnAbilityPressed();
	/** Y: the takeover screen for the general, when the player has earned him. U: the played general's own ability. */
	void OnHeroPressed();
	void OnHeroAbilityPressed();
	/** K: fall in with the nearest friendly line (his shot goes with its volleys), or fall out. */
	void OnFallInPressed();
	/** Number keys: on foot an order to his file (1 to 6); in the saddle 1 to 3 choose the weapon. */
	void OnNumberKey(int32 Number);
	void OnKey1() { OnNumberKey(1); }
	void OnKey2() { OnNumberKey(2); }
	void OnKey3() { OnNumberKey(3); }
	void OnKey4() { OnNumberKey(4); }
	void OnKey5() { OnNumberKey(5); }
	void OnKey6() { OnNumberKey(6); }
	void OnGuardPressed();
	void OnGuardReleased();
	void OnCycleGraphics();
	void OnDuckPressed();
	void OnDuckReleased();
	void OnSensitivityDown();
	void AdjustLookSensitivity(float Factor);
	static void LoadLookSensitivity();
	void ApplyThirdPartySoldierMesh();
	void RefreshNameplate();

	UFUNCTION()
	void OnDeath(AActor* Victim);
	void OnWounded(float Damage, const FVector& From);

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> SoldierIdle;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> SoldierWalk;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> SoldierRun;
	bool bUsesPeriodSoldier = false;
	bool bAnimFullQuality = false;
	uint8 LastMovingGait = 0;      // EPA250FootGait he last moved at (0: has not moved)
	float LastMovingAt = -1000.f;
	/** Debug run -PA250AnimProbe=move: the player walks, stops, turns about, runs and sprints on a
	 *  fixed script, with stills and a log line a second (speed, gait, tier, left hand to the musket). */
	void TickAnimProbe(float DeltaTime);
	float AnimProbeTime = -1.f;
	float AnimProbeLogTimer = 0.f;
	int32 AnimProbeStills = 0;
	FVector AnimProbeForward = FVector::ForwardVector;
	uint32 ClothingSeed = 0;
	bool bClothingSeedSet = false;
	void UpdateSoldierAnimation();
	/** Local player only: re-take mouse capture so mouse/trackpad look works without a click. */
	void EnsureGameMouseCapture();
	bool bAiming = false;
	bool bIsDead = false;
	bool bFlinched = false;
	float HitPulse = 0.f;
	float ViewStressSmoothed = 0.f;
	float StrideCm = 0.f;
	void TickFootsteps(float DeltaTime);
	void TickFireOverStance();
	bool bAutoDucked = false;
	bool bManualDuck = false;
	void TickViewFeedback(float DeltaTime);
	bool bSprinting = false;
	float CurrentFOV = 80.f;
	float TargetFOV = 80.f;
	float CurrentArmLength = 220.f;
	FVector CurrentSocketOffset = FVector(0.f, 60.f, 70.f);
	float ShoulderSign = 1.f;
	bool bFirstPerson = false;
	void OnShovePressed();
	float FaceControlYawTimer = 0.f;
	float RecoilPitch = 0.f;
	float RecoilYaw = 0.f;
	float RecoilRecovery = 0.35f;
	float MovementLockTimer = 0.f;
	float SensitivityToastTimer = 0.f;
	bool bMovementLocked = false;
	EActiveReloadHit LastReloadHit = EActiveReloadHit::None;
	float ReloadFeedbackTimer = 0.f;
	void TickMovementLock(float DeltaTime);
	UPROPERTY(Transient)
	TObjectPtr<APA250MountedSoldier> MountedVisual;
	void Dismount();
    void SelectMountedWeapon(int32 Index);
    int32 MountedWeaponIndex = 0;
    UPROPERTY(Transient) TObjectPtr<AMusketWeapon> MountedPistol;
	UPROPERTY(Transient) TObjectPtr<AMusketWeapon> HeroPistol;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> HeroSword;
	bool bHeroArms = false;
	float HeroPistolOutUntil = -1.f;
	float HeroAimAlpha = 0.f;
	float HeroRecoilAlpha = 0.f;
	void TickHeroArms(float DeltaTime);

	// Riding state (Mounted/PA250MountedRiding.cpp).
	/** Saddle -> foot: weapons back in his hands, foot movement restored. Returns the horse (not destroyed). */
	APA250MountedSoldier* LeaveSaddle();
	void BeginRidingMovement();
	void ApplyMountedGaitSpeed();
	void TickMounted(float DeltaTime);
	void ThrowRider(const FVector& HorseVelocity);
	/** The body goes loose (ragdoll) with these velocities: Carry for every part, Topple more the
	 *  higher the part is above the pelvis, Lift upward. TickThrown stands him up again. */
	void LooseBody(const FVector& Carry, const FVector& Topple, const FVector& Lift, float RaiseCm);
	void TickThrown(float DeltaTime);
	void FinishThrowRecovery();
	uint8 MountedGait = 0;
	float SavedMaxAcceleration = 0.f;
	float SavedBrakingDecel = 0.f;
	FRotator SavedRotationRate = FRotator::ZeroRotator;
	TEnumAsByte<ECollisionResponse> SavedCapsuleWorldDynamic = ECR_Block;
	bool bRidingMovementSaved = false;
	double RemountBlockedUntil = 0.0;
	bool bThrownFromHorse = false;
	FName GetUpClip;
	/** EnsureGameMouseCapture: when it last put the mouse back, and how often (the log is capped). */
	double LastMouseCaptureFix = 0.0;
	int32 MouseCaptureFixes = 0;
	float ThrownTime = 0.f;
	/** <0 while the body is loose; 0..1 while it blends back to his feet. */
	float ThrowRecoverAlpha = -1.f;
	FName SavedMeshProfile = NAME_None;
	float SavedBodyDamping = -1.f;
	UPROPERTY(Transient)
	TObjectPtr<APA250MountedSoldier> OwnHorse;
	FTransform SavedMeshRelative = FTransform::Identity;
};
