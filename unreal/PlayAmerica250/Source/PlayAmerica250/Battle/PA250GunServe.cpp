// PA250GunServe.cpp — -PA250GunServe: deploy onto a gun, keep a crew, cycle the load, refuse grape
// that is too far, fire grape once, and quit. Headless is enough (no window): the asserts are log lines.
//
//   UnrealEditor-Cmd <uproject> /Game/PA250/Maps/L_Lexington -game -nullrhi -unattended -nosplash -nosound \
//     -PA250GunServe -PA250Bots=0 -log
//
// Pass: a log line "PA250 gun serve: PASS".
// On this Mac, -game currently dies in FMacCursor (SIGBUS 0xbad4007) before the map, the same
// crash as -PA250ClassPick. The same steps (one man cannot fire, cycle to LOAD: GRAPE, refuse
// grape too far, two men fire once, mesh stays level) run inside -run=PA250Battle.
#include "PA250BattleGameMode.h"
#include "PA250FieldGun.h"
#include "PA250BotController.h"
#include "RFCharacter.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "PA250SoldierAnimInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void APA250BattleGameMode::MaybeStartGunServe()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250GunServe")) || !GetWorld()) return;
	UE_LOG(LogTemp, Display, TEXT("PA250 gun serve: probe armed"));
	const bool bRender = FParse::Param(FCommandLine::Get(), TEXT("PA250GunServeRender"));
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, bRender]() {
		auto Fail = [](const TCHAR* Why) {
			UE_LOG(LogTemp, Error, TEXT("PA250 gun serve: FAIL %s"), Why);
			FPlatformMisc::RequestExit(false);
		};
		if (!GetWorld())
		{
			Fail(TEXT("no world"));
			return;
		}
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (!Me)
		{
			Fail(TEXT("no player pawn"));
			return;
		}
		const FName Faction = GetPlayerFaction().IsNone() ? Me->FactionId : GetPlayerFaction();
		ConfigureSoldier(Me, Faction, TEXT("artilleryman"));
		APA250FieldGun* Gun = nullptr;
		for (APA250FieldGun* G : Guns)
		{
			if (G && G->GetFaction() == Me->FactionId)
			{
				Gun = G;
				break;
			}
		}
		if (!Gun)
		{
			const FVector At = Me->GetActorLocation() + FVector(600.f, 0.f, 0.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Gun = GetWorld()->SpawnActor<APA250FieldGun>(At, FRotator(0.f, Me->GetActorRotation().Yaw, 0.f), Params);
			if (Gun)
			{
				Gun->Place(Me->FactionId);
				Guns.Add(Gun);
				UE_LOG(LogTemp, Display, TEXT("PA250 gun serve: placed a gun (none of the player's side was on the field)"));
			}
		}
		if (!Gun)
		{
			Fail(TEXT("no gun"));
			return;
		}
		if (!PlacePlayerAtGun(Me))
		{
			Fail(TEXT("could not place the player at the gun"));
			return;
		}
		Gun = nullptr;
		for (APA250FieldGun* G : Guns)
		{
			if (G && G->GetPlayerGunner() == Me)
			{
				Gun = G;
				break;
			}
		}
		if (!Gun || Gun->GetCrew() < Rules.cannon_min_crew)
		{
			Fail(TEXT("crew still under min_crew after deploy"));
			return;
		}
		if (!bRender && FParse::Param(FCommandLine::Get(), TEXT("PA250SpyglassProbe")))
		{
			// The gunner's glass: raised from a camera at his front-right (raising, at the eye), then
			// seen through from his own eye with the HUD, then lowered. Stills in docs/preview.
			PC->SetControlRotation(FRotator(-2.f, Gun->GetActorRotation().Yaw, 0.f));
			Me->SetActorRotation(FRotator(0.f, Gun->GetActorRotation().Yaw, 0.f));
			const FVector Head = Me->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			const FVector CamAt = Head + Me->GetActorRightVector() * 170.f + Me->GetActorForwardVector() * 150.f;
			ACameraActor* Side = GetWorld()->SpawnActor<ACameraActor>(CamAt, (Head - CamAt).Rotation());
			if (Side) PC->SetViewTarget(Side);
			Me->ProbeAim(true);
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview")));
			struct FStep { float At; const TCHAR* Name; int32 View; bool bAim; };  // View: 0 side camera, 1 his eye, 2 his eye zoomed all the way in, -1 quit
			static const FStep Steps[] = {{0.25f, TEXT("spyglass_raising"), 0, true}, {1.6f, TEXT("spyglass_raised"), 0, true},
				{3.6f, TEXT("spyglass_view"), 1, true}, {5.0f, TEXT("spyglass_view_zoomed"), 2, true}, {7.4f, TEXT("spyglass_lowered"), 0, false}, {8.4f, nullptr, -1, false}};
			for (const FStep& S : Steps)
			{
				// Each change of view lands half a second before its still.
				FTimerHandle Prep;
				GetWorldTimerManager().SetTimer(Prep, FTimerDelegate::CreateWeakLambda(this, [PC, Me, Side, S]() {
					if (S.View == 1) PC->SetViewTarget(Me);
					else if (S.View == 2) Me->ZoomSpyglass(100);
					else if (S.View == 0 && Side) PC->SetViewTarget(Side);
					Me->ProbeAim(S.bAim);
				}), FMath::Max(0.05f, S.At - 0.5f), false);
				FTimerHandle Shot;
				GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [PC, Me, S, Dir]() {
					if (S.View < 0)
					{
						UE_LOG(LogTemp, Display, TEXT("PA250 spyglass probe: done"));
						FPlatformMisc::RequestExit(false);
						return;
					}
					const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("%s.png"), S.Name));
					FScreenshotRequest::RequestScreenshot(Path, S.View >= 1, false);
					if (const UStaticMeshComponent* G = Me->GetSpyglass(); G && Me->GetMesh())
					{
						const FTransform& Body = Me->GetMesh()->GetComponentTransform();
						const FVector Hand = Body.InverseTransformPosition(Me->GetMesh()->GetBoneLocation(TEXT("hand_r"), EBoneSpaces::WorldSpace));
						const FVector At = Body.InverseTransformPosition(G->GetComponentLocation());
						const FVector Tip = Body.InverseTransformPosition(G->GetComponentLocation() + G->GetForwardVector() * 50.f);
						UE_LOG(LogTemp, Display, TEXT("PA250 spyglass probe: %s body space: hand_r %s glass %s tip %s bounds %s"), S.Name,
							*Hand.ToCompactString(), *At.ToCompactString(), *Tip.ToCompactString(), *G->Bounds.BoxExtent.ToCompactString());
					}
					UE_LOG(LogTemp, Display, TEXT("PA250 spyglass probe: %s raise %.2f through=%d fov %.1f glass visible=%d upper clip %s"), S.Name,
						Me->GetSpyglassRaise(), Me->IsLookingThroughGlass() ? 1 : 0, PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0.f,
						Me->GetSpyglass() && Me->GetSpyglass()->IsVisible() ? 1 : 0,
						Me->GetMesh() && Cast<UPA250SoldierAnimInstance>(Me->GetMesh()->GetAnimInstance()) && Cast<UPA250SoldierAnimInstance>(Me->GetMesh()->GetAnimInstance())->GetUpperBodyClip()
							? *Cast<UPA250SoldierAnimInstance>(Me->GetMesh()->GetAnimInstance())->GetUpperBodyClip()->GetName() : TEXT("none"));
				}), S.At, false);
			}
			return;
		}
		if (bRender)
		{
			while (Gun->GetCrew() < Rules.cannon_crew_men)
			{
				const int32 Slot = Gun->FreePostIndex();
				ARFCharacter* Mate = Slot == INDEX_NONE ? nullptr : SummonGunMate(Gun, Slot);
				if (!Mate) { Fail(TEXT("render probe requires real artillery reserve (-PA250ExtraUnits)")); return; }
				Gun->ClaimPost(Mate); Gun->RecountCrew();
			}
		}
		Gun->SetActorRotation(FRotator(0.f, Gun->GetActorRotation().Yaw, 0.f));
		const FVector Muzzle = Gun->GetMuzzle();
		const FVector Fwd = Gun->GetActorForwardVector().GetSafeNormal2D();
		const FVector Forward = Fwd.IsNearlyZero() ? FVector::ForwardVector : Fwd;
		const FVector Close = Muzzle + Forward * (Rules.cannon_grape_range_m * 0.5f * 100.f);
		const float Lay = Gun->LayTo(Close);
		const float ActorPitch = Gun->GetActorRotation().Pitch;
		UE_LOG(LogTemp, Display, TEXT("PA250 gun serve: lay pitch %.2f actor pitch %.2f"), Lay, ActorPitch);
		if (Lay <= 0.05f || FMath::Abs(ActorPitch) > 1.f)
		{
			Fail(TEXT("pitch lay did not stay ballistic on a level mesh"));
			return;
		}
		TArray<FString> Loads;
		for (int32 i = 0; i < 3; ++i)
		{
			PlayerCycleGunShot(Me, 1);
			Loads.Add(PA250Gunnery::ShotWords(Gun->GetSelectedShot()));
			UE_LOG(LogTemp, Display, TEXT("PA250 gun serve: load %s"), PA250Gunnery::ShotWords(Gun->GetSelectedShot()));
		}
		// The loads this kind of piece fires, in [ ]'s order (cannon.gun_types: a six-pounder never shells;
		// until 2026-11-18 this expected shell of every gun and failed).
		TArray<FString> Want;
		{
			int32 I = static_cast<int32>(EPA250Shot::RoundShot);
			while (Want.Num() < 3)
			{
				I = (I + 1) % 3;
				if (PA250Gunnery::ShotAllowedForGun(Gun->GetGunType(), static_cast<EPA250Shot>(I), Rules)) Want.Add(PA250Gunnery::ShotWords(static_cast<EPA250Shot>(I)));
			}
		}
		if (Loads != Want)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 gun serve: loads %s, wanted %s"), *FString::Join(Loads, TEXT(", ")), *FString::Join(Want, TEXT(", ")));
			Fail(TEXT("load cycle was not the piece's own loads"));
			return;
		}
		for (int32 Tries = 0; Tries < 3 && Gun->GetSelectedShot() != EPA250Shot::Grape; ++Tries) PlayerCycleGunShot(Me, 1);
		if (Gun->GetSelectedShot() != EPA250Shot::Grape)
		{
			Fail(TEXT("could not select grape"));
			return;
		}
		const FVector Far = Muzzle + Forward * (Rules.cannon_grape_range_m * 2.5f * 100.f);
		const int32 ShotsBefore = Gun->GetShotsFired();
		if (Gun->DischargeAt(Far, Me) || Gun->GetShotsFired() != ShotsBefore)
		{
			Fail(TEXT("grape at long range should have been refused"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 gun serve: refused grape too far"));
		Gun->ForceLoaded();
		if (!Gun->DischargeAt(Close, Me) || Gun->GetShotsFired() != ShotsBefore + 1)
		{
			Fail(TEXT("grape inside range did not fire"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 gun serve: PASS crew %d lay %.2f fired grape"), Gun->GetCrew(), Lay);
		if (!bRender) { FPlatformMisc::RequestExit(false); return; }
		PC->SetControlRotation((Close - Me->GetActorLocation()).Rotation());
		// Keep a repeatable view of the piece and its smoke, while the possessed
		// gunner continues to drive the actual serving HUD and drill.
		const FVector CameraAt = Gun->GetActorLocation() - Forward * 650.f
			+ Gun->GetActorRightVector() * 550.f + FVector(0.f, 0.f, 300.f);
		const FVector CameraMark = Gun->GetActorLocation() + Forward * 600.f + FVector(0.f, 0.f, 100.f);
		if (auto* Camera = GetWorld()->SpawnActor<ACameraActor>(CameraAt, (CameraMark - CameraAt).Rotation()))
			PC->SetViewTarget(Camera);

		for (const float Delay : {1.f, 11.f, 24.f})
		{
			FTimerHandle Shot;
			GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [this, Gun, Delay]() {
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PA250")
					/ FString::Printf(TEXT("artillery_review_%02d.png"), FMath::RoundToInt(Delay)));
				FScreenshotRequest::RequestScreenshot(Path, true, false);
				UE_LOG(LogTemp, Display, TEXT("PA250 gun render: shot %s loaded=%d crew=%d step=%s"),
					*Path, Gun->IsLoaded(), Gun->GetCrew(), *Gun->GetDrill().GetStepLabel());
			}), Delay, false);
		}
		FTimerHandle Overrun;
		GetWorldTimerManager().SetTimer(Overrun, FTimerDelegate::CreateWeakLambda(this, [this, Gun, Me]() {
			Gun->SetPlayerGunner(nullptr);
			Me->SetActorLocation(Gun->GetActorLocation() + FVector(0.f, 4000.f, 100.f));
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				if (*It != Me && Gun->PostOf(*It) != INDEX_NONE)
				{
					// Held where they are put, or they run straight back to their posts and the gun is never taken.
					It->SetActorLocation(Gun->GetActorLocation() + FVector(0.f, 3000.f, 100.f));
					if (auto* Crew = Cast<APA250BotController>(It->GetController())) Crew->bScriptedHold = true;
				}
			}
			const FName Enemy = Gun->GetFaction() == TEXT("continental") ? FName(TEXT("british")) : FName(TEXT("continental"));
			if (ARFCharacter* Captor = SpawnBotAt(Enemy, TEXT("line_infantry"), FTransform(FRotator::ZeroRotator, Gun->GetActorLocation() + FVector(150.f, 0.f, 100.f))))
				if (auto* Bot = Cast<APA250BotController>(Captor->GetController())) Bot->bScriptedHold = true;
		}), 25.f, false);
		FTimerHandle End;
		GetWorldTimerManager().SetTimer(End, FTimerDelegate::CreateWeakLambda(this, [this, Gun]() {
			UE_LOG(LogTemp, Display, TEXT("PA250 gun render: %s loaded=%d taken=%d (serve, cycle, smoke, neutral drill, overrun)"),
				Gun->IsLoaded() && Gun->IsTaken() ? TEXT("PASS") : TEXT("FAIL"), Gun->IsLoaded(), Gun->IsTaken());
			FPlatformMisc::RequestExit(false);
		}), 34.f, false);
	}), bRender ? 12.f : 1.f, false);
}
