#include "PA250BattleGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Campaign/PA250Autoplay.h"
#include "Campaign/PA250Subsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "MusketTuning.h"
#include "MusketWeapon.h"
#include "PA250BattleHUD.h"
#include "PA250BotController.h"
#include "PA250ControlPoint.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"
#include "TimerManager.h"

namespace
{
FName ToName(const std::string& S) { return FName(UTF8_TO_TCHAR(S.c_str())); }
std::string ToStd(const FName& N) { return std::string(TCHAR_TO_UTF8(*N.ToString())); }
}

APA250BattleGameMode::APA250BattleGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	RespawnDelay = 5.f;
}

void APA250BattleGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Both sides are already filled by SpawnBots. A player pawn here would be a 13th soldier.
	if (PA250Autoplay::IsAutoplay() || PA250Autoplay::SelftestName() == TEXT("battle"))
	{
		bStartPlayersAsSpectators = true;
	}
}

// ------------------------------------------------------------------ setup

void APA250BattleGameMode::EnsureInitialized()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	bSelftest = PA250Autoplay::SelftestName() == TEXT("battle");
	bAutoplayBots = PA250Autoplay::IsAutoplay() || bSelftest;

	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	FString Err;
	if (Sub && Sub->GetFlow() != EPA250Flow::Battle)
	{
		// Level opened directly (PIE / selftest): fabricate a real battle from the sim.
		bDebugBattle = true;
		if (!Sub->PrepareDebugBattle(Err))
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 battle: could not prepare a debug battle: %s"), *Err);
		}
	}
	if (Sub)
	{
		Setup = Sub->GetBattleSetup();
	}
	if (!Setup.valid)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 battle: no valid BattleSetup"));
	}

	int32 Cli = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250Bots="), Cli) && Cli >= 0) BotsPerSide = Cli;
	float F = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250BattleTime="), F) && F > 0.f) TimeLimitSeconds = F;

	PlayerFaction = ToName(Setup.player_faction);
	for (const auto& KV : Setup.sides)
	{
		const FName Faction = ToName(KV.first);
		for (const auto& T : KV.second.tickets)
		{
			InitialPool.FindOrAdd(Faction).Add(ToName(T.first), T.second);
		}
	}
	Pool = InitialPool;

	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (Tag == TEXT("british") || Tag == TEXT("continental"))
			{
				Starts.FindOrAdd(Tag).Add(*It);
			}
		}
	}
	if (TActorIterator<APA250ControlPoint> It(GetWorld()); It)
	{
		Objective = *It;
	}
	Tuning = LoadObject<UMusketTuning>(nullptr, TEXT("/Game/PA250/Data/DA_MusketTuning.DA_MusketTuning"));
}

void APA250BattleGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureInitialized();
	StartTime = GetWorld()->GetTimeSeconds();
	Banner = FString::Printf(TEXT("Battle of %s"), UTF8_TO_TCHAR(Setup.province_name.c_str()));

	if (bAutoplayBots)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, AutoplayTimeDilation);
	}
	for (const auto& KV : InitialPool)
	{
		SpawnBots(KV.Key, BotsPerSide);
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (bAutoplayBots)
		{
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				if (!It->IsDead() && It->FactionId == PlayerFaction)
				{
					PC->SetViewTarget(*It);
					break;
				}
			}
		}
		Hud = CreateWidget<UPA250BattleHUD>(PC, UPA250BattleHUD::StaticClass());
		if (Hud)
		{
			Hud->GameMode = this;
			Hud->AddToViewport();
		}
		if (!bAutoplayBots)
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
	EnsureVisibleLighting();
	// Clear the banner after a few seconds of game time.
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this]() { if (!bBattleOver) Banner.Reset(); }), 4.f, false);
	if (FParse::Param(FCommandLine::Get(), TEXT("PA250Shot")))
	{
		FTimerHandle Shot;
		GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [this]() {
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
			ARFCharacter* Nearest = nullptr;
			float NearestD = 0.f;
			int32 Brit = 0, Cont = 0;
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				if (It->FactionId == FName(TEXT("british"))) ++Brit;
				else if (It->FactionId == FName(TEXT("continental"))) ++Cont;
				if (!Me || *It == Me || It->FactionId == Me->FactionId || It->IsDead()) continue;
				const float D = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
				if (!Nearest || D < NearestD)
				{
					Nearest = *It;
					NearestD = D;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("PA250 roster british=%d continental=%d nearest_enemy_cm=%.0f"), Brit, Cont, Nearest ? NearestD : -1.f);
			if (!Nearest || !PC)
			{
				return;
			}
			const FVector Chest = Nearest->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			const FVector CamLoc = Chest + FVector(-260.f, -140.f, 50.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamLoc, (Chest - CamLoc).Rotation(), Params))
			{
				Cam->GetCameraComponent()->SetFieldOfView(55.f);
				PC->SetViewTarget(Cam);
			}
			FTimerHandle Snap;
			GetWorldTimerManager().SetTimer(Snap, FTimerDelegate::CreateWeakLambda(this, [this]() {
				const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("battle_enemy.png"));
				FScreenshotRequest::RequestScreenshot(Path, true, false);
			}), 0.4f, false);
		}), 2.f, false);
	}
}

void APA250BattleGameMode::EnsureVisibleLighting()
{
	// The graybox sun is a default 10-lux light. With eye adaptation hunting, that frame clips to black.
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(20.f);
			L->SetAtmosphereSunLight(true);
			It->SetActorRotation(FRotator(-50.f, 35.f, 0.f));
		}
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		if (USkyLightComponent* L = It->GetLightComponent())
		{
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(5.f);
			L->SetRealTimeCapture(false);
			L->RecaptureSky();
		}
	}
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		if (UExponentialHeightFogComponent* F = It->GetComponent())
		{
			F->SetFogDensity(0.f);
		}
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* P = PC->GetPawn())
		{
			if (USpringArmComponent* Arm = P->FindComponentByClass<USpringArmComponent>())
			{
				Arm->TargetArmLength = 280.f;
			}
			if (APlayerController* ViewPC = Cast<APlayerController>(P->GetController()))
			{
				FVector LookAt = P->GetActorLocation() + P->GetActorForwardVector() * 2000.f;
				if (TActorIterator<APA250ControlPoint> Obj(GetWorld()); Obj)
				{
					LookAt = Obj->GetActorLocation();
				}
				FRotator Face = (LookAt - P->GetActorLocation()).Rotation();
				Face.Pitch = -8.f;
				Face.Roll = 0.f;
				ViewPC->SetControlRotation(Face);
			}
			if (UCameraComponent* Cam = P->FindComponentByClass<UCameraComponent>())
			{
				Cam->PostProcessSettings.bOverride_AutoExposureMethod = true;
				Cam->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
				Cam->PostProcessSettings.bOverride_AutoExposureBias = true;
				Cam->PostProcessSettings.AutoExposureBias = 7.f;
				Cam->PostProcessBlendWeight = 1.f;
			}
		}
	}
}

AActor* APA250BattleGameMode::PickStart(FName Faction) const
{
	const TArray<TWeakObjectPtr<AActor>>* List = Starts.Find(Faction);
	if (!List || List->Num() == 0)
	{
		return nullptr;
	}
	for (int32 Try = 0; Try < 8; ++Try)
	{
		AActor* A = (*List)[FMath::RandRange(0, List->Num() - 1)].Get();
		if (A) return A;
	}
	return nullptr;
}

FName APA250BattleGameMode::ChooseClassFor(FName Faction, FName Preferred) const
{
	const TMap<FName, int32>* P = Pool.Find(Faction);
	if (!P)
	{
		return Preferred;
	}
	if (const int32* N = P->Find(Preferred))
	{
		if (*N > 0) return Preferred;
	}
	// Deterministic fallback: lexicographically first class with tickets left.
	TArray<FName> Names;
	P->GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	for (const FName& C : Names)
	{
		if (P->FindChecked(C) > 0) return C;
	}
	return Preferred;
}

void APA250BattleGameMode::ConfigureSoldier(ARFCharacter* Soldier, FName Faction, FName UnitClass)
{
	if (!Soldier)
	{
		return;
	}
	Soldier->SetTeam(Faction, UnitClass);
	// Player starts were saved with pitch in the yaw slot, so the British column is upside down
	// and a hundred metres down the east road, out of the player's view.
	{
		const FVector Green = Objective.IsValid() ? Objective->GetActorLocation() : FVector(-500.f, -1000.f, 120.f);
		FVector FromGreen = Soldier->GetActorLocation() - Green;
		FromGreen.Z = 0.f;
		if (!Soldier->IsPlayerControlled() && FromGreen.Size() > 6000.f)
		{
			Soldier->SetActorLocation(Green + FromGreen.GetSafeNormal() * 2800.f + FVector(0.f, 0.f, 40.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (Soldier->GetActorUpVector().Z < 0.5f)
		{
			FVector Face = Green - Soldier->GetActorLocation();
			Face.Z = 0.f;
			Soldier->SetActorRotation(Face.IsNearlyZero() ? FRotator(0.f, 180.f, 0.f) : Face.Rotation());
		}
	}
	if (Soldier->HealthComponent)
	{
		Soldier->HealthComponent->MaxHealth = UnitClass == TEXT("militia") ? 80.f : 100.f;
		Soldier->HealthComponent->CurrentHealth = Soldier->HealthComponent->MaxHealth;
	}
	if (Soldier->Musket && Tuning)
	{
		Soldier->Musket->Tuning = Tuning;
	}
	if (APA250BotController* Bot = Cast<APA250BotController>(Soldier->GetController()))
	{
		Bot->AimErrorDegrees = UnitClass == TEXT("militia") ? 3.f : 2.f;
		Bot->SetObjective(Objective.Get());
	}
	Soldier->GetCharacterMovement()->MaxWalkSpeed = 380.f;  // bots march; the player keeps the RFCharacter defaults
	if (Soldier->IsPlayerControlled())
	{
		Soldier->GetCharacterMovement()->MaxWalkSpeed = Soldier->WalkSpeed;
	}
}

ARFCharacter* APA250BattleGameMode::SpawnBot(FName Faction, FName UnitClass)
{
	AActor* Start = PickStart(Faction);
	FTransform T = Start ? Start->GetActorTransform() : FTransform::Identity;
	T.AddToTranslation(FVector(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f));
	ARFCharacter* C = GetWorld()->SpawnActorDeferred<ARFCharacter>(
		ARFCharacter::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!C)
	{
		return nullptr;
	}
	C->AIControllerClass = APA250BotController::StaticClass();
	C->FinishSpawning(T);
	C->SpawnDefaultController();
	ConfigureSoldier(C, Faction, UnitClass);
	if (AController* Ctl = C->GetController())
	{
		ControllerFaction.Add(Ctl, Faction);
		ControllerClass.Add(Ctl, UnitClass);
	}
	return C;
}

void APA250BattleGameMode::SpawnBots(FName Faction, int32 Count)
{
	// Weighted by the initial ticket mix, deterministic order.
	const TMap<FName, int32>* P = InitialPool.Find(Faction);
	if (!P || Count <= 0)
	{
		return;
	}
	TArray<FName> Names;
	P->GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	int32 Total = 0;
	for (const FName& N : Names) Total += P->FindChecked(N);
	if (Total <= 0) return;
	int32 Spawned = 0;
	for (int32 i = 0; i < Names.Num() && Spawned < Count; ++i)
	{
		int32 N = (i == Names.Num() - 1) ? Count - Spawned : FMath::RoundToInt((float)Count * P->FindChecked(Names[i]) / Total);
		N = FMath::Min(N, Count - Spawned);
		for (int32 k = 0; k < N; ++k)
		{
			SpawnBot(Faction, Names[i]);
			++Spawned;
		}
	}
}

// ------------------------------------------------------------------ players

namespace
{
/** Continental starts sit inside Buckman Tavern. A blocked spawn leaves the player with no pawn and a black view. */
FVector ClearSpawnLocation(UWorld* World, FVector Origin)
{
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(35.f, 88.f);
	auto Blocked = [World, &Capsule](const FVector& Loc) {
		return World->OverlapBlockingTestByChannel(Loc, FQuat::Identity, ECC_Pawn, Capsule);
	};
	Origin.Z = FMath::Max(Origin.Z, 200.f);
	if (!Blocked(Origin))
	{
		return Origin;
	}
	for (int32 Ring = 1; Ring <= 10; ++Ring)
	{
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const float Ang = Step * (PI / 4.f);
			const FVector Try = Origin + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * (Ring * 200.f);
			if (!Blocked(Try))
			{
				return Try;
			}
		}
	}
	return Origin + FVector(0.f, -1600.f, 0.f);
}
}

APawn* APA250BattleGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !GetWorld())
	{
		return nullptr;
	}
	FTransform T = SpawnTransform;
	T.SetLocation(ClearSpawnLocation(GetWorld(), SpawnTransform.GetLocation()));
	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	return GetWorld()->SpawnActor<APawn>(PawnClass, T, Params);
}

AActor* APA250BattleGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureInitialized();
	if (AActor* S = PickStart(PlayerFaction))
	{
		return S;
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

void APA250BattleGameMode::RestartPlayer(AController* NewPlayer)
{
	EnsureInitialized();
	if (bAutoplayBots && NewPlayer && NewPlayer->IsA<APlayerController>())
	{
		// SpawnBots already placed BotsPerSide on this side. Spectate one; do not add another.
		if (APlayerController* PC = Cast<APlayerController>(NewPlayer))
		{
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				if (!It->IsDead() && It->FactionId == PlayerFaction)
				{
					PC->SetViewTarget(*It);
					break;
				}
			}
		}
		return;
	}
	Super::RestartPlayer(NewPlayer);
	if (ARFCharacter* C = NewPlayer ? Cast<ARFCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		const FName Cls = ChooseClassFor(PlayerFaction, TEXT("line_infantry"));
		ControllerFaction.Add(NewPlayer, PlayerFaction);
		ControllerClass.Add(NewPlayer, Cls);
		ConfigureSoldier(C, PlayerFaction, Cls);
	}
}

AActor* APA250BattleGameMode::ChooseRespawnPoint(AController* Controller)
{
	const FName* F = ControllerFaction.Find(Controller);
	if (F)
	{
		if (AActor* S = PickStart(*F)) return S;
	}
	return Super::ChooseRespawnPoint(Controller);
}

void APA250BattleGameMode::OnControllerRespawned(AController* Controller, APawn* NewPawn)
{
	ARFCharacter* C = Cast<ARFCharacter>(NewPawn);
	const FName* F = ControllerFaction.Find(Controller);
	if (!C || !F)
	{
		return;
	}
	const FName* Prev = ControllerClass.Find(Controller);
	const FName Cls = ChooseClassFor(*F, Prev ? *Prev : FName(TEXT("line_infantry")));
	ControllerClass.Add(Controller, Cls);
	ConfigureSoldier(C, *F, Cls);
}

// ------------------------------------------------------------------ tickets / end conditions

int32 APA250BattleGameMode::PoolTotal(FName Faction) const
{
	int32 N = 0;
	if (const TMap<FName, int32>* P = Pool.Find(Faction))
	{
		for (const auto& KV : *P) N += FMath::Max(0, KV.Value);
	}
	return N;
}

int32 APA250BattleGameMode::TicketsRemaining(FName Faction) const { return PoolTotal(Faction); }

void APA250BattleGameMode::SpendTicket(FName Faction, FName UnitClass)
{
	TMap<FName, int32>& P = Pool.FindOrAdd(Faction);
	const FName Charged = ChooseClassFor(Faction, UnitClass);
	int32& N = P.FindOrAdd(Charged);
	N = FMath::Max(0, N - 1);
}

void APA250BattleGameMode::OnCharacterDied(ARFCharacter* Victim, AController* VictimController, AController* Killer)
{
	if (bBattleOver || !Victim)
	{
		return;
	}
	const FName F = Victim->FactionId;
	SpendTicket(F, Victim->UnitClass);
	if (PoolTotal(F) <= 0)
	{
		EndBattle(OtherFaction(F), TEXT("tickets_exhausted"));
		return;
	}
	if (!VictimController)
	{
		return;
	}
	if (VictimController->IsA<APlayerController>())
	{
		ScheduleRespawn(VictimController, RespawnDelay);
		return;
	}
	// Bots are replaced by a fresh soldier + controller (re-possessing an AIController across
	// pawns leaves stale path-following bindings).
	TWeakObjectPtr<AController> OldCtl = VictimController;
	const FName Cls = Victim->UnitClass;
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, OldCtl, F, Cls]() {
		if (bBattleOver) return;
		if (AController* C = OldCtl.Get())
		{
			ControllerFaction.Remove(C);
			ControllerClass.Remove(C);
			C->Destroy();
		}
		SpawnBot(F, ChooseClassFor(F, Cls));
	}), RespawnDelay, false);
}

FName APA250BattleGameMode::GetObjectiveHolder() const { return Objective.IsValid() ? Objective->GetHolder() : NAME_None; }
float APA250BattleGameMode::GetObjectiveHold() const { return Objective.IsValid() ? Objective->GetHoldSeconds() : 0.f; }
float APA250BattleGameMode::TimeRemaining() const
{
	return FMath::Max(0.f, TimeLimitSeconds - (GetWorld()->GetTimeSeconds() - StartTime));
}

void APA250BattleGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBattleOver || !bInitialized)
	{
		return;
	}
	if (Objective.IsValid() && !Objective->GetHolder().IsNone() && Objective->GetHoldSeconds() >= ObjectiveHoldSeconds)
	{
		EndBattle(Objective->GetHolder(), TEXT("objective_held"));
		return;
	}
	if (TimeRemaining() <= 0.f)
	{
		const int32 B = PoolTotal(TEXT("british"));
		const int32 C = PoolTotal(TEXT("continental"));
		FName Winner;
		if (B != C)
		{
			// Compare remaining ticket share against the starting pools.
			auto Total = [this](FName F) { int32 N = 0; if (const auto* P = InitialPool.Find(F)) for (const auto& KV : *P) N += KV.Value; return FMath::Max(1, N); };
			const float RB = (float)B / Total(TEXT("british"));
			const float RC = (float)C / Total(TEXT("continental"));
			Winner = RB >= RC ? FName(TEXT("british")) : FName(TEXT("continental"));
		}
		else if (!GetObjectiveHolder().IsNone())
		{
			Winner = GetObjectiveHolder();
		}
		else
		{
			Winner = ToName(Setup.defender_faction);  // a drawn battle favours the defender
		}
		EndBattle(Winner, TEXT("time_limit"));
	}
}

void APA250BattleGameMode::EndBattle(FName Winner, const FString& Reason)
{
	if (bBattleOver)
	{
		return;
	}
	bBattleOver = true;
	EndReason = Reason;

	Result = pa250::BattleResult{};
	Result.province_id = Setup.province_id;
	Result.winner = ToStd(Winner);
	Result.objective_holder = GetObjectiveHolder().IsNone() ? std::string() : ToStd(GetObjectiveHolder());
	for (const auto& FK : InitialPool)
	{
		for (const auto& CK : FK.Value)
		{
			const int32 Left = Pool.FindChecked(FK.Key).FindRef(CK.Key);
			const int32 Lost = FMath::Max(0, CK.Value - Left);
			if (Lost > 0)
			{
				Result.losses[ToStd(FK.Key)][ToStd(CK.Key)] = Lost;
			}
		}
	}
	const std::string Loser = Winner == TEXT("british") ? "continental" : "british";
	auto It = Setup.sides.find(Loser);
	if (It != Setup.sides.end())
	{
		Result.retreat_province = It->second.retreat_province;
	}
	Banner = FString::Printf(TEXT("%s victory (%s)"), *Winner.ToString().ToUpper(), *Reason);
	UE_LOG(LogTemp, Display, TEXT("PA250 battle over: %s"), *Banner);
	WriteBattleFile(Reason);

	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250BattleGameMode::FinishAndReturn), FMath::Max(0.1f, EndDelaySeconds), false);
}

void APA250BattleGameMode::WriteBattleFile(const FString& Reason) const
{
	const FString Json = FString::Printf(TEXT("{\"reason\":\"%s\",\"elapsed_game_seconds\":%.1f,\"bots_per_side\":%d,\"setup\":%s,\"result\":%s}\n"),
		*Reason, GetWorld()->GetTimeSeconds() - StartTime, BotsPerSide,
		UTF8_TO_TCHAR(pa250::setup_to_json(Setup).c_str()), UTF8_TO_TCHAR(pa250::result_to_json(Result).c_str()));
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("battle_result.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void APA250BattleGameMode::FinishAndReturn()
{
	UPA250Subsystem* Sub = UPA250Subsystem::Get(this);
	if (!Sub)
	{
		return;
	}
	FString Err;
	const pa250::CampaignState Before = Sub->GetState();  // for invariant checks below
	const bool bApplied = Sub->ApplyBattleResult(Result, Err);

	if (bSelftest)
	{
		using namespace PA250Autoplay;
		Check(Sub, Setup.valid, TEXT("battle setup came from the sim"));
		Check(Sub, Result.winner == "british" || Result.winner == "continental", TEXT("winner is set"));
		bool bLossesOk = true;
		for (const auto& FK : Result.losses)
		{
			const FName F = ToName(FK.first);
			for (const auto& CK : FK.second)
			{
				const int32* Init = InitialPool.FindRef(F).Find(ToName(CK.first));
				bLossesOk &= Init && CK.second <= *Init && CK.second >= 0;
			}
		}
		Check(Sub, bLossesOk, TEXT("losses never exceed the ticket pools"));
		const auto Prov = Before.provinces.find(Setup.province_id);
		const bool bAdj = Prov != Before.provinces.end() &&
			(Result.retreat_province.empty() || std::find(Prov->second.adjacent.begin(), Prov->second.adjacent.end(), Result.retreat_province) != Prov->second.adjacent.end());
		Check(Sub, bAdj, TEXT("retreat province is adjacent to the battle province"));
		Check(Sub, bApplied && Sub->GetFlow() == EPA250Flow::Campaign && !Sub->IsBattlePending(), TEXT("BattleResult applied; flow back to campaign"));
		Check(Sub, Result.objective_holder.empty() || Sub->GetState().provinces.at(Setup.province_id).owner == (Result.winner == Result.objective_holder ? Result.winner : Before.provinces.at(Setup.province_id).owner), TEXT("province ownership follows the objective holder"));
		Finish(Sub, TEXT("selftest_battle"));
		return;
	}
	if (bDebugBattle)
	{
		return;  // opened directly in PIE: stay on the Green
	}
	if (PA250Autoplay::IsAutoplay())
	{
		PA250Autoplay::Check(Sub, bApplied, TEXT("autoplay battle: BattleResult applied"));
		Sub->AutoplayStage = TEXT("battle_done");
	}
	Sub->TravelToFlowLevel(this);
}
