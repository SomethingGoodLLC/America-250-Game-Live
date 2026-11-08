#include "MusketWeapon.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MusketProjectile.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PA250ThirdPartyAssets.h"
#include "PA250BattleAudio.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AnimationRuntime.h"
#include "GameFramework/Pawn.h"

AMusketWeapon::AMusketWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	MusketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MusketMesh"));
	MusketMesh->SetupAttachment(RootComponent);
	MusketMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MusketMesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	MusketMesh->SetRelativeScale3D(FVector(0.03f, 0.03f, 1.2f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		MusketMesh->SetStaticMesh(Cyl.Object);
	}

	MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	MuzzleLocation->SetupAttachment(RootComponent);
	MuzzleLocation->SetRelativeLocation(FVector(65.f, 0.f, 0.f));
	if (UStaticMesh* PeriodMusket = PA250ThirdParty::LoadFirstOf<UStaticMesh>(PA250ThirdParty::Musket))
	{
		MusketMesh->SetStaticMesh(PeriodMusket);
		const FBoxSphereBounds Bounds = PeriodMusket->GetBounds();
		const float Scale = 150.f / FMath::Max(1.f, Bounds.BoxExtent.X * 2.f);
		// X variant points down +X. Grip at the stock/barrel join, muzzle at the barrel tip.
		const FVector Grip(Bounds.Origin.X - Bounds.BoxExtent.X * 0.5f, Bounds.Origin.Y, Bounds.Origin.Z);
		MusketMesh->SetRelativeRotation(FRotator::ZeroRotator);
		MusketMesh->SetRelativeScale3D(FVector(Scale));
		MusketMesh->SetRelativeLocation(-Grip * Scale);
		MuzzleLocation->SetRelativeLocation((FVector(Bounds.Origin.X + Bounds.BoxExtent.X,
			Bounds.Origin.Y, Bounds.Origin.Z) - Grip) * Scale);
	}
	// Prefer the skeletal musket so the pack's lock/ramrod animations can play. Laid out from its own
	// bones: trigger at the actor origin (the right hand), barrel (trigger -> muzzle) along actor +X.
	if (USkeletalMesh* Rigged = PA250ThirdParty::LoadFirstOf<USkeletalMesh>(PA250ThirdParty::MusketAnimated))
	{
		const FReferenceSkeleton& Ref = Rigged->GetRefSkeleton();
		const int32 TriggerBone = Ref.FindBoneIndex(TEXT("b_gun_Trigger"));
		const int32 FireBone = Ref.FindBoneIndex(TEXT("b_gun_Fire"));
		if (TriggerBone != INDEX_NONE && FireBone != INDEX_NONE)
		{
			const FVector Trigger = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, TriggerBone).GetLocation();
			const FVector Tip = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, FireBone).GetLocation();
			const FVector Barrel = (Tip - Trigger).GetSafeNormal();
			// Mesh -> weapon space: barrel onto +X, mesh up (+Z) stays up.
			const FQuat ToWeapon = FRotationMatrix::MakeFromXZ(Barrel, FVector::UpVector).ToQuat().Inverse();
			const FVector Ext = Rigged->GetBounds().BoxExtent;
			const float Length = 2.f * FMath::Abs(FVector::DotProduct(Ext, Barrel.GetAbs()));
			const float Scale = 150.f / FMath::Max(1.f, Length);

			AnimatedMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AnimatedMesh"));
			AnimatedMesh->SetupAttachment(RootComponent);
			AnimatedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			AnimatedMesh->SetSkeletalMeshAsset(Rigged);
			AnimatedMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			AnimatedMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			AnimatedMesh->SetRelativeRotation(ToWeapon.Rotator());
			AnimatedMesh->SetRelativeScale3D(FVector(Scale));
			AnimatedMesh->SetRelativeLocation(-ToWeapon.RotateVector(Trigger) * Scale);
			MuzzleLocation->SetRelativeLocation(ToWeapon.RotateVector(Tip - Trigger) * Scale);
			MusketMesh->SetVisibility(false);
			MusketMesh->SetHiddenInGame(true);
		}
	}

	ProjectileClass = AMusketProjectile::StaticClass();
}

void AMusketWeapon::BeginPlay()
{
	Super::BeginPlay();
	if (!WeaponTuning)
	{
		WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
	}
	ApplyWeaponTuning();
}

void AMusketWeapon::SetReloadPreset(EPA250ReloadPreset Preset)
{
	ActivePreset = Preset;
	SyncTuningToSequencer();
}

void AMusketWeapon::ApplyWeaponTuning()
{
	SyncTuningToSequencer();
}

float AMusketWeapon::GetProjectileSpeed() const
{
	if (WeaponTuning) return WeaponTuning->Values.ProjectileSpeed;
	return GetTuning().ProjectileSpeed;
}

float AMusketWeapon::GetProjectileGravityZ() const
{
	const float WorldG = GetWorld() ? GetWorld()->GetGravityZ() : -980.f;
	const AMusketProjectile* CDO = ProjectileClass ? ProjectileClass->GetDefaultObject<AMusketProjectile>() : nullptr;
	const float Scale = CDO && CDO->GetProjectileMovement() ? CDO->GetProjectileMovement()->ProjectileGravityScale : 1.f;
	return WorldG * Scale;
}

bool AMusketWeapon::SolveLaunchDirection(const FVector& Muzzle, const FVector& Target, float Speed, float GravityZ, FVector& OutDir)
{
	const FVector Delta = Target - Muzzle;
	OutDir = Delta.GetSafeNormal();
	const float G = -GravityZ;
	const float D = Delta.Size2D();
	if (G <= KINDA_SMALL_NUMBER || Speed <= KINDA_SMALL_NUMBER || D < 1.f)
	{
		return true; // no drop to correct (or straight up/down)
	}
	const float H = Delta.Z;
	const float V2 = Speed * Speed;
	const float Disc = V2 * V2 - G * (G * D * D + 2.f * H * V2);
	if (Disc < 0.f)
	{
		return false;
	}
	// Low arc: tan(theta) = (v^2 - sqrt(disc)) / (g d)
	const float Theta = FMath::Atan((V2 - FMath::Sqrt(Disc)) / (G * D));
	const FVector Flat = FVector(Delta.X, Delta.Y, 0.f).GetSafeNormal();
	OutDir = (Flat * FMath::Cos(Theta) + FVector::UpVector * FMath::Sin(Theta)).GetSafeNormal();
	return true;
}

FActiveReloadTuningPlain AMusketWeapon::PresetToSequencer(const FPA250ReloadPresetValues& R)
{
	FActiveReloadTuningPlain P;
	const float Base = FMath::Max(0.3f, R.BaseSeconds);
	// Phase split 2:3:2 like the legacy reload; the three phases sum to BaseSeconds.
	P.PhasePrimePan = Base * (2.f / 7.f);
	P.PhaseRamCharge = Base * (3.f / 7.f);
	P.PhaseShoulderAim = Base * (2.f / 7.f);
	P.bTimeoutCompletesPhase = true;
	// A commit ends the phase early, so placing the window at Perfect/Base makes a centered
	// commit on every phase total PerfectSeconds. No extra speed scaling on top.
	P.WindowCenter = FMath::Clamp(R.PerfectSeconds / Base, 0.2f, 0.9f);
	P.PerfectReloadScale = 1.f;
	// Forgiving windows: half of each step is "good", a fifth of it is "perfect".
	P.SweetZoneFraction = 0.5f;
	P.PerfectZoneFraction = 0.2f;
	P.bMissResumesProgress = true;
	P.bIgnoreRepeatMiss = true;
	// A fumble pauses the step; one fumble per step adds (FailedSeconds - BaseSeconds) / 3.
	P.MissDelaySeconds = FMath::Max(0.f, (R.FailedSeconds - Base) / 3.f);
	P.JamChanceOnMiss = R.JamChanceOnMiss;
	P.JamClearHoldSeconds = R.JamClearSeconds;
	P.PerfectDamageBonus = R.PerfectDamageBonus;
	return P;
}

void AMusketWeapon::SyncTuningToSequencer()
{
	FActiveReloadTuningPlain P;
	if (WeaponTuning)
	{
		P = PresetToSequencer(WeaponTuning->Values.Preset(ActivePreset));
	}
	else
	{
		const FMusketTuningValues& T = GetTuning();
		P.PhasePrimePan = T.ActiveReload.PhasePrimePan;
		P.PhaseRamCharge = T.ActiveReload.PhaseRamCharge;
		P.PhaseShoulderAim = T.ActiveReload.PhaseShoulderAim;
		P.SweetZoneFraction = T.ActiveReload.SweetZoneFraction;
		P.PerfectZoneFraction = T.ActiveReload.PerfectZoneFraction;
		P.PerfectReloadScale = T.ActiveReload.PerfectReloadScale;
		P.MissDelaySeconds = T.ActiveReload.MissDelaySeconds;
		P.JamChanceOnMiss = T.ActiveReload.JamChanceOnMiss;
		P.JamClearHoldSeconds = T.ActiveReload.JamClearHoldSeconds;
		P.PerfectDamageBonus = T.ActiveReload.PerfectDamageBonus;
	}
	Sequencer.Configure(P);
}

void AMusketWeapon::SetState(EMusketState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(State);
	}
}

void AMusketWeapon::NoteReloadSounds()
{
	// One ramrod / shoulder sound per finished reload step (player and AI alike), a flint snap on a jam.
	const EActiveReloadPhase Ph = Sequencer.GetPhase();
	if (Ph == LastSoundPhase) return;
	const bool bWasStep = LastSoundPhase == EActiveReloadPhase::PrimePan || LastSoundPhase == EActiveReloadPhase::RamCharge
		|| LastSoundPhase == EActiveReloadPhase::ShoulderAim;
	const bool bAdvanced = bWasStep && (Ph == EActiveReloadPhase::RamCharge || Ph == EActiveReloadPhase::ShoulderAim
		|| Ph == EActiveReloadPhase::Complete || Ph == EActiveReloadPhase::Idle);
	if (bAdvanced)
	{
		FPA250BattleAudio::PlayAt(this, TEXT("ramrod"), GetActorLocation(), EPA250SoundRange::Near, 0.8f, 0.08f);
	}
	if (Ph == EActiveReloadPhase::Jammed)
	{
		FPA250BattleAudio::PlayAt(this, TEXT("flint_click"), GetActorLocation(), EPA250SoundRange::Near, 1.f, 0.05f);
	}
	LastSoundPhase = Ph;
}

void AMusketWeapon::SyncStateFromSequencer()
{
	NoteReloadSounds();
	if (Sequencer.IsJammed())
	{
		SetState(EMusketState::Jammed);
	}
	else if (Sequencer.IsComplete() || Sequencer.GetPhase() == EActiveReloadPhase::Idle)
	{
		if (State != EMusketState::Loaded)
		{
			LastReloadSeconds = CurrentReloadSeconds;
			LastReloadFumbles = Sequencer.GetStats().Miss - FumblesAtReloadStart;
			++ReloadsCompleted;
			Sequencer.Reset();
			SetState(EMusketState::Loaded);
		}
	}
	else if (Sequencer.IsReloading())
	{
		SetState(EMusketState::Reloading);
	}
}

bool AMusketWeapon::Fire(FVector Direction, bool bIsAiming, float OwnerSpeed, float AccuracyScale)
{
	if (!CanFire() || !ProjectileClass || !GetWorld())
	{
		return false;
	}
	if (!HasAuthority())
	{
		// Design for future multiplayer: only authority spawns damaging projectiles.
		// Local listen-server / single-player always has authority on the weapon owner.
	}

	SyncTuningToSequencer();
	FVector Dir = Direction.IsNearlyZero() ? MuzzleLocation->GetForwardVector() : Direction.GetSafeNormal();

	float Spread = 4.f;
	if (WeaponTuning)
	{
		Spread = WeaponTuning->Values.CurrentSpreadDegrees(bIsAiming, OwnerSpeed);
	}
	else
	{
		const FMusketTuningValues& T = GetTuning();
		Spread = T.Accuracy.BaseSpreadDegrees;
		if (bIsAiming) Spread *= T.Accuracy.AimingSpreadScale;
		if (OwnerSpeed < T.Accuracy.MoveSpeedThreshold) Spread *= T.Accuracy.StillSpreadScale;
		else Spread *= T.Accuracy.MovingSpreadScale;
	}
	Spread *= FMath::Max(0.05f, AccuracyScale);
	if (Spread > 0.01f)
	{
		Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(Spread));
	}

	const FVector Location = MuzzleLocation->GetComponentLocation();
	const FRotator Rotation = Dir.Rotation();
	const float Bonus = Sequencer.ConsumeDamageBonus();
	const float Speed = GetProjectileSpeed();

	FTransform SpawnTransform(Rotation, Location);
	AMusketProjectile* Ball = GetWorld()->SpawnActorDeferred<AMusketProjectile>(
		ProjectileClass, SpawnTransform, this, GetInstigator(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Ball)
	{
		float BaseDamage = 40.f;
		if (WeaponTuning)
		{
			// Falloff applied on hit via stored near damage; projectile carries near damage and range metadata.
			BaseDamage = WeaponTuning->Values.TorsoDamageNear;
			Ball->WeaponTuning = WeaponTuning;
		}
		else
		{
			BaseDamage = GetTuning().Damage;
		}
		Ball->Damage = BaseDamage * (1.f + Bonus);
		Ball->GetProjectileMovement()->InitialSpeed = Speed;
		Ball->GetProjectileMovement()->MaxSpeed = Speed;
		Ball->FinishSpawning(SpawnTransform);
	}

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Location);
	}
	else
	{
		FPA250BattleAudio::PlayMusketShot(this, Location);
	}
	SpawnMuzzleSmoke(Location, Rotation);
	if (AnimatedMesh && FireAnim)
	{
		AnimatedMesh->PlayAnimation(FireAnim, false);
		FireAnimRemaining = FireAnim->GetPlayLength();
	}

	StartReload();
	return Ball != nullptr;
}

void AMusketWeapon::StartReload()
{
	SyncTuningToSequencer();
	Sequencer.BeginReload();
	CurrentReloadSeconds = 0.f;
	FumblesAtReloadStart = Sequencer.GetStats().Miss;
	LastSoundPhase = Sequencer.GetPhase();
	SetState(EMusketState::Reloading);
}

EActiveReloadHit AMusketWeapon::CommitReload()
{
	if (!Sequencer.IsReloading() || Sequencer.IsJammed())
	{
		return EActiveReloadHit::None;
	}
	const EActiveReloadHit Hit = Sequencer.TryCommit();
	SyncStateFromSequencer();
	return Hit;
}

void AMusketWeapon::TickBotReload(float Skill01, FRandomStream& Rng)
{
	if (!Sequencer.IsReloading() || Sequencer.IsJammed())
	{
		return;
	}
	if (BotCommitCooldown > 0.f)
	{
		return;
	}
	// Commit at the window center like a practiced player; the roll decides quality.
	const float Center = 0.5f * (Sequencer.GetSweetStart() + Sequencer.GetSweetEnd());
	if (Sequencer.GetPhaseProgress() >= Center)
	{
		Sequencer.BotCommit(Skill01, Rng);
		BotCommitCooldown = 0.15f;
		SyncStateFromSequencer();
	}
}

EActiveReloadHit AMusketWeapon::TickAIReload(const FPA250AIReloadParams& Params, const FPA250AIReloadConditions& Conditions, FRandomStream& Rng)
{
	if (State == EMusketState::Loaded || !Sequencer.IsReloading())
	{
		return EActiveReloadHit::None;
	}
	if (Sequencer.IsJammed())
	{
		bReloadHeld = true; // clear a jam the same way the player does: hold reload
		return EActiveReloadHit::None;
	}
	bReloadHeld = false;
	const EActiveReloadHit Hit = AIPlanner.Tick(Sequencer, Params, Conditions, Rng);
	if (Hit != EActiveReloadHit::None)
	{
		SyncStateFromSequencer();
	}
	return Hit;
}

void AMusketWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BotCommitCooldown = FMath::Max(0.f, BotCommitCooldown - DeltaSeconds);
	if (State != EMusketState::Loaded)
	{
		CurrentReloadSeconds += DeltaSeconds;
		Sequencer.Tick(DeltaSeconds);
		if (Sequencer.IsJammed())
		{
			Sequencer.TryClearJam(bReloadHeld, DeltaSeconds);
		}
		SyncStateFromSequencer();
	}
	UpdateVisuals(DeltaSeconds);
}

float AMusketWeapon::ReloadAnimAlpha(EActiveReloadPhase Phase, float PhaseProgress)
{
	// The pack's 4.2 s reload is spread evenly over the three drill phases.
	const float P = FMath::Clamp(PhaseProgress, 0.f, 1.f);
	switch (Phase)
	{
	case EActiveReloadPhase::PrimePan:    return (0.f + P) / 3.f;
	case EActiveReloadPhase::RamCharge:   return (1.f + P) / 3.f;
	case EActiveReloadPhase::ShoulderAim: return (2.f + P) / 3.f;
	case EActiveReloadPhase::Idle:
	case EActiveReloadPhase::Complete:    return 1.f;
	default:                              return -1.f; // miss delay / jam: hold the pose
	}
}

void AMusketWeapon::UpdateVisuals(float DeltaSeconds)
{
	// Barrel follows the owner's aim (camera for the player, focus for bots); the hand only
	// carries its position. Without this the gun inherits the hand bone axis and points at the sky.
	if (const APawn* Pawn = Cast<APawn>(GetOwner()); Pawn && GetAttachParentActor() == Pawn)
	{
		FRotator Aim = Pawn->GetBaseAimRotation();
		Aim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -MaxBarrelPitch, MaxBarrelPitch);
		Aim.Roll = 0.f;
		if (!RootComponent->IsUsingAbsoluteRotation()) RootComponent->SetUsingAbsoluteRotation(true);
		RootComponent->SetWorldRotation(Aim);
	}
	if (!AnimatedMesh)
	{
		return;
	}
	if (!FireAnim || !ReloadAnim)
	{
		FireAnim = PA250ThirdParty::LoadFirstOf<UAnimSequence>(PA250ThirdParty::MusketFireAnim);
		ReloadAnim = PA250ThirdParty::LoadFirstOf<UAnimSequence>(PA250ThirdParty::MusketReloadAnim);
		if (!ReloadAnim) return;
	}
	if (FireAnimRemaining > 0.f)
	{
		FireAnimRemaining -= DeltaSeconds; // let the lock fall before the reload takes over
		return;
	}
	const float Alpha = State == EMusketState::Loaded ? 1.f : ReloadAnimAlpha(Sequencer.GetPhase(), Sequencer.GetPhaseProgress());
	if (Alpha < 0.f)
	{
		return;
	}
	if (AnimatedMesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
	{
		AnimatedMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
	if (UAnimSingleNodeInstance* Inst = AnimatedMesh->GetSingleNodeInstance())
	{
		if (Inst->GetCurrentAsset() != ReloadAnim)
		{
			AnimatedMesh->SetAnimation(ReloadAnim);
		}
		Inst->SetPlaying(false);
		Inst->SetPosition(Alpha * ReloadAnim->GetPlayLength(), false);
	}
}

float AMusketWeapon::GetReloadProgress() const
{
	if (State == EMusketState::Loaded)
	{
		return 1.f;
	}
	return Sequencer.GetPhaseProgress();
}

void AMusketWeapon::SpawnMuzzleSmoke(const FVector& Location, const FRotator& Rotation) const
{
	static TWeakObjectPtr<UNiagaraSystem> Smoke;
	static bool bResolved = false;
	if (!bResolved)
	{
		bResolved = true;
		Smoke = PA250ThirdParty::LoadFirstOf<UNiagaraSystem>(PA250ThirdParty::MuzzleSmoke);
	}
	if (UNiagaraSystem* System = Smoke.Get())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Location, Rotation);
	}
}
