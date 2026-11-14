// PA250MeleeProbe.cpp — rendered check of the melee moves (-PA250MeleeProbe, real window).
// Stages one soldier against one enemy who stands still, runs thrust (hit, kill), charge (hit, miss)
// and butt-stroke side-on, then one thrust from the player's own view, and saves stills to
// Saved/PA250/melee_probe/. Every bot brain is switched off for the run.
#include "PA250BattleGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "MusketWeapon.h"
#include "Kismet/GameplayStatics.h"
#include "PA250MeleeComponent.h"
#include "PA250SoldierAnimInstance.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"
#include "TimerManager.h"
#include "UnrealClient.h"

void APA250BattleGameMode::MaybeStartMeleeProbe()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250MeleeProbe"))) return;

	struct FStage
	{
		TWeakObjectPtr<ARFCharacter> Me, Foe;
		TWeakObjectPtr<ACameraActor> Cam;
		FVector Origin = FVector::ZeroVector, Fwd = FVector::ForwardVector;
		int32 Shots = 0;
	};
	TSharedRef<FStage> S = MakeShared<FStage>();
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("melee_probe"));
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	IFileManager::Get().MakeDirectory(*Dir, true);

	auto At = [this](float Seconds, TFunction<void()> Fn)
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, MoveTemp(Fn)), Seconds, false);
	};
	auto Ground = [this](FVector P)
	{
		FHitResult Hit;
		// Static world only: a channel trace would land on the soldiers' own capsules.
		if (GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 3000), P - FVector(0, 0, 6000), FCollisionObjectQueryParams(ECC_WorldStatic))) P.Z = Hit.ImpactPoint.Z;
		return P;
	};
	auto Shot = [S, Dir](const TCHAR* Name, bool bShowUI = false)
	{
		const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("%02d_%s.png"), ++S->Shots, Name));
		FScreenshotRequest::RequestScreenshot(Path, bShowUI, false);
	};
	auto Report = [S](const TCHAR* What)
	{
		const ARFCharacter* Me = S->Me.Get();
		const ARFCharacter* Foe = S->Foe.Get();
		if (!Me)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 melee probe %s: the player soldier is gone"), What);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 melee probe %s: me visual=%d blow=%d health=%.0f advanced=%.0f cm dilation=%.2f fov=%.1f busy=%d | foe %s health=%.0f flinch=%d locked=%d gap=%.0f cm"),
			What, static_cast<int32>(Me->MeleeComponent->GetVisual()), static_cast<int32>(Me->MeleeComponent->GetLastBlow()), Me->HealthComponent->CurrentHealth,
			FVector::DotProduct(Me->GetActorLocation() - S->Origin, S->Fwd), Me->CustomTimeDilation,
			Me->Camera ? Me->Camera->FieldOfView : 0.f, Me->MeleeComponent->IsBusy() ? 1 : 0,
			!Foe ? TEXT("gone") : Foe->IsDead() ? TEXT("DEAD") : TEXT("alive"),
			Foe && Foe->HealthComponent ? Foe->HealthComponent->CurrentHealth : 0.f,
			Foe && Foe->MeleeComponent && Foe->MeleeComponent->IsFlinching() ? 1 : 0, Foe && Foe->IsMovementLocked() ? 1 : 0,
			Foe ? FVector::Dist2D(Foe->GetActorLocation(), Me->GetActorLocation()) : 0.f);
	};
	// The battle's own soldiers would walk into the lane (the enemy hunts the player): switch every bot brain off.
	auto FreezeBots = [this]()
	{
		for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
		{
			It->SetActorTickEnabled(false);
			It->StopMovement();
		}
	};
	// Put the attacker back on his mark, facing down the lane, with the side camera framing the lane.
	auto Reset = [this, S, Ground, FreezeBots](float LaneCm)
	{
		FreezeBots();
		ARFCharacter* Me = S->Me.Get();
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!Me || !PC) return;
		Me->GetCharacterMovement()->StopMovementImmediately();
		Me->SetActorLocationAndRotation(Ground(S->Origin) + FVector(0, 0, 92), S->Fwd.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* C = Me->GetController()) C->SetControlRotation(S->Fwd.Rotation());
		UE_LOG(LogTemp, Display, TEXT("PA250 melee probe reset: attacker at %s (ground z %.0f) dead=%d pawn=%s"), *Me->GetActorLocation().ToCompactString(),
			Ground(S->Origin).Z, Me->IsDead() ? 1 : 0, *GetNameSafe(Me));
		Me->HealthComponent->MaxHealth = Me->HealthComponent->CurrentHealth = 1.e6f;
		// Side-on, from whichever side has the sun behind the camera (no silhouettes).
		float Side = 1.f;
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeSide="), Side);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, S->Fwd) * Side;
		const FVector Look = Ground(S->Origin + S->Fwd * (0.5f * LaneCm)) + FVector(0, 0, 95);
		const FVector From = Look + Right * FMath::Max(520.f, 1.15f * LaneCm) + FVector(0, 0, 25);
		ACameraActor* Cam = S->Cam.Get();
		if (!Cam)
		{
			Cam = GetWorld()->SpawnActor<ACameraActor>(From, (Look - From).Rotation());
			S->Cam = Cam;
		}
		if (Cam)
		{
			Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
			Cam->GetCameraComponent()->SetFieldOfView(50.f);
			Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
			PC->SetViewTarget(Cam);
		}
	};
	// One enemy standing still DistanceCm down the lane, facing the player (AI off, so nothing but the blow moves him).
	auto SpawnFoe = [this, S, Ground](float DistanceCm, float Health)
	{
		if (ARFCharacter* Old = S->Foe.Get()) Old->Destroy();
		S->Foe = nullptr;
		const ARFCharacter* Me = S->Me.Get();
		if (!Me || DistanceCm <= 0.f) return;
		const FName Faction = Me->FactionId == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));
		const FVector Loc = Ground(S->Origin + S->Fwd * DistanceCm) + FVector(0, 0, 92);
		ARFCharacter* Foe = SpawnBotAt(Faction, TEXT("line_infantry"), FTransform((-S->Fwd).Rotation(), Loc));
		if (!Foe)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 melee probe: could not spawn the enemy"));
			return;
		}
		// Keep the controller (movement and aim need one) but switch its brain off.
		if (AController* C = Foe->GetController())
		{
			C->SetActorTickEnabled(false);
			C->StopMovement();
			C->SetControlRotation((-S->Fwd).Rotation());
		}
		Foe->HealthComponent->MaxHealth = FMath::Max(100.f, Health);
		Foe->HealthComponent->CurrentHealth = Health;
		S->Foe = Foe;
	};

	// Both sides start close together when the map is launched directly; keep the player alive until staged.
	At(0.2f, [this]()
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr) Me->HealthComponent->MaxHealth = Me->HealthComponent->CurrentHealth = 1.e6f;
	});
	At(0.5f, FreezeBots);
	// Sharp stills: motion blur smears exactly the frames this probe is for.
	if (GEngine) GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));

	// Stage once the battle has settled. Side-on stills use a soldier of the player's side with his brain
	// off: a player's musket follows his view target, which here is the side camera, so it would point
	// across the lane instead of down it. The player himself takes over for the last shot.
	At(8.f, [this, S, Reset, SpawnFoe, FreezeBots]()
	{
		FreezeBots();
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ARFCharacter* Player = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (!Player)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 melee probe: no player soldier"));
			FPlatformMisc::RequestExit(false);
			return;
		}
		FVector Origin = Player->GetActorLocation();
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeX="), Origin.X);
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeY="), Origin.Y);
		float Yaw = 90.f;
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeYaw="), Yaw);
		S->Origin = Origin;
		S->Fwd = FRotator(0.f, Yaw, 0.f).Vector();
		ARFCharacter* Me = SpawnBotAt(Player->FactionId, TEXT("line_infantry"), FTransform(S->Fwd.Rotation(), Origin + FVector(0, 0, 200)));
		if (!Me)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 melee probe: could not spawn the attacker"));
			FPlatformMisc::RequestExit(false);
			return;
		}
		if (AController* C = Me->GetController()) C->SetActorTickEnabled(false);
		Me->MeleeComponent->SetBayonetOverride(true); // the bayonet cases; the club case below takes it away
		S->Me = Me;
		Reset(300.f);
		SpawnFoe(300.f, 1.e6f);
		UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: staged %s attacker (period soldier=%d) at %s"), *Me->FactionId.ToString(), Me->IsUsingPeriodSoldier() ? 1 : 0, *S->Origin.ToCompactString());
	});

	// 1. Thrust that lands on a man who survives: coil, lunge + hit-stop, flinch, recovery.
	At(10.5f, [Shot]() { Shot(TEXT("idle")); });
	At(11.f, [S, Report]() { if (ARFCharacter* Me = S->Me.Get()) { Me->MeleeComponent->DoThrust(); Report(TEXT("thrust start")); } });
	At(11.18f, [Shot, Report]() { Shot(TEXT("thrust_coil")); Report(TEXT("thrust coil")); });
	At(11.38f, [Shot, Report]() { Shot(TEXT("thrust_hit")); Report(TEXT("thrust hit")); });
	At(11.52f, [Shot, Report]() { Shot(TEXT("thrust_flinch")); Report(TEXT("thrust flinch")); });
	At(12.4f, [Shot, Report]() { Shot(TEXT("thrust_recovered")); Report(TEXT("thrust recovered")); });

	// 2. Thrust that kills.
	At(13.f, [Reset, SpawnFoe]() { Reset(300.f); SpawnFoe(300.f, 50.f); });
	At(14.f, [S, Report]() { if (ARFCharacter* Me = S->Me.Get()) { Me->MeleeComponent->DoThrust(); Report(TEXT("kill start")); } });
	At(14.42f, [Shot, Report]() { Shot(TEXT("kill_hold")); Report(TEXT("kill hold")); });
	At(15.2f, [Shot, Report]() { Shot(TEXT("kill_after")); Report(TEXT("kill after")); });

	// 3. Charge that meets a man.
	At(16.f, [Reset, SpawnFoe]() { Reset(600.f); SpawnFoe(600.f, 1.e6f); });
	At(17.f, [S, Report]() { if (ARFCharacter* Me = S->Me.Get()) { Me->MeleeComponent->DoCharge(); Report(TEXT("charge start")); } });
	At(17.22f, [Shot, Report]() { Shot(TEXT("charge_run")); Report(TEXT("charge run")); });
	At(17.5f, [Shot, Report]() { Shot(TEXT("charge_strike")); Report(TEXT("charge strike")); });
	At(17.75f, [Shot, Report]() { Shot(TEXT("charge_thrown_back")); Report(TEXT("charge thrown back")); });

	// 4. Butt-stroke.
	At(19.5f, [Reset, SpawnFoe]() { Reset(200.f); SpawnFoe(125.f, 1.e6f); });
	At(20.5f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoButtStroke(); });
	At(20.6f, [Shot, Report]() { Shot(TEXT("butt_stroke")); Report(TEXT("butt-stroke")); });
	At(20.75f, [Shot]() { Shot(TEXT("butt_flinch")); });

	// 5. Charge that meets nobody.
	At(22.f, [Reset, SpawnFoe]() { Reset(600.f); SpawnFoe(0.f, 0.f); });
	At(23.f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoCharge(); });
	At(23.85f, [Shot, Report]() { Shot(TEXT("charge_whiff")); Report(TEXT("charge whiff")); });

	// 5a. No one-blow kill from the front: a charge on a full-health man who is reloading (70 x2 x1.5 = 210) leaves him at 15.
	// (The charge waits out the 4 s cooldown from the whiff above.)
	At(24.8f, [S, Reset, SpawnFoe]()
	{
		Reset(600.f);
		SpawnFoe(600.f, 100.f);
		if (ARFCharacter* Foe = S->Foe.Get(); Foe && Foe->Musket) Foe->Musket->StartReload();
	});
	At(27.1f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoCharge(); });
	At(27.95f, [Shot, Report]() { Shot(TEXT("charge_not_a_kill")); Report(TEXT("frontal charge on a reloading man")); });

	// 5b. Guard held: the thrust is blocked (15 % gets through).
	At(28.6f, [S, Reset, SpawnFoe]()
	{
		Reset(300.f);
		SpawnFoe(300.f, 1.e6f);
		if (ARFCharacter* Foe = S->Foe.Get()) Foe->MeleeComponent->SetGuardHeld(true);
	});
	At(29.4f, [Shot]() { Shot(TEXT("guard_pose")); });
	At(29.7f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoThrust(); });
	At(30.4f, [Shot, Report]() { Shot(TEXT("guard_block")); Report(TEXT("thrust into a held guard")); });

	// 5c. Guard raised at the last moment: parry, attacker thrown off, then the riposte.
	At(31.f, [Reset, SpawnFoe]() { Reset(300.f); SpawnFoe(300.f, 1.e6f); });
	At(32.f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoThrust(); });
	At(32.22f, [S]() { if (ARFCharacter* Foe = S->Foe.Get()) Foe->MeleeComponent->SetGuardHeld(true); });
	At(32.75f, [S, Shot, Report]()
	{
		Shot(TEXT("parry"));
		Report(TEXT("thrust into a late guard"));
		if (ARFCharacter* Foe = S->Foe.Get())
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: foe can riposte=%d"), Foe->MeleeComponent->CanRiposte() ? 1 : 0);
			Foe->MeleeComponent->DoThrust();
		}
	});
	At(33.5f, [Shot, Report]() { Shot(TEXT("riposte")); Report(TEXT("after the riposte")); });

	// 5d. No bayonet: the clubbed-musket swing (raised, then brought down on him).
	At(34.f, [S, Reset, SpawnFoe]()
	{
		Reset(300.f);
		SpawnFoe(300.f, 1.e6f);
		if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->SetBayonetOverride(false);
	});
	At(35.f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoThrust(); });
	At(35.24f, [Shot]() { Shot(TEXT("club_raised")); });
	At(35.62f, [Shot, Report]() { Shot(TEXT("club_hit")); Report(TEXT("clubbed swing")); });
	At(36.3f, [Report]() { Report(TEXT("clubbed swing after")); });

	// 5e. No bayonet: the rush (G) - musket carried raised, brought down on contact.
	At(37.f, [Reset, SpawnFoe]() { Reset(600.f); SpawnFoe(600.f, 1.e6f); });
	At(38.f, [S, Report]() { if (ARFCharacter* Me = S->Me.Get()) { Me->MeleeComponent->DoCharge(); Report(TEXT("rush start")); } });
	At(38.22f, [Shot, Report]() { Shot(TEXT("rush_run")); Report(TEXT("rush run")); });
	At(38.5f, [Shot, Report]() { Shot(TEXT("rush_blow")); Report(TEXT("rush blow")); });
	At(39.f, [Report]() { Report(TEXT("rush after")); });

	// 5f. How he holds the musket: close-ups from his right and his left, at rest and on guard.
	auto CloseUp = [S](float Side, float Ahead = 150.f, float Up = 25.f)
	{
		const ARFCharacter* Me = S->Me.Get();
		ACameraActor* Cam = S->Cam.Get();
		if (!Me || !Cam) return;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, S->Fwd) * Side;
		const FVector Look = Me->GetActorLocation() + S->Fwd * 30.f + FVector(0, 0, Up);
		const FVector From = Look + S->Fwd * Ahead + Right * 120.f + FVector(0, 0, 20);
		Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
		Cam->GetCameraComponent()->SetFieldOfView(50.f);
	};
	At(40.f, [Reset, SpawnFoe, CloseUp]() { Reset(300.f); SpawnFoe(0.f, 0.f); CloseUp(1.f); });
	At(40.7f, [Shot]() { Shot(TEXT("hold_from_his_right")); });
	At(40.9f, [CloseUp]() { CloseUp(-1.f); });
	At(41.4f, [Shot]() { Shot(TEXT("hold_from_his_left")); });
	At(41.5f, [CloseUp]() { CloseUp(1.f, -90.f, 10.f); }); // from behind his right shoulder: the right hand on the stock
	At(42.f, [Shot]() { Shot(TEXT("hold_right_hand")); });
	At(42.1f, [S, CloseUp]() { CloseUp(1.f, 170.f, 45.f); if (ARFCharacter* Me = S->Me.Get()) { Me->MeleeComponent->SetGuardHeld(true); Me->MeleeComponent->SetBayonetOverride(true); } });
	At(42.7f, [Shot]() { Shot(TEXT("hold_on_guard")); });
	At(42.8f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->SetGuardHeld(false); });

	// 5g. Musket at the shoulder (aim / "Present!"): side-on and close.
	At(43.f, [S, Reset]() { Reset(300.f); if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->Present(2.6f); });
	At(43.8f, [Shot]() { Shot(TEXT("shouldered_side")); });
	At(43.9f, [CloseUp]() { CloseUp(1.f, 170.f, 45.f); });
	At(44.6f, [Shot]() { Shot(TEXT("shouldered_close")); });

	// 6. What the player sees and feels (hit-stop, camera): his own soldier, his own camera, HUD on.
	At(46.f, [this, S, Reset, SpawnFoe]()
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ARFCharacter* Player = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (ARFCharacter* Bot = S->Me.Get()) Bot->Destroy();
		S->Me = Player;
		Reset(300.f);
		SpawnFoe(300.f, 10.f); // dies to the thrust: hit effect and the kill slow-motion
		if (PC && Player) PC->SetViewTarget(Player);
	});
	At(47.7f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoThrust(); });
	At(48.08f, [Shot, Report]() { Shot(TEXT("player_view_hit"), true); Report(TEXT("player view hit")); UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: global time dilation %.2f"), UGameplayStatics::GetGlobalTimeDilation(GWorld)); });
	At(48.2f, [Shot]() { Shot(TEXT("player_view_kill"), true); UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: global time dilation %.2f"), UGameplayStatics::GetGlobalTimeDilation(GWorld)); });
	At(49.6f, []() { UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: global time dilation %.2f (after)"), UGameplayStatics::GetGlobalTimeDilation(GWorld)); });
	// 7. The reload drill on the player's own soldier (10 s Arcade reload left to run untouched), side-on.
	At(50.2f, [S, Reset, SpawnFoe]()
	{
		Reset(140.f);
		SpawnFoe(0.f, 0.f);
		if (ARFCharacter* Me = S->Me.Get(); Me && Me->Musket)
		{
			Me->Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
			Me->Musket->StartReload();
		}
	});
	auto ReloadShot = [S, Shot](const TCHAR* Name)
	{
		Shot(Name);
		if (ARFCharacter* Me = S->Me.Get(); Me && Me->Musket)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 melee probe reload %s: phase=%d progress=%.2f pose reload=%.2f"), Name,
				static_cast<int32>(Me->Musket->GetSequencer().GetPhase()), Me->Musket->GetSequencer().GetPhaseProgress(), Me->MeleeComponent->GetBodyPose().Reload);
		}
	};
	// Priming is done with the right hand: shoot it from his right front, then go back side-on for the ramming.
	auto RightFront = [S]()
	{
		const ARFCharacter* Me = S->Me.Get();
		ACameraActor* Cam = S->Cam.Get();
		if (!Me || !Cam) return;
		const FVector Look = Me->GetActorLocation() + FVector(0, 0, 25);
		const FVector From = Look + S->Fwd * 230.f + FVector::CrossProduct(FVector::UpVector, S->Fwd) * 230.f + FVector(0, 0, 30);
		Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
	};
	At(50.4f, RightFront);
	At(53.3f, [Reset]() { Reset(140.f); });
	At(51.f, [ReloadShot]() { ReloadShot(TEXT("reload_cartridge_box")); });
	At(51.75f, [ReloadShot]() { ReloadShot(TEXT("reload_bite")); });
	At(52.9f, [ReloadShot]() { ReloadShot(TEXT("reload_prime")); });
	At(54.3f, [ReloadShot]() { ReloadShot(TEXT("reload_charge_muzzle")); });
	At(55.3f, [ReloadShot]() { ReloadShot(TEXT("reload_ram_up")); });
	At(55.65f, [ReloadShot]() { ReloadShot(TEXT("reload_ram_down")); });
	At(58.4f, [ReloadShot]() { ReloadShot(TEXT("reload_recover")); });
	At(60.6f, [ReloadShot]() { ReloadShot(TEXT("reload_done")); });

	// 8. What the player sees of his own soldier, from his own camera behind: the carry (standing, walking,
	// running), the reload drill standing, and a reload begun and then walked through.
	auto PlayerView = [this, S, Reset]()
	{
		Reset(140.f);
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (ARFCharacter* Me = S->Me.Get(); PC && Me) PC->SetViewTarget(Me);
	};
	// Speed 0 stops him. The input is fed every frame, as a held key would.
	TSharedRef<float> DriveSpeed = MakeShared<float>(0.f);
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [S, DriveSpeed]()
		{
			ARFCharacter* Me = S->Me.Get();
			if (!Me || *DriveSpeed <= 0.f) return;
			Me->GetCharacterMovement()->MaxWalkSpeed = *DriveSpeed;
			Me->AddMovementInput(S->Fwd, 1.f);
		}), 0.008f, true);
	}
	auto Drive = [S, DriveSpeed](float Speed)
	{
		*DriveSpeed = Speed;
		if (ARFCharacter* Me = S->Me.Get(); Me && Speed <= 0.f) Me->GetCharacterMovement()->MaxWalkSpeed = Me->WalkSpeed;
	};
	auto BeginReload = [S]()
	{
		if (ARFCharacter* Me = S->Me.Get(); Me && Me->Musket)
		{
			Me->Musket->SetReloadPreset(EPA250ReloadPreset::Arcade);
			Me->Musket->StartReload();
		}
	};
	At(61.2f, PlayerView);
	At(62.4f, [Shot]() { Shot(TEXT("hold_player_view")); });
	At(62.5f, [Drive]() { Drive(200.f); });
	At(63.7f, [Shot]() { Shot(TEXT("hold_player_view_walk")); });
	At(63.8f, [Drive]() { Drive(450.f); });
	At(65.f, [Shot]() { Shot(TEXT("hold_player_view_run")); });
	At(65.1f, [Drive]() { Drive(0.f); });
	At(66.f, [PlayerView, BeginReload]() { PlayerView(); BeginReload(); });
	At(67.6f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_bite")); });
	At(68.7f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_prime")); });
	At(71.1f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_ram")); });
	At(77.f, [PlayerView, BeginReload]() { PlayerView(); BeginReload(); });
	At(78.2f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_before_walk")); });
	// Ordinary pace: the loading runs on. At a run (the player only) it stands still until he slows.
	At(78.3f, [S, Drive]() { if (const ARFCharacter* Me = S->Me.Get()) Drive(Me->WalkSpeed); });
	At(80.f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_walking")); });
	At(80.1f, [S, Drive]() { if (const ARFCharacter* Me = S->Me.Get()) Drive(Me->SprintSpeed); });
	At(81.f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_running")); });
	At(83.2f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_running_later")); });
	At(83.3f, [Drive]() { Drive(0.f); });
	At(85.3f, [ReloadShot]() { ReloadShot(TEXT("reload_player_view_stopped")); });
	// 9. The same with the player looking back at his own soldier (the mouse turns the camera, not the man).
	At(86.f, [S, PlayerView]()
	{
		PlayerView();
		if (ARFCharacter* Me = S->Me.Get(); Me && Me->GetController()) Me->GetController()->SetControlRotation(FRotator(-8.f, S->Fwd.Rotation().Yaw + 150.f, 0.f));
	});
	At(87.2f, [Shot]() { Shot(TEXT("hold_front_view")); });
	At(87.3f, BeginReload);
	At(88.9f, [ReloadShot]() { ReloadShot(TEXT("reload_front_view_bite")); });
	At(90.f, [ReloadShot]() { ReloadShot(TEXT("reload_front_view_prime")); });
	At(90.1f, [Drive]() { Drive(200.f); });
	At(91.6f, [ReloadShot]() { ReloadShot(TEXT("reload_front_view_walking")); });
	At(95.f, [ReloadShot]() { ReloadShot(TEXT("reload_front_view_walking_later")); });
	At(95.1f, [Drive]() { Drive(0.f); });
	At(97.f, [ReloadShot]() { ReloadShot(TEXT("reload_front_view_stopped")); });
	// Loaded again by now (walking does not stop the loading): the carry seen from in front, standing and walking.
	At(99.f, [ReloadShot]() { ReloadShot(TEXT("hold_front_view_loaded")); });
	At(99.1f, [Drive]() { Drive(200.f); });
	At(100.3f, [Shot]() { Shot(TEXT("hold_front_view_walk")); });
	At(100.5f, []() { UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: done")); FPlatformMisc::RequestExit(false); });
}
