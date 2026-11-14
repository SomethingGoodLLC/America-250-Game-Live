#include "PA250BattleCases.h"
#include "PA250UnitClasses.h"
#include "UI/PA250BattleCards.h"
#include "UI/PA250CampaignArcPanels.h"
#include "Components/CapsuleComponent.h"
#include "PA250BotController.h"
#include "PA250BattleGameMode.h"
#include "ActiveReloadSequencer.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "PhysicsEngine/BodySetup.h"
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
#include "Campaign/PA250Subsystem.h"
#include "Campaign/PA250MapWidget.h"
#include "Battle/PA250FieldWork.h"
#include "Battle/PA250Support.h"
#include "Battle/PA250FieldGun.h"
#include "Battle/PA250BattleModes.h"
#include "Engine/StaticMeshSocket.h"
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
	// Curve of 2026-11-13 (user: a man hit by a ball should go down): full to 60 m, 80 at 100 m.
	// Before: full to 40 m, 60 at 100 m.
	Log.Check(V.DamageAtDistanceCm(2000.f) == 100.f && V.DamageAtDistanceCm(6000.f) == 100.f, TEXT("weapon: 100 inside 60 m"));
	Log.Check(V.DamageAtDistanceCm(8000.f) == 90.f, TEXT("weapon: 90 at 80 m (linear)"));
	Log.Check(V.DamageAtDistanceCm(10000.f) == 80.f, TEXT("weapon: 80 at 100 m"));
	Log.Check(V.DamageAtDistanceCm(19000.f) == 80.f, TEXT("weapon: 80 out to max range"));
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

	// The player loads standing or at his ordinary pace, never at a run; bots and riders are not held to it.
	Log.Check(!ARFCharacter::ReloadBlockedByMovement(true, false, 0.f, 450.f) && !ARFCharacter::ReloadBlockedByMovement(true, false, 450.f, 450.f),
		TEXT("reload on the move: the player may load standing and at his ordinary pace"));
	Log.Check(ARFCharacter::ReloadBlockedByMovement(true, false, 650.f, 450.f), TEXT("reload on the move: the player cannot load at a run"));
	Log.Check(!ARFCharacter::ReloadBlockedByMovement(false, false, 650.f, 450.f) && !ARFCharacter::ReloadBlockedByMovement(true, true, 650.f, 450.f),
		TEXT("reload on the move: bots and riders are not held to it"));
	if (AMusketWeapon* Paused = W.World->SpawnActor<AMusketWeapon>())
	{
		Paused->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
		Paused->SetReloadPreset(EPA250ReloadPreset::Arcade);
		Paused->StartReload();
		Paused->TickActor(0.5f, LEVELTICK_All, Paused->PrimaryActorTick);
		const EActiveReloadPhase Phase = Paused->GetSequencer().GetPhase();
		const float Before = Paused->GetSequencer().GetPhaseProgress();
		const int32 Misses = Paused->GetReloadStats().Miss;
		Paused->SetReloadPaused(true);
		for (int32 i = 0; i < 20; ++i) Paused->TickActor(0.5f, LEVELTICK_All, Paused->PrimaryActorTick);
		Log.Check(Before > 0.f && Paused->IsReloadPaused() && Paused->IsReloading() && Paused->GetSequencer().GetPhase() == Phase && Paused->GetSequencer().GetPhaseProgress() == Before,
			FString::Printf(TEXT("reload on the move: a paused reload keeps its place over 10 s (%.3f -> %.3f)"), Before, Paused->GetSequencer().GetPhaseProgress()));
		Log.Check(Paused->CommitReload() == EActiveReloadHit::None && Paused->GetReloadStats().Miss == Misses, TEXT("reload on the move: a press while paused does nothing and is no fumble"));
		Paused->SetReloadPaused(false);
		Paused->TickActor(0.2f, LEVELTICK_All, Paused->PrimaryActorTick);
		Log.Check(Paused->GetSequencer().GetPhase() != Phase || Paused->GetSequencer().GetPhaseProgress() > Before, TEXT("reload on the move: it carries on from the same point once he slows"));
		Paused->Destroy();
	}

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
	Log.Check(!APA250BattleGameMode::SideIsOut(0, 6, true, 5, false) && APA250BattleGameMode::SideIsOut(0, 6, true, 0, false),
		TEXT("exhaustion: 0 tickets with men standing is not out; 0 tickets and the last man down is"));
	Log.Check(!APA250BattleGameMode::SideIsOut(0, 6, true, 1, true), TEXT("exhaustion: not out while the player is alive on that side"));
	Log.Check(!APA250BattleGameMode::SideIsOut(3, 6, true, 0, false), TEXT("exhaustion: tickets left = not out"));
	Log.Check(!APA250BattleGameMode::SideIsOut(0, 0, false, 0, false) && APA250BattleGameMode::SideIsOut(0, 0, true, 0, false)
		&& !APA250BattleGameMode::SideIsOut(0, 0, true, 2, false), TEXT("exhaustion: a side that started with no tickets is out only once fielded and wiped"));

	// A remnant breaks: no reserve, a handful left, never the player's own side while he lives.
	Log.Check(APA250BattleGameMode::RemnantBreaks(0, 3, false, 3) && APA250BattleGameMode::RemnantBreaks(0, 1, false, 3)
		&& !APA250BattleGameMode::RemnantBreaks(0, 4, false, 3) && !APA250BattleGameMode::RemnantBreaks(0, 0, false, 3),
		TEXT("remnant: no reserve and a handful standing breaks; a larger body or nobody does not"));
	Log.Check(!APA250BattleGameMode::RemnantBreaks(2, 1, false, 3) && !APA250BattleGameMode::RemnantBreaks(0, 1, true, 3),
		TEXT("remnant: not while a reserve is left, never under a living player"));

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

	// The result counts men, straight from the ledger (until 2026-11-14 deaths were rounded to units
	// and capped at the ticket pool). Nine deaths are nine men: killed and wounded by the sim's ratio.
	{
		pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
		TMap<FName, TMap<FName, int32>> Deaths, Gave;
		Deaths.FindOrAdd(TEXT("continental")).Add(TEXT("militia"), 9);
		Deaths.FindOrAdd(TEXT("continental")).Add(TEXT("officer"), 1);
		Deaths.FindOrAdd(TEXT("british")).Add(TEXT("line_infantry"), 100);
		Gave.FindOrAdd(TEXT("continental")).Add(TEXT("militia"), 2);
		Gave.FindOrAdd(TEXT("british")).Add(TEXT("grenadier"), 3);
		const auto Cas = APA250BattleGameMode::CasualtiesFromLedger(Deaths, Gave);
		const pa250::MenDown Mil = Cas.at("continental").at("militia");
		Log.Check(Mil.killed + Mil.wounded == 9 && Mil.wounded == pa250::split_fallen(9).wounded && Mil.prisoners == 2,
			FString::Printf(TEXT("conquest: 9 deaths and 2 surrendered are 9 men down and 2 prisoners (%d killed, %d wounded, %d prisoners)"), Mil.killed, Mil.wounded, Mil.prisoners));
		Log.Check(Cas.at("continental").at("officer").total() == 1, TEXT("conquest: one death is one man, not rounded away"));
		Log.Check(Cas.at("british").at("line_infantry").total() == 100 && Cas.at("british").at("grenadier").prisoners == 3 && Cas.at("british").at("grenadier").killed == 0,
			TEXT("conquest: a hundred deaths are a hundred men; a class that only surrendered has no dead"));
	}
	// A side cannot reinforce with men its army does not have, whatever its tickets say.
	Log.Check(APA250BattleGameMode::ReserveFor(50, 88, 60, 19) == 9 && APA250BattleGameMode::ReserveFor(5, 88, 10, 19) == 5
		&& APA250BattleGameMode::ReserveFor(38, 88, 79, 9) == 0 && APA250BattleGameMode::ReserveFor(38, 88, 88, 0) == 0,
		TEXT("conquest: the reserve is never more tickets than men nor more men than tickets (88 men, 79 down, 9 standing: none)"));
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
		Log.Check(Gren.MaxHealth == Line.MaxHealth && Gren.MeleeScale > Line.MeleeScale && Gren.SpeedScale < Line.SpeedScale,
			TEXT("classes: grenadiers hit harder with the bayonet, are no faster, and a ball stops them like a line soldier"));
		{
			const FPA250WeaponTuningValues& V = GetDefault<UPA250WeaponTuning>()->Values;
			float Toughest = 0.f;
			for (const TCHAR* F : {TEXT("british"), TEXT("continental")})
				for (const TCHAR* C : {TEXT("line_infantry"), TEXT("grenadier"), TEXT("light_infantry"), TEXT("officer"), TEXT("militia"), TEXT("rifleman")})
					Toughest = FMath::Max(Toughest, PA250UnitClasses::KitFor(F, C).MaxHealth);
			Log.Check(V.TorsoDamageFar * V.HeadMultiplier >= Toughest && V.DamageAtDistanceCm(V.FalloffStartMeters * 100.f) >= Toughest,
				FString::Printf(TEXT("damage: a head hit kills any class at any range, a body hit within %.0f m (toughest %.0f health)"), V.FalloffStartMeters, Toughest));
		}
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
			TEXT("rally"), TEXT("reformed"), TEXT("charge"), TEXT("charge_b"), TEXT("follow_me"), TEXT("charge_bayonets")})
		{
			const USoundWave* W = Cast<USoundWave>(FPA250BattleAudio::Voice(FName(Side), Key));
			if (!W) { ++Missing; continue; }
			Shortest = FMath::Min(Shortest, W->Duration);
		}
	}
	Log.Check(Missing == 0 && Shortest >= 0.3f, FString::Printf(TEXT("audio: 32 shouted lines present, the leader's orders among them (missing %d, shortest %.2f s)"), Missing, Shortest));

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
					// The walls along the column's approach carry their own tag (build_levels.py); the
					// Common is at its 1775 size and bearing now, so an X range no longer picks them out.
					RoadCover += A->ActorHasTag(TEXT("PA250BattleRoad")) ? 1 : 0;
				}
				if (A->IsA<APlayerStart>() && A->ActorHasTag(TEXT("continental_muster"))) ++Muster;
			}
			Log.Check(RoadCover >= 12, FString::Printf(TEXT("battle road: walls flank the British approach (%d tagged segments; %d cover in total)"), RoadCover, Cover));
			Log.Check(Muster == 3, FString::Printf(TEXT("battle road: 3 militia muster points (north, south, west) - found %d"), Muster));
		}
	}
	// Concord has its own level: three posts, a river that stops men and the built bridge.
	{
		Log.Check(UPA250Subsystem::BattleLevelForPreset(TEXT("concord_bridge")) == FName(TEXT("/Game/PA250/Maps/L_Concord")),
			TEXT("concord: the bridge fight travels to L_Concord"));
		Log.Check(UPA250Subsystem::BattleLevelForPreset(TEXT("battle_road")) == FName(TEXT("/Game/PA250/Maps/L_Lexington"))
			&& UPA250Subsystem::BattleLevelForPreset(TEXT("lexington_common")) == FName(TEXT("/Game/PA250/Maps/L_Lexington")),
			TEXT("concord: the Common and Battle Road fights stay on L_Lexington"));
		UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/PA250/Maps/L_Concord.L_Concord"));
		Log.Check(Map && Map->PersistentLevel, TEXT("concord: L_Concord loads"));
		if (Map && Map->PersistentLevel)
		{
			int32 PostCount = 0, Crossing = 0, Channel = 0, Cover = 0, Starts = 0, MenOnly = 0, BridgeHulls = 0;
			for (AActor* A : Map->PersistentLevel->Actors)
			{
				if (!A) continue;
				if (const AStaticMeshActor* SM = Cast<AStaticMeshActor>(A))
				{
					const UStaticMeshComponent* C = SM->GetStaticMeshComponent();
					// Saved state: every block stops a man. The game narrows them to men only when the
					// level starts (APA250BattleGameMode::SealLevelRiver), so shot and sight cross the water.
					if (A->ActorHasTag(TEXT("PA250Creek")) && C && C->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics
						&& C->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
					{
						++MenOnly;
					}
					if (A->ActorHasTag(TEXT("PA250LevelCrossing")) && C && C->GetStaticMesh() && C->GetStaticMesh()->GetBodySetup()
						&& C->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
					{
						BridgeHulls = C->GetStaticMesh()->GetBodySetup()->AggGeom.ConvexElems.Num();
					}
				}
				PostCount += A->IsA<APA250ControlPoint>() ? 1 : 0;
				Crossing += A->ActorHasTag(TEXT("PA250LevelCrossing")) ? 1 : 0;
				Channel += A->ActorHasTag(TEXT("PA250Creek")) ? 1 : 0;
				Cover += A->ActorHasTag(TEXT("PA250Cover")) ? 1 : 0;
				Starts += (A->IsA<APlayerStart>() && (A->ActorHasTag(TEXT("british")) || A->ActorHasTag(TEXT("continental")))) ? 1 : 0;
			}
			Log.Check(PostCount == 3 && Crossing == 1, FString::Printf(TEXT("concord: 3 posts and 1 built bridge (found %d, %d)"), PostCount, Crossing));
			Log.Check(Channel >= 62, FString::Printf(TEXT("concord: the river channel and the bridge's sides are closed to men (%d blocks)"), Channel));
			Log.Check(MenOnly == Channel, FString::Printf(TEXT("concord: every channel block stops a man (%d of %d)"), MenOnly, Channel));
			// Deck and abutments only: a hull on the railings stopped every ball fired across them.
			Log.Check(BridgeHulls == 10, FString::Printf(TEXT("concord: the bridge carries deck collision and none on its rails (%d hulls)"), BridgeHulls));
			Log.Check(Cover >= 40 && Starts == 28, FString::Printf(TEXT("concord: walls for cover and 28 starts (%d, %d)"), Cover, Starts));
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
	FName East, West;
	APA250BattleGameMode::ConcordBanks(TEXT("continental"), East, West);
	Log.Check(East == TEXT("continental") && West == TEXT("british"),
		TEXT("concord: the town's owner takes the east bank, the attacker the west"));
	APA250BattleGameMode::ConcordBanks(TEXT("british"), East, West);
	Log.Check(East == TEXT("british") && West == TEXT("continental"),
		TEXT("concord: a British-held Concord keeps the east bank"));
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
	{
		// A level-built river: its blocks end up stopping men and nothing else.
		AStaticMeshActor* Block = W.World->SpawnActor<AStaticMeshActor>(FVector(0.f, 9000.f, 0.f), FRotator::ZeroRotator);
		Block->Tags.Add(TEXT("PA250Creek"));
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		const int32 Sealed = APA250BattleGameMode::SealLevelRiver(W.World);
		const UStaticMeshComponent* C = Block->GetStaticMeshComponent();
		Log.Check(Sealed == 1 && C->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
			&& C->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore
			&& C->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Ignore
			&& C->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Ignore,
			TEXT("scenario: a level river stops men but not shot or sight"));
		Block->Destroy();
	}
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

void RunCampaignRead(FLog& Log)
{
	// The battle's numbers come from the sim's rules file, the same one the campaign uses.
	pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
	Log.Check(pa250::battle_rules_loaded(), TEXT("campaign read: battle_rules.json loads"));
	const pa250::BattleRules R = pa250::battle_rules();
	Log.Check(R.bot_budget == 40 && R.field_min == 10 && R.field_max == 30 && R.soldiers_per_unit == 4,
		TEXT("campaign read: field budget 40, 10 to 30 a side, 4 soldiers a unit"));

	// Field strength: the sim's split is what the battle spawns.
	const pa250::FieldSplit Opening = pa250::split_field_strength(96, 88);
	const pa250::FieldSplit TwoToOne = pa250::split_field_strength(80, 40);
	Log.Check(Opening.a == 21 && Opening.b == 19, FString::Printf(TEXT("campaign read: 96 v 88 fields 21 v 19 (%d v %d)"), Opening.a, Opening.b));
	Log.Check(TwoToOne.a == 27 && TwoToOne.b == 13, FString::Printf(TEXT("campaign read: 2:1 fields 27 v 13 (%d v %d)"), TwoToOne.a, TwoToOne.b));

	// army_broke: 60 % of the side's squads withdrawing or routing.
	Log.Check(PA250EndRules::BrokenSquadsNeeded(5, 0.6f) == 3 && PA250EndRules::BrokenSquadsNeeded(3, 0.6f) == 2
		&& PA250EndRules::BrokenSquadsNeeded(4, 0.6f) == 3 && PA250EndRules::BrokenSquadsNeeded(0, 0.6f) == 0,
		TEXT("campaign read: 60 % of 5 squads is 3, of 4 is 3, of 3 is 2"));
	Log.Check(PA250EndRules::ArmyIsBreaking(5, 3, 5, 300.f, R), TEXT("campaign read: 3 of 5 squads falling back is a breaking army"));
	Log.Check(!PA250EndRules::ArmyIsBreaking(5, 2, 5, 300.f, R), TEXT("campaign read: 2 of 5 is not"));
	// A skirmisher's fall-back from the open or from a charge is his drill, not a break.
	Log.Check(PA250SquadIsBroken(EPA250Morale::Routing, false) && PA250SquadIsBroken(EPA250Morale::Routing, true)
		&& PA250SquadIsBroken(EPA250Morale::Withdrawing, false) && !PA250SquadIsBroken(EPA250Morale::Withdrawing, true)
		&& !PA250SquadIsBroken(EPA250Morale::Shaken, false) && !PA250SquadIsBroken(EPA250Morale::Steady, false),
		TEXT("campaign read: a rout always counts toward a breaking army, a forced withdrawal counts, a drilled fall-back does not"));
	Log.Check(R.remnant_men >= 1 && R.remnant_mark_men >= R.remnant_men && R.remnant_seconds > 0.f, TEXT("campaign read: remnant numbers come from battle_rules.json"));
	// Guard 1: never in the first 120 seconds.
	Log.Check(!PA250EndRules::ArmyIsBreaking(5, 5, 5, R.army_broke_min_elapsed_seconds - 1.f, R)
		&& PA250EndRules::ArmyIsBreaking(5, 5, 5, R.army_broke_min_elapsed_seconds, R),
		TEXT("campaign read: an army cannot break in the first 120 seconds"));
	// Guard 2: never a side that fielded fewer than 3 squads; one worn down from 3 still can.
	Log.Check(!PA250EndRules::ArmyIsBreaking(2, 2, 2, 300.f, R), TEXT("campaign read: a side that only ever formed 2 squads cannot break"));
	Log.Check(!PA250EndRules::ArmyIsBreaking(1, 1, 1, 300.f, R), TEXT("campaign read: one squad falling back is not an army breaking"));
	Log.Check(PA250EndRules::ArmyIsBreaking(2, 2, 3, 300.f, R), TEXT("campaign read: a side worn down from 3 squads to 2 can still break"));
	Log.Check(!PA250EndRules::ArmyIsBreaking(0, 0, 4, 300.f, R), TEXT("campaign read: no squads left is tickets' business, not a break"));

	// posts_swept: every post, after the opening minute, and only posts the enemy has held.
	Log.Check(PA250EndRules::PostsAreSwept(3, 3, true, 90.f, R), TEXT("campaign read: holding all 3 posts the enemy once held is a sweep"));
	Log.Check(!PA250EndRules::PostsAreSwept(2, 3, true, 90.f, R), TEXT("campaign read: 2 of 3 is not"));
	Log.Check(!PA250EndRules::PostsAreSwept(3, 3, false, 900.f, R), TEXT("campaign read: no sweep while the enemy has never held a post (a garrison cannot win by standing still)"));
	Log.Check(!PA250EndRules::PostsAreSwept(3, 3, true, R.posts_swept_min_elapsed_seconds - 1.f, R), TEXT("campaign read: no sweep in the opening minute"));
	Log.Check(!PA250EndRules::PostsAreSwept(0, 0, true, 900.f, R), TEXT("campaign read: a field without posts cannot be swept"));
	Log.Check(FString(PA250EndRules::EndReasonWords(TEXT("army_broke"))) != FString(PA250EndRules::EndReasonWords(TEXT("posts_swept")))
		&& FString(PA250EndRules::EndReasonWords(TEXT("nonsense"))) == TEXT("the fight ended"),
		TEXT("campaign read: each end reason has its own plain words"));
	for (const TCHAR* Reason : {TEXT("tickets_exhausted"), TEXT("time_limit"), TEXT("force_preserved"), TEXT("routed"), TEXT("army_broke"), TEXT("posts_swept")})
	{
		Log.Check(pa250::known_end_reason(TCHAR_TO_UTF8(Reason)), FString::Printf(TEXT("campaign read: the sim knows end reason %s"), Reason));
	}

	// The cards read the same setup and report (text only here; the window is checked by -PA250CardProbe).
	{
		pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		St.armies.at("british_regulars").previous_province = "boston";
		St.armies.at("british_regulars").province = "charlestown";
		St.armies.at("continental_militia").province = "charlestown";
		St.pending_battle = {true, "charlestown", "british_regulars", "continental_militia"};
		const pa250::BattleSetup Setup = pa250::build_battle_setup(St, "charlestown");
		const TArray<PA250BattleCards::FRow> Mine = PA250BattleCards::SideRows(Setup.sides.at("continental"));
		auto Find = [](const TArray<PA250BattleCards::FRow>& Rows, const TCHAR* Label) -> const PA250BattleCards::FRow* {
			for (const PA250BattleCards::FRow& Row : Rows) if (Row.Label == Label) return &Row;
			return nullptr;
		};
		bool bAllSourced = Mine.Num() >= 6;
		for (const PA250BattleCards::FRow& Row : Mine) bAllSourced &= !Row.Source.IsEmpty() && !Row.Value.IsEmpty();
		Log.Check(bAllSourced, TEXT("cards: every pre-battle line has a value and a source"));
		const PA250BattleCards::FRow* Field = Find(Mine, TEXT("On the field"));
		const PA250BattleCards::FRow* Morale = Find(Mine, TEXT("Morale"));
		const PA250BattleCards::FRow* Fort = Find(Mine, TEXT("Fortification"));
		Log.Check(Find(Mine, TEXT("Men")) && Find(Mine, TEXT("Men"))->Value == TEXT("88") && Field && Field->Value == TEXT("19"),
			TEXT("cards: 88 men, 19 on the field"));
		Log.Check(Find(Mine, TEXT("Rounds a man")) != nullptr, TEXT("cards: rounds a man is on the card"));
		Log.Check(Morale && Morale->Value == TEXT("73") && Fort && Fort->Value == TEXT("L1") && Fort->Source.Contains(TEXT("Fort L1, Charlestown")),
			TEXT("cards: the fort shows as L1 with its name, and morale is the final 73 (70 + 3)"));
		bool bFortChange = false;
		for (const PA250BattleCards::FRow& Row : Mine) bFortChange |= Row.bChange && Row.Value == TEXT("+3") && Row.Source.Contains(TEXT("Fort L1"));
		Log.Check(bFortChange, TEXT("cards: the fort's +3 morale is its own line"));
		const TArray<PA250BattleCards::FRow> Enemy = PA250BattleCards::SideRows(Setup.sides.at("british"));
		const PA250BattleCards::FRow* Theirs = Find(Enemy, TEXT("Rounds a man"));
		// Carried rounds (2026-11-14): a column has its boxes and no magazine behind it in the fight.
		Log.Check(Theirs && Theirs->Value == TEXT("12") && Theirs->Source.Contains(TEXT("what the men carry in their boxes")),
			TEXT("cards: the column's rounds are the boxes its men carry"));

		pa250::BattleResult R2;
		R2.province_id = "charlestown";
		R2.winner = "british";
		R2.end_reason = "posts_swept";
		R2.losses["continental"]["militia"] = 3;
		R2.prisoners["continental"]["militia"] = 2;
		const pa250::CampaignState After = pa250::apply_battle_result(St, R2);
		const PA250BattleCards::FAfter Card = PA250BattleCards::AfterBattle(After.progress.last_battle_report, After);
		Log.Check(Card.Title == TEXT("Battle of Charlestown") && Card.Verdict == TEXT("Defeat") && Card.Reason.Contains(TEXT("every post")),
			FString::Printf(TEXT("cards: after-battle title, verdict and reason (%s / %s / %s)"), *Card.Title, *Card.Verdict, *Card.Reason));
		// Three units killed and two taken, in men (4 a unit): the card counts men.
		Log.Check(Card.LossHeading == TEXT("Losses (men)") && Card.Losses.Num() == 2 && Card.Losses[0].Contains(TEXT("12 killed")),
			FString::Printf(TEXT("cards: losses for both sides, in men (%s)"), Card.Losses.Num() ? *Card.Losses[0] : TEXT("none")));
		Log.Check(Card.Prisoners.Num() == 2 && Card.Prisoners[1].Contains(TEXT("8 men")), TEXT("cards: prisoners taken from the player, in men"));
		{
			// A battle's own count: 5 killed, 7 wounded, 3 prisoners of the militia; 2 and 1 of the regulars.
			pa250::BattleResult Rm;
			Rm.province_id = "charlestown";
			Rm.winner = "british";
			Rm.end_reason = "posts_swept";
			Rm.casualties["continental"]["militia"] = {5, 7, 3};
			Rm.casualties["british"]["line_infantry"] = {2, 1, 0};
			Rm.personal_kills = 9;
			const pa250::CampaignState Am = pa250::apply_battle_result(St, Rm);
			const PA250BattleCards::FAfter Cm = PA250BattleCards::AfterBattle(Am.progress.last_battle_report, Am);
			bool bUnitsNote = false, bHand = false;
			for (const FString& N : Cm.Notes)
			{
				bUnitsNote |= N.Contains(TEXT("Units out of the ranks: yours 3, the enemy's 0"));
				bHand |= N.Contains(TEXT("By your own hand: 3 men"));
			}
			Log.Check(Cm.Losses.Num() == 2 && Cm.Losses[0].Contains(TEXT("5 killed, 7 wounded (12 men down)")) && Cm.Losses[1].Contains(TEXT("2 killed, 1 wounded (3 men down)")),
				FString::Printf(TEXT("cards: the card shows the men the battle counted (%s)"), Cm.Losses.Num() ? *Cm.Losses[0] : TEXT("none")));
			Log.Check(Cm.Prisoners.Num() == 2 && Cm.Prisoners[1].Contains(TEXT("3 men")) && Cm.Prisoners[0].Contains(TEXT("0 men")), TEXT("cards: prisoners in men"));
			Log.Check(bUnitsNote, TEXT("cards: 15 men down cost 3 units; the 3 men left over are carried"));
			Log.Check(bHand, TEXT("cards: by your own hand never exceeds the enemy's killed and wounded (9 claimed, 3 fell)"));
			Log.Check(Am.armies.at("continental_militia").men_down.at("militia").total() == 3, TEXT("cards: the men left over stay on the army"));
		}
		bool bSiege = false, bRetreat = false, bBoxes = false, bDropped = false;
		for (const FString& L : Card.MapChanges)
		{
			bSiege |= L.Contains(TEXT("besieged")) && L.Contains(TEXT("72"));
			bRetreat |= L.Contains(TEXT("falls back to"));
			bBoxes |= L.Contains(TEXT("rounds a man left of"));
			bDropped |= L.Contains(TEXT("dropped in the retreat"));
		}
		Log.Check(bSiege && bRetreat, TEXT("cards: the map changes name the siege and the retreat"));
		Log.Check(bBoxes && bDropped, TEXT("cards: the map changes say what rounds each army has left and what the beaten one dropped"));
		const pa250::BattleReport& Rep = After.progress.last_battle_report;
		const auto Mil = After.armies.find("continental_militia");
		Log.Check(Mil != After.armies.end() && Rep.rounds_after.count("continental") && Mil->second.rounds_per_man == Rep.rounds_after.at("continental")
			&& Mil->second.rounds_per_man < Rep.rounds_before.at("continental"),
			TEXT("cards: the beaten army's boxes are saved on the army, and hold less than at form-up"));
		Log.Check(PA250BattleCards::EndReasonSentence("army_broke", true) != PA250BattleCards::EndReasonSentence("army_broke", false)
			&& PA250BattleCards::EndReasonSentence("", true) == TEXT("The fight ended."), TEXT("cards: the reason is told from the player's side"));

		// No raw ids on the cards: no lowercase faction id, no town id, nothing in snake_case.
		auto HasRawId = [&St](const FString& Text) {
			FString Tok;
			auto Bad = [&St](const FString& T) {
				if (T.IsEmpty() || !T.Equals(T.ToLower(), ESearchCase::CaseSensitive)) return false;
				return T.Contains(TEXT("_")) || T == TEXT("british") || T == TEXT("continental") || St.provinces.count(TCHAR_TO_UTF8(*T)) > 0;
			};
			for (const TCHAR Ch : Text)
			{
				if (FChar::IsAlpha(Ch) || Ch == TEXT('_')) { Tok.AppendChar(Ch); continue; }
				if (Bad(Tok)) return true;
				Tok.Reset();
			}
			return Bad(Tok);
		};
		Log.Check(HasRawId(TEXT("Concord now british")) && HasRawId(TEXT("12 line_infantry")) && HasRawId(TEXT("ammo at concord"))
			&& !HasRawId(TEXT("Concord now British: the Patriot militia fall back")), TEXT("cards: the raw-id check itself tells ids from words"));
		FString Offender;
		for (const auto& SideKv : Setup.sides)
		{
			for (const PA250BattleCards::FRow& Row : PA250BattleCards::SideRows(SideKv.second))
			{
				if (HasRawId(Row.Label) || HasRawId(Row.Source)) Offender = Row.Label + TEXT(" / ") + Row.Source;
			}
		}
		for (const TArray<FString>* Lines : {&Card.Losses, &Card.Prisoners, &Card.MapChanges, &Card.Notes})
		{
			for (const FString& L : *Lines) if (HasRawId(L)) Offender = L;
		}
		if (HasRawId(Card.Title) || HasRawId(Card.Reason)) Offender = Card.Title + TEXT(" / ") + Card.Reason;
		Log.Check(Offender.IsEmpty(), FString::Printf(TEXT("cards: no raw ids in the pre-battle or after-battle text (%s)"), *Offender));

		// A side never has more men on the field than it fields, in any of the 13 towns, with the
		// town's magazine as it opens or emptied (playtest 2026-11-14: 19 men over a 4-soldier pool).
		FString Over;
		for (const auto& Pk : St.provinces)
		{
			for (const bool bEmptied : {false, true})
			{
				pa250::CampaignState Sx = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
				Sx.armies.at("british_regulars").province = Pk.first;
				Sx.armies.at("continental_militia").province = Pk.first;
				Sx.pending_battle = {true, Pk.first, "british_regulars", "continental_militia"};
				if (bEmptied)
				{
					pa250::ProvinceStores& M = Sx.province_stores[Pk.first];
					M.muskets = M.cartridges = M.powder = 0;
				}
				const pa250::BattleSetup Bx = pa250::build_battle_setup(Sx, Pk.first);
				for (const auto& SideKv : Bx.sides)
				{
					int32 Units = 0;
					for (const auto& T : SideKv.second.tickets) Units += T.second;
					if (SideKv.second.field_strength > Units * Bx.soldiers_per_unit)
					{
						Over = FString::Printf(TEXT("%s %s: %d on the field over a pool of %d"), UTF8_TO_TCHAR(Pk.first.c_str()), UTF8_TO_TCHAR(SideKv.first.c_str()),
							SideKv.second.field_strength, Units * Bx.soldiers_per_unit);
					}
				}
			}
		}
		Log.Check(Over.IsEmpty(), FString::Printf(TEXT("campaign read: no side fields more men than its pool (%s)"), *Over));

		// Men stood down are named on the card, and the headline is the men who fight.
		{
			pa250::CampaignState Tired = St;
			Tired.armies.at("continental_militia").fatigue = 70;
			const pa250::BattleSetup Bt = pa250::build_battle_setup(Tired, "charlestown");
			const TArray<PA250BattleCards::FRow> Rows = PA250BattleCards::SideRows(Bt.sides.at("continental"));
			const PA250BattleCards::FRow* Men = Find(Rows, TEXT("Men"));
			bool bStoodDown = false;
			for (const PA250BattleCards::FRow& Row : Rows) bStoodDown |= Row.bChange && Row.Value == TEXT("-20") && Row.Source.Contains(TEXT("stand down"));
			Log.Check(Men && Men->Value == TEXT("68") && bStoodDown, FString::Printf(TEXT("cards: a tired militia shows 68 men who fight and a -20 line for those stood down (%s)"), Men ? *Men->Value : TEXT("none")));
		}

		// The correspondence panel waits while a battle card is up.
		Log.Check(UPA250MapWidget::StaffPanelVisible(true, false, false) && !UPA250MapWidget::StaffPanelVisible(true, true, false)
			&& !UPA250MapWidget::StaffPanelVisible(true, false, true) && !UPA250MapWidget::StaffPanelVisible(false, false, false),
			TEXT("cards: the correspondence panel is hidden while a pre-battle or unread after-battle card is showing"));
		// "Send stores to depot" answers a refusal in words.
		Log.Check(!HasRawId(UPA250MapWidget::SendStoresRefusal(TEXT("already_at_depot"))) && !HasRawId(UPA250MapWidget::SendStoresRefusal(TEXT("empty_order")))
			&& !HasRawId(UPA250MapWidget::SendStoresRefusal(TEXT("something_else"))) && UPA250MapWidget::SendStoresRefusal(TEXT("empty_order")).Contains(TEXT("no arms or powder")),
			TEXT("cards: a refused stores order is explained in words"));
		// The order itself, as the button gives it: Concord's arms and powder leave, its provisions stay.
		{
			pa250::CampaignState Open = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
			const int Bread = pa250::province_ledger(Open, "concord").provisions;
			std::string Err;
			const pa250::CampaignState Sent = pa250::send_stores_to_depot(Open, "concord", Err);
			const pa250::ProvinceStores& Left = pa250::province_ledger(Sent, "concord");
			Log.Check(Err.empty() && Left.powder == 0 && Left.cartridges == 0 && Left.muskets == 0 && Left.provisions == Bread && Sent.shipments.size() == Open.shipments.size() + 1,
				TEXT("campaign read: sending Concord's stores to the depot empties its arms and powder in one order (mission 1 can be done)"));
		}
	}

	// The side's ammunition pool.
	const FName B(TEXT("british")), C(TEXT("continental"));
	Log.Check(FPA250AmmoPool::OpeningPool(40, 20, 88, 9) == 800 && FPA250AmmoPool::OpeningPool(0, 20, 96, 12) == 1152
		&& FPA250AmmoPool::OpeningPool(1, 20, 88, 3) == 264,
		TEXT("campaign read: the pool is the magazine in rounds, never less than a box a man"));
	FPA250AmmoPool Pool;
	Pool.Init(C, 100);
	Pool.Init(B, 30);
	Pool.Held.Add(C, 12); // one box kept for the human player
	Log.Check(Pool.Draw(C, 12, false) == 12 && Pool.Remaining(C) == 88, TEXT("campaign read: a man draws his opening box from the pool"));
	Log.Check(Pool.Available(C, false) == 76 && Pool.Available(C, true) == 88, TEXT("campaign read: bots cannot draw the box held for the player"));
	Log.Check(Pool.Draw(C, 200, false) == 76 && Pool.Remaining(C) == 12, TEXT("campaign read: bots stop at the held box"));
	Log.Check(Pool.Draw(C, 5, false) == 0, TEXT("campaign read: nothing more for a bot"));
	Log.Check(Pool.Refill(C, 12, true) == 12 && Pool.Remaining(C) == 0 && Pool.Refills.FindRef(C) == 1 && Pool.RefillRounds.FindRef(C) == 12,
		TEXT("campaign read: the player still fills his box, and the refill is counted"));
	Log.Check(Pool.Refill(C, 12, true) == 0 && Pool.Refills.FindRef(C) == 1, TEXT("campaign read: an empty pool gives nothing and counts no refill"));
	Log.Check(Pool.Spent(C) == 100 && Pool.Remaining(B) == 30 && Pool.Spent(B) == 0, TEXT("campaign read: each side has its own pool"));

	// The cartridge box on the musket: the last cartridge loads, then the piece stands empty until resupplied.
	FScopedTestWorld W;
	AMusketWeapon* Musket = W.World ? W.World->SpawnActor<AMusketWeapon>() : nullptr;
	Log.Check(Musket && Musket->RoundsInBox == -1 && !Musket->IsDry(), TEXT("campaign read: outside a battle the box is not counted"));
	if (!Musket) return;
	Musket->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
	Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
	auto WaitLoaded = [Musket]() {
		for (int32 i = 0; i < 400 && !Musket->CanFire() && !Musket->IsDry(); ++i) Musket->TickActor(0.25f, LEVELTICK_All, Musket->PrimaryActorTick);
	};
	Musket->RoundsInBox = 1;
	Log.Check(Musket->CanFire(), TEXT("campaign read: a soldier takes the field loaded"));
	Musket->Misfire(); // the piece is discharged without a ball: the same path as a shot
	Log.Check(Musket->RoundsInBox == 0 && Musket->IsReloading() && !Musket->IsDry(), TEXT("campaign read: firing takes the last cartridge and loads it"));
	WaitLoaded();
	Log.Check(Musket->CanFire(), TEXT("campaign read: the last cartridge is loaded"));
	Musket->Misfire();
	Log.Check(Musket->IsDry() && !Musket->CanFire() && !Musket->IsReloading() && !Musket->IsJammed(),
		TEXT("campaign read: an empty box leaves the piece dry, not reloading"));
	for (int32 i = 0; i < 80; ++i) Musket->TickActor(0.25f, LEVELTICK_All, Musket->PrimaryActorTick);
	Log.Check(Musket->IsDry() && !Musket->CanFire(), TEXT("campaign read: a dry piece does not load itself in 20 s"));
	Musket->Resupply(0);
	Log.Check(Musket->IsDry(), TEXT("campaign read: an empty hand-out changes nothing"));
	Musket->Resupply(5);
	Log.Check(!Musket->IsDry() && Musket->IsReloading() && Musket->RoundsInBox == 4, TEXT("campaign read: a refill starts the load and leaves 4 in the box"));
	WaitLoaded();
	Log.Check(Musket->CanFire(), TEXT("campaign read: and the piece is loaded again"));
	Musket->Destroy();
}
void RunCampaignArc(FLog& Log)
{
	// The game loads these two files (UPA250Subsystem::LoadSimDataFiles); without them there is no arc.
	pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
	pa250::load_campaign_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::CampaignRulesPath())));
	const std::vector<std::string> Errors = pa250::load_missions(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::MissionsPath())));
	Log.Check(pa250::campaign_rules_loaded(), TEXT("arc: campaign_rules_boston_1775.json loads"));
	Log.Check(Errors.empty() && pa250::mission_defs().size() == 11, FString::Printf(TEXT("arc: missions_boston_1775.json loads (%d missions, %d errors)"),
		static_cast<int32>(pa250::mission_defs().size()), static_cast<int32>(Errors.size())));

	pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
	St.treaties.push_back({"ceasefire", 24 * 500, "", ""}); // a quiet map: only the rule under test moves
	St = pa250::advance_hour(St).state;

	// The tracker at the opening.
	PA250ArcPanels::FTracker T = PA250ArcPanels::Tracker(St, FString());
	Log.Check(T.bVisible && T.Heading == TEXT("MISSION 1 OF 6") && T.Title == TEXT("Save the stores"),
		FString::Printf(TEXT("arc: the tracker opens on the first mission (%s / %s)"), *T.Heading, *T.Title));
	Log.Check(!T.Goal.IsEmpty() && T.Progress.Contains(TEXT("Concord magazine still holds")) && T.Deadline.IsEmpty(),
		FString::Printf(TEXT("arc: it shows the goal and where it stands (%s)"), *T.Progress));
	Log.Check(!T.bCanSendForGuns && !T.bCanHaulGuns && !T.bCanReenlist, TEXT("arc: no guns and no enlistment buttons in April"));
	{
		UPA250Subsystem* Probe = nullptr; // target towns come from the same definitions the subsystem reads
		(void)Probe;
		const pa250::MissionDef& First = pa250::mission_defs().front();
		Log.Check(First.target_provinces.size() == 1 && First.target_provinces[0] == "concord", TEXT("arc: the first mission marks Concord on the map"));
	}

	// Enlistments: the warning, and the button for a selected army.
	{
		pa250::CampaignState Dec = St;
		Dec.now = pa250::GameTime{pa250::from_ymdh(pa250::campaign_rules().enlist_warning).hour_index - 1};
		Dec = pa250::advance_hour(Dec).state;
		const PA250ArcPanels::FTracker None = PA250ArcPanels::Tracker(Dec, FString());
		bool bWarned = false;
		for (const FString& N : None.Notices) bWarned |= N.Contains(TEXT("Enlistments run out")) && N.Contains(TEXT("75 gold"));
		Log.Check(bWarned && !None.bCanReenlist, TEXT("arc: from 1 December the tracker warns that enlistments run out, with the price"));
		const PA250ArcPanels::FTracker Mine = PA250ArcPanels::Tracker(Dec, TEXT("continental_militia"));
		Log.Check(Mine.bCanReenlist && Mine.ReenlistCost == 75 && Mine.SelectedArmyName == TEXT("Massachusetts Militia"),
			FString::Printf(TEXT("arc: a selected army can be re-enlisted (%d gold)"), Mine.ReenlistCost));
		Log.Check(!PA250ArcPanels::Tracker(Dec, TEXT("british_regulars")).bCanReenlist, TEXT("arc: not the enemy's army"));
	}
	// The guns: sent for when the mission is open, hauled by an army standing where they lie.
	{
		pa250::CampaignState Guns = St;
		Guns.progress.enlistments_expired = true;
		Guns.progress.missions["p5_the_noble_train"].status = "active";
		Log.Check(PA250ArcPanels::Tracker(Guns, FString()).bCanSendForGuns, TEXT("arc: the guns can be sent for while the noble train is the mission"));
		Guns.progress.knox.status = "delivered";
		Guns.progress.knox.province = "cambridge";
		PA250ArcPanels::FTracker Far = PA250ArcPanels::Tracker(Guns, TEXT("continental_militia"));
		Log.Check(!Far.bCanSendForGuns && !Far.bCanHaulGuns, TEXT("arc: an army elsewhere cannot haul the guns"));
		Guns.armies.at("continental_militia").province = "cambridge";
		Log.Check(PA250ArcPanels::Tracker(Guns, TEXT("continental_militia")).bCanHaulGuns, TEXT("arc: an army at Cambridge can"));
		Guns.progress.knox.army_id = "continental_militia";
		const PA250ArcPanels::FTracker Hauling = PA250ArcPanels::Tracker(Guns, TEXT("continental_militia"));
		Log.Check(Hauling.bCanLeaveGuns && !Hauling.bCanHaulGuns, TEXT("arc: the army hauling them can leave them"));
	}
	// Headquarters lost and a siege are standing notices.
	{
		pa250::CampaignState Hq = St;
		Hq.provinces.at("cambridge").owner = "british";
		Hq.provinces.at("charlestown").siege_by = "british";
		Hq.provinces.at("charlestown").siege_hours_remaining = 50;
		Hq.armies.at("british_regulars").province = "charlestown"; // a siege lifts the hour its besiegers are gone
		Hq = pa250::advance_hour(Hq).state;
		bool bHq = false, bSiege = false;
		for (const FString& N : PA250ArcPanels::Tracker(Hq, FString()).Notices)
		{
			bHq |= N.Contains(TEXT("Cambridge")) && N.Contains(TEXT("day 1 of 14"));
			bSiege |= N.Contains(TEXT("Charlestown is besieged"));
		}
		Log.Check(bHq, TEXT("arc: the tracker counts the days the headquarters is lost"));
		Log.Check(bSiege, TEXT("arc: a besieged town of yours is on the tracker"));
	}
	// The end screen.
	Log.Check(!PA250ArcPanels::EndScreen(St).bDecided, TEXT("arc: no end screen while the campaign runs"));
	{
		pa250::CampaignState Won = St;
		Won.provinces.at("boston").owner = "continental";
		Won = pa250::advance_hour(Won).state;
		const PA250ArcPanels::FEnd E = PA250ArcPanels::EndScreen(Won);
		Log.Check(E.bDecided && E.bPlayerWon && E.Headline == TEXT("VICTORY") && E.Subline.Contains(TEXT("Patriots")) && E.Lines.Num() >= 4,
			FString::Printf(TEXT("arc: Boston taken is a victory screen with a summary (%d lines)"), E.Lines.Num()));
		pa250::CampaignState Lost = St;
		Lost.player_vassal = true;
		Lost = pa250::advance_hour(Lost).state;
		const PA250ArcPanels::FEnd D = PA250ArcPanels::EndScreen(Lost);
		Log.Check(D.bDecided && !D.bPlayerWon && D.Headline == TEXT("DEFEAT") && D.Lines.Num() >= 4 && D.Lines[0].Contains(TEXT("Crown")),
			TEXT("arc: vassalage is a defeat screen that says why"));
	}
	Log.Check(PA250ArcPanels::TimeLeft(72) == TEXT("3 days") && PA250ArcPanels::TimeLeft(30) == TEXT("1 day") && PA250ArcPanels::TimeLeft(5) == TEXT("5 hours"),
		TEXT("arc: time left reads in days, then hours"));
	{
		const pa250::WindowScore W = pa250::window_score_snapshot(St);
		const FString Board = PA250ArcPanels::LiveScoreboard(St);
		Log.Check(Board.Contains(TEXT("You ")) && Board.Contains(TEXT("Crown ")) && Board.Contains(TEXT(" left")) &&
					  Board.Contains(FString::FromInt(W.patriot_towns)) && Board.Contains(FString::FromInt(W.patriot_points)),
			FString::Printf(TEXT("arc: live scoreboard shows towns and points (%s)"), *Board));
		pa250::CampaignState Done = St;
		Done.progress.outcome.decided = true;
		Log.Check(PA250ArcPanels::LiveScoreboard(Done).IsEmpty(), TEXT("arc: the scoreboard hides once the campaign is decided"));
	}
	pa250::unload_missions();
	pa250::unload_campaign_rules();
}
void RunEconomy(FLog& Log)
{
	// The files the game loads (UPA250Subsystem::LoadSimDataFiles), read by the sim as Unreal compiles it.
	pa250::load_logistics_tuning(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::LogisticsTuningPath())));
	pa250::load_buildings_catalog(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BuildingsCatalogPath())));
	pa250::load_province_sizes(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ProvinceSizesPath())));
	pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
	pa250::ensure_logistics_defaults(St);

	Log.Check(St.player_gold == 120 && pa250::monthly_net_gold(St) == 21,
		FString::Printf(TEXT("economy: the campaign opens with 120 gold and +21 a month (%d, %+d)"), St.player_gold, pa250::monthly_net_gold(St)));
	Log.Check(pa250::enemy_monthly_net_gold(St) == 3,
		FString::Printf(TEXT("economy: the British accounts stand where they did, +3 a month (%+d)"), pa250::enemy_monthly_net_gold(St)));
	Log.Check(pa250::building_cost("workshop", 0) == 40 && pa250::building_cost("workshop", 1) == 100 && pa250::building_cost("workshop", 2) == 240,
		FString::Printf(TEXT("economy: a Workshop costs 40, 100, 240 by level (%d, %d, %d)"), pa250::building_cost("workshop", 0),
			pa250::building_cost("workshop", 1), pa250::building_cost("workshop", 2)));
	Log.Check(pa250::building_cost("powder_mill", 0) == 50, TEXT("economy: the catalog holds the Powder Mill (50 gold)"));

	// Ground rules, with the sentence the town panel prints.
	St.player_gold = 500;
	std::string Err;
	pa250::start_construction(St, "lexington", "powder_mill", Err);
	const FString Why = UTF8_TO_TCHAR(pa250::building_preview(St, "lexington", "powder_mill").c_str());
	Log.Check(Err == "wrong_ground" && Why.Contains(TEXT("needs river or falls")),
		FString::Printf(TEXT("economy: no Powder Mill at Lexington, and the panel says why (%s)"), *Why));
	pa250::start_construction(St, "watertown", "powder_mill", Err);
	Log.Check(Err.empty(), TEXT("economy: Watertown's falls take a Powder Mill"));
	pa250::start_construction(St, "roxbury", "farm", Err);
	Log.Check(Err == "wrong_ground", TEXT("economy: no Farm in Roxbury (no farmland)"));
	int32 Marked = 0;
	for (const auto& Pk : St.provinces) Marked += static_cast<int32>(Pk.second.grandfathered.size());
	Log.Check(Marked == 0, FString::Printf(TEXT("economy: no starting building needs grandfathering (%d marked)"), Marked));

	// Powder: bought only through a harbour Depot; made up into cartridges where an army forms up.
	Log.Check(!pa250::can_buy_powder(St, "cambridge") && !pa250::can_buy_powder(St, "charlestown"),
		TEXT("economy: no powder to buy at Cambridge (no harbour) or Charlestown (no Depot)"));
	St.provinces.at("charlestown").depot_level = 1;
	Log.Check(pa250::can_buy_powder(St, "charlestown"), TEXT("economy: a Depot at Charlestown opens the powder trade"));
	const pa250::ProvinceStores& Concord = pa250::province_ledger(St, "concord");
	Log.Check(pa250::cartridges_per_powder() == 10 && pa250::magazine_cartridges(St, "concord") == Concord.cartridges + Concord.powder * 10,
		FString::Printf(TEXT("economy: Concord's %d powder is %d more cartridges at form-up (%d in all)"), Concord.powder, Concord.powder * 10,
			pa250::magazine_cartridges(St, "concord")));
}
void RunSquad(FLog& Log)
{
	// The numbers come from unit_classes.json (verbs.squad).
	Log.Check(PA250UnitClasses::LoadFromFile(PA250UnitClasses::DataPath()), TEXT("squad: unit_classes.json loads"));
	const FPA250ClassVerbs& V = PA250UnitClasses::Verbs();
	Log.Check(V.SquadFileMin == 6 && V.SquadFileMax == 8 && V.SquadFollowBehindM > 0.f && V.SquadLineAheadM > 0.f && V.SquadOrderSeconds > 0.f
		&& V.SquadAimSnapM > 0.f && V.SquadAimRayM > 0.f, TEXT("squad: the file is 6 to 8 men and its distances come from the data file"));

	// Who is in the file: the nearest men of his class, up to 8; made up to 6 with others when his class is short.
	{
		TArray<PA250Squad::FCandidate> Many;
		for (int32 i = 0; i < 10; ++i) Many.Add({i, true, 1000.f - 50.f * i});
		for (int32 i = 10; i < 14; ++i) Many.Add({i, false, 10.f});
		const TArray<int32> A = PA250Squad::PickFile(Many, 6, 8);
		Log.Check(A.Num() == 8 && A[0] == 9 && A[7] == 2 && !A.Contains(10), TEXT("squad: ten of his class on the field give a file of the 8 nearest, none of another class"));
		TArray<PA250Squad::FCandidate> Few;
		for (int32 i = 0; i < 3; ++i) Few.Add({i, true, 900.f});
		for (int32 i = 3; i < 8; ++i) Few.Add({i, false, 100.f * i});
		const TArray<int32> B = PA250Squad::PickFile(Few, 6, 8);
		Log.Check(B.Num() == 6 && B[0] == 0 && B[2] == 2 && B[3] == 3 && B[5] == 5, TEXT("squad: three of his class are made up to 6 with the nearest other men"));
		TArray<PA250Squad::FCandidate> Two;
		Two.Add({0, false, 50.f});
		Two.Add({1, false, 20.f});
		Log.Check(PA250Squad::PickFile(Two, 6, 8).Num() == 2 && PA250Squad::PickFile({}, 6, 8).Num() == 0, TEXT("squad: a side with two men left gives a file of two; with none, no file"));
	}

	// The six orders: words for each, a shouted line for four, and where the line stands.
	{
		TSet<FString> Words;
		for (const EPA250SquadOrder O : {EPA250SquadOrder::FollowMe, EPA250SquadOrder::FormLine, EPA250SquadOrder::Hold, EPA250SquadOrder::TakeCover,
				 EPA250SquadOrder::Charge, EPA250SquadOrder::FallBack})
		{
			Words.Add(PA250Squad::OrderWords(O));
		}
		Log.Check(Words.Num() == 6 && !Words.Contains(PA250Squad::OrderWords(EPA250SquadOrder::None)), TEXT("squad: six orders, each with its own words"));
		Log.Check(FString(PA250Squad::OrderVoiceKey(EPA250SquadOrder::FollowMe)) == TEXT("follow_me") && FString(PA250Squad::OrderVoiceKey(EPA250SquadOrder::Charge)) == TEXT("charge_bayonets")
			&& FString(PA250Squad::OrderVoiceKey(EPA250SquadOrder::FallBack)) == TEXT("fall_back") && PA250Squad::OrderVoiceKey(EPA250SquadOrder::TakeCover) == nullptr,
			TEXT("squad: follow me, charge bayonets and fall back are shouted"));
		const FVector Leader(1000.f, 0.f, 90.f), Facing(1.f, 0.f, 0.f), Point(5000.f, 200.f, 90.f), Here(300.f, 300.f, 90.f);
		Log.Check(PA250Squad::LedAnchor(EPA250SquadOrder::FollowMe, Leader, Facing, Point, Here, 300.f).Equals(FVector(700.f, 0.f, 90.f), 0.1f),
			TEXT("squad: follow me forms the line 3 m behind the leader"));
		Log.Check(PA250Squad::LedAnchor(EPA250SquadOrder::FormLine, Leader, Facing, Point, Here, 300.f).Equals(Point, 0.1f)
			&& PA250Squad::LedAnchor(EPA250SquadOrder::Hold, Leader, Facing, Point, Here, 300.f).Equals(Point, 0.1f)
			&& PA250Squad::LedAnchor(EPA250SquadOrder::TakeCover, Leader, Facing, Point, Here, 300.f).Equals(Point, 0.1f)
			&& PA250Squad::LedAnchor(EPA250SquadOrder::FallBack, Leader, Facing, Point, Here, 300.f).Equals(Point, 0.1f),
			TEXT("squad: form line, hold, take cover and fall back stand on the point given"));
		Log.Check(PA250Squad::LedAnchor(EPA250SquadOrder::None, Leader, Facing, Point, Here, 300.f).Equals(Here, 0.1f)
			&& PA250Squad::LedAnchor(EPA250SquadOrder::Charge, Leader, Facing, Point, Here, 300.f).Equals(Here, 0.1f),
			TEXT("squad: without an order, and in a charge, the line is where it is"));
	}

	// The leader's volley: his aim makes ready and presents, and the line waits there for his shot.
	{
		bool bFire = false;
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::None, true, false, false, true, 0.f, bFire) == EVolleyCall::MakeReady && !bFire,
			TEXT("squad: holding aim with loaded men calls make ready"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::None, true, false, false, false, 0.f, bFire) == EVolleyCall::None,
			TEXT("squad: no make ready while nobody in the file is loaded"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::MakeReady, true, false, false, true, 0.4f, bFire) == EVolleyCall::MakeReady
			&& PA250Squad::LedVolleyNext(EVolleyCall::MakeReady, true, false, false, true, 0.f, bFire) == EVolleyCall::Present,
			TEXT("squad: make ready runs its time, then present"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::MakeReady, true, true, false, true, 0.4f, bFire) == EVolleyCall::MakeReady && !bFire,
			TEXT("squad: his shot before the line is presented fires nobody"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::Present, true, false, false, true, 0.f, bFire) == EVolleyCall::Present && !bFire,
			TEXT("squad: a presented line waits for his shot however long he holds his aim"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::Present, true, true, false, true, 0.f, bFire) == EVolleyCall::Fire && bFire,
			TEXT("squad: his shot fires the presented line"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::Fire, false, false, true, false, 0.f, bFire) == EVolleyCall::Fire && !bFire
			&& PA250Squad::LedVolleyNext(EVolleyCall::Fire, true, false, false, false, 0.f, bFire) == EVolleyCall::None,
			TEXT("squad: the volley runs out its shots, then the line is at rest"));
		Log.Check(PA250Squad::LedVolleyNext(EVolleyCall::Present, false, false, false, true, 0.f, bFire) == EVolleyCall::None && !bFire,
			TEXT("squad: lowering his aim recovers arms without a shot"));
	}

	// Where he is aiming: the man on his line of sight, not the ground far behind him.
	{
		const FVector Eye(0.f, 0.f, 160.f), Ground(9000.f, 0.f, 0.f);
		TArray<FVector> Foes = {FVector(6000.f, 120.f, 90.f), FVector(3000.f, 900.f, 90.f), FVector(7000.f, -50.f, 90.f)};
		Log.Check(APA250BattleGameMode::ResolveLeaderAim(Eye, Ground, Foes, 200.f).Equals(Foes[0], 0.1f),
			TEXT("squad: his aim is the nearest man within 2 m of his line of sight"));
		Log.Check(APA250BattleGameMode::ResolveLeaderAim(Eye, Ground, {FVector(3000.f, 900.f, 90.f)}, 200.f).Equals(Ground, 0.1f)
			&& APA250BattleGameMode::ResolveLeaderAim(Eye, Ground, {}, 200.f).Equals(Ground, 0.1f),
			TEXT("squad: with nobody on his line of sight his aim is the point it strikes"));
	}

	// The HUD line: how many are loaded, the order, and that his shot fires the presented.
	{
		const FString Idle = APA250BattleGameMode::FileLineText(7, 5, 0, PA250Squad::OrderWords(EPA250SquadOrder::FollowMe), true);
		const FString Ready = APA250BattleGameMode::FileLineText(7, 5, 5, PA250Squad::OrderWords(EPA250SquadOrder::Hold), true);
		const FString Skirmish = APA250BattleGameMode::FileLineText(6, 6, 0, PA250Squad::OrderWords(EPA250SquadOrder::TakeCover), false);
		Log.Check(Idle.Contains(TEXT("7 men, 5 loaded")) && Idle.Contains(TEXT("Follow me")) && Idle.Contains(TEXT("hold aim to make ready")) && Idle.Contains(TEXT("5 charge")),
			FString::Printf(TEXT("squad: the HUD shows the men, the loaded, the order and the keys (%s)"), *Idle.Replace(TEXT("\n"), TEXT(" | "))));
		Log.Check(Ready.Contains(TEXT("5 PRESENTED")) && Ready.Contains(TEXT("your shot fires them")), TEXT("squad: the HUD says when his shot will fire the line"));
		Log.Check(Skirmish.Contains(TEXT("fire at will on your man")) && !Skirmish.Contains(TEXT("make ready")), TEXT("squad: militia, rifles and light infantry are told they fire at will on his target"));
		Log.Check(APA250BattleGameMode::FileLineText(1, 0, 0, TEXT("Hold this ground"), true).Contains(TEXT("1 man,")) && APA250BattleGameMode::FileLineText(0, 0, 0, TEXT(""), true).Contains(TEXT("no men left")),
			TEXT("squad: one man is a man; none is said plainly"));
	}

	// Which classes volley on his shot and which fire at will: the tactical role, as for the bots.
	{
		const UPA250CombatProfiles* Pr = UPA250CombatProfiles::MakeRuntimeDefaults();
		Log.Check(!Pr->RoleFor(TEXT("british"), TEXT("line_infantry")).bIndividualFire && !Pr->RoleFor(TEXT("continental"), TEXT("line_infantry")).bIndividualFire,
			TEXT("squad: line infantry present and fire by volley"));
		Log.Check(Pr->RoleFor(TEXT("continental"), TEXT("militia")).bIndividualFire && Pr->RoleFor(TEXT("continental"), TEXT("rifleman")).bIndividualFire
			&& Pr->RoleFor(TEXT("british"), TEXT("light_infantry")).bIndividualFire, TEXT("squad: militia, rifles and light infantry fire at will"));
	}

	// A man's order from his squad: nothing of the leader's unless a leader set it.
	{
		const FPA250FormationOrder O;
		Log.Check(!O.bFocusContact && !O.bAimAtPoint && !O.bHoldFire, TEXT("squad: a bot's order carries no leader's aim by default"));
	}
}

void RunWorks(FLog& Log)
{
	pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
	const pa250::BattleRules R = pa250::battle_rules();
	Log.Check(pa250::battle_rules_loaded() && R.cover_snap_cm > 0.f && R.works_breastwork_length_cm > 0.f && R.works_breastwork_health > 0.f
		&& R.works_redoubt_health > R.works_breastwork_health && R.works_breastwork_slots >= 4, TEXT("works: sizes, strengths and places come from battle_rules.json"));
	// A breastwork is something a man fires over: above his chest, below his shoulder.
	Log.Check(R.works_breastwork_height_cm > 90.f + PA250Cover::ChestOffsetCm && R.works_breastwork_height_cm < 90.f + PA250Cover::FiringOffsetCm
		&& R.works_redoubt_height_cm < 90.f + PA250Cover::FiringOffsetCm,
		FString::Printf(TEXT("works: a bank (%.0f cm) stops a ball at the chest (90) and leaves the shoulder (145) clear"), R.works_breastwork_height_cm));

	// The fire-over stance: down while he loads, up while he presents.
	Log.Check(PA250Cover::FireOverDown(true, true, false, false) && !PA250Cover::FireOverDown(true, true, true, false)
		&& !PA250Cover::FireOverDown(true, false, false, false) && !PA250Cover::FireOverDown(false, true, false, false)
		&& !PA250Cover::FireOverDown(true, true, false, true),
		TEXT("works: behind cover a man kneels to load, rises to present, and does not kneel in the open or on the run"));
	// Bots at a covered place: the same stance (down unless loaded with a target in sight).
	Log.Check(PA250ShouldDuck(true, true, false, true, false, false) && !PA250ShouldDuck(true, true, true, true, false, false)
		&& !PA250ShouldDuck(false, true, false, true, false, false) && !PA250ShouldDuck(true, true, false, true, false, true),
		TEXT("works: a bot behind cover is down while loading and up to fire; never down in a charge"));

	// What stands at the start, by fortification level: breastworks, and a redoubt for a Fort at its highest.
	{
		TArray<int32> Breast, Redoubt;
		for (const int N : R.works_breastworks_by_level) Breast.Add(N);
		for (const int N : R.works_redoubts_by_level) Redoubt.Add(N);
		Log.Check(PA250Works::CountForLevel(Breast, 0) == 0 && PA250Works::CountForLevel(Redoubt, 0) == 0, TEXT("works: no fortification, no works standing"));
		Log.Check(PA250Works::CountForLevel(Breast, 1) > 0 && PA250Works::CountForLevel(Redoubt, 1) == 0
			&& PA250Works::CountForLevel(Breast, 2) >= PA250Works::CountForLevel(Breast, 1) && PA250Works::CountForLevel(Redoubt, 2) == 0,
			TEXT("works: levels 1 and 2 give breastworks and no redoubt"));
		Log.Check(PA250Works::CountForLevel(Redoubt, 3) == 1 && PA250Works::CountForLevel(Breast, 3) > 0 && PA250Works::CountForLevel(Redoubt, 7) == 1,
			TEXT("works: the highest level gives a redoubt as well"));
		Log.Check(PA250Works::CountForLevel(Breast, 2) <= R.works_breastwork_slots, TEXT("works: there is a place for every breastwork a level stands"));
	}

	// How far a work stands: what is built of it, less what is battered away; cover from 60 %.
	Log.Check(FMath::IsNearlyEqual(PA250Works::StandingShare(1.f, 300.f, 300.f), 1.f) && FMath::IsNearlyEqual(PA250Works::StandingShare(0.5f, 150.f, 300.f), 0.25f)
		&& PA250Works::StandingShare(0.f, 300.f, 300.f) == 0.f, TEXT("works: a work stands to what is built of it less what is battered away"));
	Log.Check(PA250Works::GivesCover(1.f, 300.f, 300.f, R.works_cover_built_share) && !PA250Works::GivesCover(0.5f, 300.f, 300.f, R.works_cover_built_share)
		&& !PA250Works::GivesCover(1.f, 150.f, 300.f, R.works_cover_built_share) && PA250Works::GivesCover(0.7f, 300.f, 300.f, R.works_cover_built_share),
		TEXT("works: a bank is cover once it stands to 60 % of its height, and not when half battered down"));

	// Places made up about a post when the level has none marked.
	{
		TArray<FVector> Places, Fronts;
		const FVector Post(1000.f, 2000.f, 0.f), Enemy(1.f, 0.f, 0.f);
		PA250Works::RingSlots(Post, Enemy, R.works_slot_ring_cm, 6, R.works_breastwork_length_cm + 160.f, Places, Fronts);
		bool bRing = Places.Num() == 6 && Fronts.Num() == 6;
		float MinGap = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Places.Num(); ++i)
		{
			bRing &= FMath::IsNearlyEqual(FVector::Dist2D(Places[i], Post), R.works_slot_ring_cm, 1.f);
			bRing &= FVector::DotProduct(Fronts[i], Enemy) > 0.f; // every bank fronts the way the enemy comes
			for (int32 j = i + 1; j < Places.Num(); ++j) MinGap = FMath::Min(MinGap, FVector::Dist2D(Places[i], Places[j]));
		}
		Log.Check(bRing && Places[0].Equals(Post + Enemy * R.works_slot_ring_cm, 1.f), TEXT("works: six made-up places stand on an arc toward the enemy, the first on his line of advance"));
		Log.Check(MinGap > R.works_breastwork_length_cm, FString::Printf(TEXT("works: banks on the arc do not overlap (%.0f cm apart, %.0f long)"), MinGap, R.works_breastwork_length_cm));
	}

	// Whoever defends a post mans its works: the line among the work anchors, on the friendly side.
	{
		const FVector Post(0.f, 0.f, 0.f), EnemyRear(20000.f, 0.f, 0.f), OwnRear(-20000.f, 0.f, 0.f);
		const TArray<FVector> WorksAt = {FVector(1400.f, 0.f, 0.f), FVector(1300.f, 520.f, 0.f), FVector(9000.f, 0.f, 0.f)};
		FVector Pick, Second;
		const bool bFirst = UPA250VolleyCoordinator::PickLine(WorksAt, Post, EnemyRear, OwnRear, R.works_man_radius_cm, R.works_breastwork_length_cm, {}, Pick);
		const bool bNext = UPA250VolleyCoordinator::PickLine(WorksAt, Post, EnemyRear, OwnRear, R.works_man_radius_cm, R.works_breastwork_length_cm, {Pick}, Second);
		Log.Check(bFirst && Pick.X < 1400.f && Pick.X > 1100.f && FMath::Abs(Pick.Y) < 1.f, TEXT("works: the first squad stands behind the bank on the enemy's line of advance"));
		Log.Check(bNext && Second.Y > 400.f, TEXT("works: the next squad takes the next bank, not the same one"));
		FVector None;
		Log.Check(!UPA250VolleyCoordinator::PickLine({FVector(9000.f, 0.f, 0.f)}, Post, EnemyRear, OwnRear, R.works_man_radius_cm, R.works_breastwork_length_cm, {}, None),
			TEXT("works: a work beyond the manning radius is not this post's"));
	}

	// The sapper: a class of both sides, with its verb values in the data file.
	Log.Check(PA250UnitClasses::LoadFromFile(PA250UnitClasses::DataPath()), TEXT("works: unit_classes.json loads with the sapper in it"));
	Log.Check(PA250UnitClasses::KitFor(TEXT("continental"), TEXT("sapper")).bBuildsWorks && PA250UnitClasses::KitFor(TEXT("british"), TEXT("sapper")).bBuildsWorks
		&& !PA250UnitClasses::KitFor(TEXT("british"), TEXT("line_infantry")).bBuildsWorks, TEXT("works: sappers of both sides build works; nobody else does"));
	Log.Check(!FString(PA250UnitClasses::KitFor(TEXT("continental"), TEXT("sapper")).Role).IsEmpty() && FString(PA250UnitClasses::KitFor(TEXT("british"), TEXT("sapper")).Label) == TEXT("Pioneer"),
		TEXT("works: the sapper has a one-line role for the class picker (the British call him a pioneer)"));
	const FPA250ClassVerbs& V = PA250UnitClasses::Verbs();
	Log.Check(V.SapperBuildSeconds > 0.f && V.SapperReachM > 0.f && V.SapperSeekM > V.SapperReachM && V.SapperBurstSeconds > 0.f, TEXT("works: the sapper's labour is timed in the data file"));
	Log.Check(PA250UnitClasses::RequiredClasses(TEXT("british")).Contains(FName(TEXT("sapper"))) && PA250UnitClasses::DisplayOrder().Contains(FName(TEXT("sapper"))),
		TEXT("works: the class picker and the data check know the sapper"));
}

void RunSupport(FLog& Log)
{
	Log.Check(PA250UnitClasses::LoadFromFile(PA250UnitClasses::DataPath()), TEXT("support: unit_classes.json loads with the three roles in it"));
	const FPA250ClassVerbs& V = PA250UnitClasses::Verbs();
	for (const TCHAR* F : {TEXT("british"), TEXT("continental")})
	{
		const FPA250UnitClassKit& E = PA250UnitClasses::KitFor(F, TEXT("ensign"));
		const FPA250UnitClassKit& M = PA250UnitClasses::KitFor(F, TEXT("musician"));
		const FPA250UnitClassKit& S = PA250UnitClasses::KitFor(F, TEXT("surgeon"));
		Log.Check(E.bCarriesColours && M.bRelaysOrders && S.bTendsWounded && !E.bRelaysOrders && !M.bTendsWounded && !S.bCarriesColours,
			FString::Printf(TEXT("support: %s ensign, musician and surgeon each have their own verb"), F));
		Log.Check(E.MaxOnField == 1 && M.MaxOnField == 1 && S.MaxOnField == 1, FString::Printf(TEXT("support: %s fields one of each at a time"), F));
		Log.Check(!FString(E.Role).IsEmpty() && !FString(M.Role).IsEmpty() && !FString(S.Role).IsEmpty() && !FString(E.Label).IsEmpty(),
			FString::Printf(TEXT("support: %s roles have a one-line role for the class picker"), F));
		Log.Check(PA250UnitClasses::RequiredClasses(F).Contains(FName(TEXT("ensign"))) && PA250UnitClasses::RequiredClasses(F).Contains(FName(TEXT("surgeon")))
			&& PA250UnitClasses::RequiredClasses(F).Contains(FName(TEXT("musician"))), FString::Printf(TEXT("support: the data check requires the %s roles"), F));
	}
	Log.Check(PA250UnitClasses::KitFor(TEXT("british"), TEXT("line_infantry")).MaxOnField == 0, TEXT("support: the line has no cap on the field"));
	Log.Check(V.EnsignRadiusM > 0.f && V.EnsignRallyScale > 0.f && V.EnsignRallyScale < 1.f && V.ColoursFallPressure > 0.f && V.ColoursCaptureSeconds > 0.f
		&& V.MusicianRelayM > 0.f && V.MusicianCadenceScale < 1.f && V.SurgeonWindowSeconds > V.SurgeonTreatSeconds, TEXT("support: every value comes from the data file"));

	// One of each at a time, whatever the army has of them.
	Log.Check(PA250Support::ClassHasFieldRoom(0, 1) && !PA250Support::ClassHasFieldRoom(1, 1) && PA250Support::ClassHasFieldRoom(40, 0),
		TEXT("support: a capped class takes the field one at a time; an uncapped one as the army has men"));

	// Ensign: squads near the flying colours rally sooner.
	Log.Check(FMath::IsNearlyEqual(PA250Support::RallySecondsNearColours(12.f, true, V.EnsignRallyScale), 12.f * V.EnsignRallyScale)
		&& FMath::IsNearlyEqual(PA250Support::RallySecondsNearColours(12.f, false, V.EnsignRallyScale), 12.f),
		TEXT("support: near the colours a routing squad rallies in half the time; away from them, as ever"));
	{
		// The morale machine with the shorter calm: rallying begins sooner near the colours.
		FPA250MoraleState Near, Far;
		Near.Set(EPA250Morale::Routing);
		Far.Set(EPA250Morale::Routing);
		const float Usual = 12.f;
		float NearAt = -1.f, FarAt = -1.f;
		for (int32 i = 1; i <= 200; ++i)
		{
			const float T = i * 0.1f;
			if (NearAt < 0.f && Near.Tick(0.1f, 0.f, 1.f, PA250Support::RallySecondsNearColours(Usual, true, V.EnsignRallyScale), 5.f, false, false) && Near.State == EPA250Morale::Rallying) NearAt = T;
			if (FarAt < 0.f && Far.Tick(0.1f, 0.f, 1.f, Usual, 5.f, false, false) && Far.State == EPA250Morale::Rallying) FarAt = T;
		}
		Log.Check(NearAt > 0.f && FarAt > 0.f && NearAt < FarAt - 1.f, FString::Printf(TEXT("support: a rout near the colours rallies at %.1f s, away from them at %.1f s"), NearAt, FarAt));
	}

	// Musician: an order carries on through him; without him only an officer reaches other lines.
	{
		const FVector Giver(0.f, 0.f, 0.f), NearLine(2000.f, 0.f, 0.f), FarLine(6000.f, 0.f, 0.f), VeryFar(12000.f, 0.f, 0.f);
		const float Own = V.OrderRadiusM * 100.f, Relay = V.MusicianRelayM * 100.f;
		bool bRelayed = false;
		Log.Check(PA250Support::OrderReaches(Giver, NearLine, true, Own, {}, Relay, &bRelayed) && !bRelayed, TEXT("support: an officer's word reaches a line 20 m off by itself"));
		Log.Check(!PA250Support::OrderReaches(Giver, FarLine, true, Own, {}, Relay), TEXT("support: without a musician it does not reach a line 60 m off"));
		Log.Check(PA250Support::OrderReaches(Giver, FarLine, true, Own, {FVector(2500.f, 0.f, 0.f)}, Relay, &bRelayed) && bRelayed, TEXT("support: through a musician 25 m from him it does"));
		Log.Check(!PA250Support::OrderReaches(Giver, FarLine, true, Own, {FVector(5000.f, 0.f, 0.f)}, Relay), TEXT("support: a musician who cannot hear the order relays nothing"));
		Log.Check(!PA250Support::OrderReaches(Giver, VeryFar, true, Own, {FVector(2500.f, 0.f, 0.f)}, Relay), TEXT("support: the relay has its own limit"));
		Log.Check(!PA250Support::OrderReaches(Giver, NearLine, false, Own, {}, Relay) && PA250Support::OrderReaches(Giver, NearLine, false, Own, {FVector(1000.f, 0.f, 0.f)}, Relay),
			TEXT("support: a leader who is no officer reaches another line only through a musician"));
		Log.Check(FMath::IsNearlyEqual(PA250Support::CadenceReloadScale(true, true, V.MusicianCadenceScale), V.MusicianCadenceScale)
			&& PA250Support::CadenceReloadScale(false, true, V.MusicianCadenceScale) == 1.f && PA250Support::CadenceReloadScale(true, false, V.MusicianCadenceScale) == 1.f,
			TEXT("support: volley-role men load in cadence near a musician; skirmishers and men out of earshot do not"));
	}

	// Surgeon: a man he saves is wounded, outside the ratio; the window closes.
	{
		Log.Check(PA250Support::CanStillBeSaved(100.f, 100.f + V.SurgeonWindowSeconds, V.SurgeonWindowSeconds) && !PA250Support::CanStillBeSaved(100.f, 101.f + V.SurgeonWindowSeconds, V.SurgeonWindowSeconds),
			TEXT("support: a fallen man can be saved until the window closes"));
		pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
		TMap<FName, TMap<FName, int32>> Deaths, Gave, Saved;
		Deaths.FindOrAdd(TEXT("continental")).Add(TEXT("militia"), 10);
		const auto Plain = APA250BattleGameMode::CasualtiesFromLedger(Deaths, Gave);
		Saved.FindOrAdd(TEXT("continental")).Add(TEXT("militia"), 4);
		const auto With = APA250BattleGameMode::CasualtiesFromLedger(Deaths, Gave, Saved);
		const pa250::MenDown A = Plain.at("continental").at("militia"), B = With.at("continental").at("militia");
		Log.Check(A.total() == 10 && B.total() == 10 && B.killed < A.killed && B.wounded == pa250::split_fallen(6).wounded + 4,
			FString::Printf(TEXT("support: of 10 fallen, 4 saved by the surgeon are wounded and the ratio shares the other 6 (%d killed without him, %d with)"), A.killed, B.killed));
		Saved.FindOrAdd(TEXT("continental")).Add(TEXT("militia"), 99);
		Log.Check(APA250BattleGameMode::CasualtiesFromLedger(Deaths, Gave, Saved).at("continental").at("militia").wounded == 10, TEXT("support: he cannot save more men than fell"));
	}

	// Colours taken go into the result and move Support (the sim applies them).
	{
		pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		St.armies.at("british_regulars").province = "lexington";
		St.armies.at("continental_militia").province = "lexington";
		St.armies.at("british_regulars").units["ensign"] = 1;
		St.pending_battle = {true, "lexington", "british_regulars", "continental_militia"};
		pa250::BattleResult R;
		R.province_id = "lexington";
		R.winner = "continental";
		R.end_reason = "army_broke";
		R.colours_captured["continental"] = 1;
		const pa250::CampaignState After = pa250::apply_battle_result(St, R);
		const PA250BattleCards::FAfter Card = PA250BattleCards::AfterBattle(After.progress.last_battle_report, After);
		bool bLine = false;
		for (const FString& L : Card.MapChanges) bLine |= L.Contains(TEXT("took 1 colour"));
		Log.Check(After.progress.last_battle_report.colours_captured.count("continental") == 1 && After.local_support == St.local_support + 5 + pa250::battle_rules().colours_support_per_capture && bLine,
			TEXT("support: colours the player takes are on the report and the card, and raise Support (with the defender's +5)"));
	}
}

void RunModes(FLog& Log)
{
	pa250::load_battle_rules(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath())));
	const pa250::BattleRules& R = pa250::battle_rules();
	Log.Check(R.assault_seconds_per_post > 0.f && R.assault_garrison_share_pct > 0 && R.landing_boats >= 1 && R.landing_men_per_boat >= 1 && R.landing_boat_speed_cm > 0.f
		&& R.landing_row_out_m > 0.f && R.mode_assault_min_fort == 1, TEXT("modes: the assault and landing values come from battle_rules.json"));
	// The sim chooses; the battle only reads.
	Log.Check(pa250::choose_battle_mode("lexington_common", "road", {"farmland"}, 0) == "conquest" && pa250::choose_battle_mode("lexington_common", "neck", {"harbor"}, 1) == "assault"
		&& pa250::choose_battle_mode("lexington_common", "boat", {"harbor"}, 2) == "landing" && pa250::choose_battle_mode("concord_bridge", "boat", {"harbor"}, 2) == "conquest",
		TEXT("modes: the sim's rule gives conquest, assault, landing, and conquest for the bridge fight"));
	const FPA250ScenarioFight Open = APA250BattleGameMode::ResolveScenario(TEXT("lexington_common"), {}, TEXT("conquest"));
	const FPA250ScenarioFight Assault = APA250BattleGameMode::ResolveScenario(TEXT("lexington_common"), {}, TEXT("assault"));
	const FPA250ScenarioFight Landing = APA250BattleGameMode::ResolveScenario(TEXT("lexington_common"), {TEXT("harbor")}, TEXT("landing"));
	const FPA250ScenarioFight Bridge = APA250BattleGameMode::ResolveScenario(TEXT("concord_bridge"), {TEXT("bridge_crossing")}, TEXT("assault"));
	const FPA250ScenarioFight Junk = APA250BattleGameMode::ResolveScenario(TEXT("lexington_common"), {}, TEXT("something_else"));
	Log.Check(Open.Mode == TEXT("conquest") && Assault.Mode == TEXT("assault") && Landing.Mode == TEXT("landing") && Bridge.Mode == TEXT("conquest") && Bridge.bContestSingleCrossing
		&& Junk.Mode == TEXT("conquest"), TEXT("modes: the battle reads the setup's mode; the bridge fight and an unknown word stay conquest"));
	Log.Check(APA250BattleGameMode::UsesPostBleed(Open) && !APA250BattleGameMode::UsesPostBleed(Assault) && APA250BattleGameMode::UsesPostBleed(Landing),
		TEXT("modes: an assault is not worn down for posts; the open fight and a landing made good are"));

	// Three posts in a row; the attacker starts west of them.
	const TArray<FVector> Where = {FVector(6000.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(3500.f, 200.f, 0.f)};
	const TArray<int32> Order = PA250Modes::AttackOrder(Where, FVector(-4000.f, 0.f, 0.f));
	Log.Check(Order.Num() == 3 && Order[0] == 1 && Order[1] == 2 && Order[2] == 0, TEXT("modes: posts are ordered from the attacker's start, nearest first"));
	const FName A(TEXT("british")), D(TEXT("continental"));
	{
		TArray<FName> H = {D, D, D};
		TArray<bool> L = PA250Modes::LockedPosts(TEXT("assault"), Order, H, A, false);
		Log.Check(PA250Modes::OpenPost(Order, H, A) == 1 && !L[1] && L[0] && L[2], TEXT("modes: an assault opens with only the nearest post open to contest"));
		H[1] = A;
		L = PA250Modes::LockedPosts(TEXT("assault"), Order, H, A, false);
		Log.Check(PA250Modes::OpenPost(Order, H, A) == 2 && L[1] && !L[2] && L[0] && PA250Modes::PostsCarried(H, A) == 1,
			TEXT("modes: a post carried stays the attacker's and the next one opens"));
		H[2] = A;
		H[0] = A;
		Log.Check(PA250Modes::OpenPost(Order, H, A) == INDEX_NONE && PA250Modes::PostsCarried(H, A) == 3, TEXT("modes: with the last post carried nothing is left to take"));
		Log.Check(PA250Modes::AssaultWinnerAtTimeLimit(D) == D, TEXT("modes: when an assault's clock runs out the defender has held"));
	}
	{
		TArray<FName> H = {D, D, D};
		TArray<bool> L = PA250Modes::LockedPosts(TEXT("landing"), Order, H, A, false);
		Log.Check(!L[1] && L[0] && L[2], TEXT("modes: before the landing is made good only the landing post can be taken"));
		L = PA250Modes::LockedPosts(TEXT("landing"), Order, H, A, true);
		Log.Check(!L[0] && !L[1] && !L[2], TEXT("modes: once the landing post has been held every post is open, and stays open"));
		L = PA250Modes::LockedPosts(TEXT("conquest"), Order, H, A, false);
		Log.Check(!L[0] && !L[1] && !L[2], TEXT("modes: the open fight closes nothing"));
	}
	// Boats: abreast, the same distance out, each rowing straight in.
	{
		const FVector Place(0.f, 0.f, 0.f), Sea(0.f, 1.f, 0.f);
		FVector S0, B0, S1, B1, S2, B2;
		PA250Modes::BoatCourse(0, 3, Place, Sea, 900.f, R.landing_row_out_m * 100.f, S0, B0);
		PA250Modes::BoatCourse(1, 3, Place, Sea, 900.f, R.landing_row_out_m * 100.f, S1, B1);
		PA250Modes::BoatCourse(2, 3, Place, Sea, 900.f, R.landing_row_out_m * 100.f, S2, B2);
		Log.Check(B1.Equals(Place) && FMath::IsNearlyEqual(FVector::Dist(B0, B2), 1800.f) && FMath::IsNearlyEqual(FVector::Dist(S1, B1), R.landing_row_out_m * 100.f)
			&& FMath::IsNearlyEqual((S0 - B0).GetSafeNormal() | Sea, 1.f), TEXT("modes: three boats land abreast 9 m apart, each rowed straight in from the same distance out"));
	}
	// The level: a landing is fought on the harbour level when it is built, else on the common.
	{
		const FName Harbor = UPA250Subsystem::BattleLevelFor(TEXT("lexington_common"), TEXT("landing"));
		const bool bBuilt = FPackageName::DoesPackageExist(TEXT("/Game/PA250/Maps/L_Harbor"));
		Log.Check(Harbor == (bBuilt ? FName(TEXT("/Game/PA250/Maps/L_Harbor")) : UPA250Subsystem::BattleLevelForPreset(TEXT("lexington_common"))),
			FString::Printf(TEXT("modes: a landing plays on %s"), *Harbor.ToString()));
		Log.Check(UPA250Subsystem::BattleLevelFor(TEXT("concord_bridge"), TEXT("landing")) == UPA250Subsystem::BattleLevelForPreset(TEXT("concord_bridge"))
			&& UPA250Subsystem::BattleLevelFor(TEXT("lexington_common"), TEXT("assault")) == UPA250Subsystem::BattleLevelForPreset(TEXT("lexington_common")),
			TEXT("modes: the bridge fight keeps its level; an assault is fought on the town's own ground"));
	}
	// The card says what kind of fight it is, in words.
	{
		pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		St.armies.at("british_regulars").province = "dorchester";
		St.armies.at("british_regulars").previous_province = "boston";
		St.armies.at("continental_militia").province = "dorchester";
		St.pending_battle = {true, "dorchester", "british_regulars", "continental_militia"};
		const pa250::BattleSetup Land = pa250::build_battle_setup(St, "dorchester");
		const FString Line = PA250BattleCards::ModeLine(Land);
		Log.Check(Land.mode == "landing" && Line.Contains(TEXT("British attack")) && Line.Contains(TEXT("landing from the boats")) && !Line.Contains(TEXT("_")),
			FString::Printf(TEXT("modes: a British attack on Dorchester from Boston is a landing, and the card says so (%s)"), *Line));
		St.armies.at("british_regulars").province = "charlestown";
		St.armies.at("british_regulars").previous_province = "medford";
		St.armies.at("continental_militia").province = "charlestown";
		St.pending_battle = {true, "charlestown", "british_regulars", "continental_militia"};
		const pa250::BattleSetup Storm = pa250::build_battle_setup(St, "charlestown");
		Log.Check(Storm.mode == "assault" && PA250BattleCards::ModeLine(Storm).Contains(TEXT("assault on the works")), TEXT("modes: Charlestown by the neck is an assault on its works"));
	}
}

void RunGunnery(FLog& Log)
{
	Log.Check(PA250UnitClasses::LoadFromFile(PA250UnitClasses::DataPath()), TEXT("guns: unit_classes.json loads with the gunner in it"));
	const std::string RulesText = pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::BattleRulesPath()));
	Log.Check(RulesText.find("\"cannon\"") != std::string::npos && RulesText.find("\"guns\"") != std::string::npos, TEXT("guns: battle_rules.json has the guns and cannon blocks"));
	pa250::load_battle_rules(RulesText);
	const pa250::BattleRules& R = pa250::battle_rules();
	Log.Check(R.guns_crew_class == "artilleryman" && R.guns_crew_units_per_gun == 1 && R.cannon_crew_men == 4 && R.cannon_min_crew == 2
		&& FMath::IsNearlyEqual(R.cannon_length_cm, 333.f) && R.cannon_round_range_m > R.cannon_canister_range_m && R.cannon_works_damage > 0.f,
		TEXT("guns: the values are the file's (crew of 4, 2 can still work it, the mesh's own 333 cm)"));
	for (const TCHAR* F : {TEXT("british"), TEXT("continental")})
	{
		const FPA250UnitClassKit& G = PA250UnitClasses::KitFor(F, TEXT("artilleryman"));
		Log.Check(G.bServesGuns && !G.bBuildsWorks && !G.bTendsWounded && !FString(G.Role).IsEmpty() && !FString(G.Label).IsEmpty()
			&& PA250UnitClasses::RequiredClasses(F).Contains(FName(TEXT("artilleryman"))), FString::Printf(TEXT("guns: the %s gunner serves guns and the data check requires him"), F));
		Log.Check(!PA250UnitClasses::KitFor(F, TEXT("line_infantry")).bServesGuns, FString::Printf(TEXT("guns: %s line infantry are not gunners"), F));
	}

	// Which shot: canister close, round shot beyond, nothing past its range.
	{
		EPA250Shot Shot = EPA250Shot::RoundShot;
		const bool bClose = PA250Gunnery::ChooseShot(R.cannon_canister_range_m * 100.f - 100.f, R, Shot) && Shot == EPA250Shot::Canister;
		const bool bFar = PA250Gunnery::ChooseShot(R.cannon_canister_range_m * 100.f + 100.f, R, Shot) && Shot == EPA250Shot::RoundShot;
		const bool bEnd = PA250Gunnery::ChooseShot(R.cannon_round_range_m * 100.f, R, Shot) && !PA250Gunnery::ChooseShot(R.cannon_round_range_m * 100.f + 100.f, R, Shot);
		Log.Check(bClose && bFar && bEnd, FString::Printf(TEXT("guns: canister inside %.0f m, round shot to %.0f m, no shot beyond"), R.cannon_canister_range_m, R.cannon_round_range_m));
	}

	// The drill: a full crew of gunners at full speed, a short crew slower, untrained men slower still, too few not at all.
	{
		const float Full = PA250Gunnery::DrillSpeed(4, 4, R), Half = PA250Gunnery::DrillSpeed(2, 2, R), Raw = PA250Gunnery::DrillSpeed(4, 0, R), Mixed = PA250Gunnery::DrillSpeed(4, 2, R);
		Log.Check(FMath::IsNearlyEqual(Full, 1.f) && FMath::IsNearlyEqual(Half, 0.5f) && FMath::IsNearlyEqual(Raw, 1.f / R.cannon_untrained_scale) && Mixed > Raw && Mixed < Full
			&& PA250Gunnery::DrillSpeed(1, 1, R) == 0.f && FMath::IsNearlyEqual(PA250Gunnery::DrillSpeed(9, 9, R), 1.f),
			FString::Printf(TEXT("guns: drill speed 4 gunners %.2f, 2 gunners %.2f, 4 infantry %.2f, 2 and 2 %.2f, one man 0; more than a crew adds nothing"), Full, Half, Raw, Mixed));
		// Timed on the sequencer the gun uses: left alone the drill completes in the three steps' time, and a half crew takes twice as long.
		auto TimeIt = [&R](float Speed) {
			FActiveReloadSequencer Drill;
			Drill.Configure(PA250Gunnery::DrillPreset(R));
			Drill.Reset();
			Drill.BeginReload();
			float T = 0.f;
			while (!Drill.IsComplete() && T < 120.f)
			{
				Drill.Tick(0.05f * Speed);
				T += 0.05f;
			}
			return T;
		};
		const float Whole = R.cannon_sponge_load_seconds + R.cannon_ram_seconds + R.cannon_prime_lay_seconds;
		const float TFull = TimeIt(Full), THalf = TimeIt(Half);
		Log.Check(FMath::Abs(TFull - Whole) < 1.f && FMath::Abs(THalf - 2.f * Whole) < 2.f,
			FString::Printf(TEXT("guns: the drill takes %.1f s with a full crew (%.0f s in the file) and %.1f s with two men"), TFull, Whole, THalf));
		Log.Check(FString(PA250Gunnery::DrillWords(EActiveReloadPhase::PrimePan)) == TEXT("Sponge and load") && FString(PA250Gunnery::DrillWords(EActiveReloadPhase::RamCharge)) == TEXT("Ram home")
			&& FString(PA250Gunnery::DrillWords(EActiveReloadPhase::ShoulderAim)) == TEXT("Prime and lay"), TEXT("guns: the drill's steps have their own words on the HUD"));
	}

	// The piece is laid above its mark by what the shot falls on the way.
	{
		const float Speed = R.cannon_round_speed_m * 100.f;
		const float At100 = PA250Gunnery::RoundShotDrop(10000.f, Speed), At300 = PA250Gunnery::RoundShotDrop(30000.f, Speed);
		// Flown in steps as the ball flies: it lands within a hand of the mark.
		FVector P(0.f, 0.f, 0.f);
		FVector Vel = (FVector(30000.f, 0.f, At300)).GetSafeNormal() * Speed;
		while (P.X < 30000.f)
		{
			Vel.Z -= 490.f * 0.01f;
			P += Vel * 0.01f;
		}
		Log.Check(At100 > 50.f && At300 > At100 * 8.f && FMath::Abs(P.Z) < 40.f,
			FString::Printf(TEXT("guns: round shot falls %.0f cm in 100 m and %.0f cm in 300 m; laid that much high it arrives %.0f cm from the mark"), At100, At300, P.Z));
	}

	// Overrun and push.
	Log.Check(PA250Gunnery::IsOverrun(0, 1) && !PA250Gunnery::IsOverrun(1, 3) && !PA250Gunnery::IsOverrun(0, 0), TEXT("guns: a gun is overrun only with the enemy at it and none of its own side"));
	Log.Check(PA250Gunnery::CanPush(2, false, R) && !PA250Gunnery::CanPush(1, false, R) && !PA250Gunnery::CanPush(4, true, R), TEXT("guns: two men can push a gun, one cannot, and not while it is loading"));

	// The purchased asset: one mesh, with the sockets and collision the setup script gave it.
	{
		UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/ThirdParty/PTM_Cannon/Mesh.Mesh"));
		Log.Check(Asset != nullptr, TEXT("guns: the purchased cannon mesh loads (/Game/ThirdParty/PTM_Cannon/Mesh)"));
		if (Asset)
		{
			bool bSockets = Asset->FindSocket(TEXT("Muzzle")) && Asset->FindSocket(TEXT("Vent"));
			for (int32 i = 1; i <= 4; ++i) bSockets = bSockets && Asset->FindSocket(*FString::Printf(TEXT("Crew_%d"), i));
			Log.Check(bSockets, TEXT("guns: the mesh has Muzzle, Vent and four crew sockets (Scripts/setup_cannon_asset.py)"));
			const UBodySetup* Body = Asset->GetBodySetup();
			Log.Check(Body && (Body->CollisionTraceFlag == CTF_UseComplexAsSimple || Body->AggGeom.GetElementCount() > 0), TEXT("guns: the mesh has collision"));
			const FBoxSphereBounds B = Asset->GetBounds();
			const float Length = 2.f * FMath::Max(B.BoxExtent.X, B.BoxExtent.Y), Height = 2.f * B.BoxExtent.Z;
			Log.Check(FMath::Abs(Length - R.cannon_length_cm) < 5.f && Height > 180.f * 0.5f && Height < 180.f * 0.85f,
				FString::Printf(TEXT("guns: the piece is %.0f cm long and %.0f cm high: chest high to a 180 cm soldier, at its own scale"), Length, Height));
			if (const UStaticMeshSocket* Muzzle = Asset->FindSocket(TEXT("Muzzle")))
			{
				Log.Check(Muzzle->RelativeLocation.X > B.Origin.X + B.BoxExtent.X * 0.7f && Muzzle->RelativeLocation.Z > B.Origin.Z,
					TEXT("guns: the muzzle socket is at the +X end, above the axle"));
			}
		}
	}

	// The campaign's guns reach the battle and come back from it.
	{
		pa250::CampaignState St = pa250::load_scenario(pa250::read_text_file(TCHAR_TO_UTF8(*UPA250Subsystem::ScenarioPath())));
		Log.Check(St.province_stores.count("concord") && St.province_stores.at("concord").guns == 2 && St.armies.at("british_garrison").guns == 4
			&& pa250::served_guns(St.armies.at("british_garrison")) == 4 && St.armies.at("continental_militia").guns == 0 && St.armies.at("british_regulars").guns == 0,
			TEXT("guns: the scenario opens with 2 guns laid up at Concord and 4 served guns in the Boston garrison; neither field army has any"));
		St.armies.at("british_regulars").province = "lexington";
		St.armies.at("continental_militia").province = "lexington";
		St.armies.at("british_regulars").guns = 2;
		St.armies.at("british_regulars").units["artilleryman"] = 1;
		St.pending_battle = {true, "lexington", "british_regulars", "continental_militia"};
		const pa250::BattleSetup Setup = pa250::build_battle_setup(St, "lexington");
		bool bRow = false;
		for (const auto& F : Setup.sides.at("british").factors) bRow |= F.label == "guns" && F.value == 1;
		Log.Check(Setup.sides.at("british").guns == 1 && Setup.sides.at("continental").guns == 0 && bRow,
			TEXT("guns: an army with 2 guns and one crew brings 1 to the field, and the setup says why"));
		pa250::BattleResult Res;
		Res.province_id = "lexington";
		Res.winner = "continental";
		Res.end_reason = "army_broke";
		Res.guns_taken["continental"] = 1;
		const pa250::CampaignState After = pa250::apply_battle_result(St, Res);
		const PA250BattleCards::FAfter Done = PA250BattleCards::AfterBattle(After.progress.last_battle_report, After);
		bool bLine = false;
		for (const FString& L : Done.MapChanges) bLine |= L.Contains(TEXT("gun"));
		Log.Check(After.armies.at("continental_militia").guns == 1 && After.armies.at("british_regulars").guns == 1 && After.progress.last_battle_report.guns_taken.count("continental") == 1 && bLine,
			TEXT("guns: a gun the player overran is his after the battle, on the report and on the card"));
	}
}

void RunClasses(FLog& Log)
{
	// The class numbers live in unit_classes.json (user decision 2026-11-14). The built-in table is
	// only a fallback: this case fails if the file is missing or does not cover a class.
	const FString Path = PA250UnitClasses::DataPath();
	TArray<FString> Errors;
	const bool bLoaded = PA250UnitClasses::LoadFromFile(Path, &Errors);
	Log.Check(FPaths::FileExists(Path), FString::Printf(TEXT("classes data: %s exists"), *Path));
	Log.Check(bLoaded && Errors.Num() == 0 && PA250UnitClasses::LoadedFromData(),
		FString::Printf(TEXT("classes data: unit_classes.json loads with no problems (%s)"), Errors.Num() ? *Errors[0] : TEXT("none")));
	PA250UnitClasses::ResetFallbackUses();
	for (const TCHAR* F : {TEXT("british"), TEXT("continental")})
	{
		for (const FName& Class : PA250UnitClasses::RequiredClasses(F))
		{
			const int32 Before = PA250UnitClasses::FallbackUses();
			const FPA250UnitClassKit& K = PA250UnitClasses::KitFor(F, Class);
			Log.Check(PA250UnitClasses::FallbackUses() == Before && K.Id == Class,
				FString::Printf(TEXT("classes data: %s %s is served from the file, not the fallback"), F, *Class.ToString()));
			Log.Check(!FString(K.Role).IsEmpty() && !FString(K.Verb).IsEmpty() && !FString(K.Label).IsEmpty(),
				FString::Printf(TEXT("classes data: %s %s has a label, a one-line role and a verb"), F, *Class.ToString()));
			Log.Check(K.PreferMaxM >= K.PreferMinM && K.AccurateM > 0.f && K.TicketCost > 0.f && K.CaptureWeight > 0.f,
				FString::Printf(TEXT("classes data: %s %s has a sane band and firelock (%.0f-%.0f m, accurate %.0f m)"), F, *Class.ToString(), K.PreferMinM, K.PreferMaxM, K.AccurateM));
		}
	}
	Log.Check(PA250UnitClasses::FallbackUses() == 0, FString::Printf(TEXT("classes data: no class fell back to the built-in table (%d uses)"), PA250UnitClasses::FallbackUses()));

	// The fallback is used, counted and loud when the file is missing...
	{
		PA250UnitClasses::FScopedExpectFallback Expected; // logged as Display here; an Error anywhere else
		TArray<FString> Missing;
		Log.Check(!PA250UnitClasses::LoadFromFile(Path + TEXT(".nowhere"), &Missing) && Missing.Num() > 0 && !PA250UnitClasses::LoadedFromData(),
			TEXT("classes data: a missing file is refused"));
		PA250UnitClasses::ResetFallbackUses();
		const FPA250UnitClassKit& K = PA250UnitClasses::KitFor(TEXT("continental"), TEXT("rifleman"));
		Log.Check(PA250UnitClasses::FallbackUses() == 1 && K.Id == TEXT("rifleman") && K.RangeScale >= 2.f && !K.bOfficerHunter,
			TEXT("classes data: with no file the built-in kit is served and the use is counted (it has no verb)"));
	}
	// ...and when the file lacks a class either side can field.
	{
		PA250UnitClasses::FScopedExpectFallback Expected;
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		const FString Broken = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("unit_classes_missing_rifleman.json"));
		FFileHelper::SaveStringToFile(Text.Replace(TEXT("\"rifleman\":"), TEXT("\"rifleman_removed\":")), *Broken);
		TArray<FString> Lacking;
		const bool bOk = PA250UnitClasses::LoadFromFile(Broken, &Lacking);
		bool bNamed = false;
		for (const FString& E : Lacking) bNamed |= E.Contains(TEXT("continental has no kit for rifleman"));
		Log.Check(!bOk && bNamed, TEXT("classes data: a file that lacks a class is refused, and says which"));
		IFileManager::Get().Delete(*Broken);
	}
	Log.Check(PA250UnitClasses::LoadFromFile(Path) && PA250UnitClasses::LoadedFromData(), TEXT("classes data: the real file loads again"));
	PA250UnitClasses::ResetFallbackUses();

	// Engagement bands (metres).
	auto Band = [](const TCHAR* F, const TCHAR* C, float Min, float Max) {
		const FPA250UnitClassKit& K = PA250UnitClasses::KitFor(F, C);
		return K.PreferMinM == Min && K.PreferMaxM == Max;
	};
	Log.Check(Band(TEXT("continental"), TEXT("rifleman"), 100.f, 200.f) && Band(TEXT("british"), TEXT("light_infantry"), 50.f, 90.f)
		&& Band(TEXT("british"), TEXT("line_infantry"), 30.f, 60.f) && Band(TEXT("continental"), TEXT("line_infantry"), 30.f, 60.f)
		&& Band(TEXT("continental"), TEXT("militia"), 40.f, 70.f) && Band(TEXT("british"), TEXT("grenadier"), 0.f, 25.f),
		TEXT("classes: rifles 100-200 m, light infantry 50-90, regulars 30-60, militia 40-70, grenadiers close to under 25"));
	// A single musket shot loses accuracy fast past 50 m; a rifle holds to 150 m.
	const FPA250UnitClassKit& Musket = PA250UnitClasses::KitFor(TEXT("british"), TEXT("line_infantry"));
	const FPA250UnitClassKit& Rifle = PA250UnitClasses::KitFor(TEXT("continental"), TEXT("rifleman"));
	Log.Check(Musket.RangeSpreadScale(3000.f) == 1.f && Musket.RangeSpreadScale(5000.f) == 1.f && FMath::IsNearlyEqual(Musket.RangeSpreadScale(10000.f), 2.5f)
		&& FMath::IsNearlyEqual(Musket.RangeSpreadScale(15000.f), 4.f),
		FString::Printf(TEXT("classes: a musket's cone is x1 to 50 m, x2.5 at 100 m, x4 at 150 m (%.2f / %.2f)"), Musket.RangeSpreadScale(10000.f), Musket.RangeSpreadScale(15000.f)));
	Log.Check(Rifle.RangeSpreadScale(15000.f) == 1.f && Rifle.RangeSpreadScale(20000.f) < 1.3f,
		FString::Printf(TEXT("classes: a rifle's cone is still x1 at 150 m (x%.2f at 200 m)"), Rifle.RangeSpreadScale(20000.f)));
	Log.Check(Rifle.SpreadScale * Rifle.RangeSpreadScale(15000.f) < 0.2f * Musket.SpreadScale * Musket.RangeSpreadScale(15000.f),
		TEXT("classes: at 150 m the rifle's cone is under a fifth of the musket's"));
	{
		FScopedTestWorld W;
		AMusketWeapon* Gun = W.World ? W.World->SpawnActor<AMusketWeapon>() : nullptr;
		Log.Check(Gun && Gun->RangeSpreadScale(5000.f) == 1.f && FMath::IsNearlyEqual(Gun->RangeSpreadScale(10000.f), 2.5f),
			TEXT("classes: the weapon's own default is the musket curve"));
		if (Gun)
		{
			Gun->ClassAccurateRangeCm = Rifle.AccurateM * 100.f;
			Gun->ClassSpreadGrowth = Rifle.SpreadGrowth;
			Log.Check(FMath::IsNearlyEqual(Gun->RangeSpreadScale(20000.f), Rifle.RangeSpreadScale(20000.f)), TEXT("classes: the weapon and the kit agree on the rifle's curve"));
			Gun->Destroy();
		}
	}
	// One verb a class.
	auto Flags = [](const FPA250UnitClassKit& K) {
		return int32(K.bVolleyShock) + int32(K.bSteadyInCover) + int32(K.bOfficerHunter) + int32(K.bChargeRouts) + int32(K.bForwardSpawn) + int32(K.bRallies) + int32(K.bTakesPrisoners)
			+ int32(K.bBuildsWorks) + int32(K.bCarriesColours) + int32(K.bRelaysOrders) + int32(K.bTendsWounded) + int32(K.bServesGuns);
	};
	bool bOneEach = true;
	for (const TCHAR* F : {TEXT("british"), TEXT("continental")})
		for (const FName& Class : PA250UnitClasses::RequiredClasses(F)) bOneEach &= Flags(PA250UnitClasses::KitFor(F, Class)) == 1;
	Log.Check(bOneEach, TEXT("classes: every class has exactly one verb"));
	Log.Check(Musket.bVolleyShock && PA250UnitClasses::KitFor(TEXT("continental"), TEXT("militia")).bSteadyInCover && Rifle.bOfficerHunter
		&& PA250UnitClasses::KitFor(TEXT("british"), TEXT("grenadier")).bChargeRouts && PA250UnitClasses::KitFor(TEXT("british"), TEXT("light_infantry")).bForwardSpawn
		&& PA250UnitClasses::KitFor(TEXT("british"), TEXT("officer")).bRallies && PA250UnitClasses::KitFor(TEXT("continental"), TEXT("dragoon")).bTakesPrisoners,
		TEXT("classes: regular volley, militia cover, rifleman officer, grenadier charge, light infantry ground, officer rally, dragoon prisoners"));
	const FPA250ClassVerbs& V = PA250UnitClasses::Verbs();
	Log.Check(V.VolleyMinShooters == 3 && V.VolleyWindowSeconds == 1.f && V.RallyCooldownSeconds > 0.f && V.SurrenderRadiusM > 0.f,
		TEXT("classes: a volley is three muskets within a second; the rally has a cooldown"));

	// Grenadier: a charge that reaches a squad forces its check.
	Log.Check(PA250ChargeRoutResult(EPA250Morale::Shaken) == EPA250Morale::Routing && PA250ChargeRoutResult(EPA250Morale::Withdrawing) == EPA250Morale::Routing
		&& PA250ChargeRoutResult(EPA250Morale::Steady) == EPA250Morale::Shaken && PA250ChargeRoutResult(EPA250Morale::Routing) == EPA250Morale::Routing,
		TEXT("classes: a grenadier charge routs a shaken squad and shakes a steady one"));
	// Militia: half a ticket. Four dead militia take two tickets; four dead regulars take four. Every death is counted.
	{
		const FName C(TEXT("continental")), B(TEXT("british"));
		FPA250TicketLedger L;
		L.Pool.Add(C, 20);
		L.Pool.Add(B, 20);
		const float Half = PA250UnitClasses::KitFor(C, TEXT("militia")).TicketCost;
		for (int32 i = 0; i < 4; ++i) L.SpendDeath(C, TEXT("militia"), Half);
		for (int32 i = 0; i < 4; ++i) L.SpendDeath(B, TEXT("line_infantry"), PA250UnitClasses::KitFor(B, TEXT("line_infantry")).TicketCost);
		Log.Check(Half == 0.5f && L.Remaining(C) == 18 && L.Remaining(B) == 16 && L.Deaths.FindRef(C).FindRef(TEXT("militia")) == 4,
			FString::Printf(TEXT("classes: four militia lost cost two tickets, four regulars four (%d / %d left of 20)"), L.Remaining(C), L.Remaining(B)));
		FPA250TicketLedger Odd;
		Odd.Pool.Add(C, 5);
		Odd.SpendDeath(C, TEXT("militia"), Half);
		Log.Check(Odd.Remaining(C) == 5, TEXT("classes: the first militiaman lost takes no ticket yet"));
		Odd.SpendDeath(C, TEXT("militia"), Half);
		Log.Check(Odd.Remaining(C) == 4, TEXT("classes: the second does"));
	}
	// Officer: a rally stops a rout. Routing men flee and cannot fire; rallied, they stop, re-form and are steady again.
	{
		FPA250MoraleState St;
		St.Set(EPA250Morale::Routing);
		Log.Check(FPA250MoraleOrders::For(St.State).bFleeToRear && FPA250MoraleOrders::For(St.State).bNeedsRally, TEXT("classes: a routing squad runs for the rear"));
		// Left alone under the same pressure it keeps running.
		FPA250MoraleState Left = St;
		for (int32 i = 0; i < 100; ++i) Left.Tick(0.1f, 0.5f, 1.f, 8.f, 3.5f, false, false);
		Log.Check(Left.State == EPA250Morale::Routing, TEXT("classes: under pressure it does not rally by itself"));
		St.Set(EPA250Morale::Rallying); // what UPA250VolleyCoordinator::RallyNearestRouting does
		Log.Check(!FPA250MoraleOrders::For(St.State).bFleeToRear && FPA250MoraleOrders::For(St.State).MarchScale == 0.f, TEXT("classes: rallied, it stops where it is"));
		for (int32 i = 0; i < 40; ++i) St.Tick(0.1f, 0.5f, 1.f, 8.f, 3.5f, false, false);
		Log.Check(St.State == EPA250Morale::Steady, TEXT("classes: and after re-forming it is steady again: the rally stopped the rout"));
		FPA250MoraleState Broken;
		Broken.Set(EPA250Morale::Rallying);
		Broken.Tick(0.1f, 2.f, 1.f, 8.f, 3.5f, false, false);
		Log.Check(Broken.State == EPA250Morale::Routing, TEXT("classes: a rally under overwhelming pressure does not hold"));
	}
	// Light infantry: more weight at a post.
	Log.Check(PA250UnitClasses::KitFor(TEXT("british"), TEXT("light_infantry")).CaptureWeight > 1.5f && Musket.CaptureWeight == 1.f,
		TEXT("classes: a light infantryman counts for more than one man at a post"));
}
} // namespace PA250BattleCases
