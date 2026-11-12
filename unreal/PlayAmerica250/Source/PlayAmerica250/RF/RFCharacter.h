// RFCharacter.h — third-person musket infantryman. Fires an AMusketWeapon (projectile); hitscan is retired.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "ActiveReloadSequencer.h"
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
	/** True once after a wound he survived (the bot loses his aim). */
	bool ConsumeFlinch() { const bool b = bFlinched; bFlinched = false; return b; }
	/** How long the fallen stay on the field (pa250.CorpseSeconds) and how many at once (pa250.MaxCorpses). */
	static float GetCorpseSeconds();
	static int32 GetMaxCorpses();
	float SuppressionSwayDegrees = 0.f;
	float SuppressionTimer = 0.f;
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
	void NotifyFired();
	void ApplyCameraKick(float PitchDeg, float YawDeg);
	/** Momentary FOV change (deg, - = punch in); the camera blend eases it back. */
	void PunchFOV(float Degrees) { CurrentFOV = FMath::Clamp(CurrentFOV + Degrees, 30.f, 110.f); }
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
	UPROPERTY(EditAnywhere, Category = "Movement")
	float WalkSpeed = 450.f;
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
	void OnReloadCompleted();
	void OnShoulderSwap();
	void OnMeleePressed();
	void OnButtStrokePressed();
	void OnSensitivityUp();
	void OnChargePressed();
	void OnGuardPressed();
	void OnGuardReleased();
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
	void TickViewFeedback(float DeltaTime);
	bool bSprinting = false;
	float CurrentFOV = 80.f;
	float TargetFOV = 80.f;
	float CurrentArmLength = 220.f;
	FVector CurrentSocketOffset = FVector(0.f, 60.f, 70.f);
	float ShoulderSign = 1.f;
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

	// Riding state (Mounted/PA250MountedRiding.cpp).
	/** Saddle -> foot: weapons back in his hands, foot movement restored. Returns the horse (not destroyed). */
	APA250MountedSoldier* LeaveSaddle();
	void BeginRidingMovement();
	void ApplyMountedGaitSpeed();
	void TickMounted(float DeltaTime);
	void ThrowRider(const FVector& HorseVelocity);
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
	float ThrownTime = 0.f;
	/** <0 while the body is loose; 0..1 while it blends back to his feet. */
	float ThrowRecoverAlpha = -1.f;
	FName SavedMeshProfile = NAME_None;
	float SavedBodyDamping = -1.f;
	UPROPERTY(Transient)
	TObjectPtr<APA250MountedSoldier> OwnHorse;
	FTransform SavedMeshRelative = FTransform::Identity;
};
