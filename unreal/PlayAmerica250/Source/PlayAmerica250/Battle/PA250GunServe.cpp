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
		if (!(Loads.Num() == 3 && Loads[0] == TEXT("canister") && Loads[1] == TEXT("shell") && Loads[2] == TEXT("round shot")))
		{
			Fail(TEXT("load cycle was not canister, shell, round shot"));
			return;
		}
		PlayerCycleGunShot(Me, 1);
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
					It->SetActorLocation(Gun->GetActorLocation() + FVector(0.f, 3000.f, 100.f));
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
