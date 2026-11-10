// PA250MeleeProbe.cpp — rendered check of the melee moves (-PA250MeleeProbe, real window).
// Stages the player's own soldier against one enemy who stands still, runs thrust (hit, kill),
// charge (hit, miss) and butt-stroke, and saves stills to Saved/PA250/melee_probe/.
#include "PA250BattleGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "PA250MeleeComponent.h"
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
		if (GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 3000), P - FVector(0, 0, 6000), ECC_WorldStatic)) P.Z = Hit.ImpactPoint.Z;
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
		UE_LOG(LogTemp, Display, TEXT("PA250 melee probe %s: me visual=%d advanced=%.0f cm dilation=%.2f fov=%.1f busy=%d | foe %s health=%.0f flinch=%d locked=%d gap=%.0f cm"),
			What, static_cast<int32>(Me->MeleeComponent->GetVisual()),
			FVector::DotProduct(Me->GetActorLocation() - S->Origin, S->Fwd), Me->CustomTimeDilation,
			Me->Camera ? Me->Camera->FieldOfView : 0.f, Me->MeleeComponent->IsBusy() ? 1 : 0,
			!Foe ? TEXT("gone") : Foe->IsDead() ? TEXT("DEAD") : TEXT("alive"),
			Foe && Foe->HealthComponent ? Foe->HealthComponent->CurrentHealth : 0.f,
			Foe && Foe->MeleeComponent && Foe->MeleeComponent->IsFlinching() ? 1 : 0, Foe && Foe->IsMovementLocked() ? 1 : 0,
			Foe ? FVector::Dist2D(Foe->GetActorLocation(), Me->GetActorLocation()) : 0.f);
	};
	// Put the player back on his mark, facing down the lane, with the side camera framing the lane.
	auto Reset = [this, S, Ground](float LaneCm)
	{
		ARFCharacter* Me = S->Me.Get();
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!Me || !PC) return;
		Me->SetActorLocationAndRotation(Ground(S->Origin) + FVector(0, 0, 92), S->Fwd.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(S->Fwd.Rotation());
		UE_LOG(LogTemp, Display, TEXT("PA250 melee probe reset: player at %s (ground z %.0f) dead=%d pawn=%s"), *Me->GetActorLocation().ToCompactString(),
			Ground(S->Origin).Z, Me->IsDead() ? 1 : 0, *GetNameSafe(PC->GetPawn()));
		Me->HealthComponent->MaxHealth = Me->HealthComponent->CurrentHealth = 1.e6f;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, S->Fwd);
		const FVector Look = Ground(S->Origin + S->Fwd * (0.5f * LaneCm)) + FVector(0, 0, 95);
		const FVector From = Look + Right * FMath::Max(480.f, 1.1f * LaneCm) + FVector(0, 0, 25);
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
	// One enemy standing still DistanceCm down the lane, facing the player (no AI, so nothing but the blow moves him).
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
		if (AController* C = Foe->GetController()) C->UnPossess();
		Foe->GetCharacterMovement()->bRunPhysicsWithNoController = true;
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

	// Stage once the battle has settled: a lane 20 m west of the Green, away from both spawns.
	At(8.f, [this, S, Reset, SpawnFoe]()
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (!Me)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 melee probe: no player soldier"));
			FPlatformMisc::RequestExit(false);
			return;
		}
		S->Me = Me;
		FVector Origin = Me->GetActorLocation();
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeX="), Origin.X);
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeY="), Origin.Y);
		float Yaw = 90.f;
		FParse::Value(FCommandLine::Get(), TEXT("PA250MeleeProbeYaw="), Yaw);
		S->Origin = Origin;
		S->Fwd = FRotator(0.f, Yaw, 0.f).Vector();
		Reset(300.f);
		SpawnFoe(300.f, 1.e6f);
		UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: staged %s (period soldier=%d) at %s"), *Me->FactionId.ToString(), Me->IsUsingPeriodSoldier() ? 1 : 0, *S->Origin.ToCompactString());
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
	At(14.f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoThrust(); });
	At(14.42f, [Shot, Report]() { Shot(TEXT("kill_hold")); Report(TEXT("kill hold")); });
	At(15.2f, [Shot, Report]() { Shot(TEXT("kill_after")); Report(TEXT("kill after")); });

	// 3. Charge that meets a man.
	At(16.f, [Reset, SpawnFoe]() { Reset(600.f); SpawnFoe(600.f, 1.e6f); });
	At(17.f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoCharge(); });
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

	// 6. What the player sees: his own camera, HUD on.
	At(24.8f, [this, S, Reset, SpawnFoe]()
	{
		Reset(300.f);
		SpawnFoe(300.f, 1.e6f);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetViewTarget(S->Me.Get());
	});
	At(26.5f, [S]() { if (ARFCharacter* Me = S->Me.Get()) Me->MeleeComponent->DoThrust(); });
	At(26.88f, [Shot, Report]() { Shot(TEXT("player_view_hit"), true); Report(TEXT("player view hit")); });
	At(28.5f, []() { UE_LOG(LogTemp, Display, TEXT("PA250 melee probe: done")); FPlatformMisc::RequestExit(false); });
}
