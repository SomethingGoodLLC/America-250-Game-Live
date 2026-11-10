#include "PA250BattleGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Campaign/PA250Autoplay.h"
#include "Campaign/PA250Subsystem.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
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
#include "PA250AimComponent.h"
#include "PA250BattleHUD.h"
#include "PA250BotController.h"
#include "PA250BotTuning.h"
#include "PA250BattleAudio.h"
#include "PA250CombatProfiles.h"
#include "PA250ControlPoint.h"
#include "PA250FactionVisuals.h"
#include "PA250MeleeComponent.h"
#include "PA250MeleeTuning.h"
#include "PA250SpawnScreen.h"
#include "PA250VolleyCoordinator.h"
#include "PA250WeaponTuning.h"
#include "PA250Smoke.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"
#include "TimerManager.h"
#include "Navigation/CrowdFollowingComponent.h"

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
	// Flag shots are placed relative to their post at runtime (Loc/Rot unused) and keep the HUD.
	{TEXT("Shot_FlagGreen"), TEXT("flag_green"), FVector::ZeroVector, FRotator::ZeroRotator, 45.f},
	{TEXT("Shot_FlagMeetinghouse"), TEXT("flag_meetinghouse"), FVector::ZeroVector, FRotator::ZeroRotator, 45.f},
	// Close-up of the cover wall nearest Loc (placed at runtime from the wall's collision box).
	{TEXT("Shot_WallE"), TEXT("wall_east"), FVector(6100.f, -1500.f, 0.f), FRotator::ZeroRotator, 55.f},
	// Behind Patriot wall W1, looking over it at flag B on the Green.
	{TEXT("Shot_WallPatriot"), TEXT("wall_patriot"), FVector(-4735.f, -253.f, 0.f), FRotator::ZeroRotator, 60.f},
	// -PA250Preview=smoke: scripted volleys west of the Green, then fresh smoke and the drifting bank.
	{TEXT("Shot_SmokeFresh"), TEXT("smoke_fresh"), FVector::ZeroVector, FRotator::ZeroRotator, 60.f},
	{TEXT("Shot_SmokeBank"), TEXT("smoke_bank"), FVector::ZeroVector, FRotator::ZeroRotator, 60.f},
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

void APA250BattleGameMode::NotifyProjectileHit(ARFCharacter* Shooter, ARFCharacter* Victim, float Damage)
{
	if (!Victim) return;
	// OnDied has already run inside ApplyPointDamage, so IsDead is the kill signal.
	// Kill-feed text is owned by OnCharacterDied (melee and musket). This is the shooter's marker only.
	const bool bKill = Victim->IsDead() || (Victim->HealthComponent && Victim->HealthComponent->CurrentHealth <= 0.f);
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Hud && PC && Shooter && Shooter == PC->GetPawn())
	{
		const float DistM = FVector::Dist(Shooter->GetActorLocation(), Victim->GetActorLocation()) / 100.f;
		Hud->ShowHitmarker(bKill, Damage, DistM);
		// Confirmation you hear even when the target is 80 m away: a tick for a hit, a thump for a kill.
		FPA250BattleAudio::Play2D(this, bKill ? TEXT("kill_confirm") : TEXT("hit_confirm"));
	}
	if (Hud && PC && Victim == PC->GetPawn() && Shooter)
	{
		Hud->ShowDamageDirection(Shooter->GetActorLocation());
	}
}

FVector APA250BattleGameMode::GetRearPoint(FName Faction, const FVector& Fallback) const
{
	const TArray<TWeakObjectPtr<AActor>>* List = Starts.Find(Faction);
	FVector Sum = FVector::ZeroVector;
	int32 N = 0;
	if (List)
	{
		for (const TWeakObjectPtr<AActor>& A : *List)
		{
			if (A.IsValid()) { Sum += A->GetActorLocation(); ++N; }
		}
	}
	return N > 0 ? Sum / N : Fallback;
}

void APA250BattleGameMode::RecordAIReload(FName Training, float Seconds, int32 Fumbles)
{
	AIReloadSeconds.FindOrAdd(Training).Add(Seconds);
	AIReloadFumbles.FindOrAdd(Training) += Fumbles;
}

FString APA250BattleGameMode::ReloadByTrainingJson() const
{
	FString J = TEXT("{");
	TArray<FName> Keys;
	AIReloadSeconds.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		TArray<float> V = AIReloadSeconds.FindRef(Keys[i]);
		V.Sort();
		float Sum = 0.f;
		for (float X : V) Sum += X;
		const float Mean = V.Num() ? Sum / V.Num() : 0.f;
		const float P90 = V.Num() ? V[FMath::Clamp(FMath::FloorToInt(0.9f * V.Num()), 0, V.Num() - 1)] : 0.f;
		J += FString::Printf(TEXT("%s\"%s\":{\"n\":%d,\"mean_s\":%.2f,\"p90_s\":%.2f,\"min_s\":%.2f,\"fumbles\":%d}"),
			i ? TEXT(",") : TEXT(""), *Keys[i].ToString(), V.Num(), Mean, P90, V.Num() ? V[0] : 0.f, AIReloadFumbles.FindRef(Keys[i]));
	}
	return J + TEXT("}");
}

void APA250BattleGameMode::PushKillFeed(const FString& Line)
{
	KillFeed.Insert(Line, 0);
	while (KillFeed.Num() > 6) KillFeed.RemoveAt(KillFeed.Num() - 1);
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

FPA250ScenarioFight APA250BattleGameMode::ResolveScenario(const FString& Preset, const TArray<FString>& TerrainTags)
{
	FPA250ScenarioFight Fight;
	const bool bBridge = Preset == TEXT("concord_bridge") || TerrainTags.Contains(TEXT("bridge_crossing"));
	const bool bRoad = Preset == TEXT("battle_road") || TerrainTags.Contains(TEXT("return_march"));
	// The preset wins when a stray tag disagrees. A crossing fight is not also a preservation fight.
	Fight.bContestSingleCrossing = bBridge && Preset != TEXT("battle_road");
	Fight.bRewardForcePreservation = bRoad && !Fight.bContestSingleCrossing;
	return Fight;
}

void APA250BattleGameMode::ConfigurePostForScenario(APA250ControlPoint* P, const FPA250ScenarioFight& Fight)
{
	if (!P)
	{
		return;
	}
	P->SetContestLocked(false);
	P->SetObjectiveTitle(FString());
	if (Fight.bContestSingleCrossing)
	{
		// Concord North Bridge: three contestable posts (muster, bridge, far bridgehead).
		P->MarkHoldersAssigned();
		P->SetContestLocked(false);
		if (P->PostId == TEXT("green"))
		{
			P->DebugSetHolder(NAME_None);
			P->SetObjectiveTitle(TEXT("North Bridge"));
		}
		else if (P->PostId == TEXT("meetinghouse"))
		{
			P->DebugSetHolder(TEXT("continental"));
			P->SetObjectiveTitle(TEXT("Muster ground"));
		}
		else if (P->PostId == TEXT("buckman"))
		{
			P->DebugSetHolder(TEXT("british"));
			P->SetObjectiveTitle(TEXT("Far bridgehead"));
		}
	}
	else if (Fight.bRewardForcePreservation)
	{
		P->MarkHoldersAssigned();
		// Return march: the column holds the road; the flanks start open. Posts do not decide the clock.
		if (P->PostId == TEXT("green"))
		{
			P->DebugSetHolder(TEXT("british"));
		}
		else if (P->PostId == TEXT("meetinghouse") || P->PostId == TEXT("buckman"))
		{
			P->DebugSetHolder(NAME_None);
		}
	}
}

FName APA250BattleGameMode::PreservationWinner(int32 RemainingA, int32 InitialA, int32 RemainingB, int32 InitialB,
	FName SideA, FName SideB, FName DefenderOnTie)
{
	auto Kept = [](int32 Remaining, int32 Initial) -> int64 {
		if (Initial <= 0)
		{
			return 0;
		}
		return static_cast<int64>(FMath::Max(0, Remaining)) * 1000 / Initial;
	};
	const int64 A = Kept(RemainingA, InitialA);
	const int64 B = Kept(RemainingB, InitialB);
	if (A > B)
	{
		return SideA;
	}
	if (B > A)
	{
		return SideB;
	}
	return DefenderOnTie;
}

FPA250CreekBridge APA250BattleGameMode::PlanCreekBridge(const FVector& GroundPoint)
{
	FPA250CreekBridge Span;
	Span.CreekCenter = GroundPoint + FVector(0.f, 0.f, -25.f);
	Span.DeckCenter = GroundPoint + FVector(0.f, 0.f, 14.f);
	return Span;
}

int32 APA250BattleGameMode::SpawnCreekBridge(UWorld* World, const FVector& GroundPoint)
{
	if (!World)
	{
		return 0;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cube)
	{
		return 0;
	}
	const FPA250CreekBridge Span = PlanCreekBridge(GroundPoint);
	auto Place = [&](const FVector& Center, const FVector& SizeCm, const FLinearColor& Color, bool bBlock, const TCHAR* Tag) -> bool {
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator, Params);
		if (!A)
		{
			return false;
		}
		A->Tags.Add(Tag);
		UStaticMeshComponent* Mesh = A->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetWorldScale3D(SizeCm / 100.f);
		Mesh->SetCollisionEnabled(bBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bBlock)
		{
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		}
		if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
			Mesh->SetMaterial(0, Mid);
		}
		return true;
	};

	const FVector Creek = Span.CreekSizeCm;
	const FVector Deck = Span.DeckSizeCm;
	const float BankLen = (Creek.Y - Deck.Y) * 0.5f;
	const float BankOff = Deck.Y * 0.5f + BankLen * 0.5f;
	const FLinearColor Water(0.15f, 0.32f, 0.38f);
	const FLinearColor Plank(0.42f, 0.28f, 0.14f);
	const FLinearColor Rail(0.28f, 0.18f, 0.09f);
	int32 N = 0;
	// Water showing under the planks, and two banks that close the creek on either side of the span.
	N += Place(Span.CreekCenter, FVector(Creek.X, Deck.Y, 30.f), Water, false, TEXT("PA250Creek")) ? 1 : 0;
	N += Place(Span.CreekCenter + FVector(0.f, BankOff, 10.f), FVector(Creek.X, BankLen, 90.f), Water, true, TEXT("PA250Creek")) ? 1 : 0;
	N += Place(Span.CreekCenter + FVector(0.f, -BankOff, 10.f), FVector(Creek.X, BankLen, 90.f), Water, true, TEXT("PA250Creek")) ? 1 : 0;
	N += Place(Span.DeckCenter, Deck, Plank, true, TEXT("PA250Bridge")) ? 1 : 0;
	const float RailZ = Span.DeckCenter.Z + Deck.Z * 0.5f + 30.f;
	N += Place(FVector(Span.DeckCenter.X, Span.DeckCenter.Y + Deck.Y * 0.5f, RailZ), FVector(Deck.X, 14.f, 70.f), Rail, false, TEXT("PA250Bridge")) ? 1 : 0;
	N += Place(FVector(Span.DeckCenter.X, Span.DeckCenter.Y - Deck.Y * 0.5f, RailZ), FVector(Deck.X, 14.f, 70.f), Rail, false, TEXT("PA250Bridge")) ? 1 : 0;
	UE_LOG(LogTemp, Display, TEXT("PA250 creek bridge: %d pieces, deck %.0f cm across a %.0f cm creek"),
		N, Deck.X, Creek.X);
	return N;
}

void APA250BattleGameMode::ApplyScenarioPreset()
{
	TArray<FString> Tags;
	Tags.Reserve(static_cast<int32>(Setup.terrain_tags.size()));
	for (const std::string& Tag : Setup.terrain_tags)
	{
		Tags.Add(UTF8_TO_TCHAR(Tag.c_str()));
	}
	const FString Preset = UTF8_TO_TCHAR(Setup.scenario_preset.c_str());
	ScenarioFight = ResolveScenario(Preset, Tags);
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		ConfigurePostForScenario(WP.Get(), ScenarioFight);
	}
	// Fort / sim-authored holders override scenario defaults (defender starts holding a post).
	for (const auto& Kv : Setup.initial_post_holders)
	{
		const FName PostId = UTF8_TO_TCHAR(Kv.first.c_str());
		const FName Holder = UTF8_TO_TCHAR(Kv.second.c_str());
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			APA250ControlPoint* P = WP.Get();
			if (!P || P->PostId != PostId) continue;
			P->DebugSetHolder(Holder);
			P->MarkHoldersAssigned();
			UE_LOG(LogTemp, Display, TEXT("PA250 fort/setup: post %s starts held by %s"), *PostId.ToString(),
				   *Holder.ToString());
		}
	}
	if (Setup.fort_level > 0)
	{
		SpawnFortCoverNearHeldPosts();
	}
	if (ScenarioFight.bContestSingleCrossing)
	{
		if (APA250ControlPoint* Green = FindGreenPost())
		{
			// The post actor sits above the ground. The creek and the planks are on the ground under it.
			SpawnCreekBridge(GetWorld(), Green->GetActorLocation() + FVector(0.f, 0.f, -140.f));
		}
	}
	// DebugSetHolder marks flips; clear so scenario setup is not scored as capture flips.
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (WP.IsValid())
		{
			WP->ConsumeFlipFlag();
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 battle scenario_preset=%s province=%s crossing=%d preserve=%d tags=%d fort=%d"),
		*Preset, UTF8_TO_TCHAR(Setup.province_id.c_str()), ScenarioFight.bContestSingleCrossing ? 1 : 0,
		ScenarioFight.bRewardForcePreservation ? 1 : 0, Tags.Num(), Setup.fort_level);
}

float APA250BattleGameMode::GetCampaignConfidencePressure(FName Faction) const
{
	const auto It = Setup.sides.find(TCHAR_TO_UTF8(*Faction.ToString()));
	if (It == Setup.sides.end())
	{
		return 0.f;
	}
	// High campaign morale → less pressure; low morale / readiness → more. Caps keep battlefield
	// Steady/Shaken/Rout as the primary system.
	const float Morale01 = FMath::Clamp(It->second.morale / 100.f, 0.f, 1.f);
	const float Ready01 = FMath::Clamp(It->second.readiness / 100.f, 0.f, 1.f);
	return FMath::Clamp((1.f - Morale01) * 0.2f + (1.f - Ready01) * 0.1f, 0.f, 0.35f);
}

void APA250BattleGameMode::SpawnFortCoverNearHeldPosts()
{
	UWorld* World = GetWorld();
	if (!World || Setup.fort_level <= 0) return;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250 fort cover: /Engine/BasicShapes/Cube.Cube failed to load; no cover spawned"));
		return;
	}
	int32 Spawned = 0;
	for (const auto& Kv : Setup.initial_post_holders)
	{
		const FName PostId = UTF8_TO_TCHAR(Kv.first.c_str());
		APA250ControlPoint* Post = nullptr;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			if (WP.IsValid() && WP->PostId == PostId)
			{
				Post = WP.Get();
				break;
			}
		}
		if (!Post) continue;
		FVector Ground = Post->GetActorLocation();
		FHitResult Hit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PA250FortCover), false, Post);
		const FVector TraceStart = Ground + FVector(0.f, 0.f, 400.f);
		const FVector TraceEnd = Ground - FVector(0.f, 0.f, 1200.f);
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams) && Hit.bBlockingHit)
		{
			Ground = Hit.ImpactPoint;
		}
		else
		{
			Ground.Z -= 120.f;
			UE_LOG(LogTemp, Warning, TEXT("PA250 fort cover: no ground under post %s; using a fixed drop"),
				*Post->PostId.ToString());
		}
		// 100 cm engine cube, scale Z 1.2 → 120 cm tall. Center sits half that above the ground.
		const float HalfHeight = 60.f;
		const FVector Offsets[] = {FVector(220.f, 0.f, HalfHeight), FVector(-220.f, 0.f, HalfHeight),
								   FVector(0.f, 220.f, HalfHeight)};
		for (const FVector& Off : Offsets)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Wall =
				World->SpawnActor<AStaticMeshActor>(Ground + Off, FRotator::ZeroRotator, Params);
			if (!Wall) continue;
			Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Wall->SetActorScale3D(FVector(3.5f, 0.4f, 1.2f));
			Wall->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Wall->Tags.Add(TEXT("PA250Cover"));
			++Spawned;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 fort cover: spawned %d cover walls (fort_level=%d)"), Spawned,
		   Setup.fort_level);
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
	WeaponTuningAsset = LoadObject<UPA250WeaponTuning>(nullptr, TEXT("/Game/PA250/Data/DA_WeaponTuning.DA_WeaponTuning"));
	if (!WeaponTuningAsset) WeaponTuningAsset = UPA250WeaponTuning::MakeRuntimeDefaults();
	BotTuningAsset = LoadObject<UPA250BotTuning>(nullptr, TEXT("/Game/PA250/Data/DA_BotTuning.DA_BotTuning"));
	if (!BotTuningAsset) BotTuningAsset = UPA250BotTuning::MakeRuntimeDefaults();
	MeleeTuningAsset = LoadObject<UPA250MeleeTuning>(nullptr, TEXT("/Game/PA250/Data/DA_MeleeTuning.DA_MeleeTuning"));
	if (!MeleeTuningAsset) MeleeTuningAsset = UPA250MeleeTuning::MakeRuntimeDefaults();
	CombatProfilesAsset = LoadObject<UPA250CombatProfiles>(nullptr, TEXT("/Game/PA250/Data/DA_CombatProfiles.DA_CombatProfiles"));
	if (!CombatProfilesAsset) CombatProfilesAsset = UPA250CombatProfiles::MakeRuntimeDefaults();
	FactionVisualsAsset = LoadObject<UPA250FactionVisuals>(nullptr, TEXT("/Game/PA250/Data/DA_FactionVisuals.DA_FactionVisuals"));
	if (!FactionVisualsAsset) FactionVisualsAsset = UPA250FactionVisuals::MakeRuntimeDefaults();

	if (Tuning)
	{
		BotsPerSide = Tuning->Values.Conquest.BotsPerSide;
		TimeLimitSeconds = Tuning->Values.Conquest.TimeLimitSeconds;
		BleedIntervalSeconds = Tuning->Values.Conquest.BleedIntervalSeconds;
		MinSecondsBeforeExhaustion = Tuning->Values.Conquest.MinSecondsBeforeExhaustion;
		SoldiersPerUnit = Tuning->Values.Conquest.SoldiersPerUnit;
		// DA_MusketTuning was saved while the game mode forced a 5s bleed. That cooked
		// 5 overrides the C++ default of 12. Treat exactly 5 as the retired hard-code.
		if (FMath::IsNearlyEqual(BleedIntervalSeconds, 5.f, 0.05f))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("PA250 bleed: DA_MusketTuning BleedIntervalSeconds is 5 (retired hard-code); using 12s"));
			BleedIntervalSeconds = 12.f;
		}
		if (MinSecondsBeforeExhaustion <= 0.f)
		{
			MinSecondsBeforeExhaustion = 120.f;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 bleed: interval %.1fs, exhaustion gate %.0fs%s"), BleedIntervalSeconds,
		MinSecondsBeforeExhaustion, Tuning ? TEXT(" (DA_MusketTuning)") : TEXT(" (C++ defaults, DA missing)"));

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
	// Tickets = reinforcement pool in soldiers. Spent on death + bleed (M6), not spawn.
	for (const auto& FK : InitialPool)
	{
		int32 Units = 0;
		int32 Total = 0;
		for (const auto& CK : FK.Value)
		{
			Units += CK.Value;
			Total += CK.Value * SoldiersPerUnit;
		}
		Ledger.Pool.Add(FK.Key, Total);
		UE_LOG(LogTemp, Display, TEXT("PA250 tickets %s: %d units -> %d soldiers"), *FK.Key.ToString(), Units,
			Total);
	}

	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (Tag == TEXT("british") || Tag == TEXT("continental"))
			{
				Starts.FindOrAdd(Tag).Add(*It);
			}
			else if (Tag.ToString().EndsWith(TEXT("_muster")))
			{
				MusterPoints.FindOrAdd(FName(*Tag.ToString().LeftChop(7))).Add(*It);
			}
		}
	}
	GatherPosts();
	ApplyScenarioPreset();
	{
		const float PostR = Tuning ? Tuning->Values.Conquest.PostRadius : 1000.f;
		const float CapS = Tuning ? Tuning->Values.Conquest.CaptureSeconds : 3.5f;
		const float CapScale = Tuning ? Tuning->Values.Conquest.CapturePerAttackerScale : 0.35f;
		const float UseRadius = 1000.f; // M6: 10 m capture radius
		(void)PostR;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			if (APA250ControlPoint* P = WP.Get())
			{
				P->ApplyTuning(UseRadius, CapS, CapScale);
				if (BotTuningAsset)
				{
					P->BuildSlotRing(BotTuningAsset->Values.SlotsPerPost,
						BotTuningAsset->Values.SlotRingMinCm, BotTuningAsset->Values.SlotRingMaxCm);
				}
			}
		}
	}
	RegisterConsoleCommands();
}

void APA250BattleGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureInitialized();
	if (!bAutoplayBots)
	{
		FPA250BattleAudio::PlayBattleBegins(this);
	}
	StartTime = GetWorld()->GetTimeSeconds();
	Banner = FString::Printf(TEXT("Battle of %s"), UTF8_TO_TCHAR(Setup.province_name.c_str()));
	if (ScenarioFight.bContestSingleCrossing)
	{
		Banner += TEXT(" — hold the crossing");
	}
	else if (ScenarioFight.bRewardForcePreservation)
	{
		Banner += TEXT(" — preserve your force");
	}

	if (bAutoplayBots)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, AutoplayTimeDilation);
	}
	for (const auto& KV : InitialPool)
	{
		SpawnInitialForces(KV.Key, BotsPerSide);
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
	MaybeStartMeleeProbe();
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
	if (!PC) return;
	ARFCharacter* Any = nullptr;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Exclude || It->IsDead()) continue;
		if (!Any) Any = *It;
		if (It->FactionId == PlayerFaction)
		{
			ApplyLowCinematicSpectator(PC, *It);
			return;
		}
	}
	if (Any)
	{
		ApplyLowCinematicSpectator(PC, Any);
	}
}

void APA250BattleGameMode::FrameHumanCamera(APlayerController* PC, APawn* P)
{
	if (!PC || !P)
	{
		return;
	}
	// M1 shoulder camera: hip 220 / (0,±60,70) / FOV 80 — ADS handled by ARFCharacter.
	if (ARFCharacter* C = Cast<ARFCharacter>(P))
	{
		if (USpringArmComponent* Arm = C->SpringArm)
		{
			Arm->bUsePawnControlRotation = true;
			Arm->bInheritPitch = true;
			Arm->SetRelativeRotation(FRotator::ZeroRotator);
			Arm->bEnableCameraRotationLag = false;
			Arm->TargetArmLength = C->HipArmLength;
			Arm->SocketOffset = FVector(0.f, C->GetShoulderSign() * C->HipSocketOffset.Y, C->HipSocketOffset.Z);
			Arm->bEnableCameraLag = true;
			Arm->CameraLagSpeed = 10.f;
		}
		if (C->Camera) C->Camera->SetFieldOfView(C->DefaultFOV);
	}
	else if (USpringArmComponent* Arm = P->FindComponentByClass<USpringArmComponent>())
	{
		Arm->bUsePawnControlRotation = true;
		Arm->bInheritPitch = true;
		Arm->SetRelativeRotation(FRotator::ZeroRotator);
		Arm->bEnableCameraRotationLag = false;
		Arm->TargetArmLength = 220.f;
		Arm->SocketOffset = FVector(0.f, 60.f, 70.f);
		Arm->bEnableCameraLag = true;
		Arm->CameraLagSpeed = 10.f;
	}
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->ViewPitchMin = -89.f;
		PC->PlayerCameraManager->ViewPitchMax = 89.f;
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
	if (PreviewLabel == TEXT("smoke"))
	{
		// Three 15-man volleys, 3 s apart, from a line west of the Green firing east (visual only).
		for (int32 V = 0; V < 3; ++V)
		{
			FTimerHandle Volley;
			GetWorldTimerManager().SetTimer(Volley, FTimerDelegate::CreateWeakLambda(this, [this]() {
				UPA250SmokeSubsystem* Bank = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>();
				const APA250ControlPoint* Green = nullptr;
				for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
					if (WP.IsValid() && WP->PostId == TEXT("green")) Green = WP.Get();
				if (!Bank || !Green) return;
				for (int32 i = 0; i < 15; ++i)
				{
					FVector M = Green->GetActorLocation() + FVector(-2000.f, -700.f + i * 100.f, 0.f);
					FHitResult Hit;
					if (GetWorld()->LineTraceSingleByChannel(Hit, M + FVector(0, 0, 500), M - FVector(0, 0, 2000), ECC_WorldStatic))
						M.Z = Hit.ImpactPoint.Z + 140.f;
					Bank->AddShot(M, FVector(1.f, 0.f, 0.03f));
				}
			}), 1.f + 3.f * V, false);
		}
	}
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
	{
		// Special labels capture only their own group; any other label takes the general views.
		const FString T(Spec.Tag);
		const FString Group = T.StartsWith(TEXT("Shot_Flag")) ? TEXT("flags") : T.StartsWith(TEXT("Shot_Wall")) ? TEXT("walls")
		                    : T.StartsWith(TEXT("Shot_Smoke")) ? TEXT("smoke") : TEXT("");
		const bool bSpecialLabel = PreviewLabel == TEXT("flags") || PreviewLabel == TEXT("walls") || PreviewLabel == TEXT("smoke");
		if (Group != (bSpecialLabel ? PreviewLabel : FString()))
		{
			if (Index + 1 < Total) CapturePreviewShot(GPreviewCams[Index + 1].Tag, GPreviewCams[Index + 1].Stem, Index + 1, Total);
			else FPlatformMisc::RequestExit(false);
			return;
		}
	}
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
	const FString TagStr(Spec.Tag);
	const bool bFlagShot = TagStr.StartsWith(TEXT("Shot_Flag"));
	if (bFlagShot)
	{
		const FName Want = TagStr.EndsWith(TEXT("Green")) ? FName(TEXT("green")) : FName(TEXT("meetinghouse"));
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			if (APA250ControlPoint* P = WP.Get(); P && P->PostId == Want)
			{
				// Preview only: fly British colours on the Green so both flags can be inspected.
				if (Want == TEXT("green")) P->DebugSetHolder(TEXT("british"));
				const FVector Look = P->GetMarkerLocation() - FVector(0.f, 0.f, 350.f);
				// From the open middle of the map toward the post, so buildings stay behind the pole.
				const FVector Toward = (-P->GetActorLocation() * FVector(1.f, 1.f, 0.f)).GetSafeNormal();
				const FVector From = P->GetActorLocation() + Toward * 1600.f + FVector(0.f, 0.f, 250.f);
				Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
			}
		}
	}
	if (TagStr.StartsWith(TEXT("Shot_Smoke")))
	{
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			if (WP.IsValid() && WP->PostId == TEXT("green"))
			{
				const FVector G = WP->GetActorLocation();
				const FVector From = G + FVector(-600.f, 2300.f, 150.f);
				const FVector Look = G + FVector(-1500.f, 0.f, 0.f);
				Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
			}
		}
	}
	if (TagStr.StartsWith(TEXT("Shot_Wall")))
	{
		AActor* Best = nullptr;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("PA250Cover")) && (!Best || FVector::Dist2D(It->GetActorLocation(), Spec.Loc) < FVector::Dist2D(Best->GetActorLocation(), Spec.Loc)))
				Best = *It;
		}
		const APA250ControlPoint* GreenPost = nullptr;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
			if (WP.IsValid() && WP->PostId == TEXT("green")) GreenPost = WP.Get();
		if (Best && GreenPost && TagStr == TEXT("Shot_WallPatriot"))
		{
			const FVector Look = GreenPost->GetActorLocation() + FVector(0.f, 0.f, 200.f);
			const FVector Away = (Best->GetActorLocation() - GreenPost->GetActorLocation()).GetSafeNormal2D();
			const FVector From = Best->GetActorLocation() + Away * 450.f + FVector(0.f, 0.f, 110.f);
			Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
		}
		else if (Best)
		{
			const FVector Side = Best->GetActorRightVector();
			const FVector Look = Best->GetActorLocation();
			const FVector From = Look + Side * 650.f + Best->GetActorForwardVector() * 250.f + FVector(0.f, 0.f, 90.f);
			Cam->SetActorLocationAndRotation(From, (Look - From).Rotation());
		}
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
	const float Settle = TagStr == TEXT("Shot_SmokeBank") ? 14.f : 1.5f;
	GetWorldTimerManager().SetTimer(Snap, FTimerDelegate::CreateWeakLambda(this, [this, SavedPath, PreviewDir, FinalName, Index, Total, bFlagShot]() {
		FScreenshotRequest::RequestScreenshot(SavedPath, /*bShowUI=*/bFlagShot, /*bAddFilenameSuffix=*/false);
		PollPreviewFile(SavedPath, FPaths::Combine(PreviewDir, FinalName), Index, Total, 0);
	}), Settle, false);
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
	float SampleDelay = 5.f;
	FParse::Value(FCommandLine::Get(), TEXT("PA250FpsDelay="), SampleDelay);
	FTimerHandle Start;
	GetWorldTimerManager().SetTimer(Start, FTimerDelegate::CreateWeakLambda(this, [this]() {
		bFpsSampling = true;
		FpsSampleAccum = 0.f;
		FpsGameMs = FpsRenderMs = FpsGpuMs = 0.0;
		FpsSampleTime = 0.f;
		FpsSampleFrames = 0;
		UE_LOG(LogTemp, Display, TEXT("PA250 FPS sample started"));
	}), FMath::Max(0.5f, SampleDelay), false);  // after shader warm-up and the first runtime navmesh build
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
	const TMap<FName, int32>* P = InitialPool.Find(Faction);
	if (!P)
	{
		return Preferred;
	}
	if (P->Contains(Preferred))
	{
		return Preferred;
	}
	TArray<FName> Names;
	P->GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	return Names.Num() > 0 ? Names[0] : Preferred;
}

bool APA250BattleGameMode::CanSpawn(FName Faction) const
{
	return Ledger.Remaining(Faction) > 0;
}

void APA250BattleGameMode::SpendDeathTicket(FName Faction, FName UnitClass)
{
	Ledger.SpendDeath(Faction, UnitClass);
}

void APA250BattleGameMode::RegisterConsoleCommands()
{
	static bool bRegistered = false;
	if (bRegistered) return;
	bRegistered = true;
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("pa250.BotSpacingReport"),
		TEXT("Print bot spacing stats for the live battle."),
		FConsoleCommandDelegate::CreateLambda([]() {
			if (GEngine)
			{
				for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
				{
					if (UWorld* W = Ctx.World())
					{
						if (APA250BattleGameMode* GM = W->GetAuthGameMode<APA250BattleGameMode>())
						{
							if (GM->GetVolleys()) GM->GetVolleys()->PrintSpacingReport();
						}
					}
				}
			}
		}),
		ECVF_Default);
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
		// Far spawns are pulled in to 55 m from the Green, so the two sides start ~110 m apart:
		// room to form ranks and march into the 40-80 m musket band (28 m put lines face to face).
		if (!Soldier->IsPlayerControlled() && FromGreen.Size() > 7000.f)
		{
			Soldier->SetActorLocation(Anchor + FromGreen.GetSafeNormal() * 5500.f + FVector(0.f, 0.f, 40.f), false, nullptr, ETeleportType::TeleportPhysics);
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
	Soldier->WeaponTuning = WeaponTuningAsset;
	Soldier->FactionVisuals = FactionVisualsAsset;
	Soldier->ApplyFactionVisuals();
	if (Soldier->MeleeComponent)
	{
		Soldier->MeleeComponent->Tuning = MeleeTuningAsset;
	}
	if (Soldier->Musket)
	{
		if (Tuning) Soldier->Musket->Tuning = Tuning;
		Soldier->Musket->WeaponTuning = WeaponTuningAsset;
		Soldier->Musket->SetReloadPreset(ReloadPreset == 1 ? EPA250ReloadPreset::Authentic : EPA250ReloadPreset::Arcade);
		Soldier->Musket->ApplyWeaponTuning();
	}
	if (APA250BotController* Bot = Cast<APA250BotController>(Soldier->GetController()))
	{
		Bot->SetPosts(Posts);
		Bot->BotTuning = BotTuningAsset;
		// Training + tactical role come from DA_CombatProfiles via (faction, class). The reload RNG is
		// seeded from the battle seed and a spawn counter (never the wall clock).
		if (CombatProfilesAsset)
		{
			const uint32 Seed = HashCombine(GetTypeHash(static_cast<uint64>(Setup.seed)), static_cast<uint32>(++BotProfileSerial));
			Bot->ApplyCombatProfile(CombatProfilesAsset->TrainingFor(Faction, UnitClass),
				CombatProfilesAsset->RoleFor(Faction, UnitClass), static_cast<int32>(Seed));
			Bot->AimErrorDegrees = CombatProfilesAsset->TrainingFor(Faction, UnitClass).AimErrorDegrees; // training, not class or side
		}
		Bot->ConfigureCrowd(BotTuningAsset);
	}
	Soldier->GetCharacterMovement()->MaxWalkSpeed = 380.f;
	if (Soldier->IsPlayerControlled())
	{
		Soldier->GetCharacterMovement()->MaxWalkSpeed = Soldier->WalkSpeed;
	}
}

ARFCharacter* APA250BattleGameMode::SpawnBot(FName Faction, FName UnitClass, APA250ControlPoint* PreferredPost, uint32 ClothingSeed)
{
	const FName Charged = ChooseClassFor(Faction, UnitClass);
	if (!CanSpawn(Faction))
	{
		return nullptr;
	}
	return SpawnBotAt(Faction, Charged, PickSpawnTransform(Faction, PreferredPost), ClothingSeed);
}

ARFCharacter* APA250BattleGameMode::SpawnBotAt(FName Faction, FName Charged, const FTransform& T, uint32 ClothingSeed)
{
	if (!CanSpawn(Faction))
	{
		return nullptr;
	}
	ARFCharacter* C = GetWorld()->SpawnActorDeferred<ARFCharacter>(
		ARFCharacter::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!C)
	{
		return nullptr;
	}
	if (ClothingSeed == 0)
	{
		ClothingSeed = HashCombine(GetTypeHash(static_cast<uint64>(Setup.seed)), static_cast<uint32>(++ClothesSerial));
	}
	C->SetClothingSeed(ClothingSeed);
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

void APA250BattleGameMode::SpawnInitialForces(FName Faction, int32 Count)
{
	// Militia muster (GAME INVENTION): only part of a side's militia is on the field at the start;
	// the rest arrive in companies (TickMuster). Everyone else deploys as before. Tickets are not
	// touched (they are spent on death), so BattleResult losses still never exceed the pools.
	const TMap<FName, int32>* P = InitialPool.Find(Faction);
	const UPA250BotTuning* BT = BotTuningAsset;
	const int32 MilitiaUnits = P ? P->FindRef(TEXT("militia")) : 0;
	if (!P || !BT || !BT->Values.bMilitiaMuster || MilitiaUnits <= 0)
	{
		SpawnBots(Faction, Count);
		return;
	}
	int32 Total = 0;
	for (const auto& KV : *P) Total += KV.Value;
	int32 Others = 0, Initial = 0, Cap = 0;
	MusterPlan(Count, MilitiaUnits, Total, BT->Values.MusterInitialFraction, BT->Values.MusterMaxAliveScale, Others, Initial, Cap);
	const int32 MilitiaPlaces = Count - Others;
	// Non-militia classes, in proportion.
	TArray<FName> Names;
	P->GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	const int32 OtherUnits = Total - MilitiaUnits;
	int32 Spawned = 0;
	for (const FName& N : Names)
	{
		if (N == TEXT("militia") || OtherUnits <= 0) continue;
		const int32 K = FMath::Min(Others - Spawned, FMath::Max(1, FMath::RoundToInt(float(Others) * P->FindRef(N) / OtherUnits)));
		for (int32 i = 0; i < K; ++i) Spawned += SpawnBot(Faction, N, nullptr) ? 1 : 0;
	}
	for (int32 i = 0; i < Initial; ++i) SpawnBot(Faction, TEXT("militia"), nullptr);
	MusterCap.Add(Faction, Cap);
	MusterTimer.Add(Faction, 0.f);
	UE_LOG(LogTemp, Display, TEXT("PA250 muster: %s fields %d others + %d of %d militia; up to %d militia as companies arrive"),
		*Faction.ToString(), Spawned, Initial, MilitiaPlaces, MusterCap.FindRef(Faction));
}

void APA250BattleGameMode::MusterPlan(int32 Count, int32 MilitiaUnits, int32 TotalUnits, float InitialFraction, float MaxAliveScale,
	int32& OutOthers, int32& OutInitialMilitia, int32& OutMilitiaCap)
{
	const int32 MilitiaPlaces = FMath::Clamp(FMath::RoundToInt(float(Count) * MilitiaUnits / FMath::Max(1, TotalUnits)), 0, Count);
	OutOthers = Count - MilitiaPlaces;
	OutInitialMilitia = MilitiaPlaces > 0 ? FMath::Clamp(FMath::RoundToInt(MilitiaPlaces * InitialFraction), 1, MilitiaPlaces) : 0;
	OutMilitiaCap = FMath::Max(OutInitialMilitia, FMath::RoundToInt(MilitiaPlaces * FMath::Max(1.f, MaxAliveScale)));
}

int32 APA250BattleGameMode::MusterWave(int32 MilitiaCap, int32 MilitiaAlive, int32 TicketsLeft, int32 SideAlive, int32 WaveSize)
{
	return FMath::Max(0, FMath::Min3(WaveSize, MilitiaCap - MilitiaAlive, TicketsLeft - SideAlive));
}

void APA250BattleGameMode::TickMuster(float DeltaSeconds)
{
	const UPA250BotTuning* BT = BotTuningAsset;
	if (!BT) return;
	for (TPair<FName, int32>& KV : MusterCap)
	{
		const FName F = KV.Key;
		float& Timer = MusterTimer.FindOrAdd(F);
		Timer += DeltaSeconds;
		if (Timer < BT->Values.MusterWaveSeconds) continue;
		Timer = 0.f;
		int32 AliveSide = 0;
		for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It) AliveSide += (!It->IsDead() && It->FactionId == F) ? 1 : 0;
		const int32 N = MusterWave(KV.Value, CountAlive(F, TEXT("militia")), PoolTotal(F), AliveSide, BT->Values.MusterWaveSize);
		if (N <= 0) continue;
		// A company arrives together from one of the side's muster points (else its normal start).
		const TArray<TWeakObjectPtr<AActor>>* Points = MusterPoints.Find(F);
		AActor* From = nullptr;
		if (Points && Points->Num() > 0) From = (*Points)[FMath::RandHelper(Points->Num())].Get();
		if (!From) From = PickStart(F);
		if (!From) continue;
		int32 Arrived = 0;
		for (int32 i = 0; i < N; ++i)
		{
			FVector Loc = From->GetActorLocation() + FVector(FMath::FRandRange(-500.f, 500.f), FMath::FRandRange(-500.f, 500.f), 0.f);
			Loc = ClearSpawnLocation(GetWorld(), Loc);
			Arrived += SpawnBotAt(F, TEXT("militia"), FTransform(From->GetActorRotation(), Loc)) ? 1 : 0;
		}
		MusterArrivals.FindOrAdd(F) += Arrived;
		if (Arrived > 0)
		{
			const FVector Dir = (From->GetActorLocation() - (Posts.Num() && Posts[0].IsValid() ? Posts[0]->GetActorLocation() : FVector::ZeroVector)).GetSafeNormal2D();
			const TCHAR* From4 = FMath::Abs(Dir.X) > FMath::Abs(Dir.Y) ? (Dir.X > 0 ? TEXT("east") : TEXT("west")) : (Dir.Y > 0 ? TEXT("south") : TEXT("north"));
			PushKillFeed(F == PlayerFaction ? FString::Printf(TEXT("Militia company arrives from the %s! (+%d)"), From4, Arrived)
				: FString::Printf(TEXT("Enemy militia arriving from the %s! (+%d)"), From4, Arrived));
		}
	}
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
	if (!CanSpawn(Faction))
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
	if (!CanSpawn(PlayerFaction))
	{
		CloseSpawnScreenToSpectate(PC);
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

void APA250BattleGameMode::CloseSpawnScreenToSpectate(APlayerController* PC)
{
	if (SpawnScreen)
	{
		SpawnScreen->RemoveFromParent();
	}
	if (!PC) return;
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
	SpectateNextSoldier(PC, PC->GetPawn());
}

APawn* APA250BattleGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !GetWorld())
	{
		return nullptr;
	}
	const FName Cls = ChooseClassFor(PlayerFaction, TEXT("line_infantry"));
	if (!CanSpawn(PlayerFaction))
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
		if (PlayerClothingSeed == 0)
		{
			PlayerClothingSeed = HashCombine(GetTypeHash(static_cast<uint64>(Setup.seed)), 0xC107u);
		}
		C->SetClothingSeed(PlayerClothingSeed);
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
	if (!CanSpawn(*F))
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
	return Ledger.Remaining(Faction);
}

int32 APA250BattleGameMode::InitialSoldiers(FName Faction) const
{
	int32 Units = 0;
	for (const auto& CK : InitialPool.FindRef(Faction))
	{
		Units += CK.Value;
	}
	return Units * FMath::Max(1, SoldiersPerUnit);
}

int32 APA250BattleGameMode::TicketsRemaining(FName Faction) const { return PoolTotal(Faction); }

int32 APA250BattleGameMode::TicketsRemaining(FName Faction, FName UnitClass) const
{
	(void)UnitClass;
	return TicketsRemaining(Faction);
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
		if (WP.IsValid() && !WP->IsContestLocked() && WP->GetHolder() == Faction) ++N;
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

int32 APA250BattleGameMode::CountPosts() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts) N += WP.IsValid() ? 1 : 0;
	return N;
}

int32 APA250BattleGameMode::GetBleedPerInterval(FName Faction) const
{
	if (!UsesPostBleed(ScenarioFight)) return 0;
	const int32 Mine = PostsOwned(Faction);
	const int32 Theirs = PostsOwned(Faction == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")));
	return Mine < Theirs ? FPA250TicketLedger::BleedAmount(Mine, Theirs, CountPosts()) : 0;
}

void APA250BattleGameMode::BleedTickets(float DeltaSeconds)
{
	if (!UsesPostBleed(ScenarioFight))
	{
		return;
	}
	// Battlefront-style: the bleed grows with the post advantage, fastest when one side holds them all.
	Ledger.Bleed(DeltaSeconds, BleedIntervalSeconds,
		TEXT("british"), PostsOwned(TEXT("british")), TEXT("continental"), PostsOwned(TEXT("continental")), CountPosts());
}

void APA250BattleGameMode::CheckTicketExhaustion()
{
	// Keep early fights from ending on bleed alone (default MinSecondsBeforeExhaustion ≈ 2 min).
	const float Elapsed = GetWorld() ? (GetWorld()->GetTimeSeconds() - StartTime) : 0.f;
	if (!TicketsMayExhaust(Elapsed, MinSecondsBeforeExhaustion))
	{
		return;
	}
	// Conquest: a side at 0 tickets loses at once, even with soldiers still standing.
	// A side that started with no reinforcement tickets only loses after it has fielded at
	// least once and been wiped — never on the first Tick before spawn (stores gating used
	// to leave Continental at 0 tickets and kick Concord back to the map immediately).
	auto Out = [this](FName F) {
		if (PoolTotal(F) > 0) return false;
		int32 Initial = 0;
		for (const auto& CK : InitialPool.FindRef(F)) Initial += CK.Value;
		const int32 Alive =
			CountAlive(F, TEXT("line_infantry")) + CountAlive(F, TEXT("militia"));
		if (Initial > 0)
		{
			return true;
		}
		return EverFielded.FindRef(F) && Alive <= 0;
	};
	const bool BritOut = Out(TEXT("british"));
	const bool ContOut = Out(TEXT("continental"));
	if (BritOut && ContOut)
	{
		FName Winner = PluralityHolder();
		if (Winner.IsNone()) Winner = ToName(Setup.defender_faction);
		EndBattle(Winner, TEXT("tickets_exhausted"));
		return;
	}
	if (BritOut)
	{
		EndBattle(TEXT("continental"), TEXT("tickets_exhausted"));
	}
	else if (ContOut)
	{
		EndBattle(TEXT("british"), TEXT("tickets_exhausted"));
	}
}

void APA250BattleGameMode::BeginDeathCamThenSpawn(APlayerController* PC)
{
	if (!PC) return;
	// OnDeath sets a 3 s lifespan, the same instant this cam ends, so the view target
	// can be destroyed before the spawn screen opens. Keep the corpse through the cam.
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetLifeSpan(DeathCamSeconds + 2.f);
	}
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, WeakPC]() {
		APlayerController* C = WeakPC.Get();
		if (!C || bBattleOver) return;
		if (!CanSpawn(PlayerFaction))
		{
			CloseSpawnScreenToSpectate(C);
			return;
		}
		ShowSpawnScreen(C);
	}), DeathCamSeconds, false);
}

void APA250BattleGameMode::OnCharacterDied(ARFCharacter* Victim, AController* VictimController, AController* Killer)
{
	if (bBattleOver || !Victim)
	{
		return;
	}
	// M6: tickets spent on death (and bleed), not spawn.
	if (HasAuthority())
	{
		SpendDeathTicket(Victim->FactionId, Victim->UnitClass);
		if (Killer)
		{
			if (const ARFCharacter* KillerChar = Cast<ARFCharacter>(Killer->GetPawn()))
			{
				auto Label = [](const ARFCharacter* C, bool bYou) {
					if (bYou) return FString(TEXT("You"));
					const FString Side = C->FactionId == TEXT("british") ? TEXT("Regular") : TEXT("Provincial");
					return C->UnitClass == TEXT("militia") ? FString::Printf(TEXT("%s militia"), *Side) : Side;
				};
				PushKillFeed(FString::Printf(TEXT("%s  x  %s"),
					*Label(KillerChar, Killer->IsLocalPlayerController()),
					*Label(Victim, VictimController && VictimController->IsLocalPlayerController())));
				if (VictimController && VictimController->IsLocalPlayerController())
				{
					LastKillerLabel = Label(KillerChar, false);
				}
			}
		}
		else if (VictimController && VictimController->IsLocalPlayerController())
		{
			LastKillerLabel = TEXT("Unknown");
		}
	}
	CheckTicketExhaustion();
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		const ARFCharacter* PlayerPawn = Cast<ARFCharacter>(PC->GetPawn());
		const bool bPlayerDown = bAutoplayBots || !PlayerPawn || PlayerPawn->IsDead();
		// Own corpse stays the death-cam target. Retarget only when a watched soldier dies.
		if (bPlayerDown && PC->GetViewTarget() == Victim && PC->GetPawn() != Victim)
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
		BeginDeathCamThenSpawn(Cast<APlayerController>(VictimController));
		return;
	}
	TWeakObjectPtr<AController> OldCtl = VictimController;
	const FName Cls = Victim->UnitClass;
	const uint32 Clothes = Victim->GetClothingSeed();
	float Delay = RespawnDelay;
	{
		const auto Sit = Setup.sides.find(TCHAR_TO_UTF8(*F.ToString()));
		if (Sit != Setup.sides.end() && Sit->second.reinforcement_scale > 0.f)
		{
			Delay = FMath::Max(3.f, RespawnDelay * Sit->second.reinforcement_scale);
		}
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, OldCtl, F, Cls, Clothes]() {
		if (bBattleOver) return;
		if (AController* C = OldCtl.Get())
		{
			ControllerFaction.Remove(C);
			ControllerClass.Remove(C);
			C->Destroy();
		}
		SpawnBot(F, ChooseClassFor(F, Cls), nullptr, Clothes);
		CheckTicketExhaustion();
	}), Delay, false);
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
			const UPA250SmokeSubsystem* Smoke = GetWorld() ? GetWorld()->GetSubsystem<UPA250SmokeSubsystem>() : nullptr;
			const FString Json = FString::Printf(
				TEXT("{\"avg_fps\":%.2f,\"frames\":%d,\"seconds\":%.3f,\"pass\":%s,\"bots_per_side\":%d,\"soldiers_alive\":%d,\"puffs\":%d,\"gi_method\":%d,\"reflection_method\":%d,\"time_dilation\":%.1f,\"game_ms\":%.2f,\"render_ms\":%.2f,\"gpu_ms\":%.2f,\"res\":\"%dx%d\"}\n"),
				AvgFps, FpsSampleFrames, FpsSampleAccum, AvgFps >= 30.f ? TEXT("true") : TEXT("false"),
				BotsPerSide, Soldiers, Smoke ? Smoke->NumPuffs() : 0, GI ? GI->GetInt() : -1, Refl ? Refl->GetInt() : -1,
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
		if (bAutoplayBots)
		{
			// Headless runs: log squad spacing / engagement ranges so formation regressions show in the trace.
			SpacingReportTimer -= DeltaSeconds;
			if (SpacingReportTimer <= 0.f)
			{
				SpacingReportTimer = 15.f;
				Volleys->PrintSpacingReport();
			}
		}
	}
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (WP.IsValid() && WP->ConsumeFlipFlag())
		{
			++PostFlips;
		}
	}
	BleedTickets(DeltaSeconds);
	TickMuster(DeltaSeconds);
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		if (CountAlive(F, TEXT("line_infantry")) + CountAlive(F, TEXT("militia")) > 0)
		{
			EverFielded.FindOrAdd(F) = true;
		}
	}
	CheckTicketExhaustion();
	if (SpawnScreen && SpawnScreen->IsInViewport() && !CanSpawn(PlayerFaction))
	{
		CloseSpawnScreenToSpectate(SpawnScreen->GetOwningPlayer());
	}
	if (bBattleOver)
	{
		return;
	}
	if (TimeRemaining() <= 0.f)
	{
		const int32 TB = PoolTotal(TEXT("british"));
		const int32 TC = PoolTotal(TEXT("continental"));
		FName Winner;
		if (ScenarioFight.bRewardForcePreservation)
		{
			Winner = PreservationWinner(TB, InitialSoldiers(TEXT("british")), TC, InitialSoldiers(TEXT("continental")),
				TEXT("british"), TEXT("continental"), ToName(Setup.defender_faction));
			EndBattle(Winner, TEXT("force_preserved"));
		}
		else
		{
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
	for (const auto& FK : Ledger.Deaths)
	{
		for (const auto& CK : FK.Value)
		{
			const int32 Lost = FPA250TicketLedger::LossesInUnits(CK.Value, SoldiersPerUnit,
				InitialPool.FindRef(FK.Key).FindRef(CK.Key));
			if (Lost > 0)
			{
				Result.losses[ToStd(FK.Key)][ToStd(CK.Key)] = Lost;
			}
			// Rough wounded estimate: half of kills as wounded (gameplay report, not historical).
			const int32 Wounded = Lost / 2;
			if (Wounded > 0)
			{
				Result.wounded[ToStd(FK.Key)][ToStd(CK.Key)] = Wounded;
			}
		}
	}
	// Cartridges spent: clamp to province stock; prefer side budgets from setup.
	const int32 ProvinceCart = Setup.province_cartridges;
	int32 Remaining = ProvinceCart;
	for (const auto& Side : Setup.sides)
	{
		const int32 Fielded = [&]() {
			int32 N = 0;
			for (const auto& T : Side.second.tickets) N += std::max(0, T.second);
			return N;
		}();
		int32 Spend = Fielded * 2;
		if (Side.second.cartridge_budget > 0) Spend = FMath::Min(Spend, Side.second.cartridge_budget);
		Spend = FMath::Clamp(Spend, 0, Remaining);
		Remaining -= Spend;
		Result.cartridges_spent[Side.first] = Spend;
	}
	++BattleResultEmitCount;
	const std::string Loser = Winner == TEXT("british") ? "continental" : "british";
	auto It = Setup.sides.find(Loser);
	if (It != Setup.sides.end())
	{
		Result.retreat_province = It->second.retreat_province;
	}
	Banner = FString::Printf(TEXT("%s victory (%s)"), *Winner.ToString().ToUpper(), *Reason);
	UE_LOG(LogTemp, Display, TEXT("PA250 battle over: %s flips=%d bleed=%d"), *Banner, PostFlips, Ledger.BleedTicks);
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
		GetWorld()->GetTimeSeconds() - StartTime, BotsPerSide, SoldiersPerUnit, PostFlips, Ledger.BleedTicks);
	Json += PostTrace;
	Json += TEXT(",\"reload_by_training\":");
	Json += ReloadByTrainingJson();
	Json += FString::Printf(TEXT(",\"muster_arrivals\":{\"british\":%d,\"continental\":%d}"),
		MusterArrivals.FindRef(TEXT("british")), MusterArrivals.FindRef(TEXT("continental")));
	Json += TEXT(",\"morale\":");
	Json += Volleys ? Volleys->MoraleTraceJson() : FString(TEXT("{}"));
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
