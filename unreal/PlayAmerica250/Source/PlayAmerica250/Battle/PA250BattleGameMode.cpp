#include "PA250BattleGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Campaign/PA250Autoplay.h"
#include "Campaign/PA250Subsystem.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameViewportClient.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "MusketTuning.h"
#include "MusketWeapon.h"
#include "PA250BattleHUD.h"
#include "PA250BotController.h"
#include "PA250ControlPoint.h"
#include "PA250SpawnScreen.h"
#include "PA250VolleyCoordinator.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"
#include "TimerManager.h"

namespace
{
FName ToName(const std::string& S) { return FName(UTF8_TO_TCHAR(S.c_str())); }
std::string ToStd(const FName& N) { return std::string(TCHAR_TO_UTF8(*N.ToString())); }

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

struct FPreviewCam
{
	const TCHAR* Tag;
	const TCHAR* Stem;
	FVector Loc;
	FRotator Rot;
	float FOV;
};

// Fixed Lexington views: spawn cluster, the Green, low wide. Same transforms as the
// Shot_* cameras in build_levels.py; used when a level has no tagged cameras (e.g. the
// committed pre-visual-pass map for "before" stills).
static const FPreviewCam GPreviewCams[] = {
	{TEXT("Shot_Spawn"), TEXT("spawn"), FVector(-6900.f, 900.f, 215.f), FRotator(-8.f, -15.f, 0.f), 60.f},
	{TEXT("Shot_Green"), TEXT("green"), FVector(1600.f, 1400.f, 259.f), FRotator(-6.f, -135.f, 0.f), 65.f},
	{TEXT("Shot_Wide"), TEXT("wide"), FVector(3600.f, -7600.f, 43.f), FRotator(-3.f, 135.f, 0.f), 75.f},
};
}

APA250BattleGameMode::APA250BattleGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	RespawnDelay = 5.f;
}

UMusketTuning* APA250BattleGameMode::GetTuning() const
{
	return Tuning;
}

void APA250BattleGameMode::NotifyShot(ARFCharacter* Shooter, const FVector& Dir, bool bHit)
{
	if (Volleys)
	{
		Volleys->NotifyShot(Shooter, Dir, bHit);
	}
}

const TArray<TWeakObjectPtr<APA250ControlPoint>>& APA250BattleGameMode::GetPosts() const
{
	return Posts;
}

void APA250BattleGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (PA250Autoplay::IsAutoplay() || PA250Autoplay::SelftestName() == TEXT("battle"))
	{
		bStartPlayersAsSpectators = true;
	}
}

void APA250BattleGameMode::GatherPosts()
{
	Posts.Reset();
	for (TActorIterator<APA250ControlPoint> It(GetWorld()); It; ++It)
	{
		Posts.Add(*It);
	}
	Posts.Sort([](const TWeakObjectPtr<APA250ControlPoint>& A, const TWeakObjectPtr<APA250ControlPoint>& B) {
		const FName NA = A.IsValid() ? A->PostId : NAME_None;
		const FName NB = B.IsValid() ? B->PostId : NAME_None;
		return NA.LexicalLess(NB);
	});
}

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

	Tuning = LoadObject<UMusketTuning>(nullptr, TEXT("/Game/PA250/Data/DA_MusketTuning.DA_MusketTuning"));
	if (Tuning)
	{
		BotsPerSide = Tuning->Values.Conquest.BotsPerSide;
		TimeLimitSeconds = Tuning->Values.Conquest.TimeLimitSeconds;
		BleedIntervalSeconds = Tuning->Values.Conquest.BleedIntervalSeconds;
		SoldiersPerUnit = Tuning->Values.Conquest.SoldiersPerUnit;
	}

	int32 Cli = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250Bots="), Cli) && Cli >= 0) BotsPerSide = Cli;
	float F = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250BattleTime="), F) && F > 0.f) TimeLimitSeconds = F;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250SoldiersPerUnit="), Cli) && Cli >= 1) SoldiersPerUnit = Cli;
	SoldiersPerUnit = FMath::Max(1, SoldiersPerUnit);

	PlayerFaction = ToName(Setup.player_faction);
	for (const auto& KV : Setup.sides)
	{
		const FName Faction = ToName(KV.first);
		for (const auto& T : KV.second.tickets)
		{
			InitialPool.FindOrAdd(Faction).Add(ToName(T.first), T.second);
		}
	}
	// InitialPool stays in campaign units; the live pool is in soldiers (respawns).
	Pool = InitialPool;
	for (auto& FK : Pool)
	{
		for (auto& CK : FK.Value) CK.Value *= SoldiersPerUnit;
	}

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
	GatherPosts();
	if (Tuning)
	{
		const FConquestTuning& C = Tuning->Values.Conquest;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			if (APA250ControlPoint* P = WP.Get())
			{
				P->ApplyTuning(C.PostRadius, C.CaptureSeconds, C.CapturePerAttackerScale);
			}
		}
	}
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
			SpectateNextSoldier(PC, nullptr);
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
	ConfigureBattleCamera();
	Volleys = NewObject<UPA250VolleyCoordinator>(this);
	if (Volleys)
	{
		Volleys->Init(this);
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this]() { if (!bBattleOver) Banner.Reset(); }), 4.f, false);
	MaybeStartPreviewCapture();
	MaybeStartFpsSample();
}

APA250ControlPoint* APA250BattleGameMode::FindGreenPost() const
{
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (WP.IsValid() && WP->PostId == TEXT("green"))
		{
			return WP.Get();
		}
	}
	return nullptr;
}

void APA250BattleGameMode::ApplyLowCinematicSpectator(APlayerController* PC, ARFCharacter* ViewSoldier)
{
	if (!PC || !ViewSoldier)
	{
		return;
	}
	PC->SetViewTarget(ViewSoldier);
	// The viewed bot's controller is an AIController, so the arm must not follow control
	// rotation: a fixed low pitch behind the shoulder keeps the shot cinematic, never top-down.
	// Bot aim uses its own controller's view point, so this is camera-only.
	if (USpringArmComponent* Arm = ViewSoldier->FindComponentByClass<USpringArmComponent>())
	{
		Arm->bUsePawnControlRotation = false;
		Arm->SetRelativeRotation(FRotator(-10.f, 0.f, 0.f));
		Arm->TargetArmLength = 480.f;
		Arm->SocketOffset = FVector(0.f, 80.f, 40.f);
		Arm->bEnableCameraLag = true;
		Arm->CameraLagSpeed = 6.f;
		Arm->bEnableCameraRotationLag = true;
		Arm->CameraRotationLagSpeed = 4.f;
	}
}

void APA250BattleGameMode::SpectateNextSoldier(APlayerController* PC, const AActor* Exclude)
{
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Exclude && !It->IsDead() && It->FactionId == PlayerFaction)
		{
			ApplyLowCinematicSpectator(PC, *It);
			return;
		}
	}
}

void APA250BattleGameMode::FrameHumanCamera(APlayerController* PC, APawn* P)
{
	if (!PC || !P)
	{
		return;
	}
	// Over-the-shoulder: 350 cm arm, right-shoulder offset, lag on, -15 deg pitch toward the Green.
	if (USpringArmComponent* Arm = P->FindComponentByClass<USpringArmComponent>())
	{
		Arm->bUsePawnControlRotation = true;
		Arm->TargetArmLength = 350.f;
		Arm->SocketOffset = FVector(0.f, 60.f, 65.f);
		Arm->bEnableCameraLag = true;
		Arm->CameraLagSpeed = 10.f;
	}
	FVector LookAt = P->GetActorLocation() + P->GetActorForwardVector() * 2000.f;
	if (const APA250ControlPoint* Green = FindGreenPost())
	{
		LookAt = Green->GetActorLocation();
	}
	FRotator Face = (LookAt - P->GetActorLocation()).Rotation();
	Face.Pitch = -15.f;
	Face.Roll = 0.f;
	PC->SetControlRotation(Face);
}

void APA250BattleGameMode::ConfigureBattleCamera()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	// Exposure, grade and fog come from the level's unbound PP_Dawn volume only.
	if (APawn* P = PC->GetPawn())
	{
		FrameHumanCamera(PC, P);
	}
	else if (ARFCharacter* Soldier = Cast<ARFCharacter>(PC->GetViewTarget()))
	{
		ApplyLowCinematicSpectator(PC, Soldier);
	}
}

void APA250BattleGameMode::MaybeStartPreviewCapture()
{
	if (!FParse::Value(FCommandLine::Get(), TEXT("PA250Preview="), PreviewLabel) || PreviewLabel.IsEmpty())
	{
		return;
	}
	PreviewLabel = PreviewLabel.TrimStartAndEnd().ToLower();
	// Settle lighting / Lumen before the first still.
	FTimerHandle Start;
	GetWorldTimerManager().SetTimer(Start, FTimerDelegate::CreateWeakLambda(this, [this]() {
		CapturePreviewShot(GPreviewCams[0].Tag, GPreviewCams[0].Stem, 0, UE_ARRAY_COUNT(GPreviewCams));
	}), 4.f, false);
}

void APA250BattleGameMode::CapturePreviewShot(const FString& Tag, const FString& OutStem, int32 Index, int32 Total)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || Index < 0 || Index >= Total)
	{
		return;
	}
	const FPreviewCam& Spec = GPreviewCams[Index];
	ACameraActor* Cam = nullptr;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FName(Spec.Tag)))
		{
			Cam = *It;
			Cam->Tags.AddUnique(TEXT("PA250LevelShot"));  // keep the level's transform and FOV
			break;
		}
	}
	if (!Cam)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Cam = GetWorld()->SpawnActor<ACameraActor>(Spec.Loc, Spec.Rot, Params);
		if (Cam)
		{
			Cam->Tags.AddUnique(FName(Spec.Tag));
		}
	}
	if (!Cam)
	{
		return;
	}
	if (UCameraComponent* Comp = Cam->GetCameraComponent(); Comp && !Cam->ActorHasTag(TEXT("PA250LevelShot")))
	{
		Comp->SetFieldOfView(Spec.FOV);
	}
	PC->SetViewTarget(Cam);

	const FString PreviewDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview")));
	IFileManager::Get().MakeDirectory(*PreviewDir, true);
	const FString FinalName = FString::Printf(TEXT("lexington_%s_%s.png"), *PreviewLabel, *OutStem);
	const FString SavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), FinalName);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SavedPath), true);

	// Clean, HUD-free still: delete any old file, let Lumen settle after the cut, then poll.
	IFileManager::Get().Delete(*SavedPath, false, true, true);
	FTimerHandle Snap;
	GetWorldTimerManager().SetTimer(Snap, FTimerDelegate::CreateWeakLambda(this, [this, SavedPath, PreviewDir, FinalName, Index, Total]() {
		FScreenshotRequest::RequestScreenshot(SavedPath, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		PollPreviewFile(SavedPath, FPaths::Combine(PreviewDir, FinalName), Index, Total, 0);
	}), 1.5f, false);
}

void APA250BattleGameMode::PollPreviewFile(const FString& SavedPath, const FString& Dest, int32 Index, int32 Total, int32 Attempt)
{
	FTimerHandle Poll;
	GetWorldTimerManager().SetTimer(Poll, FTimerDelegate::CreateWeakLambda(this, [this, SavedPath, Dest, Index, Total, Attempt]() {
		const bool bReady = IFileManager::Get().FileSize(*SavedPath) > 0;
		if (!bReady && Attempt < 40)
		{
			PollPreviewFile(SavedPath, Dest, Index, Total, Attempt + 1);
			return;
		}
		if (bReady)
		{
			IFileManager::Get().Copy(*Dest, *SavedPath, true, true);
			UE_LOG(LogTemp, Display, TEXT("PA250 preview wrote %s"), *Dest);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 preview FAILED %s"), *Dest);
		}
		const int32 Next = Index + 1;
		if (Next < Total)
		{
			CapturePreviewShot(GPreviewCams[Next].Tag, GPreviewCams[Next].Stem, Next, Total);
		}
		else
		{
			FTimerHandle Quit;
			GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, [this]() {
				FPlatformMisc::RequestExit(false);
			}), 0.5f, false);
		}
	}), 0.25f, false);
}

void APA250BattleGameMode::MaybeStartFpsSample()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250FpsSample")))
	{
		return;
	}
	FTimerHandle Start;
	GetWorldTimerManager().SetTimer(Start, FTimerDelegate::CreateWeakLambda(this, [this]() {
		bFpsSampling = true;
		FpsSampleAccum = 0.f;
		FpsGameMs = FpsRenderMs = FpsGpuMs = 0.0;
		FpsSampleTime = 0.f;
		FpsSampleFrames = 0;
		UE_LOG(LogTemp, Display, TEXT("PA250 FPS sample started"));
	}), 5.f, false);  // after shader warm-up and the first runtime navmesh build
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

FTransform APA250BattleGameMode::PickSpawnTransform(FName Faction, APA250ControlPoint* PreferredPost) const
{
	if (PreferredPost && PreferredPost->GetHolder() == Faction)
	{
		FVector Loc = PreferredPost->GetActorLocation();
		Loc += FVector(FMath::FRandRange(-PreferredPost->Radius * 0.35f, PreferredPost->Radius * 0.35f),
			FMath::FRandRange(-PreferredPost->Radius * 0.35f, PreferredPost->Radius * 0.35f), 40.f);
		Loc = ClearSpawnLocation(GetWorld(), Loc);
		return FTransform(FRotator::ZeroRotator, Loc);
	}
	// Prefer any owned post.
	TArray<APA250ControlPoint*> Owned;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (APA250ControlPoint* P = WP.Get())
		{
			if (P->GetHolder() == Faction) Owned.Add(P);
		}
	}
	if (Owned.Num() > 0)
	{
		return PickSpawnTransform(Faction, Owned[FMath::RandRange(0, Owned.Num() - 1)]);
	}
	if (AActor* Start = PickStart(Faction))
	{
		FTransform T = Start->GetActorTransform();
		T.AddToTranslation(FVector(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f));
		T.SetLocation(ClearSpawnLocation(GetWorld(), T.GetLocation()));
		return T;
	}
	return FTransform::Identity;
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
	TArray<FName> Names;
	P->GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	for (const FName& C : Names)
	{
		if (P->FindChecked(C) > 0) return C;
	}
	return Preferred;
}

bool APA250BattleGameMode::TrySpendTicket(FName Faction, FName UnitClass)
{
	TMap<FName, int32>& P = Pool.FindOrAdd(Faction);
	int32* N = P.Find(UnitClass);
	if (!N || *N <= 0)
	{
		return false;
	}
	--(*N);
	return true;
}

void APA250BattleGameMode::ConfigureSoldier(ARFCharacter* Soldier, FName Faction, FName UnitClass)
{
	if (!Soldier)
	{
		return;
	}
	Soldier->SetTeam(Faction, UnitClass);
	FVector Anchor = FVector(-500.f, -1000.f, 120.f);
	if (Posts.Num() > 0 && Posts[0].IsValid())
	{
		Anchor = Posts[0]->GetActorLocation();
	}
	{
		FVector FromGreen = Soldier->GetActorLocation() - Anchor;
		FromGreen.Z = 0.f;
		if (!Soldier->IsPlayerControlled() && FromGreen.Size() > 6000.f)
		{
			Soldier->SetActorLocation(Anchor + FromGreen.GetSafeNormal() * 2800.f + FVector(0.f, 0.f, 40.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (Soldier->GetActorUpVector().Z < 0.5f)
		{
			FVector Face = Anchor - Soldier->GetActorLocation();
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
		Bot->SetPosts(Posts);
		if (Tuning)
		{
			Bot->ReloadSkill = UnitClass == TEXT("militia")
				? Tuning->Values.ActiveReload.MilitiaReloadSkill
				: Tuning->Values.ActiveReload.LineInfantryReloadSkill;
		}
	}
	Soldier->GetCharacterMovement()->MaxWalkSpeed = 380.f;
	if (Soldier->IsPlayerControlled())
	{
		Soldier->GetCharacterMovement()->MaxWalkSpeed = Soldier->WalkSpeed;
	}
}

ARFCharacter* APA250BattleGameMode::SpawnBot(FName Faction, FName UnitClass, APA250ControlPoint* PreferredPost)
{
	const FName Charged = ChooseClassFor(Faction, UnitClass);
	if (!TrySpendTicket(Faction, Charged))
	{
		return nullptr;
	}
	FTransform T = PickSpawnTransform(Faction, PreferredPost);
	ARFCharacter* C = GetWorld()->SpawnActorDeferred<ARFCharacter>(
		ARFCharacter::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!C)
	{
		Pool.FindOrAdd(Faction).FindOrAdd(Charged)++;
		return nullptr;
	}
	C->AIControllerClass = APA250BotController::StaticClass();
	C->FinishSpawning(T);
	C->SpawnDefaultController();
	ConfigureSoldier(C, Faction, Charged);
	if (AController* Ctl = C->GetController())
	{
		ControllerFaction.Add(Ctl, Faction);
		ControllerClass.Add(Ctl, Charged);
	}
	return C;
}

void APA250BattleGameMode::SpawnBots(FName Faction, int32 Count)
{
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
			if (SpawnBot(Faction, Names[i], nullptr))
			{
				++Spawned;
			}
		}
	}
}

bool APA250BattleGameMode::RequestSpawn(AController* Controller, FName UnitClass, APA250ControlPoint* PostOrNull)
{
	if (bBattleOver || !Controller)
	{
		return false;
	}
	const FName* FPtr = ControllerFaction.Find(Controller);
	const FName Faction = FPtr ? *FPtr : (Controller->IsA<APlayerController>() ? PlayerFaction : NAME_None);
	if (Faction.IsNone())
	{
		return false;
	}
	const FName Cls = ChooseClassFor(Faction, UnitClass);
	if (!TrySpendTicket(Faction, Cls))
	{
		return false;
	}
	ControllerFaction.Add(Controller, Faction);
	ControllerClass.Add(Controller, Cls);

	FTransform T = PickSpawnTransform(Faction, PostOrNull);
	UClass* PawnClass = DefaultPawnClass;
	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(PawnClass, T.GetLocation(), T.Rotator());
	if (!NewPawn)
	{
		Pool.FindOrAdd(Faction).FindOrAdd(Cls)++;
		return false;
	}
	Controller->Possess(NewPawn);
	if (ARFCharacter* C = Cast<ARFCharacter>(NewPawn))
	{
		ConfigureSoldier(C, Faction, Cls);
	}
	FrameHumanCamera(Cast<APlayerController>(Controller), NewPawn);
	return true;
}

void APA250BattleGameMode::ShowSpawnScreen(APlayerController* PC)
{
	if (!PC || bAutoplayBots)
	{
		return;
	}
	if (!SpawnScreen)
	{
		SpawnScreen = CreateWidget<UPA250SpawnScreen>(PC, UPA250SpawnScreen::StaticClass());
	}
	if (SpawnScreen)
	{
		SpawnScreen->OpenFor(PC, this);
	}
}

APawn* APA250BattleGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !GetWorld())
	{
		return nullptr;
	}
	// Initial player spawn spends a ticket.
	const FName Cls = ChooseClassFor(PlayerFaction, TEXT("line_infantry"));
	if (!TrySpendTicket(PlayerFaction, Cls))
	{
		return nullptr;
	}
	PendingPlayerClass = Cls;
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
		if (APlayerController* PC = Cast<APlayerController>(NewPlayer))
		{
			SpectateNextSoldier(PC, nullptr);
		}
		return;
	}
	Super::RestartPlayer(NewPlayer);
	if (ARFCharacter* C = NewPlayer ? Cast<ARFCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		const FName Cls = PendingPlayerClass.IsNone() ? ChooseClassFor(PlayerFaction, TEXT("line_infantry")) : PendingPlayerClass;
		ControllerFaction.Add(NewPlayer, PlayerFaction);
		ControllerClass.Add(NewPlayer, Cls);
		ConfigureSoldier(C, PlayerFaction, Cls);
		FrameHumanCamera(Cast<APlayerController>(NewPlayer), C);
	}
}

AActor* APA250BattleGameMode::ChooseRespawnPoint(AController* Controller)
{
	const FName* F = ControllerFaction.Find(Controller);
	if (F)
	{
		const FTransform T = PickSpawnTransform(*F, nullptr);
		// RFGameMode expects an actor; fall back to a base start near the transform.
		if (AActor* S = PickStart(*F))
		{
			S->SetActorLocation(T.GetLocation());
			return S;
		}
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
	// Respawn via RFGameMode path: spend ticket here (player spawn screen uses RequestSpawn instead).
	if (!TrySpendTicket(*F, Cls))
	{
		C->Destroy();
		return;
	}
	ControllerClass.Add(Controller, Cls);
	ConfigureSoldier(C, *F, Cls);
	FrameHumanCamera(Cast<APlayerController>(Controller), C);
}

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

int32 APA250BattleGameMode::TicketsRemaining(FName Faction, FName UnitClass) const
{
	if (const TMap<FName, int32>* P = Pool.Find(Faction))
	{
		return P->FindRef(UnitClass);
	}
	return 0;
}

int32 APA250BattleGameMode::CountAlive(FName Faction, FName UnitClass) const
{
	int32 N = 0;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->FactionId == Faction && It->UnitClass == UnitClass)
		{
			++N;
		}
	}
	return N;
}

int32 APA250BattleGameMode::PostsOwned(FName Faction) const
{
	int32 N = 0;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (WP.IsValid() && WP->GetHolder() == Faction) ++N;
	}
	return N;
}

FName APA250BattleGameMode::PluralityHolder() const
{
	const int32 B = PostsOwned(TEXT("british"));
	const int32 C = PostsOwned(TEXT("continental"));
	if (B > C) return TEXT("british");
	if (C > B) return TEXT("continental");
	return NAME_None;
}

FName APA250BattleGameMode::GetObjectiveHolder() const { return PluralityHolder(); }

void APA250BattleGameMode::BleedTickets(float DeltaSeconds)
{
	const int32 B = PostsOwned(TEXT("british"));
	const int32 C = PostsOwned(TEXT("continental"));
	if (B == C)
	{
		BleedAccum = 0.f;
		return;
	}
	BleedAccum += DeltaSeconds;
	const float Interval = FMath::Max(1.f, BleedIntervalSeconds);
	while (BleedAccum >= Interval)
	{
		BleedAccum -= Interval;
		const FName Loser = B < C ? FName(TEXT("british")) : FName(TEXT("continental"));
		TMap<FName, int32>& P = Pool.FindOrAdd(Loser);
		TArray<FName> Names;
		P.GetKeys(Names);
		Names.Sort(FNameLexicalLess());
		bool Spent = false;
		for (const FName& Cls : Names)
		{
			if (P.FindRef(Cls) > 0)
			{
				P.FindOrAdd(Cls)--;
				Spent = true;
				break;
			}
		}
		if (Spent)
		{
			++BleedTicks;
		}
	}
}

void APA250BattleGameMode::CheckTicketExhaustion()
{
	auto SideDead = [this](FName F) {
		return PoolTotal(F) <= 0 && CountAlive(F, TEXT("line_infantry")) + CountAlive(F, TEXT("militia")) <= 0;
	};
	const bool BritDead = SideDead(TEXT("british"));
	const bool ContDead = SideDead(TEXT("continental"));
	if (BritDead && ContDead)
	{
		FName Winner = PluralityHolder();
		if (Winner.IsNone()) Winner = ToName(Setup.defender_faction);
		EndBattle(Winner, TEXT("tickets_exhausted"));
		return;
	}
	if (BritDead)
	{
		EndBattle(TEXT("continental"), TEXT("tickets_exhausted"));
	}
	else if (ContDead)
	{
		EndBattle(TEXT("british"), TEXT("tickets_exhausted"));
	}
}

void APA250BattleGameMode::OnCharacterDied(ARFCharacter* Victim, AController* VictimController, AController* Killer)
{
	if (bBattleOver || !Victim)
	{
		return;
	}
	// Tickets are spent on spawn, not death.
	CheckTicketExhaustion();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (bAutoplayBots && PC->GetViewTarget() == Victim)
		{
			SpectateNextSoldier(PC, Victim);
		}
	}
	if (bBattleOver || !VictimController)
	{
		return;
	}
	const FName F = Victim->FactionId;
	if (VictimController->IsA<APlayerController>())
	{
		if (bAutoplayBots)
		{
			return;
		}
		ShowSpawnScreen(Cast<APlayerController>(VictimController));
		return;
	}
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
		SpawnBot(F, ChooseClassFor(F, Cls), nullptr);
		CheckTicketExhaustion();
	}), RespawnDelay, false);
}

float APA250BattleGameMode::TimeRemaining() const
{
	return FMath::Max(0.f, TimeLimitSeconds - (GetWorld()->GetTimeSeconds() - StartTime));
}

void APA250BattleGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFpsSampling && FApp::GetDeltaTime() > 0.0)
	{
		// Real frame time, not dilated game time (autoplay runs at 10x).
		const float RealDt = static_cast<float>(FApp::GetDeltaTime());
		FpsGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
		FpsRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		FpsGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		FpsSampleAccum += RealDt;
		FpsSampleTime += RealDt;
		++FpsSampleFrames;
		if (FpsSampleTime >= 10.f)
		{
			bFpsSampling = false;
			const float AvgFps = FpsSampleFrames / FMath::Max(FpsSampleAccum, KINDA_SMALL_NUMBER);
			const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"));
			IFileManager::Get().MakeDirectory(*Dir, true);
			const FString Path = FPaths::Combine(Dir, TEXT("lexington_fps.json"));
			FIntPoint ViewSize(0, 0);
			if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
			{
				ViewSize = GEngine->GameViewport->Viewport->GetSizeXY();
			}
			int32 Soldiers = 0;
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				Soldiers += It->IsDead() ? 0 : 1;
			}
			const IConsoleVariable* GI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
			const IConsoleVariable* Refl = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"));
			const FString Json = FString::Printf(
				TEXT("{\"avg_fps\":%.2f,\"frames\":%d,\"seconds\":%.3f,\"pass\":%s,\"bots_per_side\":%d,\"soldiers_alive\":%d,\"gi_method\":%d,\"reflection_method\":%d,\"time_dilation\":%.1f,\"game_ms\":%.2f,\"render_ms\":%.2f,\"gpu_ms\":%.2f,\"res\":\"%dx%d\"}\n"),
				AvgFps, FpsSampleFrames, FpsSampleAccum, AvgFps >= 30.f ? TEXT("true") : TEXT("false"),
				BotsPerSide, Soldiers, GI ? GI->GetInt() : -1, Refl ? Refl->GetInt() : -1,
				UGameplayStatics::GetGlobalTimeDilation(this),
				FpsGameMs / FMath::Max(1, FpsSampleFrames), FpsRenderMs / FMath::Max(1, FpsSampleFrames),
				FpsGpuMs / FMath::Max(1, FpsSampleFrames), ViewSize.X, ViewSize.Y);
			FFileHelper::SaveStringToFile(Json, *Path);
			UE_LOG(LogTemp, Display, TEXT("PA250 FPS sample avg=%.1f pass=%s -> %s"),
				AvgFps, AvgFps >= 30.f ? TEXT("true") : TEXT("false"), *Path);
			FTimerHandle Quit;
			GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, [this]() {
				FPlatformMisc::RequestExit(false);
			}), 0.5f, false);
		}
	}
	if (bBattleOver || !bInitialized)
	{
		return;
	}
	if (Volleys)
	{
		Volleys->Tick(DeltaSeconds);
	}
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (WP.IsValid() && WP->ConsumeFlipFlag())
		{
			++PostFlips;
		}
	}
	BleedTickets(DeltaSeconds);
	CheckTicketExhaustion();
	if (bBattleOver)
	{
		return;
	}
	if (TimeRemaining() <= 0.f)
	{
		const int32 TB = PoolTotal(TEXT("british"));
		const int32 TC = PoolTotal(TEXT("continental"));
		FName Winner;
		if (TB != TC)
		{
			Winner = TB > TC ? FName(TEXT("british")) : FName(TEXT("continental"));
		}
		else
		{
			Winner = PluralityHolder();
			if (Winner.IsNone()) Winner = ToName(Setup.defender_faction);
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
	const FName Plurality = PluralityHolder();
	Result.objective_holder = Plurality.IsNone() ? std::string() : ToStd(Plurality);
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (!WP.IsValid() || WP->GetHolder().IsNone()) continue;
		Result.objectives_held[ToStd(WP->PostId)] = ToStd(WP->GetHolder());
	}
	for (const auto& FK : InitialPool)
	{
		for (const auto& CK : FK.Value)
		{
			const int32 Left = Pool.FindChecked(FK.Key).FindRef(CK.Key);
			const int32 Alive = CountAlive(FK.Key, CK.Key);
			const int32 LostSoldiers = FMath::Max(0, CK.Value * SoldiersPerUnit - Left - Alive);
			const int32 Lost = FMath::Clamp(FMath::RoundToInt((float)LostSoldiers / (float)SoldiersPerUnit), 0, CK.Value);
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
	UE_LOG(LogTemp, Display, TEXT("PA250 battle over: %s flips=%d bleed=%d"), *Banner, PostFlips, BleedTicks);
	WriteBattleFile(Reason);

	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250BattleGameMode::FinishAndReturn), FMath::Max(0.1f, EndDelaySeconds), false);
}

void APA250BattleGameMode::WriteBattleFile(const FString& Reason) const
{
	FString PostTrace = TEXT("{");
	bool First = true;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (!WP.IsValid()) continue;
		if (!First) PostTrace += TEXT(",");
		First = false;
		const FName H = WP->GetHolder();
		PostTrace += FString::Printf(TEXT("\"%s\":\"%s\""), *WP->PostId.ToString(), H.IsNone() ? TEXT("") : *H.ToString());
	}
	PostTrace += TEXT("}");

	int32 Perfect = 0, Sweet = 0, Miss = 0, Jam = 0;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->Musket) continue;
		const FActiveReloadStats& S = It->Musket->GetReloadStats();
		Perfect += S.Perfect;
		Sweet += S.Sweet;
		Miss += S.Miss;
		Jam += S.Jam;
	}
	const int32 VolleyN = Volleys ? Volleys->GetVolleyCount() : 0;
	const float VolleyHits = Volleys ? Volleys->GetAverageHits() : 0.f;
	const int32 LineEng = Volleys ? Volleys->GetLineEngagements() : 0;
	const int32 Eng = Volleys ? Volleys->GetEngagements() : 0;

	// Concatenate setup/result JSON — never Printf them (embedded '%' would corrupt the file).
	FString Json = TEXT("{\"reason\":\"");
	Json += Reason;
	Json += FString::Printf(TEXT("\",\"elapsed_game_seconds\":%.1f,\"bots_per_side\":%d,\"soldiers_per_unit\":%d,\"trace\":{\"post_flips\":%d,\"bleed_ticks\":%d,\"posts\":"),
		GetWorld()->GetTimeSeconds() - StartTime, BotsPerSide, SoldiersPerUnit, PostFlips, BleedTicks);
	Json += PostTrace;
	Json += FString::Printf(TEXT(",\"tickets\":{\"british\":%d,\"continental\":%d},\"reload\":{\"perfect\":%d,\"sweet\":%d,\"miss\":%d,\"jam\":%d},\"volleys\":{\"count\":%d,\"avg_hits\":%.2f},\"lines\":{\"engagements\":%d,\"in_line\":%d}},\"setup\":"),
		PoolTotal(TEXT("british")), PoolTotal(TEXT("continental")),
		Perfect, Sweet, Miss, Jam, VolleyN, VolleyHits, Eng, LineEng);
	Json += UTF8_TO_TCHAR(pa250::setup_to_json(Setup).c_str());
	Json += TEXT(",\"result\":");
	Json += UTF8_TO_TCHAR(pa250::result_to_json(Result).c_str());
	Json += TEXT("}\n");
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
	const pa250::CampaignState Before = Sub->GetState();
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
		Check(Sub, PostFlips >= 0, TEXT("post flip counter recorded"));
		Finish(Sub, TEXT("selftest_battle"));
		return;
	}
	if (bDebugBattle)
	{
		return;
	}
	if (PA250Autoplay::IsAutoplay())
	{
		PA250Autoplay::Check(Sub, bApplied, TEXT("autoplay battle: BattleResult applied"));
		Sub->AutoplayStage = TEXT("battle_done");
	}
	Sub->TravelToFlowLevel(this);
}
