#include "PA250BattleCases.h"
#include "PA250UnitClasses.h"
#include "Components/CapsuleComponent.h"
#include "PA250BotController.h"
#include "PA250BattleGameMode.h"
#include "ActiveReloadSequencer.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif
#include "MusketWeapon.h"
#include "MusketTuning.h"
#include "MusketProjectile.h"
#include "PA250AimComponent.h"
#include "PA250BotSpacingTest.h"
#include "PA250ControlPoint.h"
#include "PA250FlagArt.h"
#include "PA250BattleHUD.h"
#include "PA250FactionVisuals.h"
#include "PA250MeleeComponent.h"
#include "PA250MeleeTuning.h"
#include "PA250SoldierAnimInstance.h"
#include "PA250TicketLedger.h"
#include "PA250AIReload.h"
#include "PA250CombatProfiles.h"
#include "PA250BotTuning.h"
#include "PA250Cover.h"
#include "PA250Morale.h"
#include "PA250Smoke.h"
#include "PA250BattleAudio.h"
#include "Sound/SoundWave.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "PA250VolleyCoordinator.h"
#include "PA250WeaponTuning.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"

namespace PA250BattleCases
{
void FLog::Check(bool bCond, const FString& What)
{
	UE_LOG(LogTemp, Display, TEXT("PA250.Battle %s: %s"), bCond ? TEXT("PASS") : TEXT("FAIL"), *What);
	if (bCond) ++Passed;
	else Failures.Add(What);
}

namespace
{
/** Bare game world for trace / spawn cases. Not begun-play: actors are configured by hand. */
struct FScopedTestWorld
{
	UWorld* World = nullptr;
	FScopedTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PA250BattleCases"));
		if (!World || !GEngine) return;
		FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
		Ctx.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}
	~FScopedTestWorld()
	{
		if (!World) return;
		if (GEngine) GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
	AStaticMeshActor* SpawnBox(const FVector& Center, const FVector& SizeCm) const
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!World || !Cube) return nullptr;
#if WITH_EDITOR
		FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
		if (!A) return nullptr;
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Cube);
		C->SetWorldScale3D(SizeCm / 100.f);
		C->SetCollisionProfileName(TEXT("BlockAll"));
		// Editor builds compile meshes asynchronously and skip the body until that finishes;
		// a non-ticking test world never gets the deferred recreate, so force it here.
		if (!C->IsPhysicsStateCreated()) C->RecreatePhysicsState();
		return A;
	}
	/** Let the physics scene pick up new bodies before tracing. */
	void Settle() const
	{
		for (int32 i = 0; World && i < 2; ++i) World->Tick(LEVELTICK_All, 1.f / 60.f);
	}
};

enum class EReloadDrive { Untouched, CenterCommit, EarlyMiss };

/** Seconds to a loaded musket for one way of driving the reload. */
float TimeReload(const FActiveReloadTuningPlain& P, EReloadDrive Drive, bool& bOutBonus)
{
	FActiveReloadSequencer S;
	S.Configure(P);
	S.BeginReload();
	const float Dt = 0.005f;
	float T = 0.f;
	EActiveReloadPhase MissedPhase = EActiveReloadPhase::Idle;
	while (!S.IsComplete() && T < 120.f)
	{
		const EActiveReloadPhase Ph = S.GetPhase();
		const bool bCombat = Ph == EActiveReloadPhase::PrimePan || Ph == EActiveReloadPhase::RamCharge || Ph == EActiveReloadPhase::ShoulderAim;
		if (bCombat && Drive == EReloadDrive::CenterCommit && S.GetPhaseProgress() >= P.WindowCenter)
		{
			S.TryCommit();
			continue;
		}
		if (bCombat && Drive == EReloadDrive::EarlyMiss && MissedPhase != Ph && S.GetPhaseProgress() > 0.f)
		{
			MissedPhase = Ph; // one early miss per phase, then let the retry run out
			S.TryCommit();
			continue;
		}
		S.Tick(Dt);
		T += Dt;
	}
	bOutBonus = S.PeekDamageBonus() > 0.f;
	return T;
}
} // namespace

void RunAim(FLog& Log)
{
	FScopedTestWorld W;
	Log.Check(W.World != nullptr, TEXT("aim: test world"));
	if (!W.World) return;
	// Wall 20 m out; camera on the axis, muzzle 60 cm to the right (shoulder offset).
	W.SpawnBox(FVector(2000.f, 0.f, 100.f), FVector(100.f, 1000.f, 600.f));
	W.Settle();
	const FVector Cam(0.f, 0.f, 100.f);
	const FVector Dir(1.f, 0.f, 0.f);
	const FVector Muzzle(40.f, 60.f, 100.f);
	{
		const FPA250AimResult R = UPA250AimComponent::ResolveAim(W.World, Cam, Dir, Muzzle, 0.f, 20000.f, nullptr, nullptr);
		Log.Check(FMath::IsNearlyEqual(R.CameraAimPoint.X, 1950.f, 2.f), FString::Printf(TEXT("aim: camera ray hits wall face (x=%.1f)"), R.CameraAimPoint.X));
		Log.Check(FVector::Dist(R.MuzzleHitPoint, R.CameraAimPoint) <= 5.f,
			FString::Printf(TEXT("aim: clear muzzle lands within 5 cm of reticle (%.2f cm)"), FVector::Dist(R.MuzzleHitPoint, R.CameraAimPoint)));
		Log.Check(!R.bBlocked, TEXT("aim: clear muzzle not blocked"));
		Log.Check(R.FireDirection.Equals((R.CameraAimPoint - Muzzle).GetSafeNormal(), 1e-3f), TEXT("aim: zero spread fires muzzle->aim"));
	}
	{
		// Post beside the muzzle: camera ray (y=0) passes, muzzle ray (y 60 -> 0) is cut at x~500.
		W.SpawnBox(FVector(500.f, 45.f, 100.f), FVector(20.f, 20.f, 200.f));
		W.Settle();
		const FPA250AimResult R = UPA250AimComponent::ResolveAim(W.World, Cam, Dir, Muzzle, 0.f, 20000.f, nullptr, nullptr);
		Log.Check(FMath::IsNearlyEqual(R.CameraAimPoint.X, 1950.f, 2.f), TEXT("aim: obstruction off the camera ray"));
		Log.Check(R.bBlocked, TEXT("aim: obstructed muzzle -> bBlocked"));
		Log.Check(R.MuzzleHitPoint.X < 600.f, FString::Printf(TEXT("aim: blocked point at the post (x=%.1f)"), R.MuzzleHitPoint.X));
	}
	{
		const FPA250AimResult R = UPA250AimComponent::ResolveAim(W.World, Cam, Dir, Muzzle, 4.f, 20000.f, nullptr, nullptr);
		const FVector Clean = (R.CameraAimPoint - Muzzle).GetSafeNormal();
		const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Clean, R.FireDirection), -1.f, 1.f)));
		Log.Check(Off <= 4.01f, FString::Printf(TEXT("aim: 4 deg spread stays in cone (%.2f)"), Off));
	}
}

void RunWeapon(FLog& Log)
{
	UPA250WeaponTuning* Tun = UPA250WeaponTuning::MakeRuntimeDefaults();
	const FPA250WeaponTuningValues& V = Tun->Values;
	Log.Check(V.DamageAtDistanceCm(2000.f) == 100.f, TEXT("weapon: 100 inside 40 m"));
	Log.Check(V.DamageAtDistanceCm(7000.f) == 80.f, TEXT("weapon: 80 at 70 m (linear)"));
	Log.Check(V.DamageAtDistanceCm(10000.f) == 60.f, TEXT("weapon: 60 at 100 m"));
	Log.Check(V.DamageAtDistanceCm(19000.f) == 60.f, TEXT("weapon: 60 out to max range"));
	Log.Check(V.DamageAtDistanceCm(25000.f) == 0.f, TEXT("weapon: 0 beyond 200 m"));
	Log.Check(V.CurrentSpreadDegrees(true, 0.f) < V.CurrentSpreadDegrees(false, 0.f), TEXT("weapon: ADS tighter than hip"));
	Log.Check(V.CurrentSpreadDegrees(false, 400.f) > V.CurrentSpreadDegrees(false, 0.f), TEXT("weapon: moving widens spread"));

	struct FCase { const TCHAR* Name; EPA250ReloadPreset P; float Base, Perfect, Failed; };
	const FCase Cases[] = {
		{TEXT("arcade"), EPA250ReloadPreset::Arcade, 10.f, 7.f, 14.f},
		{TEXT("authentic"), EPA250ReloadPreset::Authentic, 30.f, 15.f, 45.f},
	};
	for (const FCase& C : Cases)
	{
		FActiveReloadTuningPlain P = AMusketWeapon::PresetToSequencer(V.Preset(C.P));
		P.JamChanceOnMiss = 0.f; // timing only
		bool bBonus = false;
		const float Base = TimeReload(P, EReloadDrive::Untouched, bBonus);
		Log.Check(FMath::IsNearlyEqual(Base, C.Base, 0.05f), FString::Printf(TEXT("weapon: %s untouched %.2fs (want %.1f)"), C.Name, Base, C.Base));
		Log.Check(!bBonus, FString::Printf(TEXT("weapon: %s untouched has no bonus"), C.Name));
		const float Perfect = TimeReload(P, EReloadDrive::CenterCommit, bBonus);
		Log.Check(FMath::IsNearlyEqual(Perfect, C.Perfect, 0.05f), FString::Printf(TEXT("weapon: %s perfect %.2fs (want %.1f)"), C.Name, Perfect, C.Perfect));
		Log.Check(bBonus, FString::Printf(TEXT("weapon: %s perfect grants damage bonus"), C.Name));
		const float Failed = TimeReload(P, EReloadDrive::EarlyMiss, bBonus);
		Log.Check(FMath::IsNearlyEqual(Failed, C.Failed, 0.1f), FString::Printf(TEXT("weapon: %s failed %.2fs (want %.1f)"), C.Name, Failed, C.Failed));
	}
	// Ballistics: the solved launch lands on the aim point, and the ball never climbs visibly
	// above the sight line (the old 30 m/s ball + max-range compensation went metres high).
	{
		const float Speed = V.ProjectileSpeed;
		const float G = -980.f * 0.3f;
		const FVector Muzzle(0.f, 0.f, 150.f);
		for (const float Range : {2000.f, 6000.f, 15000.f})
		{
			const FVector Target(Range, 300.f, 120.f);
			FVector Dir;
			const bool bOk = AMusketWeapon::SolveLaunchDirection(Muzzle, Target, Speed, G, Dir);
			const FVector Vel = Dir * Speed;
			const float T = FVector(Target - Muzzle).Size2D() / Vel.Size2D();
			const FVector Land = Muzzle + Vel * T + FVector(0.f, 0.f, 0.5f * G * T * T);
			float MaxAbove = 0.f;
			for (int32 i = 1; i < 100; ++i)
			{
				const float t = T * i / 100.f;
				const FVector P = Muzzle + Vel * t + FVector(0.f, 0.f, 0.5f * G * t * t);
				const float LineZ = FMath::Lerp(Muzzle.Z, Target.Z, i / 100.f);
				MaxAbove = FMath::Max(MaxAbove, P.Z - LineZ);
			}
			Log.Check(bOk && FVector::Dist(Land, Target) <= 5.f,
				FString::Printf(TEXT("weapon: ball lands on the aim point at %.0f m (%.1f cm off)"), Range / 100.f, FVector::Dist(Land, Target)));
			Log.Check(MaxAbove <= 10.f, FString::Printf(TEXT("weapon: ball stays within 10 cm of the sight line at %.0f m (%.1f cm)"), Range / 100.f, MaxAbove));
		}
		FVector Dir;
		Log.Check(!AMusketWeapon::SolveLaunchDirection(Muzzle, FVector(1.0e7f, 0.f, 0.f), 3000.f, G, Dir) && Dir.Equals(FVector(1, 0, 0), 1e-3f),
			TEXT("weapon: out of ballistic reach fires straight, not into the sky"));
		Log.Check(V.FireStillSeconds > 0.f, TEXT("weapon: firing plants the feet"));
	}

	// Authentic (15-45 s) is the permanent default everywhere a preset is chosen.
	Log.Check(GetDefault<APA250BattleGameMode>()->ReloadPreset == 1 && GetDefault<AMusketWeapon>()->ActivePreset == EPA250ReloadPreset::Authentic
		&& V.DefaultPreset == EPA250ReloadPreset::Authentic, TEXT("weapon: Authentic reload is the default (game mode, musket, tuning)"));
	const FActiveReloadTuningPlain Arc = AMusketWeapon::PresetToSequencer(V.Arcade);
	Log.Check(FMath::IsNearlyEqual(Arc.PerfectDamageBonus, 0.15f) && Arc.JamChanceOnMiss == 0.f
		&& FMath::IsNearlyEqual(Arc.JamClearHoldSeconds, 1.5f), TEXT("weapon: arcade +15%, never jams"));
	Log.Check(FMath::IsNearlyEqual(AMusketWeapon::PresetToSequencer(V.Authentic).JamChanceOnMiss, 0.10f), TEXT("weapon: authentic misses can jam (10%)"));
	Log.Check(Arc.SweetZoneFraction >= 0.5f && Arc.PerfectZoneFraction >= 0.2f, TEXT("weapon: arcade press zone is half the step, perfect a fifth"));

	// Forgiving arcade reload: a fumble pauses the step instead of restarting it, and mashing costs nothing more.
	{
		FActiveReloadSequencer S;
		S.Configure(Arc);
		S.BeginReload();
		while (S.GetPhaseProgress() < 0.3f) S.Tick(0.005f);
		const float Before = S.GetPhaseProgress();
		Log.Check(S.TryCommit() == EActiveReloadHit::Miss, TEXT("weapon: early press fumbles"));
		Log.Check(S.TryCommit() == EActiveReloadHit::None, TEXT("weapon: mashing during the fumble is ignored"));
		while (S.GetPhase() == EActiveReloadPhase::MissDelay) S.Tick(0.005f);
		Log.Check(S.GetPhase() == EActiveReloadPhase::PrimePan && S.GetPhaseProgress() >= Before - 0.01f,
			FString::Printf(TEXT("weapon: fumble keeps step progress (%.2f -> %.2f)"), Before, S.GetPhaseProgress()));
		Log.Check(S.TryCommit() == EActiveReloadHit::None, TEXT("weapon: second early press in the same step is ignored"));
		while (S.GetPhaseProgress() < Arc.WindowCenter) S.Tick(0.005f);
		Log.Check(S.TryCommit() == EActiveReloadHit::Perfect && S.GetPhase() == EActiveReloadPhase::RamCharge, TEXT("weapon: then a press in the zone still lands"));
	}

	// The shipped DataAssets are made from the class defaults - they must carry the brief's values.
	{
		const UPA250WeaponTuning* Shipped = LoadObject<UPA250WeaponTuning>(nullptr, TEXT("/Game/PA250/Data/DA_WeaponTuning.DA_WeaponTuning"));
		const UPA250WeaponTuning* Cdo = GetDefault<UPA250WeaponTuning>();
		for (const UPA250WeaponTuning* A : {Cdo, Shipped})
		{
			if (!A) continue;
			const TCHAR* Which = A == Cdo ? TEXT("class default") : TEXT("DA_WeaponTuning");
			Log.Check(A->Values.Authentic.BaseSeconds == 30.f && A->Values.Authentic.PerfectSeconds == 15.f && A->Values.Authentic.FailedSeconds == 45.f,
				FString::Printf(TEXT("weapon: %s authentic is 15-45 s (base %.1f / perfect %.1f / failed %.1f)"), Which,
					A->Values.Authentic.BaseSeconds, A->Values.Authentic.PerfectSeconds, A->Values.Authentic.FailedSeconds));
			Log.Check(A->Values.Arcade.BaseSeconds == 10.f && A->Values.Arcade.PerfectSeconds == 7.f && A->Values.ProjectileSpeed == 30000.f, FString::Printf(TEXT("weapon: %s arcade ~10 s (7 perfect), 300 m/s ball"), Which));
		}
		Log.Check(Shipped != nullptr, TEXT("weapon: DA_WeaponTuning loads"));
	}

	// Jam blocks fire until cleared, on a real musket.
	FScopedTestWorld W;
	AMusketWeapon* Musket = W.World ? W.World->SpawnActor<AMusketWeapon>() : nullptr;
	Log.Check(Musket != nullptr, TEXT("weapon: spawn musket"));
	if (!Musket) return;
	UPA250WeaponTuning* JamTun = UPA250WeaponTuning::MakeRuntimeDefaults();
	JamTun->Values.Arcade.JamChanceOnMiss = 1.f;
	Musket->WeaponTuning = JamTun;
	Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
	Log.Check(Musket->CanFire(), TEXT("weapon: starts loaded"));
	Musket->StartReload();
	Musket->GetSequencer().Tick(0.01f);
	Log.Check(Musket->CommitReload() == EActiveReloadHit::Miss, TEXT("weapon: early press misses"));
	Log.Check(Musket->IsJammed() && !Musket->CanFire(), TEXT("weapon: jammed musket cannot fire"));
	Log.Check(!Musket->Fire(FVector::ForwardVector), TEXT("weapon: Fire() refused while jammed"));
	float Held = 0.f;
	while (Musket->GetSequencer().IsJammed() && Held < 3.f)
	{
		Musket->GetSequencer().TryClearJam(true, 0.05f);
		Held += 0.05f;
	}
	Log.Check(!Musket->GetSequencer().IsJammed() && FMath::IsNearlyEqual(Held, 1.5f, 0.06f),
		FString::Printf(TEXT("weapon: jam clears after %.2fs held (want 1.5)"), Held));
	Log.Check(Musket->GetReloadPhaseLabel().StartsWith(TEXT("1/3")), FString::Printf(TEXT("weapon: reload steps are numbered (%s)"), *Musket->GetReloadPhaseLabel()));
	Musket->Destroy();

	// Firing plants a real soldier's feet, then lets him walk again.
	ARFCharacter* Soldier = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator);
	AMusketWeapon* Gun = W.World->SpawnActor<AMusketWeapon>();
	Log.Check(Soldier && Gun, TEXT("weapon: spawn soldier + musket"));
	if (!Soldier || !Gun) return;
	Gun->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
	Gun->SetReloadPreset(EPA250ReloadPreset::Arcade);
	Soldier->Musket = Gun;
	Soldier->WeaponTuning = Gun->WeaponTuning;
	UCharacterMovementComponent* Move = Soldier->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Walking);
	Move->Velocity = FVector(400.f, 0.f, 0.f);
	Log.Check(Soldier->FireMusketAt(FVector(3000.f, 0.f, 100.f)), TEXT("weapon: soldier fires"));
	Log.Check(Soldier->IsMovementLocked() && Move->MovementMode == MOVE_None && Move->Velocity.IsNearlyZero(),
		TEXT("weapon: firing stops the shooter dead"));
	Soldier->TickActor(Gun->WeaponTuning->Values.FireStillSeconds + 0.05f, LEVELTICK_All, Soldier->PrimaryActorTick);
	Log.Check(!Soldier->IsMovementLocked() && Move->MovementMode == MOVE_Walking, TEXT("weapon: can walk again after the shot"));
	Move->SetMovementMode(MOVE_Falling);
	Log.Check(!Soldier->FireMusketAt(FVector(3000.f, 0.f, 100.f)), TEXT("weapon: no firing in mid-air"));
	Soldier->Destroy();

	// The ball carries a visible glowing streak, which never reaches back past the muzzle.
	AMusketProjectile* Ball = W.World->SpawnActor<AMusketProjectile>(FVector::ZeroVector, FRotator::ZeroRotator);
	Log.Check(Ball && Ball->GetTracer() && Ball->GetTracer()->GetStaticMesh() && Ball->GetTracer()->GetMaterial(0), TEXT("weapon: ball has a tracer mesh + glow material"));
	if (Ball && Ball->GetTracer())
	{
		Ball->SpawnLocation = FVector::ZeroVector;
		Ball->SetActorLocation(FVector(10.f, 0.f, 0.f));
		Ball->UpdateTracer();
		Log.Check(!Ball->GetTracer()->IsVisible(), TEXT("weapon: tracer hidden at the muzzle"));
		Ball->SetActorLocation(FVector(300.f, 0.f, 0.f));
		Ball->UpdateTracer();
		const float Len300 = Ball->GetTracer()->GetRelativeScale3D().Z * 100.f;
		Ball->SetActorLocation(FVector(5000.f, 0.f, 0.f));
		Ball->UpdateTracer();
		const float LenFar = Ball->GetTracer()->GetRelativeScale3D().Z * 100.f;
		Log.Check(Ball->GetTracer()->IsVisible() && FMath::IsNearlyEqual(Len300, 300.f, 1.f) && FMath::IsNearlyEqual(LenFar, 450.f, 1.f),
			FString::Printf(TEXT("weapon: tracer grows from the muzzle then caps (%.0f, %.0f cm)"), Len300, LenFar));
		Ball->Destroy();
	}
}

void RunFaction(FLog& Log)
{
	UPA250FactionVisuals* V = UPA250FactionVisuals::MakeRuntimeDefaults();
	Log.Check(V->British.CoatColor.R > V->British.CoatColor.G, TEXT("faction: british coat red"));
	Log.Check(V->Continental.CoatColor.B > V->Continental.CoatColor.R, TEXT("faction: continental coat blue"));
	Log.Check(V->British.HatStyle == FName(TEXT("tricorn")), TEXT("faction: british tricorn"));
	Log.Check(V->Militia.HatStyle == FName(TEXT("round")), TEXT("faction: militia round hat"));
	Log.Check(&V->LookFor(TEXT("continental"), TEXT("militia")) == &V->Militia, TEXT("faction: militia class -> militia look"));
	Log.Check(&V->LookFor(TEXT("british"), TEXT("line_infantry")) == &V->British, TEXT("faction: british line -> british look"));
	const UPA250FactionVisuals* Shipped = LoadObject<UPA250FactionVisuals>(nullptr, TEXT("/Game/PA250/Data/DA_FactionVisuals.DA_FactionVisuals"));
	Log.Check(Shipped && Shipped->British.CoatColor.R > 0.4f && Shipped->British.CoatColor.G < 0.2f
		&& Shipped->Continental.CoatColor.B > Shipped->Continental.CoatColor.R && Shipped->Militia.HatStyle == FName(TEXT("round")),
		TEXT("faction: DA_FactionVisuals carries the faction looks (not white defaults)"));
}

void RunMelee(FLog& Log)
{
	UPA250MeleeTuning* T = UPA250MeleeTuning::MakeRuntimeDefaults();
	const FPA250MeleeTuningValues& V = T->Values;
	Log.Check(V.ThrustWindupSeconds == 0.35f && V.ThrustReachCm == 220.f && V.ThrustDamage == 75.f, TEXT("melee: thrust 0.35 s / 2.2 m / 75"));
	Log.Check(V.ButtStrokeDamage == 35.f && V.ButtStrokeStaggerSeconds == 0.6f, TEXT("melee: butt-stroke 35 + 0.6 s stagger"));
	Log.Check(V.ChargeLungeCm == 600.f && V.ChargeDamage == 70.f && V.ChargeCooldownSeconds == 4.f && V.ChargeHitRadiusCm >= 70.f,
		TEXT("melee: charge 6 m / 70 / 4 s, wide sweep"));

	const FVector Fwd(1.f, 0.f, 0.f);
	Log.Check(UPA250MeleeComponent::ComputeHitDamage(75.f, true, Fwd, FVector(1, 0, 0), false, V) == 75.f, TEXT("melee: frontal thrust x1"));
	Log.Check(UPA250MeleeComponent::ComputeHitDamage(75.f, true, Fwd, FVector(0, 1, 0), false, V) == 75.f, TEXT("melee: flank (90 deg) x1"));
	Log.Check(UPA250MeleeComponent::ComputeHitDamage(75.f, true, Fwd, FVector(-1, 0.2f, 0), false, V) == 150.f, TEXT("melee: from behind (>120 deg) x2"));
	Log.Check(UPA250MeleeComponent::ComputeHitDamage(75.f, true, Fwd, FVector(1, 0, 0), true, V) == 150.f, TEXT("melee: vs reloading x2"));
	Log.Check(UPA250MeleeComponent::ComputeHitDamage(35.f, false, Fwd, FVector(-1, 0, 0), true, V) == 35.f, TEXT("melee: butt-stroke never doubles"));

	// Butt-stroke mid-reload on a real soldier leaves the reload where it was.
	FScopedTestWorld W;
	ARFCharacter* Soldier = W.World ? W.World->SpawnActor<ARFCharacter>() : nullptr;
	AMusketWeapon* Musket = W.World ? W.World->SpawnActor<AMusketWeapon>() : nullptr;
	Log.Check(Soldier && Musket && Soldier->MeleeComponent, TEXT("melee: spawn soldier + musket"));
	if (!Soldier || !Musket || !Soldier->MeleeComponent) return;
	Soldier->Musket = Musket;
	Soldier->MeleeComponent->Tuning = T;
	Musket->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
	Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
	Musket->StartReload();
	Musket->GetSequencer().Tick(0.6f);
	const EActiveReloadPhase Phase = Musket->GetSequencer().GetPhase();
	const float Before = Musket->GetReloadProgress();
	Log.Check(Soldier->MeleeComponent->DoButtStroke(), TEXT("melee: butt-stroke accepted while reloading"));
	Log.Check(Musket->IsReloading() && Musket->GetSequencer().GetPhase() == Phase && Musket->GetReloadProgress() == Before,
		FString::Printf(TEXT("melee: reload progress kept (%.3f -> %.3f)"), Before, Musket->GetReloadProgress()));
	Log.Check(!Soldier->MeleeComponent->DoButtStroke(), TEXT("melee: busy window blocks a second swing"));

	// Charge is a dash with a cooldown; it needs footing and blocks other swings while it runs.
	ARFCharacter* Runner = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 500.f, 100.f), FRotator::ZeroRotator);
	if (Runner && Runner->MeleeComponent)
	{
		Runner->MeleeComponent->Tuning = T;
		Runner->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Log.Check(!Runner->MeleeComponent->DoCharge(), TEXT("melee: no charge in mid-air"));
		Runner->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Log.Check(Runner->MeleeComponent->DoCharge() && Runner->MeleeComponent->IsCharging(), TEXT("melee: charge starts a dash"));
		Log.Check(FMath::IsNearlyEqual(Runner->MeleeComponent->GetChargeCooldownRemaining(), 4.f), TEXT("melee: charge goes on a 4 s cooldown"));
		Log.Check(!Runner->MeleeComponent->DoThrust() && !Runner->MeleeComponent->DoCharge(), TEXT("melee: no other swing mid-charge"));
		Log.Check(Runner->MeleeComponent->GetFeedbackText() == TEXT("CHARGE!"), TEXT("melee: charge is announced on the HUD"));
		Runner->Destroy();
	}

	// Charge assist: an enemy roughly in front (within 40 deg, 9 m, in sight) pulls the dash onto him.
	{
		ARFCharacter* Me = W.World->SpawnActor<ARFCharacter>(FVector(0.f, -3000.f, 100.f), FRotator::ZeroRotator);
		ARFCharacter* Foe = W.World->SpawnActor<ARFCharacter>(FVector(500.f, -2750.f, 100.f), FRotator::ZeroRotator);
		ARFCharacter* Flank = W.World->SpawnActor<ARFCharacter>(FVector(0.f, -2400.f, 100.f), FRotator::ZeroRotator);
		if (Me && Foe && Flank && Me->MeleeComponent)
		{
			Me->FactionId = TEXT("continental");
			Foe->FactionId = TEXT("british");
			Flank->FactionId = TEXT("british");
			Me->MeleeComponent->Tuning = T;
			Log.Check(Me->MeleeComponent->FindChargeTarget() == Foe, TEXT("melee: charge assist picks the enemy in front, not the one at 90 deg"));
			Me->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			Me->MeleeComponent->DoCharge();
			const float Yaw = Me->GetActorRotation().Yaw;
			const float Want = FMath::RadiansToDegrees(FMath::Atan2(250.f, 500.f));
			Log.Check(FMath::Abs(Yaw - Want) < 2.f, FString::Printf(TEXT("melee: charging turns him onto the enemy (yaw %.1f, want %.1f)"), Yaw, Want));
			Foe->FactionId = TEXT("continental");
			Flank->FactionId = TEXT("continental");
			Log.Check(Me->MeleeComponent->FindChargeTarget() == nullptr, TEXT("melee: no assist onto friends"));
		}
		else
		{
			Log.Check(false, TEXT("melee: spawn charge-assist soldiers"));
		}
	}

	// Lunge timing: coil first, full extension exactly when the hit is resolved, then hold and recover.
	{
		using C = UPA250MeleeComponent;
		const float Wu = V.ThrustWindupSeconds;
		const FPA250MeleePose Coiled = C::ThrustPose(C::CoilFraction * Wu - 0.001f, Wu, 0.1f);
		const FPA250MeleePose Landed = C::ThrustPose(Wu, Wu, 0.1f);
		const FPA250MeleePose Held = C::ThrustPose(Wu + 0.09f, Wu, 0.1f);
		const FPA250MeleePose Done = C::ThrustPose(Wu + 0.1f + C::ThrustRecoverSeconds + 0.01f, Wu, 0.1f);
		Log.Check(!C::ThrustPose(0.f, Wu, 0.1f).IsActive() && Coiled.Coil > 0.95f && Coiled.Lunge == 0.f, TEXT("melee: a thrust starts from rest and coils first"));
		Log.Check(Landed.Lunge == 1.f && Landed.Coil == 0.f && Held.Lunge == 1.f, TEXT("melee: the lunge is at full extension when the hit lands, and holds"));
		Log.Check(!Done.IsActive(), TEXT("melee: the lunge recovers to rest"));
		Log.Check(C::ButtPose(0.1f).Butt == 1.f && !C::ButtPose(C::ButtSeconds).IsActive(), TEXT("melee: butt-stroke snaps out and comes back"));
		Log.Check(C::ChargePose(0.3f).Level == 1.f && C::StrikePose(C::StrikeDriveSeconds, 0.2f).Lunge == 1.f
			&& !C::StrikePose(C::StrikeDriveSeconds + 0.2f + C::StrikeRecoverSeconds, 0.2f).IsActive(), TEXT("melee: charge carries the bayonet levelled, drives it home on contact, recovers"));
		Log.Check(C::WhiffPose(0.25f, 0.5f).Lunge > 0.5f && !C::WhiffPose(0.5f, 0.5f).IsActive(), TEXT("melee: a missed charge stumbles on and recovers"));
		Log.Check(C::FlinchWeight(0.05f, 0.6f) == 1.f && C::FlinchWeight(0.3f, 0.6f) < 0.5f && C::FlinchWeight(0.6f, 0.6f) == 0.f, TEXT("melee: flinch snaps on and eases out"));
	}

	// Thrust assist and hit reaction on real soldiers.
	{
		ARFCharacter* Me = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 3000.f, 100.f), FRotator::ZeroRotator);
		ARFCharacter* Foe = W.World->SpawnActor<ARFCharacter>(FVector(300.f, 3100.f, 100.f), FRotator(0.f, 180.f, 0.f));
		Log.Check(Me && Foe && Me->MeleeComponent && Foe->MeleeComponent, TEXT("melee: spawn thrust pair"));
		if (Me && Foe && Me->MeleeComponent && Foe->MeleeComponent)
		{
			Me->FactionId = TEXT("continental");
			Foe->FactionId = TEXT("british");
			Me->MeleeComponent->Tuning = T;
			Foe->MeleeComponent->Tuning = T;
			Log.Check(Me->MeleeComponent->FindThrustTarget() == Foe, TEXT("melee: thrust assist finds the man in front within 3.8 m"));
			Log.Check(Me->MeleeComponent->DoThrust() && Me->MeleeComponent->GetVisual() == EPA250MeleeVisual::Thrust, TEXT("melee: thrust starts the lunge"));
			const float Want = FMath::RadiansToDegrees(FMath::Atan2(100.f, 300.f));
			Log.Check(FMath::Abs(Me->GetActorRotation().Yaw - Want) < 2.f, TEXT("melee: the lunge turns onto its man"));

			// The first point home wins: a blow breaks the other man's wind-up and rocks him.
			Log.Check(Foe->MeleeComponent->DoThrust(), TEXT("melee: foe winds up"));
			Foe->MeleeComponent->ReceiveHit(Me->GetActorForwardVector(), 0.8f, V.ThrustHitStaggerSeconds);
			Log.Check(Foe->MeleeComponent->IsFlinching() && Foe->MeleeComponent->GetVisual() == EPA250MeleeVisual::None && Foe->MeleeComponent->IsBusy(),
				TEXT("melee: a blow that lands breaks the victim's wind-up and makes him flinch"));
			Log.Check(Foe->IsMovementLocked() && FMath::IsNearlyEqual(Foe->GetMovementLockRemaining(), V.ThrustHitStaggerSeconds),
				TEXT("melee: a bayoneted man is stopped for 0.35 s"));
			Me->Destroy();
			Foe->Destroy();
		}
	}

	// Guard, parry, riposte and the no-one-blow-kill rule.
	{
		ARFCharacter* Me = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 6000.f, 100.f), FRotator::ZeroRotator);
		Log.Check(Me && Me->MeleeComponent && Me->HealthComponent, TEXT("melee: spawn guard soldier"));
		if (Me && Me->MeleeComponent && Me->HealthComponent)
		{
			UPA250MeleeComponent* M = Me->MeleeComponent;
			M->Tuning = T;
			const FVector Front = Me->GetActorLocation() + FVector(200.f, 0.f, 0.f), Behind = Me->GetActorLocation() - FVector(200.f, 0.f, 0.f);
			Log.Check(M->ResolveGuard(Front) == EPA250Guard::None, TEXT("guard: no guard, nothing is blocked"));
			M->SetGuardHeld(true);
			M->TickComponent(0.05f, LEVELTICK_All, nullptr);
			Log.Check(M->IsGuarding() && M->ResolveGuard(Front) == EPA250Guard::Parry, TEXT("guard: raised 0.05 s ago, a frontal blow is parried"));
			Log.Check(M->ResolveGuard(Behind) == EPA250Guard::None, TEXT("guard: nothing covers his back"));
			M->TickComponent(0.5f, LEVELTICK_All, nullptr);
			Log.Check(M->ResolveGuard(Front) == EPA250Guard::Block && M->GetBodyPose().Guard > 0.9f, TEXT("guard: held longer than the parry window it only blocks; the body is in the guard pose"));
			const bool bThrust = M->DoThrust();
			M->TickComponent(0.02f, LEVELTICK_All, nullptr);
			Log.Check(bThrust && !M->IsGuarding(), TEXT("guard: thrusting drops the guard"));
			M->SetGuardHeld(false);

			URFHealthComponent* H = Me->HealthComponent;
			H->MaxHealth = H->CurrentHealth = 100.f;
			H->MinHealthAfterNextDamage = V.FrontalBlowLeavesHealth;
			H->OnTakeAnyDamage(Me, 300.f, nullptr, nullptr, nullptr);
			Log.Check(H->CurrentHealth == V.FrontalBlowLeavesHealth && !H->IsDead(), TEXT("melee: a frontal blow on a fit man leaves him 15 health, however it multiplies (was 300 = dead)"));
			H->OnTakeAnyDamage(Me, 70.f, nullptr, nullptr, nullptr);
			Log.Check(H->IsDead(), TEXT("melee: the floor is spent after one blow; the next one kills"));
			Log.Check(UPA250MeleeComponent::IsBehind(FVector(1, 0, 0), FVector(-1, 0.2f, 0), V) && !UPA250MeleeComponent::IsBehind(FVector(1, 0, 0), FVector(0, 1, 0), V),
				TEXT("melee: only a blow from behind (>120 deg) may kill outright"));
			Log.Check(V.ThrustWindupVsPlayerSeconds > V.ThrustWindupSeconds + 0.5f * V.ParryWindowSeconds, TEXT("melee: a bot's thrust at the player is telegraphed longer than his own"));
			Me->Destroy();
		}
	}

	// Clubbed musket: who has no bayonet, and what E does then.
	{
		using C = UPA250MeleeComponent;
		Log.Check(C::IssuedBayonet(TEXT("british"), TEXT("line_infantry"), 7) && C::IssuedBayonet(TEXT("british"), TEXT("militia"), 7), TEXT("club: every British soldier has a bayonet"));
		Log.Check(!C::IssuedBayonet(TEXT("continental"), TEXT("militia"), 4) && !C::IssuedBayonet(TEXT("continental"), TEXT("militia"), 0), TEXT("club: Patriot militia have none"));
		int32 With = 0;
		for (uint32 Seed = 1; Seed <= 400; ++Seed) With += C::IssuedBayonet(TEXT("continental"), TEXT("line_infantry"), Seed) ? 1 : 0;
		Log.Check(With == 100, TEXT("club: one Continental line soldier in four has a bayonet, fixed per man"));
		const float Cw = V.ClubWindupSeconds;
		Log.Check(C::ClubPose(C::CoilFraction * Cw - 0.001f, Cw, 0.1f).Raise > 0.95f && C::ClubPose(Cw, Cw, 0.1f).Chop == 1.f && C::ClubPose(Cw, Cw, 0.1f).Raise == 0.f
			&& !C::ClubPose(Cw + 0.1f + C::ThrustRecoverSeconds + 0.01f, Cw, 0.1f).IsActive(), TEXT("club: raised first, down at full weight when the hit lands, then recovers"));
		Log.Check(V.ClubWindupSeconds > V.ThrustWindupSeconds && V.ClubReachCm < V.ThrustReachCm && V.ClubDamage < V.ThrustDamage && V.ClubStaggerSeconds > V.ThrustHitStaggerSeconds,
			TEXT("club: slower, shorter and weaker than the bayonet, but it stops a man longer"));
		ARFCharacter* Farmer = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 9000.f, 100.f), FRotator::ZeroRotator);
		Log.Check(Farmer && Farmer->MeleeComponent, TEXT("club: spawn militiaman"));
		if (Farmer && Farmer->MeleeComponent)
		{
			Farmer->FactionId = TEXT("continental");
			Farmer->UnitClass = TEXT("militia");
			Farmer->MeleeComponent->Tuning = T;
			Log.Check(!Farmer->MeleeComponent->HasBayonet() && Farmer->MeleeComponent->DoThrust() && Farmer->MeleeComponent->IsClubSwing(), TEXT("club: a militiaman's E is the clubbed swing"));
			Farmer->MeleeComponent->TickComponent(C::CoilFraction * Cw, LEVELTICK_All, nullptr);
			Log.Check(Farmer->MeleeComponent->GetBodyPose().Raise > 0.9f, TEXT("club: mid wind-up the musket is up over his shoulder"));
			Farmer->MeleeComponent->SetBayonetOverride(true);
			Log.Check(Farmer->MeleeComponent->HasBayonet(), TEXT("club: bayonet can be forced for tests"));
			Farmer->Destroy();
		}
	}

	// Squad charge orders: the same rules for every side (no faction input).
	{
		const float R = 3500.f;
		Log.Check(PA250ShouldOrderCharge(EPA250Morale::Steady, false, 2500.f, R, false, 0.f, 1.f, 8, 6), TEXT("melee: a steady line that outnumbers the enemy nearby charges"));
		Log.Check(PA250ShouldOrderCharge(EPA250Morale::Steady, false, 2500.f, R, false, 0.7f, 0.2f, 6, 6), TEXT("melee: a line with empty muskets charges an enemy caught reloading"));
		Log.Check(!PA250ShouldOrderCharge(EPA250Morale::Steady, false, 2500.f, R, false, 0.2f, 0.9f, 6, 6), TEXT("melee: an even, loaded fight is settled by volleys"));
		Log.Check(!PA250ShouldOrderCharge(EPA250Morale::Shaken, false, 2500.f, R, true, 1.f, 0.f, 9, 3), TEXT("melee: a shaken squad does not charge"));
		Log.Check(!PA250ShouldOrderCharge(EPA250Morale::Steady, false, 5000.f, R, true, 1.f, 0.f, 9, 3), TEXT("melee: no charge from beyond 35 m"));
		Log.Check(PA250ShouldOrderCharge(EPA250Morale::Steady, true, 2500.f, R, true, 0.f, 1.f, 3, 8), TEXT("melee: militia charge a wavering enemy"));
		Log.Check(!PA250ShouldOrderCharge(EPA250Morale::Steady, true, 2500.f, R, false, 0.7f, 0.2f, 6, 6)
			&& PA250ShouldOrderCharge(EPA250Morale::Steady, true, 2500.f, R, false, 0.7f, 0.2f, 8, 6),
			TEXT("melee: militia charge a reloading enemy only when they also outnumber him"));
		Log.Check(FMath::IsNearlyEqual(PA250MoralePressure(0.f, 0.f, 1.f, false, false, true) - PA250MoralePressure(0.f, 0.f, 1.f, false, false, false), 0.3f),
			TEXT("melee: being charged adds morale pressure"));
		UPA250CombatProfiles* Pr = UPA250CombatProfiles::MakeRuntimeDefaults();
		Log.Check(!Pr->RoleFor(TEXT("british"), TEXT("line_infantry")).bCautiousCharge && !Pr->RoleFor(TEXT("continental"), TEXT("line_infantry")).bCautiousCharge,
			TEXT("melee: British and Continental line troops charge under the same rules"));
	}
	Musket->Destroy();
	Soldier->Destroy();
}

void RunConquest(FLog& Log)
{
	const FName B(TEXT("british")), C(TEXT("continental")), Line(TEXT("line_infantry"));
	FPA250TicketLedger L;
	L.Pool.Add(B, 20);
	L.Pool.Add(C, 20);
	L.SpendDeath(B, Line);
	Log.Check(L.Remaining(B) == 19 && L.Deaths.FindRef(B).FindRef(Line) == 1, TEXT("conquest: -1 ticket per death"));

	for (int32 s = 0; s < 25; ++s) L.Bleed(1.f, 5.f, B, 0, C, 2);
	Log.Check(L.Remaining(B) == 14 && L.Remaining(C) == 20 && L.BleedTicks == 5, TEXT("conquest: fewer posts bleeds 1 per 5 s"));
	Log.Check(L.Deaths.FindRef(B).FindRef(Line) == 1, TEXT("conquest: bleed is not a death"));

	for (int32 s = 0; s < 4; ++s) L.Bleed(1.f, 5.f, B, 0, C, 1);
	L.Bleed(1.f, 5.f, B, 1, C, 1);
	for (int32 s = 0; s < 4; ++s) L.Bleed(1.f, 5.f, B, 0, C, 1);
	Log.Check(L.Remaining(B) == 14, TEXT("conquest: equal posts reset the bleed clock"));
	for (int32 s = 0; s < 10; ++s) L.Bleed(1.f, 5.f, B, 2, C, 1);
	Log.Check(L.Remaining(C) == 18 && L.Remaining(B) == 14, TEXT("conquest: bleed follows the side holding fewer posts"));

	// Scaled bleed (Battlefront-style): 1 per post of difference, +1 when one side holds every post.
	Log.Check(FPA250TicketLedger::BleedAmount(1, 2, 3) == 1 && FPA250TicketLedger::BleedAmount(0, 2, 3) == 2
		&& FPA250TicketLedger::BleedAmount(0, 3, 3) == 4 && FPA250TicketLedger::BleedAmount(3, 0, 3) == 4
		&& FPA250TicketLedger::BleedAmount(2, 2, 3) == 0 && FPA250TicketLedger::BleedAmount(0, 2, 0) == 1,
		TEXT("conquest: bleed per 5 s = posts of difference, +1 for holding them all (1 / 2 / 4)"));
	{
		FPA250TicketLedger All;
		All.Pool.Add(B, 96);
		for (int32 s2 = 0; s2 < 60; ++s2) All.Bleed(1.f, 5.f, B, 0, C, 3, 3);
		Log.Check(All.Remaining(B) == 96 - 48, FString::Printf(TEXT("conquest: holding all 3 posts drains 48 enemy tickets a minute (%d left of 96)"), All.Remaining(B)));
		FPA250TicketLedger Low;
		Low.Pool.Add(B, 3);
		for (int32 s2 = 0; s2 < 5; ++s2) Low.Bleed(1.f, 5.f, B, 0, C, 3, 3);
		Log.Check(Low.Remaining(B) == 0 && Low.BleedTicks == 3, TEXT("conquest: bleed never goes below zero"));
	}

	FPA250TicketLedger Empty;
	Empty.Pool.Add(B, 1);
	Empty.SpendDeath(B, Line);
	Empty.SpendDeath(B, Line);
	Log.Check(Empty.Remaining(B) == 0, TEXT("conquest: tickets floor at 0"));

	// Screen clutter and view feedback (pure rules).
	Log.Check(!ARFCharacter::ShouldShowNameplate(true, false, 3000.f, true) && ARFCharacter::ShouldShowNameplate(true, false, 1200.f, true)
		&& !ARFCharacter::ShouldShowNameplate(true, false, 1200.f, false), TEXT("look: an enemy is labelled only inside 15 m and in sight"));
	Log.Check(ARFCharacter::ShouldShowNameplate(false, true, 2400.f, false) && !ARFCharacter::ShouldShowNameplate(false, false, 2400.f, true)
		&& ARFCharacter::ShouldShowNameplate(false, false, 1000.f, true), TEXT("look: squadmates are labelled to 25 m, other friends to 12 m"));
	Log.Check(ARFCharacter::NameplateLabel(TEXT("british"), TEXT("line_infantry")).ToString() == TEXT("Regular")
		&& ARFCharacter::NameplateLabel(TEXT("continental"), TEXT("militia")).ToString() == TEXT("Militia")
		&& ARFCharacter::NameplateLabel(TEXT("continental"), TEXT("line_infantry")).ToString() == TEXT("Provincial"), TEXT("look: labels are words, not faction ids"));
	Log.Check(ARFCharacter::ViewStress(1.f, false, 0.f) == 0.f, TEXT("look: an unhurt man not under fire sees the field as it is"));
	Log.Check(ARFCharacter::ViewStress(1.f, true, 0.f) > 0.2f && ARFCharacter::ViewStress(0.15f, false, 0.f) > ARFCharacter::ViewStress(0.6f, false, 0.f)
		&& ARFCharacter::ViewStress(1.f, false, 1.f) >= 0.6f && ARFCharacter::ViewStress(0.f, true, 1.f) == 1.f,
		TEXT("look: the view closes in under fire, more when badly hurt, most on a fresh hit, never past 1"));
	Log.Check(UPA250BattleHUD::LegendFade(0.f) == 1.f && UPA250BattleHUD::LegendFade(25.f) == 1.f && FMath::IsNearlyEqual(UPA250BattleHUD::LegendFade(27.5f), 0.5f)
		&& UPA250BattleHUD::LegendFade(40.f) == 0.f, TEXT("look: the controls legend steps back after the first half-minute"));
	Log.Check(FPA250BattleAudio::Pick(TEXT("footstep_grass")) != nullptr
		&& LoadObject<USoundBase>(nullptr, TEXT("/Game/PA250/Audio/SFX/ambience_dawn.ambience_dawn")) != nullptr, TEXT("look: footsteps and the dawn ambience are imported"));

	// Losing on tickets: never while the human player is alive on that side.
	Log.Check(APA250BattleGameMode::SideIsOut(0, 6, true, 5, false), TEXT("exhaustion: 0 tickets = out, even with men standing"));
	Log.Check(!APA250BattleGameMode::SideIsOut(0, 6, true, 1, true), TEXT("exhaustion: not out while the player is alive on that side"));
	Log.Check(!APA250BattleGameMode::SideIsOut(3, 6, true, 0, false), TEXT("exhaustion: tickets left = not out"));
	Log.Check(!APA250BattleGameMode::SideIsOut(0, 0, false, 0, false) && APA250BattleGameMode::SideIsOut(0, 0, true, 0, false)
		&& !APA250BattleGameMode::SideIsOut(0, 0, true, 2, false), TEXT("exhaustion: a side that started with no tickets is out only once fielded and wiped"));

	// One ticket held for the human player.
	{
		FPA250TicketLedger H;
		H.Pool.Add(B, 3);
		H.Held.Add(B, 1);
		Log.Check(H.CanReinforce(B) && H.HeldFor(B) == 1 && !H.DrawHeld(B), TEXT("reserve: with spare tickets bots are reinforced and the held one is not drawn"));
		for (int32 i = 0; i < 5; ++i) H.SpendDeath(B, Line);
		Log.Check(H.Remaining(B) == 1 && H.Deaths.FindRef(B).FindRef(Line) == 5, TEXT("reserve: deaths cannot spend the held ticket (but are still counted)"));
		Log.Check(!H.CanReinforce(B), TEXT("reserve: bots are not reinforced from the held ticket"));
		for (int32 s3 = 0; s3 < 30; ++s3) H.Bleed(1.f, 5.f, B, 0, C, 3, 3);
		Log.Check(H.Remaining(B) == 1, TEXT("reserve: bleed cannot spend the held ticket"));
		Log.Check(H.DrawHeld(B) && H.HeldFor(B) == 0 && H.Remaining(B) == 1 && !H.DrawHeld(B), TEXT("reserve: the human draws it once; it is then an ordinary ticket"));
		H.SpendDeath(B, Line);
		Log.Check(H.Remaining(B) == 0 && !H.CanReinforce(B), TEXT("reserve: once drawn, the next death spends it"));
		FPA250TicketLedger None;
		None.Pool.Add(B, 0);
		None.Held.Add(B, 1);
		Log.Check(None.HeldFor(B) == 0 && !None.CanReinforce(B) && !None.DrawHeld(B), TEXT("reserve: nothing is held from an empty pool"));
	}

	Log.Check(FPA250TicketLedger::LossesInUnits(9, 4, 5) == 2, TEXT("conquest: 9 deaths / 4 per unit -> 2 units"));
	Log.Check(FPA250TicketLedger::LossesInUnits(100, 4, 5) == 5, TEXT("conquest: losses capped at starting units"));
	Log.Check(FPA250TicketLedger::LossesInUnits(1, 4, 5) == 0, TEXT("conquest: one death rounds to 0 units"));
	// 5.f above is the ledger's interval argument, not the shipped bleed. Exhaustion waits on elapsed time.
	Log.Check(!APA250BattleGameMode::TicketsMayExhaust(119.f, 120.f) &&
			  APA250BattleGameMode::TicketsMayExhaust(120.f, 120.f),
		TEXT("conquest: ticket exhaustion waits until MinSecondsBeforeExhaustion"));
	Log.Check(GetDefault<APA250BattleGameMode>()->BleedIntervalSeconds == 12.f &&
			  GetDefault<APA250BattleGameMode>()->MinSecondsBeforeExhaustion == 120.f,
		TEXT("conquest: C++ defaults are 12s bleed and a 120s exhaustion gate"));
	if (UMusketTuning* DA = LoadObject<UMusketTuning>(nullptr, TEXT("/Game/PA250/Data/DA_MusketTuning.DA_MusketTuning")))
	{
		Log.Check(FMath::IsNearlyEqual(DA->Values.Conquest.BleedIntervalSeconds, 12.f),
			TEXT("conquest: DA_MusketTuning bleed interval is 12s, not the retired 5s"));
		Log.Check(DA->Values.Conquest.MinSecondsBeforeExhaustion >= 120.f,
			TEXT("conquest: DA_MusketTuning exhaustion gate is at least 120s"));
	}
	else
	{
		Log.Check(false, TEXT("conquest: DA_MusketTuning loads"));
	}

	const float R1 = APA250ControlPoint::CaptureRate(1, 0, 0.35f, 0.35f);
	const float R3 = APA250ControlPoint::CaptureRate(3, 0, 0.35f, 0.35f);
	const float R3d2 = APA250ControlPoint::CaptureRate(3, 2, 0.35f, 0.35f);
	Log.Check(R3 > R1, TEXT("conquest: more attackers capture faster"));
	Log.Check(R3d2 < R3, TEXT("conquest: defenders slow capture"));
	Log.Check(APA250ControlPoint::CaptureRate(0, 3, 0.35f, 0.35f) == 0.f, TEXT("conquest: no attackers, no capture"));
	Log.Check(GetDefault<APA250ControlPoint>()->Radius == 1000.f, TEXT("conquest: 10 m capture radius"));

	// Flag: held = holder's flag at the top; enemy neutralising lowers it; neutral post raises the capturer's.
	{
		FName F; float H;
		const FName Brit(TEXT("british")), Cont(TEXT("continental"));
		APA250ControlPoint::FlagTarget(Cont, NAME_None, 0.f, F, H);
		Log.Check(F == Cont && H == 1.f, TEXT("flag: held post flies the holder's flag at the top"));
		APA250ControlPoint::FlagTarget(Cont, Brit, 0.25f, F, H);
		Log.Check(F == Cont && FMath::IsNearlyEqual(H, 0.75f), TEXT("flag: enemy neutralising lowers the holder's flag"));
		APA250ControlPoint::FlagTarget(NAME_None, Brit, 0.6f, F, H);
		Log.Check(F == Brit && FMath::IsNearlyEqual(H, 0.6f), TEXT("flag: capturer's flag rises on a neutral post"));
		APA250ControlPoint::FlagTarget(NAME_None, NAME_None, 0.f, F, H);
		Log.Check(F.IsNone() && H == 0.f, TEXT("flag: neutral, uncontested post is a bare pole"));
		APA250ControlPoint::FlagTarget(Brit, Brit, 0.5f, F, H);
		Log.Check(F == Brit && H == 1.f, TEXT("flag: holder in the zone keeps the flag up"));
		Log.Check(APA250ControlPoint::DisplayName(TEXT("green")) == TEXT("Lexington Green"), TEXT("flag: posts have readable names"));
		// Artwork: Union flag = red St George centre, blue field corner, white saltire on the diagonal; Pine Tree = green tree on white.
		const FLinearColor UC = PA250FlagArt::Pixel(Brit, 0.5f, 0.5f), UCorner = PA250FlagArt::Pixel(Brit, 0.03f, 0.2f), UDiag = PA250FlagArt::Pixel(Brit, 0.2f, 0.8f);
		Log.Check(UC.R > 0.5f && UC.B < 0.1f && UCorner.B > UCorner.R && UDiag.R > 0.8f && UDiag.B > 0.8f, TEXT("flag art: British Union flag (cross, saltire, blue field)"));
		const FLinearColor PC = PA250FlagArt::Pixel(Cont, 0.5f, 0.4f), PCorner = PA250FlagArt::Pixel(Cont, 0.05f, 0.1f);
		Log.Check(PC.G > PC.R && PC.G > PC.B && PCorner.R > 0.8f && PCorner.G > 0.8f, TEXT("flag art: Pine Tree flag (green tree on white)"));
		Log.Check(PA250FlagArt::GetTexture(Brit) != nullptr && PA250FlagArt::GetTexture(Cont) != nullptr, TEXT("flag art: textures build"));
		// Cloth: tied at the pole, moves at the fly, stays within a sane envelope, changes over time.
		bool bTied = true, bBounded = true;
		for (float T = 0.f; T < 3.f; T += 0.1f)
		{
			bTied &= PA250FlagArt::ClothOffset(0.f, 0.5f, T, 180.f).IsNearlyZero(0.01f);
			bBounded &= PA250FlagArt::ClothOffset(1.f, 0.5f, T, 180.f).Size() < 45.f;
		}
		const bool bMoves = !PA250FlagArt::ClothOffset(1.f, 0.5f, 0.f, 180.f).Equals(PA250FlagArt::ClothOffset(1.f, 0.5f, 0.3f, 180.f), 1.f);
		Log.Check(bTied && bBounded && bMoves, TEXT("flag cloth: tied at the pole, waves at the fly, bounded"));
		Log.Check(UPA250BattleHUD::PostOrder(Brit, Cont, NAME_None, 0.f, false) == TEXT("ATTACK")
			&& UPA250BattleHUD::PostOrder(Brit, NAME_None, NAME_None, 0.f, false) == TEXT("ATTACK"), TEXT("marker: enemy and neutral posts say ATTACK"));
		Log.Check(UPA250BattleHUD::PostOrder(Brit, Brit, NAME_None, 0.f, false).IsEmpty(), TEXT("marker: a secure post of ours shows no order"));
		Log.Check(UPA250BattleHUD::PostOrder(Brit, Brit, Cont, 0.3f, false) == TEXT("DEFEND"), TEXT("marker: our post under capture says DEFEND"));
		Log.Check(UPA250BattleHUD::PostOrder(Brit, Cont, Brit, 0.3f, false) == TEXT("CAPTURING"), TEXT("marker: our capture in progress says CAPTURING"));
	}
}

void RunSpacing(FLog& Log)
{
	FScopedTestWorld W;
	APA250ControlPoint* Post = W.World ? W.World->SpawnActor<APA250ControlPoint>(FVector::ZeroVector, FRotator::ZeroRotator) : nullptr;
	Log.Check(Post != nullptr, TEXT("spacing: spawn post"));
	if (Post)
	{
		Post->BuildSlotRing(48, 600.f, 2500.f);
		const TArray<FPA250PostSlot>& Slots = Post->GetSlots();
		Log.Check(Slots.Num() == 48, FString::Printf(TEXT("spacing: 48 ring slots (%d)"), Slots.Num()));
		float MinR = TNumericLimits<float>::Max(), MaxR = 0.f, MinGap = TNumericLimits<float>::Max();
		TArray<int32> PerSector;
		PerSector.Init(0, APA250ControlPoint::NumSectors);
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const float R = FVector::Dist2D(Slots[i].Location, FVector::ZeroVector);
			MinR = FMath::Min(MinR, R);
			MaxR = FMath::Max(MaxR, R);
			PerSector[Slots[i].Sector]++;
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Slots[i].Location.Y, Slots[i].Location.X));
			const int32 Expected = FMath::FloorToInt(FMath::Fmod(Angle + 360.01f, 360.f) / 45.f); // +0.01 deg: boundary slots sit exactly on 45 deg multiples
			if (Expected != Slots[i].Sector)
			{
				Log.Check(false, FString::Printf(TEXT("spacing: slot %d angle %.1f in sector %d"), i, Angle, Slots[i].Sector));
			}
			for (int32 j = i + 1; j < Slots.Num(); ++j)
			{
				MinGap = FMath::Min(MinGap, FVector::Dist2D(Slots[i].Location, Slots[j].Location));
			}
		}
		Log.Check(FMath::IsNearlyEqual(MinR, 600.f, 1.f) && FMath::IsNearlyEqual(MaxR, 2500.f, 1.f),
			FString::Printf(TEXT("spacing: ring spans 6-25 m (%.0f-%.0f)"), MinR, MaxR));
		Log.Check(MinGap >= 200.f, FString::Printf(TEXT("spacing: >= 2 m between slots (%.0f cm)"), MinGap));
		bool bEven = true;
		for (int32 N : PerSector) bEven &= N == 6;
		Log.Check(bEven, TEXT("spacing: each octant holds 6 slots (one arc per squad)"));

		AActor* A = W.World->SpawnActor<AActor>();
		AActor* Bb = W.World->SpawnActor<AActor>();
		const int32 S3 = Post->FindFreeSlot(3);
		Log.Check(Slots.IsValidIndex(S3) && Slots[S3].Sector == 3, TEXT("spacing: free slot comes from the asked sector"));
		Log.Check(Post->ClaimSlot(S3, A) && !Post->ClaimSlot(S3, Bb), TEXT("spacing: never two bots on one slot"));
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (Slots[i].Sector == 3) Post->ClaimSlot(i, A);
		}
		const int32 Spill = Post->FindFreeSlot(3);
		Log.Check(Slots.IsValidIndex(Spill) && APA250ControlPoint::SectorDistance(Slots[Spill].Sector, 3) == 1,
			TEXT("spacing: full sector spills into a neighbouring octant"));
		const int32 Wrap = Post->FindFreeSlot(7);
		Log.Check(Slots.IsValidIndex(Wrap) && Slots[Wrap].Sector == 7, TEXT("spacing: sector 7 is its own arc"));
		Post->ReleaseSlot(A);
		Log.Check(Post->FindFreeSlot(3) != INDEX_NONE && Slots[Post->FindFreeSlot(3)].Sector == 3, TEXT("spacing: release frees the claims"));
	}

	Log.Check(APA250ControlPoint::SectorDistance(7, 0) == 1 && APA250ControlPoint::SectorDistance(0, 4) == 4, TEXT("spacing: sector distance wraps"));
	bool bApart = true;
	for (int32 a = 0; a < 3; ++a)
	{
		for (int32 b = a + 1; b < 3; ++b)
		{
			bApart &= APA250ControlPoint::SectorDistance(UPA250VolleyCoordinator::SectorForSquad(a), UPA250VolleyCoordinator::SectorForSquad(b)) >= 2;
		}
	}
	Log.Check(bApart, TEXT("spacing: three squads on one post sit >= 90 deg apart (>= 15 m at mid ring)"));
	Log.Check(UPA250VolleyCoordinator::EngagementBucket(1500.f) == 0 && UPA250VolleyCoordinator::EngagementBucket(3000.f) == 1
		&& UPA250VolleyCoordinator::EngagementBucket(6000.f) == 2 && UPA250VolleyCoordinator::EngagementBucket(8000.f) == 2
		&& UPA250VolleyCoordinator::EngagementBucket(10000.f) == 3 && UPA250VolleyCoordinator::EngagementBucket(15000.f) == 4,
		TEXT("spacing: engagement histogram buckets"));

	// Formation: two staggered ranks, 2 m files - nobody stands on top of anybody.
	for (const int32 N : {3, 6, 8})
	{
		TArray<FVector2D> Offs;
		UPA250VolleyCoordinator::FormationOffsets(N, 200.f, 200.f, 6, Offs);
		float MinNN = TNumericLimits<float>::Max();
		int32 MaxClose = 0;
		float MaxBack = 0.f;
		for (int32 i = 0; i < Offs.Num(); ++i)
		{
			int32 Close = 0;
			MaxBack = FMath::Max(MaxBack, Offs[i].Y);
			for (int32 j = 0; j < Offs.Num(); ++j)
			{
				if (i == j) continue;
				const float D = FVector2D::Distance(Offs[i], Offs[j]);
				MinNN = FMath::Min(MinNN, D);
				if (D < 300.f) ++Close;
			}
			MaxClose = FMath::Max(MaxClose, Close);
		}
		const int32 Ranks = FMath::RoundToInt(MaxBack / 200.f) + 1;
		Log.Check(Offs.Num() == N && MinNN >= 199.f && MaxClose <= 4,
			FString::Printf(TEXT("spacing: %d men in line - nearest %.0f cm, at most %d within 3 m"), N, MinNN, MaxClose));
		Log.Check(Ranks == (N < 4 ? 1 : 2), FString::Printf(TEXT("spacing: %d men stand in %d rank(s)"), N, Ranks));
		if (N >= 4)
		{
			int32 Front = 0, Rear = 0;
			for (const FVector2D& O : Offs) { if (O.Y < 1.f) ++Front; else ++Rear; }
			Log.Check(Front > 0 && Rear > 0, FString::Printf(TEXT("spacing: %d men keep two ranks (front=%d rear=%d)"), N, Front, Rear));
		}
	}

	// Coordinated volley gates (pure): ready fraction, max-wait, epoch cancel.
	Log.Check(!UPA250VolleyCoordinator::VolleyReadyToCall(5, 8, 0.7f, 0.f, 6.f),
		TEXT("volley: 5/8 safe is below 70% — wait"));
	Log.Check(UPA250VolleyCoordinator::VolleyReadyToCall(6, 8, 0.7f, 0.f, 6.f),
		TEXT("volley: 6/8 safe meets 70% — call"));
	Log.Check(UPA250VolleyCoordinator::VolleyReadyToCall(1, 8, 0.7f, 6.f, 6.f),
		TEXT("volley: one safe shooter after max wait — call (no deadlock)"));
	Log.Check(!UPA250VolleyCoordinator::VolleyReadyToCall(0, 8, 0.7f, 99.f, 6.f),
		TEXT("volley: zero safe shooters never calls, even after max wait"));
	Log.Check(UPA250VolleyCoordinator::ShouldDischargePending(3, 3, true, true, true, true),
		TEXT("volley: matching epoch fires"));
	Log.Check(!UPA250VolleyCoordinator::ShouldDischargePending(4, 3, true, true, true, true),
		TEXT("volley: stale epoch after cancel does not fire"));
	Log.Check(!UPA250VolleyCoordinator::ShouldDischargePending(3, 3, true, false, true, true),
		TEXT("volley: unloaded pending shot sits out"));
	Log.Check(!UPA250VolleyCoordinator::ShouldDischargePending(3, 3, true, true, false, true),
		TEXT("volley: blocked lane pending shot sits out"));
	Log.Check(!UPA250VolleyCoordinator::ShouldDischargePending(3, 3, true, true, true, false),
		TEXT("volley: pending shot at a dead target does not fire"));

	FString Reason;
	Log.Check(APA250BotSpacingTest::EvaluateSnapshot(250.f, 0.05f, 3, 3, 8, 0.75f, 200.f, 0.10f, 3, 5, 0.70f, Reason), TEXT("spacing: FTEST pass case"));
	Log.Check(!APA250BotSpacingTest::EvaluateSnapshot(250.f, 0.05f, 3, 3, 2, 0.75f, 200.f, 0.10f, 3, 5, 0.70f, Reason), TEXT("spacing: FTEST fails on too few kills"));
	Log.Check(!APA250BotSpacingTest::EvaluateSnapshot(120.f, 0.05f, 3, 3, 8, 0.75f, 200.f, 0.10f, 3, 5, 0.70f, Reason), TEXT("spacing: FTEST fails on clumping"));
	Log.Check(!APA250BotSpacingTest::EvaluateSnapshot(250.f, 0.05f, 3, 3, 8, 0.20f, 200.f, 0.10f, 3, 5, 0.70f, Reason), TEXT("spacing: FTEST fails off the engage band"));
}

namespace
{
struct FAIReloadRun
{
	float Seconds = 0.f;
	int32 Fumbles = 0;
	int32 StepsSeen = 0;
	bool bBonus = false;
};

/** One AI reload on the Arcade preset, driven only through the planner (which uses TryCommit). */
FAIReloadRun SimAIReload(const FActiveReloadTuningPlain& Weapon, const FPA250AIReloadParams& P, const FPA250AIReloadConditions& C, int32 Seed)
{
	FActiveReloadSequencer S;
	S.Configure(Weapon);
	S.BeginReload();
	FPA250AIReloadPlanner Planner;
	FRandomStream Rng(Seed);
	FAIReloadRun R;
	TSet<int32> Steps;
	const float Dt = 0.005f;
	while (!S.IsComplete() && R.Seconds < 120.f)
	{
		const EActiveReloadPhase Ph = S.GetPhase();
		if (Ph == EActiveReloadPhase::PrimePan || Ph == EActiveReloadPhase::RamCharge || Ph == EActiveReloadPhase::ShoulderAim) Steps.Add((int32)Ph);
		if (Planner.Tick(S, P, C, Rng) == EActiveReloadHit::Miss) ++R.Fumbles;
		S.Tick(Dt);
		R.Seconds += Dt;
	}
	R.StepsSeen = Steps.Num();
	R.bBonus = S.PeekDamageBonus() > 0.f;
	return R;
}

void MeanStd(const TArray<float>& V, float& OutMean, float& OutStd)
{
	double Sum = 0.0, Sq = 0.0;
	for (float X : V) { Sum += X; Sq += double(X) * X; }
	OutMean = V.Num() ? float(Sum / V.Num()) : 0.f;
	OutStd = V.Num() ? FMath::Sqrt(FMath::Max(0.f, float(Sq / V.Num()) - OutMean * OutMean)) : 0.f;
}
}

void RunTraining(FLog& Log)
{
	const UPA250WeaponTuning* WT = GetDefault<UPA250WeaponTuning>();
	const FActiveReloadTuningPlain Arc = AMusketWeapon::PresetToSequencer(WT->Values.Arcade);
	const float Ideal = WT->Values.Arcade.PerfectSeconds; // provisional balance baseline: centred press on every step

	// Player loading untouched: the preset mapping is exactly what shipped before this change.
	// Golden for the user-chosen ~10 s Arcade reload (7 perfect / 10 untouched / 14 fumbled): same rules, longer steps.
	Log.Check(FMath::IsNearlyEqual(Arc.PhasePrimePan, 10.f * 2.f / 7.f) && FMath::IsNearlyEqual(Arc.PhaseRamCharge, 10.f * 3.f / 7.f)
		&& FMath::IsNearlyEqual(Arc.PhaseShoulderAim, 10.f * 2.f / 7.f) && FMath::IsNearlyEqual(Arc.WindowCenter, 0.7f)
		&& Arc.SweetZoneFraction == 0.5f && Arc.PerfectZoneFraction == 0.2f && FMath::IsNearlyEqual(Arc.MissDelaySeconds, 4.f / 3.f)
		&& Arc.JamChanceOnMiss == 0.f && Arc.PerfectReloadScale == 1.f && Arc.bTimeoutCompletesPhase && Arc.bMissResumesProgress
		&& Arc.bIgnoreRepeatMiss && FMath::IsNearlyEqual(Arc.PerfectDamageBonus, 0.15f),
		TEXT("training: player's Arcade reload settings unchanged (golden)"));

	UPA250CombatProfiles* Profiles = UPA250CombatProfiles::MakeRuntimeDefaults();
	const FPA250TrainingProfile& Vet = Profiles->TrainingNamed(TEXT("Veteran"));
	const FPA250TrainingProfile& Rec = Profiles->TrainingNamed(TEXT("Recruit"));
	Log.Check(Profiles->TrainingFor(TEXT("british"), TEXT("line_infantry")).Name == TEXT("Veteran")
		&& Profiles->TrainingFor(TEXT("continental"), TEXT("line_infantry")).Name == TEXT("Trained")
		&& Profiles->TrainingFor(TEXT("continental"), TEXT("militia")).Name == TEXT("Trained")
		&& Profiles->RoleFor(TEXT("continental"), TEXT("militia")).Name == TEXT("Irregular")
		&& Profiles->RoleFor(TEXT("british"), TEXT("line_infantry")).Name == TEXT("Line"),
		TEXT("training: default assignment Veteran/Line, Trained/Line, Trained/Irregular (minutemen)"));
	const UPA250CombatProfiles* Shipped = LoadObject<UPA250CombatProfiles>(nullptr, TEXT("/Game/PA250/Data/DA_CombatProfiles.DA_CombatProfiles"));
	Log.Check(Shipped && Shipped->Trainings.Num() == 3 && Shipped->Roles.Num() == 3 && Shipped->Assignments.Num() >= 3,
		TEXT("training: DA_CombatProfiles exists with the defaults"));

	// Soldier classes (PA250UnitClasses): every class either side can field has a drill and a kit.
	{
		Log.Check(Profiles->RoleFor(TEXT("british"), TEXT("light_infantry")).Name == TEXT("Light")
			&& Profiles->RoleFor(TEXT("british"), TEXT("grenadier")).Name == TEXT("Line")
			&& Profiles->TrainingFor(TEXT("british"), TEXT("grenadier")).Name == TEXT("Veteran")
			&& Profiles->RoleFor(TEXT("continental"), TEXT("rifleman")).Name == TEXT("Irregular")
			&& Profiles->RoleFor(TEXT("continental"), TEXT("officer")).Name == TEXT("Line"),
			TEXT("classes: grenadiers and officers fight in the line, light infantry and riflemen skirmish"));
		Log.Check(Shipped && Shipped->RoleFor(TEXT("british"), TEXT("light_infantry")).Name == TEXT("Light")
			&& Shipped->RoleFor(TEXT("continental"), TEXT("rifleman")).Name == TEXT("Irregular"),
			TEXT("classes: an asset saved before the new classes still resolves them (built-in rows)"));
		const FPA250UnitClassKit& Line = PA250UnitClasses::KitFor(TEXT("british"), TEXT("line_infantry"));
		const FPA250UnitClassKit& Gren = PA250UnitClasses::KitFor(TEXT("british"), TEXT("grenadier"));
		const FPA250UnitClassKit& Light = PA250UnitClasses::KitFor(TEXT("british"), TEXT("light_infantry"));
		const FPA250UnitClassKit& Rifle = PA250UnitClasses::KitFor(TEXT("continental"), TEXT("rifleman"));
		const FPA250UnitClassKit& Mil = PA250UnitClasses::KitFor(TEXT("continental"), TEXT("militia"));
		Log.Check(Line.MaxHealth == 100.f && Line.SpeedScale == 1.f && Line.SpreadScale == 1.f && Line.RangeScale == 1.f
			&& Line.ReloadScale == 1.f && Line.MeleeScale == 1.f && Mil.MaxHealth == 80.f,
			TEXT("classes: the line soldier is the unchanged baseline; militia keep 80 health"));
		Log.Check(Gren.MaxHealth > Line.MaxHealth && Gren.MeleeScale > Line.MeleeScale && Gren.SpeedScale < Line.SpeedScale,
			TEXT("classes: grenadiers are tougher and hit harder, and are no faster for it"));
		Log.Check(Light.SpeedScale > Line.SpeedScale && Light.MaxHealth < Line.MaxHealth && Light.MeleeScale < Line.MeleeScale,
			TEXT("classes: light infantry are faster and lighter"));
		Log.Check(Rifle.RangeScale >= 2.f && Rifle.SpreadScale < 0.6f && Rifle.ReloadScale > 1.4f && Rifle.MeleeScale < 1.f
			&& !UPA250MeleeComponent::IssuedBayonet(TEXT("continental"), TEXT("rifleman"), 4)
			&& UPA250MeleeComponent::IssuedBayonet(TEXT("british"), TEXT("grenadier"), 3),
			TEXT("classes: the rifle reaches further and shoots straighter, loads slower, and carries no bayonet"));
		Log.Check(PA250UnitClasses::KitFor(TEXT("british"), TEXT("officer")).bOfficer && PA250UnitClasses::KitFor(TEXT("continental"), TEXT("officer")).bOfficer
			&& !Line.bOfficer && !Rifle.bOfficer && PA250UnitClasses::OfficerSpreadScale < 1.f && PA250UnitClasses::OfficerReloadScale < 1.f
			&& PA250UnitClasses::InOfficerRange(FVector::ZeroVector, FVector(1400.f, 0.f, 0.f))
			&& !PA250UnitClasses::InOfficerRange(FVector::ZeroVector, FVector(1600.f, 0.f, 0.f)),
			TEXT("classes: an officer steadies and speeds men within 15 m, not beyond"));
		Log.Check(&PA250UnitClasses::KitFor(TEXT("british"), TEXT("no_such_class")) == &Line
			&& ARFCharacter::NameplateLabel(TEXT("british"), TEXT("grenadier")).ToString() == TEXT("Grenadier")
			&& ARFCharacter::NameplateLabel(TEXT("continental"), TEXT("rifleman")).ToString() == TEXT("Rifleman")
			&& PA250UnitClasses::TraitLine(Rifle).Contains(TEXT("no bayonet")) && PA250UnitClasses::TraitLine(Rifle).Contains(TEXT("range x2")),
			TEXT("classes: unknown classes fall back to the line soldier; labels and trait lines read in words"));
		// A rifle ball keeps its force over twice the distance: at 150 m a musket ball is at its far damage,
		// a rifle ball (range x2 -> judged as 75 m) is still above it.
		const FPA250WeaponTuningValues W = UPA250WeaponTuning::MakeRuntimeDefaults()->Values;
		Log.Check(W.DamageAtDistanceCm(15000.f / Rifle.RangeScale) > W.DamageAtDistanceCm(15000.f)
			&& W.DamageAtDistanceCm(30000.f) == 0.f && W.DamageAtDistanceCm(30000.f / Rifle.RangeScale) > 0.f,
			TEXT("classes: rifle damage falloff is stretched by its range scale (still hurts at 300 m)"));
	}

	const FPA250AIReloadConditions Calm;
	const int32 N = 2000;
	// Every AI reload goes through all three steps, never beats the drill ideal, and readiness holds.
	{
		TArray<float> VetT, RecT;
		bool bAllSteps = true, bNeverFaster = true;
		float MinT = TNumericLimits<float>::Max();
		for (int32 i = 0; i < N; ++i)
		{
			const FAIReloadRun A = SimAIReload(Arc, Vet.ToReloadParams(), Calm, 1000 + i);
			const FAIReloadRun B = SimAIReload(Arc, Rec.ToReloadParams(), Calm, 1000 + i);
			VetT.Add(A.Seconds);
			RecT.Add(B.Seconds);
			bAllSteps &= A.StepsSeen == 3 && B.StepsSeen == 3;
			MinT = FMath::Min3(MinT, A.Seconds, B.Seconds);
		}
		bNeverFaster = MinT >= Ideal - 0.01f;
		float VM, VS, RM, RS;
		MeanStd(VetT, VM, VS);
		MeanStd(RecT, RM, RS);
		Log.Check(bAllSteps, TEXT("training: every AI reload passes all three steps"));
		Log.Check(bNeverFaster, FString::Printf(TEXT("training: no AI reload beats the %.2f s drill ideal (fastest %.2f s)"), Ideal, MinT));
		Log.Check(VM >= Ideal - 0.01f && VM <= Ideal * 1.1f, FString::Printf(TEXT("training: calm Veteran mean %.2f s within [%.2f, %.2f]"), VM, Ideal, Ideal * 1.1f));
		Log.Check(RM > VM && RS > VS, FString::Printf(TEXT("training: Recruit slower and less consistent (%.2f+-%.2f vs %.2f+-%.2f s)"), RM, RS, VM, VS));
	}

	// Equal training -> equal results, whatever the faction: make Continental line Veteran too.
	{
		UPA250CombatProfiles* Equal = UPA250CombatProfiles::MakeRuntimeDefaults();
		for (FPA250CombatAssignment& A : Equal->Assignments)
		{
			if (A.Faction == TEXT("continental") && A.UnitClass == TEXT("line_infantry")) A.Training = TEXT("Veteran");
		}
		const FPA250AIReloadParams Brit = Equal->TrainingFor(TEXT("british"), TEXT("line_infantry")).ToReloadParams();
		const FPA250AIReloadParams Cont = Equal->TrainingFor(TEXT("continental"), TEXT("line_infantry")).ToReloadParams();
		bool bSame = true;
		TArray<float> BT, CT;
		for (int32 i = 0; i < N; ++i)
		{
			bSame &= SimAIReload(Arc, Brit, Calm, 7 + i).Seconds == SimAIReload(Arc, Cont, Calm, 7 + i).Seconds;
			BT.Add(SimAIReload(Arc, Brit, Calm, 20000 + i).Seconds);
			CT.Add(SimAIReload(Arc, Cont, Calm, 50000 + i).Seconds);
		}
		float BM, BS, CM, CS;
		MeanStd(BT, BM, BS);
		MeanStd(CT, CM, CS);
		Log.Check(bSame, TEXT("training: same training + same seed -> identical British and Continental reloads"));
		Log.Check(FMath::Abs(BM - CM) <= 0.01f * BM, FString::Printf(TEXT("training: same training, different seeds -> means within 1%% (%.3f vs %.3f s)"), BM, CM));
	}

	// Stress, movement and disorder never make a reload faster - checked per sample, same random draws.
	{
		bool bMonotone = true;
		int32 Worst = -1;
		for (const FPA250TrainingProfile* Tr : {&Vet, &Rec})
		{
			const FPA250AIReloadParams P = Tr->ToReloadParams();
			for (int32 i = 0; i < 500; ++i)
			{
				float Prev = 0.f;
				for (const float Stress : {0.f, 0.25f, 0.5f, 0.75f, 1.f})
				{
					FPA250AIReloadConditions C;
					C.Stress01 = Stress;
					const float Still = SimAIReload(Arc, P, C, 300 + i).Seconds;
					C.bMoving = true;
					const float Moving = SimAIReload(Arc, P, C, 300 + i).Seconds;
					if (Still + 0.006f < Prev || Moving + 0.006f < Still) { bMonotone = false; Worst = i; }
					Prev = Still;
				}
			}
		}
		Log.Check(bMonotone, FString::Printf(TEXT("training: more stress / moving never shortens any reload (1000 x 5 x 2 runs%s)"),
			Worst >= 0 ? *FString::Printf(TEXT(", first failure seed %d"), 300 + Worst) : TEXT("")));
		// ...and the slowdown is real, not zero: full stress and moving cost every training level.
		for (const TCHAR* Name : {TEXT("Veteran"), TEXT("Trained"), TEXT("Recruit")})
		{
			const FPA250AIReloadParams P = Profiles->TrainingNamed(Name).ToReloadParams();
			float CalmSum = 0.f, StressSum = 0.f, MoveSum = 0.f;
			FPA250AIReloadConditions Stressed;
			Stressed.Stress01 = 1.f;
			FPA250AIReloadConditions Moving;
			Moving.bMoving = true;
			for (int32 i = 0; i < 500; ++i)
			{
				CalmSum += SimAIReload(Arc, P, Calm, 4000 + i).Seconds;
				StressSum += SimAIReload(Arc, P, Stressed, 4000 + i).Seconds;
				MoveSum += SimAIReload(Arc, P, Moving, 4000 + i).Seconds;
			}
			Log.Check(StressSum >= 1.15f * CalmSum, FString::Printf(TEXT("training: %s under full stress is >= 15%% slower (%.2f vs %.2f s)"), Name, StressSum / 500.f, CalmSum / 500.f));
			Log.Check(MoveSum >= 1.10f * CalmSum, FString::Printf(TEXT("training: %s reloading on the move is >= 10%% slower (%.2f vs %.2f s)"), Name, MoveSum / 500.f, CalmSum / 500.f));
		}
		Log.Check(FPA250AIReloadPlanner::PlanTarget(0.625f, Vet.ToReloadParams(), Calm, 0.f) >= 0.625f
			&& FPA250AIReloadPlanner::FumbleChance(Vet.ToReloadParams(), FPA250AIReloadConditions{1.f, true}) > FPA250AIReloadPlanner::FumbleChance(Vet.ToReloadParams(), Calm),
			TEXT("training: planner never presses before the ideal; stress raises fumbles"));
	}

	// On a real musket: no firing while the AI reloads, loaded only at the end, time recorded.
	{
		FScopedTestWorld W;
		AMusketWeapon* Musket = W.World ? W.World->SpawnActor<AMusketWeapon>() : nullptr;
		Log.Check(Musket != nullptr, TEXT("training: spawn musket"));
		if (!Musket) return;
		Musket->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
		Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
		Musket->StartReload();
		FRandomStream Rng(42);
		bool bNeverFired = true;
		float T = 0.f;
		while (!Musket->CanFire() && T < 30.f)
		{
			bNeverFired &= !Musket->Fire(FVector::ForwardVector);
			Musket->TickAIReload(Vet.ToReloadParams(), Calm, Rng);
			Musket->TickActor(0.01f, LEVELTICK_All, Musket->PrimaryActorTick);
			T += 0.01f;
		}
		Log.Check(bNeverFired && Musket->CanFire() && Musket->GetReloadsCompleted() == 1, TEXT("training: AI musket cannot fire until all steps are done, then is loaded"));
		Log.Check(Musket->GetLastReloadSeconds() >= Ideal - 0.02f, FString::Printf(TEXT("training: AI reload took %.2f s (>= ideal)"), Musket->GetLastReloadSeconds()));
		Log.Check(Musket->TickAIReload(Vet.ToReloadParams(), Calm, Rng) == EActiveReloadHit::None && Musket->CanFire(),
			TEXT("training: AI planner does nothing to a loaded musket"));
		Musket->Destroy();
	}

	// ---- Fire discipline: how each kind of soldier's fire differs ----
	{
		const UPA250CombatProfiles* FireProfiles = UPA250CombatProfiles::MakeRuntimeDefaults();
		const FPA250TrainingProfile& FVet = FireProfiles->TrainingNamed(TEXT("Veteran"));
		const FPA250TrainingProfile& FTrn = FireProfiles->TrainingNamed(TEXT("Trained"));
		const FPA250TrainingProfile& FRec = FireProfiles->TrainingNamed(TEXT("Recruit"));
		Log.Check(FVet.VolleyRaggedness < 1.f && FVet.VolleyRaggedness < FTrn.VolleyRaggedness && FTrn.VolleyRaggedness < FRec.VolleyRaggedness,
			TEXT("fire: veterans volley crisply, recruits raggedly"));
		Log.Check(FVet.RollingFireFileSeconds > 0.f && FTrn.RollingFireFileSeconds == 0.f, TEXT("fire: only veterans keep up a rolling fire by files"));
		Log.Check(FVet.MisfireChance < FTrn.MisfireChance && FTrn.MisfireChance < FRec.MisfireChance && FRec.MisfireChance <= 0.15f,
			TEXT("fire: misfires are rarer with training and never common"));
		Log.Check(FireProfiles->RoleNamed(TEXT("Line")).RestedAimScale == 1.f && FireProfiles->RoleNamed(TEXT("Irregular")).RestedAimScale < 1.f
			&& FireProfiles->RoleNamed(TEXT("Light")).RestedAimScale < 1.f, TEXT("fire: skirmishers shoot straighter from cover, the line does not"));

		Log.Check(UPA250VolleyCoordinator::VolleyShotDelay(0, 0.f, 0.f, 0.7f) == 0.f, TEXT("fire: no jitter, no rolling = all on the same instant"));
		Log.Check(FMath::IsNearlyEqual(UPA250VolleyCoordinator::VolleyShotDelay(4, 0.f, 0.1f, 0.5f), 0.05f), TEXT("fire: a volley fired together ignores a man's place in the line"));
		Log.Check(FMath::IsNearlyEqual(UPA250VolleyCoordinator::VolleyShotDelay(4, 0.14f, 0.f, 0.f), 0.56f)
			&& UPA250VolleyCoordinator::VolleyShotDelay(5, 0.14f, 0.06f, 0.f) > UPA250VolleyCoordinator::VolleyShotDelay(4, 0.14f, 0.06f, 1.f),
			TEXT("fire: rolling fire runs down the line in order, even at full raggedness"));

		bool bInRange = true, bStable = true;
		float Lo = 10.f, Hi = 0.f;
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			const float S = UPA250CombatProfiles::AimScaleFor(Seed, 0.25f);
			bInRange &= S >= 0.75f - KINDA_SMALL_NUMBER && S <= 1.25f + KINDA_SMALL_NUMBER;
			bStable &= S == UPA250CombatProfiles::AimScaleFor(Seed, 0.25f);
			Lo = FMath::Min(Lo, S);
			Hi = FMath::Max(Hi, S);
		}
		Log.Check(bInRange && bStable && Hi - Lo > 0.3f, FString::Printf(TEXT("fire: each man's aim is his own, fixed by seed (%.2f..%.2f)"), Lo, Hi));
		Log.Check(UPA250CombatProfiles::AimScaleFor(7, 0.f) == 1.f, TEXT("fire: no variance = the training's average aim"));
		Log.Check(UPA250CombatProfiles::MisfireChanceAt(0.05f, 0.f) == 0.05f && FMath::IsNearlyEqual(UPA250CombatProfiles::MisfireChanceAt(0.05f, 1.f), 0.1f)
			&& UPA250CombatProfiles::MisfireChanceAt(0.4f, 1.f) == 0.5f && UPA250CombatProfiles::MisfireChanceAt(0.f, 1.f) == 0.f,
			TEXT("fire: stress doubles the misfire chance at most, capped at one in two"));
	}
	// ---- Hand to hand: how a bot chooses his blow ----
	{
		FPA250BotMeleeView V;
		V.DistCm = 240.f;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Thrust, TEXT("melee ai: in reach of an open man he thrusts"));
		V.bResting = true;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Close, TEXT("melee ai: no second blow while he recovers from the first"));
		V.bResting = false;
		V.bFoeGuardsMe = true;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Thrust, TEXT("melee ai: a man who does not read the guard thrusts into it"));
		V.bReadsGuard = true;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Close, TEXT("melee ai: reading a guard, he closes instead of thrusting into it"));
		V.DistCm = 180.f;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Butt, TEXT("melee ai: inside butt-stroke reach he breaks the guard with the stock"));
		V.bFoeGuardsMe = false;
		V.DistCm = 500.f;
		V.bChargeReady = true;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Charge, TEXT("melee ai: inside the lunge with the charge ready he dashes"));
		V.bFacing = false;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Close, TEXT("melee ai: no dash until he faces his man"));
		V.bFacing = true;
		V.DistCm = 900.f;
		Log.Check(PA250ChooseMeleeMove(V) == EPA250BotMelee::Close, TEXT("melee ai: out of reach he closes"));

		const UPA250CombatProfiles* MeleeProfiles = UPA250CombatProfiles::MakeRuntimeDefaults();
		const FPA250TrainingProfile& MVet = MeleeProfiles->TrainingNamed(TEXT("Veteran"));
		const FPA250TrainingProfile& MRec = MeleeProfiles->TrainingNamed(TEXT("Recruit"));
		Log.Check(MVet.MeleeRecoverSeconds < MRec.MeleeRecoverSeconds && MVet.MeleeSkill > MRec.MeleeSkill && MRec.MeleeSkill > 0.f,
			TEXT("melee ai: veterans recover faster and answer a guard more often; recruits sometimes do"));

		// On real soldiers: a bot facing a raised guard uses the stock, facing an open man the bayonet.
		{
			FScopedTestWorld MW;
			ARFCharacter* Bot = MW.World ? MW.World->SpawnActor<ARFCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator) : nullptr;
			ARFCharacter* Foe = MW.World ? MW.World->SpawnActor<ARFCharacter>(FVector(170.f, 0.f, 100.f), FRotator(0.f, 180.f, 0.f)) : nullptr;
			APA250BotController* Brain = MW.World ? MW.World->SpawnActor<APA250BotController>() : nullptr;
			Log.Check(Bot && Foe && Brain && Bot->MeleeComponent && Foe->MeleeComponent, TEXT("melee ai: spawn bot, foe and controller"));
			if (Bot && Foe && Brain && Bot->MeleeComponent && Foe->MeleeComponent)
			{
				Bot->SetTeam(TEXT("british"), TEXT("line_infantry"));
				Foe->SetTeam(TEXT("continental"), TEXT("militia"));
				Foe->MeleeComponent->SetGuardHeld(true);
				Foe->MeleeComponent->TickComponent(0.5f, LEVELTICK_All, nullptr);
				Log.Check(Foe->MeleeComponent->ResolveGuard(Bot->GetActorLocation()) == EPA250Guard::Block, TEXT("melee ai: the foe's guard is up and turned on the bot"));
				Brain->MeleeAttack(Bot, Foe, 170.f, true);
				Log.Check(Bot->MeleeComponent->GetVisual() == EPA250MeleeVisual::ButtStroke, TEXT("melee ai: the bot answers the guard with a butt-stroke"));
				Bot->MeleeComponent->TickComponent(1.f, LEVELTICK_All, nullptr);
				Foe->MeleeComponent->SetGuardHeld(false);
				Foe->MeleeComponent->TickComponent(0.1f, LEVELTICK_All, nullptr);
				Log.Check(!Bot->MeleeComponent->IsBusy() && !Foe->MeleeComponent->IsGuarding(), TEXT("melee ai: swing over, guard down"));
				Brain->MeleeAttack(Bot, Foe, 170.f, true);
				Log.Check(Bot->MeleeComponent->GetVisual() == EPA250MeleeVisual::Thrust, TEXT("melee ai: against an open man the bot thrusts"));
			}
		}
		// Sounds: the new banks are on disk, and a missing bank falls back instead of going silent.
		Log.Check(FPA250BattleAudio::Pick(TEXT("bayonet_clash")) && FPA250BattleAudio::Pick(TEXT("bayonet_stab")) && FPA250BattleAudio::Pick(TEXT("butt_thud")),
			TEXT("melee audio: clash, stab and stock sounds are imported"));
		FScopedTestWorld W;
		AMusketWeapon* Ctx = W.World ? W.World->SpawnActor<AMusketWeapon>() : nullptr;
		if (Ctx)
		{
			const int32 Before = FPA250BattleAudio::GetPlayCount(TEXT("flint_click"));
			FPA250BattleAudio::PlayAtOr(Ctx, TEXT("no_such_bank"), TEXT("flint_click"), FVector::ZeroVector, EPA250SoundRange::Near);
			const int32 Clash = FPA250BattleAudio::GetPlayCount(TEXT("bayonet_clash"));
			FPA250BattleAudio::PlayAtOr(Ctx, TEXT("bayonet_clash"), TEXT("flint_click"), FVector::ZeroVector, EPA250SoundRange::Near);
			Log.Check(FPA250BattleAudio::GetPlayCount(TEXT("flint_click")) == Before + 1 && FPA250BattleAudio::GetPlayCount(TEXT("bayonet_clash")) == Clash + 1,
				TEXT("melee audio: a missing bank plays its fallback, a present one plays itself"));
			Ctx->Destroy();
		}
	}
	// A misfire on a real soldier: no ball, the musket must be loaded again, and the player never misfires.
	{
		FScopedTestWorld W;
		ARFCharacter* Soldier = W.World ? W.World->SpawnActor<ARFCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator) : nullptr;
		Log.Check(Soldier != nullptr, TEXT("fire: spawn soldier"));
		if (Soldier)
		{
			Soldier->DispatchBeginPlay();
			Log.Check(Soldier->MisfireChance == 0.f, TEXT("fire: a soldier without a training profile (the player) never misfires"));
			if (Soldier->Musket && Soldier->Musket->CanFire())
			{
				int32 Balls = 0;
				for (TActorIterator<AMusketProjectile> It(W.World); It; ++It) ++Balls;
				Soldier->MisfireChance = 0.5f;
				Soldier->MisfireStress = 1.f; // 0.5 * 2, capped at 0.5: roll until it happens
				bool bMisfired = false;
				for (int32 Try = 0; Try < 64 && !bMisfired; ++Try)
				{
					if (Soldier->FireMusketAt(FVector(3000.f, 0.f, 100.f)))
					{
						Soldier->Musket->GetSequencer().Reset(); // went off: load and try again
						Soldier->Musket->TickActor(0.01f, LEVELTICK_All, Soldier->Musket->PrimaryActorTick);
						for (TActorIterator<AMusketProjectile> It(W.World); It; ++It) It->Destroy();
						Balls = 0;
						for (TActorIterator<AMusketProjectile> It(W.World); It; ++It) ++Balls;
						continue;
					}
					bMisfired = Soldier->Misfires == 1;
				}
				int32 BallsAfter = 0;
				for (TActorIterator<AMusketProjectile> It(W.World); It; ++It) ++BallsAfter;
				Log.Check(bMisfired && BallsAfter == Balls, TEXT("fire: a flash in the pan sends no ball"));
				Log.Check(bMisfired && !Soldier->Musket->CanFire() && Soldier->Musket->IsReloading(), TEXT("fire: after a misfire the musket must be loaded again"));
				Log.Check(!Soldier->Musket->Misfire(), TEXT("fire: an unloaded musket cannot misfire"));
			}
			// A wound he survives staggers him and spoils his aim; a killing one does not flinch a dead man.
			Soldier->MisfireChance = 0.f;
			Log.Check(!Soldier->ConsumeFlinch(), TEXT("fire: no flinch before he is hit"));
			// A pawn takes no damage in a world without a game mode: drive the health component directly.
			Soldier->HealthComponent->OnTakeAnyDamage(Soldier, 30.f, nullptr, nullptr, nullptr);
			Log.Check(!Soldier->IsDead() && Soldier->SuppressionTimer > 0.f && Soldier->ConsumeFlinch() && !Soldier->ConsumeFlinch(),
				TEXT("fire: a wounded man flinches once and his aim suffers"));
			Soldier->HealthComponent->OnTakeAnyDamage(Soldier, 500.f, nullptr, nullptr, nullptr);
			Log.Check(Soldier->IsDead() && !Soldier->ConsumeFlinch(), TEXT("fire: the killing shot is a death, not a flinch"));
			Log.Check(Soldier->GetLifeSpan() >= 30.f, FString::Printf(TEXT("fire: the fallen stay on the field (%.0f s)"), Soldier->GetLifeSpan()));
		}
	}
	// Smoke bookkeeping (pure data): dust falls back and is not powder smoke.
	{
		FPA250SmokePuff Dust;
		Dust.bPowder = false;
		Dust.Rise = -60.f;
		Dust.WindShare = 0.f;
		Dust.Life = 1.f;
		const float Z0 = Dust.Pos.Z;
		for (int32 i = 0; i < 50; ++i) Dust.Step(0.01f, FVector(70.f, 30.f, 0.f));
		Log.Check(Dust.Pos.Z < Z0 && FMath::IsNearlyZero(Dust.Pos.X) && !Dust.IsDead(), TEXT("fire: thrown dirt falls back and ignores the breeze when told to"));
		FPA250SmokePuff Smoke;
		for (int32 i = 0; i < 50; ++i) Smoke.Step(0.01f, FVector(70.f, 30.f, 0.f));
		Log.Check(Smoke.Pos.Z > 0.f && Smoke.Pos.X > 0.f, TEXT("fire: powder smoke still rises and drifts downwind"));
	}
}

void RunMorale(FLog& Log)
{
	const FPA250MoraleTuning M;
	// Hysteresis + dwell: pressure wobbling around the Shaken threshold does not flap.
	{
		FPA250MoraleState S;
		for (int32 i = 0; i < 1200; ++i)
		{
			const float P = M.ShakenEnter + 0.05f * FMath::Sin(i * 0.7f);
			S.Tick(0.1f, P, 1.f, 8.f, 3.f, false, false);
		}
		Log.Check(S.Transitions <= 2, FString::Printf(TEXT("morale: pressure +-0.05 around the threshold for 120 s -> %d transitions (<= 2)"), S.Transitions));
	}
	{
		FPA250MoraleState S;
		for (int32 i = 0; i < 50; ++i) S.Tick(0.1f, 0.5f, 1.f, 8.f, 3.f, false, false);
		const bool bShaken = S.State == EPA250Morale::Shaken;
		for (int32 i = 0; i < 100; ++i) S.Tick(0.1f, 0.38f, 1.f, 8.f, 3.f, false, false);
		const bool bStillShaken = S.State == EPA250Morale::Shaken;
		for (int32 i = 0; i < 50; ++i) S.Tick(0.1f, 0.2f, 1.f, 8.f, 3.f, false, false);
		Log.Check(bShaken && bStillShaken && S.State == EPA250Morale::Steady, TEXT("morale: separate enter (0.45) / leave (0.30) thresholds"));
	}
	// ...but it still reacts promptly to a real, lasting change (stable is not the same as numb).
	{
		auto SecondsUntil = [](FPA250MoraleState& S, float Pressure, float Resolve, EPA250Morale Want) {
			float t = 0.f;
			while (S.State != Want && t < 60.f)
			{
				S.Tick(0.1f, Pressure, Resolve, 8.f, 3.f, false, false);
				t += 0.1f;
			}
			return t;
		};
		FPA250MoraleState S;
		const float ToShaken = SecondsUntil(S, 0.7f, 1.f, EPA250Morale::Shaken);
		const float ToSteady = SecondsUntil(S, 0.1f, 1.f, EPA250Morale::Steady);
		Log.Check(ToShaken <= 1.f, FString::Printf(TEXT("morale: sustained heavy fire shakes a steady squad within 1 s (%.1f s)"), ToShaken));
		Log.Check(ToSteady <= 3.5f, FString::Printf(TEXT("morale: once the pressure lifts it steadies within 3.5 s (%.1f s)"), ToSteady));
		FPA250MoraleState R;
		const float ToRout = SecondsUntil(R, 1.5f, 1.2f, EPA250Morale::Routing);
		Log.Check(ToRout <= 1.f, FString::Printf(TEXT("morale: overwhelming pressure routs a Trained squad within 1 s (%.1f s)"), ToRout));
	}

	// Training resists panic; nobody is fearless.
	{
		UPA250CombatProfiles* Pr = UPA250CombatProfiles::MakeRuntimeDefaults();
		const FPA250TrainingProfile& Vet = Pr->TrainingNamed(TEXT("Veteran"));
		const FPA250TrainingProfile& Rec = Pr->TrainingNamed(TEXT("Recruit"));
		auto After = [](const FPA250TrainingProfile& T, float Pressure, float Seconds) {
			FPA250MoraleState S;
			for (float t = 0.f; t < Seconds; t += 0.1f) S.Tick(0.1f, Pressure, T.MoraleResolve, T.RallySeconds, T.ReformSeconds, false, false);
			return S.State;
		};
		Log.Check(After(Vet, 0.9f, 20.f) != EPA250Morale::Routing && After(Rec, 0.9f, 20.f) == EPA250Morale::Routing,
			TEXT("morale: the same punishment routs Recruits but not Veterans"));
		Log.Check(After(Vet, 1.6f, 20.f) == EPA250Morale::Routing, TEXT("morale: Veterans still rout under overwhelming pressure (not fearless)"));

		// Rally only after a calm delay, then re-form; Veterans recover faster than Recruits.
		auto RecoverSeconds = [](const FPA250TrainingProfile& T) {
			FPA250MoraleState S;
			S.Set(EPA250Morale::Routing);
			float t = 0.f;
			bool bRallyTooEarly = false;
			while (S.State != EPA250Morale::Steady && t < 120.f)
			{
				S.Tick(0.1f, 0.f, T.MoraleResolve, T.RallySeconds, T.ReformSeconds, false, false);
				t += 0.1f;
				if (S.State == EPA250Morale::Rallying && t + 0.05f < T.RallySeconds) bRallyTooEarly = true;
			}
			return bRallyTooEarly ? -1.f : t;
		};
		const float VetRec = RecoverSeconds(Vet);
		const float RecRec = RecoverSeconds(Rec);
		Log.Check(VetRec > 0.f && RecRec > 0.f && VetRec >= Vet.RallySeconds + Vet.ReformSeconds - 0.2f,
			FString::Printf(TEXT("morale: rally waits the delay, then re-forms (Veteran %.1f s, Recruit %.1f s)"), VetRec, RecRec));
		Log.Check(VetRec < RecRec && Vet.FormationToleranceScale < Rec.FormationToleranceScale && Vet.StragglerToleranceScale < Rec.StragglerToleranceScale,
			TEXT("morale: Veterans re-form faster and keep tighter ranks than Recruits"));
	}
	// Deliberate withdrawal is not a rout.
	{
		const FPA250MoraleOrders Wd = FPA250MoraleOrders::For(EPA250Morale::Withdrawing);
		const FPA250MoraleOrders Rt = FPA250MoraleOrders::For(EPA250Morale::Routing);
		Log.Check(Wd.bKeepFormation && Wd.bFallBack && !Wd.bFleeToRear && !Wd.bNeedsRally && !Wd.bMayVolley,
			TEXT("morale: withdrawal keeps ranks, backs off facing the enemy, holds fire while moving, needs no rally"));
		Log.Check(!Rt.bKeepFormation && Rt.bFleeToRear && Rt.bNeedsRally && !Rt.bMayVolley && !Rt.bMayCharge,
			TEXT("morale: rout abandons ranks, runs for the rear, never fires or charges, must rally"));
		FPA250MoraleState S;
		S.Tick(0.1f, 0.1f, 1.f, 8.f, 3.f, true, false);
		const bool bWithdrawing = S.State == EPA250Morale::Withdrawing;
		S.Tick(0.1f, 0.1f, 1.f, 8.f, 3.f, false, true);
		Log.Check(bWithdrawing && S.State == EPA250Morale::Steady, TEXT("morale: an ordered withdrawal ends in Steady without a rally"));
		FPA250MoraleState S2;
		S2.Tick(0.1f, 0.1f, 1.f, 8.f, 3.f, true, false);
		S2.Tick(0.1f, 1.5f, 1.f, 8.f, 3.f, false, false);
		Log.Check(S2.State == EPA250Morale::Routing, TEXT("morale: a withdrawal under overwhelming pressure can still break into a rout"));
		Log.Check(FPA250MoraleOrders::For(EPA250Morale::Routing).ReloadStress >= FPA250MoraleOrders::For(EPA250Morale::Shaken).ReloadStress
			&& FPA250MoraleOrders::For(EPA250Morale::Shaken).ReloadStress >= FPA250MoraleOrders::For(EPA250Morale::Steady).ReloadStress,
			TEXT("morale: worse morale only adds reload stress"));
	}
	// The player's character is never taken over by squad morale or tactics.
	{
		FScopedTestWorld W;
		ARFCharacter* Player = W.World ? W.World->SpawnActor<ARFCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator) : nullptr;
		ARFCharacter* Bot = W.World ? W.World->SpawnActor<ARFCharacter>(FVector(500, 0, 100), FRotator::ZeroRotator) : nullptr;
		APlayerController* PC = W.World ? W.World->SpawnActor<APlayerController>() : nullptr;
		if (Player && Bot && PC)
		{
			Player->FactionId = TEXT("continental");
			Bot->FactionId = TEXT("continental");
			PC->Possess(Player);
			Log.Check(Player->GetController() == PC && !UPA250VolleyCoordinator::ShouldJoinSquad(Player) && UPA250VolleyCoordinator::ShouldJoinSquad(Bot),
				TEXT("morale: the player's soldier never joins a squad (no morale/withdrawal/rout orders)"));
		}
		else
		{
			Log.Check(false, TEXT("morale: spawn player + bot"));
		}
	}
}

void RunTactics(FLog& Log)
{
	FScopedTestWorld W;
	Log.Check(W.World != nullptr, TEXT("tactics: test world"));
	if (!W.World) return;
	// A 1.1 m fence along Y at x = 0; the enemy stands 30 m to the east.
	W.SpawnBox(FVector(0.f, 0.f, 55.f), FVector(20.f, 2000.f, 110.f));
	// A 4 m wall far to the north for line-of-sight checks.
	W.SpawnBox(FVector(0.f, 5000.f, 200.f), FVector(20.f, 600.f, 400.f));
	W.Settle();
	const FVector Enemy(3000.f, 0.f, 90.f);
	const TArray<const AActor*> None;
	FVector Pos;
	Log.Check(PA250Cover::IsFiringPosition(W.World, FVector(-100.f, 0.f, 90.f), Enemy, None), TEXT("tactics: behind the fence = covered and can still fire"));
	Log.Check(!PA250Cover::IsFiringPosition(W.World, FVector(-2000.f, 0.f, 90.f), Enemy, None), TEXT("tactics: 20 m back from the fence is not cover"));
	Log.Check(!PA250Cover::IsFiringPosition(W.World, FVector(200.f, 0.f, 90.f), Enemy, None), TEXT("tactics: on the enemy's side of the fence is not cover"));
	const bool bFound = PA250Cover::FindFiringPosition(W.World, FVector(200.f, 0.f, 90.f), 600.f, Enemy, None, Pos);
	Log.Check(bFound && Pos.X < 0.f, FString::Printf(TEXT("tactics: cover search moves him behind the fence (x=%.0f)"), Pos.X));
	Log.Check(!PA250Cover::FindFiringPosition(W.World, FVector(-2500.f, -3000.f, 90.f), 600.f, Enemy, None, Pos), TEXT("tactics: no cover found in the open"));
	// Visibility: no seeing (or shooting at, or charging) through a wall.
	Log.Check(!PA250Cover::HasLineOfSight(W.World, FVector(-500.f, 5000.f, 150.f), FVector(500.f, 5000.f, 150.f), None)
		&& PA250Cover::HasLineOfSight(W.World, FVector(-500.f, -5000.f, 150.f), FVector(500.f, -5000.f, 150.f), None),
		TEXT("tactics: an enemy behind a wall is not seen; in the open he is"));

	// Hit-and-run is not free: repositioning holds fire, has a cooldown, and slows the reload.
	UPA250CombatProfiles* Pr = UPA250CombatProfiles::MakeRuntimeDefaults();
	const FPA250TacticalRole& Light = Pr->RoleNamed(TEXT("Light"));
	const FPA250TacticalRole& Irr = Pr->RoleNamed(TEXT("Irregular"));
	const FPA250TacticalRole& Line = Pr->RoleNamed(TEXT("Line"));
	Log.Check(Light.bPrefersCover && Light.bWithdrawWhenExposed && Light.RepositionCooldownSeconds > 0.f
		&& Irr.bPrefersCover && Irr.RepositionCooldownSeconds > 0.f && !Line.bIndividualFire,
		TEXT("tactics: light/irregular prefer cover and pay a reposition cooldown; line volleys"));
	Log.Check(Line.bIndividualFire == false && Light.bIndividualFire && Irr.bIndividualFire,
		TEXT("tactics: coordinated volley mode is role-editable (Line on, Light/Irregular off)"));
	Log.Check(!FPA250MoraleOrders::For(EPA250Morale::Withdrawing).bMayVolley, TEXT("tactics: no shots while falling back"));

	// Firing lane: world block and a friendly in the way sit the shooter out.
	{
		ARFCharacter* Rear = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator);
		ARFCharacter* Front = W.World->SpawnActor<ARFCharacter>(FVector(250.f, 0.f, 100.f), FRotator::ZeroRotator);
		ARFCharacter* Foe = W.World->SpawnActor<ARFCharacter>(FVector(3000.f, 0.f, 100.f), FRotator::ZeroRotator);
		Log.Check(Rear && Front && Foe, TEXT("tactics: lane actors spawned"));
		if (Rear && Front && Foe)
		{
			Rear->FactionId = TEXT("british");
			Front->FactionId = TEXT("british");
			Foe->FactionId = TEXT("continental");
			W.Settle();
			const FVector Aim = Foe->GetActorLocation() + FVector(0.f, 0.f, 20.f);
			Log.Check(!UPA250VolleyCoordinator::HasClearFiringLane(W.World, Rear, Foe, Aim),
				TEXT("tactics: rear man blocked by friendly sits out"));
			Log.Check(UPA250VolleyCoordinator::HasClearFiringLane(W.World, Front, Foe, Aim),
				TEXT("tactics: front man with clear lane may fire"));
			ARFCharacter* Interloper = W.World->SpawnActor<ARFCharacter>(FVector(1500.f, 0.f, 100.f), FRotator::ZeroRotator);
			if (Interloper)
			{
				Interloper->FactionId = TEXT("continental");
				W.Settle();
				Log.Check(UPA250VolleyCoordinator::HasClearFiringLane(W.World, Front, Foe, Aim),
					TEXT("tactics: a different enemy in the lane is a hit, not a block"));
			}
		}
		AStaticMeshActor* Wall = W.SpawnBox(FVector(0.f, -5000.f, 150.f), FVector(40.f, 400.f, 300.f));
		ARFCharacter* Shooter = W.World->SpawnActor<ARFCharacter>(FVector(-400.f, -5000.f, 100.f), FRotator::ZeroRotator);
		ARFCharacter* Beyond = W.World->SpawnActor<ARFCharacter>(FVector(400.f, -5000.f, 100.f), FRotator::ZeroRotator);
		if (Wall && Shooter && Beyond)
		{
			Shooter->FactionId = TEXT("british");
			Beyond->FactionId = TEXT("continental");
			W.Settle();
			Log.Check(!UPA250VolleyCoordinator::HasClearFiringLane(W.World, Shooter, Beyond,
				Beyond->GetActorLocation() + FVector(0.f, 0.f, 20.f)),
				TEXT("tactics: world geometry in the lane blocks the shot"));
		}
	}
	{
		const FActiveReloadTuningPlain Arc = AMusketWeapon::PresetToSequencer(GetDefault<UPA250WeaponTuning>()->Values.Arcade);
		const FPA250AIReloadParams P = Pr->TrainingNamed(TEXT("Recruit")).ToReloadParams();
		float Still = 0.f, Moving = 0.f;
		FPA250AIReloadConditions C;
		for (int32 i = 0; i < 200; ++i)
		{
			C.bMoving = false;
			Still += SimAIReload(Arc, P, C, 900 + i).Seconds;
			C.bMoving = true;
			Moving += SimAIReload(Arc, P, C, 900 + i).Seconds;
		}
		Log.Check(Moving > Still * 1.05f, FString::Printf(TEXT("tactics: reloading on the move is slower (%.2f vs %.2f s avg)"), Moving / 200.f, Still / 200.f));
	}
	// Faction, training and role are independent: British light troops are one asset edit away.
	{
		UPA250CombatProfiles* Edit = UPA250CombatProfiles::MakeRuntimeDefaults();
		for (FPA250CombatAssignment& A : Edit->Assignments)
		{
			if (A.Faction == TEXT("british")) A.Role = TEXT("Light");
		}
		Log.Check(Edit->RoleFor(TEXT("british"), TEXT("line_infantry")).bPrefersCover
			&& Edit->TrainingFor(TEXT("british"), TEXT("line_infantry")).Name == TEXT("Veteran")
			&& !Edit->RoleFor(TEXT("continental"), TEXT("line_infantry")).bPrefersCover,
			TEXT("tactics: role is set independently of training and faction (British Veteran Light)"));
	}
}

void RunAudio(FLog& Log)
{
	// Every bank resolves (and has variety where it matters).
	struct FBank { const TCHAR* Name; int32 MinVariants; };
	for (const FBank& B : {FBank{TEXT("musket_fire"), 4}, FBank{TEXT("musket_distant"), 2}, FBank{TEXT("ball_whiz"), 3},
		FBank{TEXT("impact_body"), 2}, FBank{TEXT("impact_ground"), 2}, FBank{TEXT("ramrod"), 3},
		FBank{TEXT("flint_click"), 1}, FBank{TEXT("melee_swing"), 1}})
	{
		TSet<USoundBase*> Seen;
		for (int32 i = 0; i < 64; ++i) if (USoundBase* S = FPA250BattleAudio::Pick(B.Name)) Seen.Add(S);
		Log.Check(Seen.Num() >= B.MinVariants, FString::Printf(TEXT("audio: %s has %d variant(s) (want >= %d)"), B.Name, Seen.Num(), B.MinVariants));
	}
	// Every shouted line exists for both sides and is a real recording.
	int32 Missing = 0;
	float Shortest = 100.f;
	for (const TCHAR* Side : {TEXT("british"), TEXT("continental")})
	{
		for (const TCHAR* Key : {TEXT("make_ready"), TEXT("present"), TEXT("fire"), TEXT("forward"), TEXT("enemy_advancing"),
			TEXT("steady"), TEXT("fall_back"), TEXT("enemy_falling_back"), TEXT("breaking"), TEXT("enemy_running"),
			TEXT("rally"), TEXT("reformed"), TEXT("charge"), TEXT("charge_b")})
		{
			const USoundWave* W = Cast<USoundWave>(FPA250BattleAudio::Voice(FName(Side), Key));
			if (!W) { ++Missing; continue; }
			Shortest = FMath::Min(Shortest, W->Duration);
		}
	}
	Log.Check(Missing == 0 && Shortest >= 0.3f, FString::Printf(TEXT("audio: 28 shouted lines present (missing %d, shortest %.2f s)"), Missing, Shortest));

	// Gameplay actions make sound (counted at the hook; no audio device needed headless).
	FScopedTestWorld W;
	if (!W.World) { Log.Check(false, TEXT("audio: test world")); return; }
	FPA250BattleAudio::ResetPlayCounts();
	AMusketWeapon* Musket = W.World->SpawnActor<AMusketWeapon>();
	Musket->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
	Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
	Log.Check(Musket->Fire(FVector::ForwardVector) && FPA250BattleAudio::GetPlayCount(TEXT("musket_shot")) == 1, TEXT("audio: a shot is heard"));
	FRandomStream Rng(5);
	const FPA250AIReloadParams Vet = UPA250CombatProfiles::MakeRuntimeDefaults()->TrainingNamed(TEXT("Veteran")).ToReloadParams();
	for (float T = 0.f; !Musket->CanFire() && T < 20.f; T += 0.01f)
	{
		Musket->TickAIReload(Vet, FPA250AIReloadConditions(), Rng);
		Musket->TickActor(0.01f, LEVELTICK_All, Musket->PrimaryActorTick);
	}
	Log.Check(FPA250BattleAudio::GetPlayCount(TEXT("ramrod")) == 3, FString::Printf(TEXT("audio: one ramrod/shoulder sound per reload step (%d of 3)"), FPA250BattleAudio::GetPlayCount(TEXT("ramrod"))));

	UPA250WeaponTuning* JamTun = UPA250WeaponTuning::MakeRuntimeDefaults();
	JamTun->Values.Arcade.JamChanceOnMiss = 1.f;
	Musket->WeaponTuning = JamTun;
	Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
	Musket->Fire(FVector::ForwardVector);
	Musket->GetSequencer().Tick(0.01f);
	Musket->CommitReload();
	Log.Check(Musket->IsJammed() && FPA250BattleAudio::GetPlayCount(TEXT("flint_click")) >= 1, TEXT("audio: a jam snaps the flint"));

	ARFCharacter* Soldier = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 300.f, 100.f), FRotator::ZeroRotator);
	if (Soldier && Soldier->MeleeComponent)
	{
		Soldier->FactionId = TEXT("british");
		Soldier->MeleeComponent->Tuning = UPA250MeleeTuning::MakeRuntimeDefaults();
		Soldier->MeleeComponent->DoButtStroke();
		Log.Check(FPA250BattleAudio::GetPlayCount(TEXT("melee_swing")) >= 1, TEXT("audio: a butt-stroke swishes"));
		ARFCharacter* Charger = W.World->SpawnActor<ARFCharacter>(FVector(0.f, 900.f, 100.f), FRotator::ZeroRotator);
		Log.Check(Charger && Charger->MeleeComponent, TEXT("audio: spawn charger"));
		if (!Charger || !Charger->MeleeComponent) return;
		Charger->FactionId = TEXT("british");
		Charger->MeleeComponent->Tuning = UPA250MeleeTuning::MakeRuntimeDefaults();
		Charger->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		FPA250BattleAudio::ResetPlayCounts();
		Charger->MeleeComponent->DoCharge();
		Log.Check(FPA250BattleAudio::GetPlayCount(TEXT("vo:british/charge")) + FPA250BattleAudio::GetPlayCount(TEXT("vo:british/charge_b")) >= 1,
			TEXT("audio: a bayonet charge comes with a cry"));
	}

	// Hit feedback: a tick for a hit and a thump for a kill (2D, for the shooter), and a marker where his ball landed.
	Log.Check(FPA250BattleAudio::Pick(TEXT("hit_confirm")) && FPA250BattleAudio::Pick(TEXT("kill_confirm")), TEXT("audio: hit / kill confirmation sounds exist"));
	Log.Check(FPA250BattleAudio::Pick(TEXT("battle_begins")) != nullptr, TEXT("audio: battle-begins stinger exists"));
	{
		AActor* Miss = AMusketProjectile::SpawnImpactMarker(W.World, FVector(1000.f, 0.f, 50.f), false);
		AActor* HitM = AMusketProjectile::SpawnImpactMarker(W.World, FVector(1200.f, 0.f, 50.f), true);
		Log.Check(Miss && HitM && FMath::IsNearlyEqual(Miss->GetLifeSpan(), 1.5f, 0.05f) && HitM->GetActorScale3D().X > Miss->GetActorScale3D().X,
			TEXT("audio: the player's shots leave a 1.5 s impact marker (bigger + red on a man)"));
	}

	// A volley is thinned so it doesn't clip: at most 5 shots start in one 80 ms window.
	FPA250BattleAudio::ResetPlayCounts();
	int32 Started = 0;
	for (int32 i = 0; i < 8; ++i) Started += FPA250BattleAudio::PlayMusketShot(Musket, FVector(i * 200.f, 0.f, 100.f)) ? 1 : 0;
	Log.Check(Started == 5, FString::Printf(TEXT("audio: an 8-man volley starts %d shot sounds (want 5)"), Started));
}

void RunBattleRoad(FLog& Log)
{
	// The map: every stone wall is tagged as cover, the Battle Road walls flank the British approach,
	// and three militia muster points exist.
	{
		UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/PA250/Maps/L_Lexington.L_Lexington"));
		Log.Check(Map && Map->PersistentLevel, TEXT("battle road: L_Lexington loads"));
		if (Map && Map->PersistentLevel)
		{
			int32 Cover = 0, RoadCover = 0, Muster = 0;
			for (AActor* A : Map->PersistentLevel->Actors)
			{
				if (!A) continue;
				if (A->ActorHasTag(TEXT("PA250Cover")))
				{
					++Cover;
					// Loaded-not-started level: world transforms aren't computed yet; root relative = world.
					const FVector L = A->GetRootComponent() ? A->GetRootComponent()->GetRelativeLocation() : FVector::ZeroVector;
					RoadCover += (L.X > 1500.f && L.X < 7000.f) ? 1 : 0; // between the Green and the British start
				}
				if (A->IsA<APlayerStart>() && A->ActorHasTag(TEXT("continental_muster"))) ++Muster;
			}
			Log.Check(RoadCover >= 12, FString::Printf(TEXT("battle road: walls flank the British approach (%d tagged segments; %d cover in total)"), RoadCover, Cover));
			Log.Check(Muster == 3, FString::Printf(TEXT("battle road: 3 militia muster points (north, south, west) - found %d"), Muster));
		}
	}
	// Cover-line choice: a wall in front of the post (toward the enemy), distinct walls per squad.
	{
		UPA250VolleyCoordinator* V = NewObject<UPA250VolleyCoordinator>();
		V->SetCoverAnchorsForTest({FVector(2000.f, 0.f, 0.f), FVector(-3000.f, 0.f, 0.f), FVector(9000.f, 0.f, 0.f), FVector(2000.f, 2500.f, 0.f)});
		const FVector Post(0.f, 0.f, 0.f), EnemyRear(10000.f, 0.f, 0.f), OwnRear(-10000.f, 0.f, 0.f);
		TArray<FVector> Taken;
		FVector A, B;
		const bool bA = V->PickCoverLine(Post, EnemyRear, OwnRear, 5000.f, 1500.f, Taken, A);
		Log.Check(bA && A.Equals(FVector(1850.f, 0.f, 0.f), 1.f), FString::Printf(TEXT("battle road: squad takes the wall in front of its post, on its own side (%s)"), *A.ToString()));
		Taken.Add(A);
		const bool bB = V->PickCoverLine(Post, EnemyRear, OwnRear, 5000.f, 1500.f, Taken, B);
		Log.Check(bB && FMath::IsNearlyEqual(B.Y, 2500.f, 1.f), TEXT("battle road: the next squad takes a different wall (>= 15 m apart)"));
		Taken.Add(B);
		FVector C;
		Log.Check(!V->PickCoverLine(Post, EnemyRear, OwnRear, 5000.f, 1500.f, Taken, C), TEXT("battle road: walls far behind or out of reach are not used"));
	}
	// Ducking behind a wall: a crouched man is shorter than the wall; he rises only to fire.
	{
		Log.Check(ARFCharacter::CrouchedHalfHeightCm * 2.f < 110.f, TEXT("battle road: a crouched man (1.0 m) is fully behind a 1.1 m wall"));
		Log.Check(PA250ShouldDuck(true, true, false, true, false, false), TEXT("battle road: reloading behind the wall -> duck"));
		Log.Check(!PA250ShouldDuck(true, true, true, true, false, false), TEXT("battle road: loaded with a target in sight -> rise and fire"));
		Log.Check(PA250ShouldDuck(true, true, true, false, false, false), TEXT("battle road: loaded, nobody in sight -> stay down"));
		Log.Check(!PA250ShouldDuck(false, true, false, true, false, false) && !PA250ShouldDuck(true, false, false, true, false, false),
			TEXT("battle road: no ducking in the open or on the move"));
		Log.Check(!PA250ShouldDuck(true, true, false, true, true, false) && !PA250ShouldDuck(true, true, false, true, false, true),
			TEXT("battle road: routing or charging men don't duck"));
		FScopedTestWorld W;
		if (W.World)
		{
			W.SpawnBox(FVector(0.f, 0.f, 55.f), FVector(20.f, 2000.f, 110.f)); // 1.1 m wall at x = 0
			ARFCharacter* Man = W.World->SpawnActor<ARFCharacter>(FVector(-100.f, 0.f, 90.f), FRotator::ZeroRotator);
			W.Settle();
			if (Man)
			{
				const FVector Eye(3000.f, 0.f, 160.f);
				const TArray<const AActor*> Ignore = {Man, Man->Musket.Get()};
				const bool bHeadStanding = PA250Cover::HasLineOfSight(W.World, Eye, FVector(-100.f, 0.f, 170.f), Ignore);
				Man->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				Man->GetCharacterMovement()->bWantsToCrouch = true;
				Man->GetCharacterMovement()->Crouch(false);
				const float Top = Man->GetActorLocation().Z + Man->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const bool bTopHidden = Top <= 105.f && !PA250Cover::HasLineOfSight(W.World, Eye, FVector(-100.f, 0.f, Top - 5.f), Ignore);
				Log.Check(bHeadStanding && Man->bIsCrouched && bTopHidden,
					FString::Printf(TEXT("battle road: standing, his head shows over the wall; ducked (%s, capsule top %.0f), he is hidden"),
						Man->bIsCrouched ? TEXT("crouched") : TEXT("NOT crouched"), Top));
			}
		}
	}

	// Militia muster (Lexington 1775 setup: 4 line + 18 militia units, 16 places per side).
	{
		int32 Others = 0, Initial = 0, Cap = 0;
		const FPA250BotTuningValues MT;
		APA250BattleGameMode::MusterPlan(16, 18, 22, MT.MusterInitialFraction, MT.MusterMaxAliveScale, Others, Initial, Cap);
		Log.Check(Others == 3 && Initial == 7 && Cap == 26,
			FString::Printf(TEXT("battle road: Americans start with %d line + %d militia, militia grow to %d (16 British all present)"), Others, Initial, Cap));
		int32 None = 0, X = 0, C2 = 0;
		APA250BattleGameMode::MusterPlan(16, 0, 24, 0.35f, 1.6f, None, X, C2);
		Log.Check(None == 16 && X == 0, TEXT("battle road: a side with no militia deploys normally"));
		Log.Check(APA250BattleGameMode::MusterWave(21, 5, 100, 8, 4) == 4, TEXT("battle road: a company of 4 arrives"));
		Log.Check(APA250BattleGameMode::MusterWave(21, 19, 100, 22, 4) == 2, TEXT("battle road: companies stop at the militia cap"));
		Log.Check(APA250BattleGameMode::MusterWave(21, 5, 10, 8, 4) == 2, TEXT("battle road: never more men in the field than tickets left"));
		Log.Check(APA250BattleGameMode::MusterWave(21, 5, 6, 8, 4) == 0, TEXT("battle road: no arrivals once tickets are spent"));
	}
}

void RunScenario(FLog& Log)
{
	const FPA250ScenarioFight Lex = APA250BattleGameMode::ResolveScenario(TEXT("lexington_common"), {});
	Log.Check(!Lex.bContestSingleCrossing && !Lex.bRewardForcePreservation && APA250BattleGameMode::UsesPostBleed(Lex),
		TEXT("scenario: Lexington keeps three posts and post bleed"));

	const FPA250ScenarioFight Concord = APA250BattleGameMode::ResolveScenario(TEXT("concord_bridge"), {TEXT("woods"), TEXT("bridge_crossing")});
	Log.Check(Concord.bContestSingleCrossing && !Concord.bRewardForcePreservation,
		TEXT("scenario: Concord uses the bridge preset (three contestable posts)"));
	const FPA250ScenarioFight BridgeTag = APA250BattleGameMode::ResolveScenario(TEXT(""), {TEXT("bridge_crossing")});
	Log.Check(BridgeTag.bContestSingleCrossing && !BridgeTag.bRewardForcePreservation,
		TEXT("scenario: bridge_crossing tag is read without the preset label"));

	const FPA250ScenarioFight Road = APA250BattleGameMode::ResolveScenario(TEXT("battle_road"), {TEXT("return_march")});
	Log.Check(!Road.bContestSingleCrossing && Road.bRewardForcePreservation && !APA250BattleGameMode::UsesPostBleed(Road),
		TEXT("scenario: Battle Road rewards force preservation and does not bleed for ground"));
	const FPA250ScenarioFight RoadTag = APA250BattleGameMode::ResolveScenario(TEXT("lexington_common"), {TEXT("return_march")});
	Log.Check(RoadTag.bRewardForcePreservation, TEXT("scenario: return_march tag is read even under a generic preset"));

	const FName Kept = APA250BattleGameMode::PreservationWinner(8, 10, 30, 40, TEXT("continental"), TEXT("british"), TEXT("british"));
	Log.Check(Kept == TEXT("continental"), TEXT("scenario: 8/10 kept beats 30/40 kept"));
	const FName Tied = APA250BattleGameMode::PreservationWinner(10, 20, 5, 10, TEXT("british"), TEXT("continental"), TEXT("continental"));
	Log.Check(Tied == TEXT("continental"), TEXT("scenario: equal preservation goes to the defender"));

	FScopedTestWorld W;
	Log.Check(W.World != nullptr, TEXT("scenario: test world"));
	if (!W.World)
	{
		return;
	}
	APA250ControlPoint* Green = W.World->SpawnActor<APA250ControlPoint>(FVector(0.f, 0.f, 0.f), FRotator::ZeroRotator);
	APA250ControlPoint* House = W.World->SpawnActor<APA250ControlPoint>(FVector(3000.f, 0.f, 0.f), FRotator::ZeroRotator);
	APA250ControlPoint* Buck = W.World->SpawnActor<APA250ControlPoint>(FVector(-3000.f, 0.f, 0.f), FRotator::ZeroRotator);
	Log.Check(Green && House && Buck, TEXT("scenario: three posts spawn"));
	if (!Green || !House || !Buck)
	{
		return;
	}
	Green->PostId = TEXT("green");
	House->PostId = TEXT("meetinghouse");
	Buck->PostId = TEXT("buckman");
	APA250BattleGameMode::ConfigurePostForScenario(Green, Concord);
	APA250BattleGameMode::ConfigurePostForScenario(House, Concord);
	APA250BattleGameMode::ConfigurePostForScenario(Buck, Concord);
	Log.Check(!Green->IsContestLocked() && Green->GetHolder().IsNone(), TEXT("scenario: North Bridge starts neutral and can be taken"));
	Log.Check(!House->IsContestLocked() && House->GetHolder() == TEXT("continental"), TEXT("scenario: Muster ground starts Continental and is contestable"));
	Log.Check(!Buck->IsContestLocked() && Buck->GetHolder() == TEXT("british"), TEXT("scenario: Far bridgehead starts British and is contestable"));
	Log.Check(House->GetObjectiveTitle() == TEXT("Muster ground"), TEXT("scenario: meetinghouse labeled Muster ground"));
	Log.Check(Buck->GetObjectiveTitle() == TEXT("Far bridgehead"), TEXT("scenario: buckman labeled Far bridgehead"));
	const bool bHouseMoved = House->ApplyCapturePressure(TEXT("british"), 6, 0, 30.f);
	// Capture of a held post first clears the holder, then a second push assigns the capturer.
	const bool bHouseTaken = House->ApplyCapturePressure(TEXT("british"), 6, 0, 30.f);
	Log.Check(bHouseMoved && bHouseTaken && House->GetHolder() == TEXT("british"),
		TEXT("scenario: muster ground can change hands"));
	const bool bGreenMoved = Green->ApplyCapturePressure(TEXT("continental"), 4, 0, 10.f);
	Log.Check(bGreenMoved && Green->GetHolder() == TEXT("continental"), TEXT("scenario: the crossing changes hands"));
	Log.Check(Green->GetObjectiveTitle() == TEXT("North Bridge"), TEXT("scenario: the crossing post is the North Bridge"));
	const FPA250CreekBridge Span = APA250BattleGameMode::PlanCreekBridge(FVector(100.f, 200.f, 50.f));
	Log.Check(Span.SpansCreek(), FString::Printf(TEXT("scenario: deck %.0f cm crosses a %.0f cm creek"), Span.DeckSizeCm.X, Span.CreekSizeCm.X));
	const int32 Pieces = APA250BattleGameMode::SpawnCreekBridge(W.World, FVector(0.f, 0.f, 0.f));
	int32 Creek = 0, Deck = 0;
	for (TActorIterator<AStaticMeshActor> It(W.World); It; ++It)
	{
		Creek += It->Tags.Contains(TEXT("PA250Creek")) ? 1 : 0;
		Deck += It->Tags.Contains(TEXT("PA250Bridge")) ? 1 : 0;
	}
	Log.Check(Pieces == 6 && Creek == 3 && Deck == 3, FString::Printf(TEXT("scenario: creek and bridge placed (%d pieces, %d water, %d span)"), Pieces, Creek, Deck));

	APA250BattleGameMode::ConfigurePostForScenario(Green, Road);
	APA250BattleGameMode::ConfigurePostForScenario(House, Road);
	APA250BattleGameMode::ConfigurePostForScenario(Buck, Road);
	Log.Check(!Green->IsContestLocked() && Green->GetHolder() == TEXT("british") && House->GetHolder().IsNone() && Buck->GetHolder().IsNone(),
		TEXT("scenario: Battle Road holds the road and leaves the flanks open"));
}
} // namespace PA250BattleCases
