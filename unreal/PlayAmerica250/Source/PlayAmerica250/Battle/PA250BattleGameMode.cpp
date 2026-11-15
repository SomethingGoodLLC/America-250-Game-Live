#include "PA250BattleGameMode.h"
#include "Battle/PA250AnimClips.h"
#include "Battle/PA250BattleFeel.h"
#include "RF/PA250SoldierAnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Campaign/PA250Autoplay.h"
#include "Campaign/PA250Subsystem.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Engine/TargetPoint.h"
#include <algorithm>
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
#include "PA250Intensity.h"
#include "PA250BattleLook.h"
#include "PA250UnitClasses.h"
#include "PA250VolleyCoordinator.h"
#include "PA250FieldWork.h"
#include "PA250Support.h"
#include "PA250FieldGun.h"
#include "PA250BattleModes.h"
#include "PA250RowBoat.h"
#include "Engine/TargetPoint.h"
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
	{TEXT("Shot_WallE"), TEXT("wall_east"), FVector(13547.f, 10769.f, 0.f), FRotator::ZeroRotator, 55.f},
	// Behind Patriot wall W1, looking over it at flag B on the Green.
	{TEXT("Shot_WallPatriot"), TEXT("wall_patriot"), FVector(-4555.f, -5053.f, 0.f), FRotator::ZeroRotator, 60.f},
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
	if (Shooter && !Shooter->FactionId.IsNone())
	{
		ShotsFiredByFaction.FindOrAdd(Shooter->FactionId)++;
	}
	if (Volleys)
	{
		Volleys->NotifyShot(Shooter, Dir, bHit);
		// The player's own shot fires every man of his file who is presented.
		if (Shooter && Shooter->IsPlayerControlled()) Volleys->LeaderFired(Shooter);
	}
}

void APA250BattleGameMode::RecordMisfire(FName Faction)
{
	++Misfires;
	if (!Faction.IsNone())
	{
		MisfiresByFaction.FindOrAdd(Faction)++;
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

FPA250ScenarioFight APA250BattleGameMode::ResolveScenario(const FString& Preset, const TArray<FString>& TerrainTags, const FString& Mode)
{
	FPA250ScenarioFight Fight;
	const bool bBridge = Preset == TEXT("concord_bridge") || TerrainTags.Contains(TEXT("bridge_crossing"));
	const bool bRoad = Preset == TEXT("battle_road") || TerrainTags.Contains(TEXT("return_march"));
	// The preset wins when a stray tag disagrees. A crossing fight is not also a preservation fight.
	Fight.bContestSingleCrossing = bBridge && Preset != TEXT("battle_road");
	Fight.bRewardForcePreservation = bRoad && !Fight.bContestSingleCrossing;
	// The scripted fights keep their own rules whatever mode a stray setup names.
	const bool bScripted = Fight.bContestSingleCrossing || Fight.bRewardForcePreservation;
	Fight.Mode = (!bScripted && (Mode == TEXT("assault") || Mode == TEXT("landing"))) ? FName(*Mode) : FName(TEXT("conquest"));
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

void APA250BattleGameMode::ConcordBanks(FName TownHolder, FName& East, FName& West)
{
	// The level bakes the village bank (east of the river) under the british tag. That bank starts
	// beside the bridge and already holds the far bridgehead, so a side that begins there takes
	// the crossing. Whoever owns the town stands there; the other side comes on from the west.
	East = (TownHolder == TEXT("british") || TownHolder == TEXT("continental")) ? TownHolder : FName(TEXT("british"));
	West = East == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));
}

void APA250BattleGameMode::AssignConcordGround()
{
	if (FString(UTF8_TO_TCHAR(Setup.scenario_preset.c_str())) != TEXT("concord_bridge"))
	{
		return;
	}
	FName Holder = ToName(Setup.defender_faction);
	for (const auto& KV : Setup.sides)
	{
		if (!Setup.province_id.empty() && KV.second.magazine_province == Setup.province_id)
		{
			Holder = ToName(KV.first);
			break;
		}
	}
	FName East, West;
	ConcordBanks(Holder, East, West);
	if (East != TEXT("british"))
	{
		// The east-bank markers are tagged british and the west-bank markers continental.
		auto SwapSide = [](TMap<FName, TArray<TWeakObjectPtr<AActor>>>& Map) {
			TArray<TWeakObjectPtr<AActor>> EastMarks = Map.FindRef(TEXT("british"));
			TArray<TWeakObjectPtr<AActor>> WestMarks = Map.FindRef(TEXT("continental"));
			Map.Add(TEXT("continental"), EastMarks);
			Map.Add(TEXT("british"), WestMarks);
		};
		SwapSide(Starts);
		SwapSide(MusterPoints);
	}
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		APA250ControlPoint* P = WP.Get();
		if (!P)
		{
			continue;
		}
		if (P->PostId == TEXT("buckman"))
		{
			P->DebugSetHolder(East);
		}
		else if (P->PostId == TEXT("meetinghouse"))
		{
			P->DebugSetHolder(West);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 concord ground: %s holds the town and takes the east bank; %s starts on the west"),
		*East.ToString(), *West.ToString());
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
	// Battlefield kit (Scripts/import_battlefield_kit.py): the North Bridge model, river water and
	// its wet margin. When they are present the six gameplay boxes below keep their size, tags and
	// collision but are not drawn; the kit pieces are drawn instead and never collide.
	UStaticMesh* BridgeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/PA250/Battlefield/Meshes/SM_NorthBridge.SM_NorthBridge"));
	UMaterialInterface* WaterMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Battlefield/Materials/M_Water.M_Water"));
	UMaterialInterface* BankMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Battlefield/Materials/M_CreekBank.M_CreekBank"));
	const bool bKitRiver = WaterMat && BankMat;
	auto Place = [&](const FVector& Center, const FVector& SizeCm, const FLinearColor& Color, bool bBlock, const TCHAR* Tag,
		bool bDraw = true, UMaterialInterface* Override = nullptr) -> bool {
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
		if (Override)
		{
			Mesh->SetMaterial(0, Override);
		}
		else if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
			Mesh->SetMaterial(0, Mid);
		}
		if (!bDraw)
		{
			Mesh->SetVisibility(false);
			Mesh->SetCastShadow(false);
		}
		if (Override)
		{
			Mesh->SetCastShadow(false);  // flat ground dressing
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
	N += Place(Span.CreekCenter, FVector(Creek.X, Deck.Y, 30.f), Water, false, TEXT("PA250Creek"), !bKitRiver) ? 1 : 0;
	N += Place(Span.CreekCenter + FVector(0.f, BankOff, 10.f), FVector(Creek.X, BankLen, 90.f), Water, true, TEXT("PA250Creek"), !bKitRiver) ? 1 : 0;
	N += Place(Span.CreekCenter + FVector(0.f, -BankOff, 10.f), FVector(Creek.X, BankLen, 90.f), Water, true, TEXT("PA250Creek"), !bKitRiver) ? 1 : 0;
	N += Place(Span.DeckCenter, Deck, Plank, true, TEXT("PA250Bridge"), !BridgeMesh) ? 1 : 0;
	const float RailZ = Span.DeckCenter.Z + Deck.Z * 0.5f + 30.f;
	N += Place(FVector(Span.DeckCenter.X, Span.DeckCenter.Y + Deck.Y * 0.5f, RailZ), FVector(Deck.X, 14.f, 70.f), Rail, false, TEXT("PA250Bridge"), !BridgeMesh) ? 1 : 0;
	N += Place(FVector(Span.DeckCenter.X, Span.DeckCenter.Y - Deck.Y * 0.5f, RailZ), FVector(Deck.X, 14.f, 70.f), Rail, false, TEXT("PA250Bridge"), !BridgeMesh) ? 1 : 0;
	// Drawn pieces (not counted, never blocking). The ground point is the deck's centre at ground level.
	const FVector Ground = Span.DeckCenter - FVector(0.f, 0.f, 14.f);
	int32 Drawn = 0;
	if (bKitRiver)
	{
		// The river lies bank-full in its meadow, as the Concord does in April: a wet margin, then water.
		// Tops 1 cm (margin) and 2 cm (water) above the levelled Common: no lip to catch the low sun.
		Drawn += Place(Ground + FVector(0.f, 0.f, -19.f), FVector(Creek.X + 110.f, Creek.Y + 110.f, 40.f), Water, false, TEXT("PA250CreekVisual"), true, BankMat) ? 1 : 0;
		Drawn += Place(Ground + FVector(0.f, 0.f, -18.f), FVector(Creek.X, Creek.Y, 40.f), Water, false, TEXT("PA250CreekVisual"), true, WaterMat) ? 1 : 0;
	}
	if (BridgeMesh)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Ground, FRotator::ZeroRotator, Params))
		{
			A->Tags.Add(TEXT("PA250BridgeVisual"));
			UStaticMeshComponent* Mesh = A->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(BridgeMesh);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			++Drawn;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 creek bridge: kit visuals %d (bridge=%d river=%d)"), Drawn, BridgeMesh ? 1 : 0, bKitRiver ? 1 : 0);
	UE_LOG(LogTemp, Display, TEXT("PA250 creek bridge: %d pieces, deck %.0f cm across a %.0f cm creek"),
		N, Deck.X, Creek.X);
	return N;
}

int32 APA250BattleGameMode::SealLevelRiver(UWorld* World)
{
	int32 N = 0;
	if (!World)
	{
		return N;
	}
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		UStaticMeshComponent* Mesh = It->ActorHasTag(TEXT("PA250Creek")) ? It->GetStaticMeshComponent() : nullptr;
		if (!Mesh)
		{
			continue;
		}
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		++N;
	}
	return N;
}

void APA250BattleGameMode::ApplyScenarioPreset()
{
	// Before any plank crossing is spawned (its banks carry the same tag and block everything).
	if (const int32 Sealed = SealLevelRiver(GetWorld()))
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 river: %d channel blocks stop men only"), Sealed);
	}
	TArray<FString> TerrainTags;
	TerrainTags.Reserve(static_cast<int32>(Setup.terrain_tags.size()));
	for (const std::string& Tag : Setup.terrain_tags)
	{
		TerrainTags.Add(UTF8_TO_TCHAR(Tag.c_str()));
	}
	const FString Preset = UTF8_TO_TCHAR(Setup.scenario_preset.c_str());
	ScenarioFight = ResolveScenario(Preset, TerrainTags, UTF8_TO_TCHAR(Setup.mode.c_str()));
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		ConfigurePostForScenario(WP.Get(), ScenarioFight);
	}
	// Assault and landing: the ground is the defender's, every post of it, and the posts are taken
	// in the order the attacker comes to them.
	ModeOrder.Reset();
	ModeCarried = 0;
	bLanded = false;
	ModeClockAdded = 0.f;
	if (PA250Modes::IsOrdered(ScenarioFight.Mode))
	{
		const FName Defender = ToName(Setup.defender_faction);
		const FName Attacker = ToName(Setup.attacker_faction);
		TArray<FVector> Where;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			Where.Add(WP.IsValid() ? WP->GetActorLocation() : FVector::ZeroVector);
			if (!WP.IsValid()) continue;
			WP->MarkHoldersAssigned();
			WP->DebugSetHolder(Defender);
		}
		ModeOrder = PA250Modes::AttackOrder(Where, GetRearPoint(Attacker, FVector::ZeroVector));
		if (PA250Modes::IsLanding(ScenarioFight.Mode) && ModeOrder.Num() > 0 && Posts[ModeOrder[0]].IsValid())
		{
			Posts[ModeOrder[0]]->SetObjectiveTitle(TEXT("Landing place"));
		}
		TickBattleMode();
		FString OrderText;
		for (const int32 I : ModeOrder) OrderText += (OrderText.IsEmpty() ? TEXT("") : TEXT(" > ")) + (Posts[I].IsValid() ? Posts[I]->PostId.ToString() : FString(TEXT("?")));
		UE_LOG(LogTemp, Display, TEXT("PA250 mode: %s; %s attack, the posts in order: %s"), *ScenarioFight.Mode.ToString(), *Attacker.ToString(), *OrderText);
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
	// Works for every battle: with no fortification they are only places a sapper can build on.
	SpawnFieldWorks();
	SpawnFieldGuns();
	if (ScenarioFight.bContestSingleCrossing)
	{
		// L_Concord is built with its river and the full-length North Bridge (tagged
		// PA250LevelCrossing by build_levels.py); only a level without one gets the plank crossing.
		bool bLevelCrossing = false;
		for (TActorIterator<AActor> It(GetWorld()); It && !bLevelCrossing; ++It)
		{
			bLevelCrossing = It->ActorHasTag(TEXT("PA250LevelCrossing"));
		}
		if (bLevelCrossing)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 crossing: the level's own river and bridge"));
		}
		else if (APA250ControlPoint* Green = FindGreenPost())
		{
			// The post actor sits above the ground. The creek and the planks are on the ground under it.
			SpawnCreekBridge(GetWorld(), Green->GetActorLocation() + FVector(0.f, 0.f, -140.f));
		}
	}
	AssignConcordGround();
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
		ScenarioFight.bRewardForcePreservation ? 1 : 0, TerrainTags.Num(), Setup.fort_level);
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

void APA250BattleGameMode::SpawnFieldWorks()
{
	UWorld* World = GetWorld();
	if (!World) return;
	const FName Defender = ToName(Setup.defender_faction);
	const FName Attacker = OtherFaction(Defender);
	const auto DefSide = Setup.sides.find(Setup.defender_faction);
	const int32 Level = DefSide != Setup.sides.end() ? DefSide->second.fortification_level : 0;

	// The defender's post: the one the setup gives him (the fort's), else one he holds nearest his
	// own start, else the post nearest his start.
	APA250ControlPoint* Post = nullptr;
	for (const auto& Kv : Setup.initial_post_holders)
	{
		if (Kv.second != Setup.defender_faction) continue;
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
		{
			if (WP.IsValid() && WP->PostId == FName(UTF8_TO_TCHAR(Kv.first.c_str()))) Post = WP.Get();
		}
	}
	const FVector DefRear = GetRearPoint(Defender, FVector::ZeroVector);
	float BestD = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (Post || !WP.IsValid()) continue;
		const float D = FVector::Dist2D(WP->GetActorLocation(), DefRear) + (WP->GetHolder() == Defender ? 0.f : 100000.f);
		if (D < BestD)
		{
			BestD = D;
		}
	}
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (Post || !WP.IsValid()) continue;
		const float D = FVector::Dist2D(WP->GetActorLocation(), DefRear) + (WP->GetHolder() == Defender ? 0.f : 100000.f);
		if (D <= BestD) Post = WP.Get();
	}
	if (!Post) return;
	const FVector PostLoc = Post->GetActorLocation();
	FVector TowardEnemy = (GetRearPoint(Attacker, PostLoc + FVector(10000.f, 0.f, 0.f)) - PostLoc).GetSafeNormal2D();
	if (TowardEnemy.IsNearlyZero()) TowardEnemy = FVector::ForwardVector;

	// Places: marked in the level, else made up on an arc in front of the post.
	TArray<FVector> Places, Fronts;
	TArray<FVector> RedoubtPlaces, RedoubtFronts;
	// A level marks places round every post; the defender's works are the ones about his own.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("PA250WorksSlot"))) continue;
		if (FVector::Dist2D(It->GetActorLocation(), PostLoc) > Rules.works_man_radius_cm) continue;
		const bool bRedoubt = It->ActorHasTag(TEXT("redoubt"));
		(bRedoubt ? RedoubtPlaces : Places).Add(It->GetActorLocation());
		// A breastwork fronts as its marker does (outward from the post); a redoubt fronts the enemy.
		(bRedoubt ? RedoubtFronts : Fronts).Add(bRedoubt ? TowardEnemy : It->GetActorForwardVector().GetSafeNormal2D());
	}
	const bool bMarked = Places.Num() > 0;
	if (!bMarked)
	{
		PA250Works::RingSlots(PostLoc, TowardEnemy, Rules.works_slot_ring_cm, FMath::Max(0, Rules.works_breastwork_slots),
			Rules.works_breastwork_length_cm + 160.f, Places, Fronts);
		UE_LOG(LogTemp, Warning, TEXT("PA250 works: %s has no PA250WorksSlot markers; %d places made up about post %s"), *World->GetMapName(), Places.Num(), *Post->PostId.ToString());
	}
	else
	{
		// The places on the enemy's side of the post first, so the works that stand at the start are
		// the ones between the post and the attack (PA250Works::EnemySideFirst).
		const TArray<int32> Order = PA250Works::EnemySideFirst(Places, PostLoc, TowardEnemy);
		TArray<FVector> P2, F2;
		for (int32 i : Order)
		{
			P2.Add(Places[i]);
			F2.Add(Fronts[i]);
		}
		Places = P2;
		Fronts = F2;
	}
	if (RedoubtPlaces.Num() == 0)
	{
		RedoubtPlaces.Add(PostLoc);
		RedoubtFronts.Add(TowardEnemy);
	}
	auto OnGround = [World, Post](FVector At) {
		FHitResult Hit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PA250Works), false, Post);
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 600.f), At - FVector(0.f, 0.f, 1500.f), ECC_WorldStatic, TraceParams) && Hit.bBlockingHit)
		{
			At.Z = Hit.ImpactPoint.Z;
		}
		return At;
	};
	auto Raise = [&](EPA250WorkKind Kind, const FVector& At, const FVector& Front, float BuiltShare) {
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APA250FieldWork* W = World->SpawnActor<APA250FieldWork>(OnGround(At), FRotator(0.f, Front.Rotation().Yaw, 0.f), Params);
		if (!W) return;
		W->Configure(Kind, Defender, BuiltShare);
		Works.Add(W);
	};
	auto ForLevel = [Level](const std::vector<int>& ByLevel) {
		TArray<int32> A;
		for (const int N : ByLevel) A.Add(N);
		return PA250Works::CountForLevel(A, Level);
	};
	const int32 Standing = ForLevel(Rules.works_breastworks_by_level);
	const int32 Redoubts = FMath::Min(ForLevel(Rules.works_redoubts_by_level), RedoubtPlaces.Num());
	int32 Built = 0;
	for (int32 i = 0; i < Places.Num(); ++i)
	{
		const bool bStands = i < Standing;
		Raise(EPA250WorkKind::Breastwork, Places[i], Fronts[i], bStands ? 1.f : 0.f);
		Built += bStands ? 1 : 0;
	}
	for (int32 i = 0; i < Redoubts; ++i)
	{
		Raise(EPA250WorkKind::Redoubt, RedoubtPlaces[i], RedoubtFronts[i], 1.f);
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 works: fortification L%d of %s: %d breastworks and %d redoubt standing about post %s, %d places a sapper can build on (%s)"),
		Level, *Defender.ToString(), Built, Redoubts, *Post->PostId.ToString(), Places.Num() - Built,
		bMarked ? TEXT("marked places") : TEXT("made-up places"));
}

APA250FieldWork* APA250BattleGameMode::NearestWorkNeedingLabour(const FVector& From, float ReachCm) const
{
	APA250FieldWork* Best = nullptr;
	float BestD = ReachCm;
	for (APA250FieldWork* W : Works)
	{
		if (!W || !W->NeedsWork()) continue;
		const float D = FVector::Dist2D(W->GetWorkPoint(), From);
		if (D <= BestD)
		{
			BestD = D;
			Best = W;
		}
	}
	return Best;
}

void APA250BattleGameMode::SpawnFieldGuns()
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (const auto& Kv : Setup.sides)
	{
		const FName F = ToName(Kv.first);
		const int32 Count = FMath::Max(0, Kv.second.guns);
		if (Count <= 0) continue;
		const FName Enemy = OtherFaction(F);
		// Places: the level's PA250GunSlot markers for this side (tagged with the faction as well),
		// else drawn up in line a little in front of the side's own start, fronting the enemy.
		TArray<FTransform> Slots;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const FName RoleTag = Kv.first == Setup.attacker_faction ? FName(TEXT("PA250Attacker")) : FName(TEXT("PA250Defender"));
			if (It->ActorHasTag(TEXT("PA250GunSlot")) && (It->ActorHasTag(F) || It->ActorHasTag(RoleTag))) Slots.Add(It->GetActorTransform());
		}
		const bool bMarked = Slots.Num() >= Count;
		if (!bMarked)
		{
			Slots.Reset();
			const FVector Start = GetRearPoint(F, FVector::ZeroVector);
			const FVector Across = GetRearPoint(Enemy, Start + FVector(10000.f, 0.f, 0.f)) - Start;
			FVector Front = Across.GetSafeNormal2D();
			if (Front.IsNearlyZero()) Front = FVector::ForwardVector;
			const FVector Right(-Front.Y, Front.X, 0.f);
			// Debug battles: -PA250GunsForward=<metres> stands the made-up battery that far out toward
			// the enemy (an exposed battery, for the matchup where it is overrun).
			float ForwardCm = 900.f;
			if (float Metres = 0.f; bDebugBattle && FParse::Value(FCommandLine::Get(), TEXT("PA250GunsForward="), Metres) && Metres > 0.f) ForwardCm = Metres * 100.f;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector At = Start + Front * ForwardCm + Right * ((i - 0.5f * (Count - 1)) * 700.f);
				Slots.Add(FTransform(Front.Rotation(), At));
			}
			UE_LOG(LogTemp, Warning, TEXT("PA250 guns: %s has no PA250GunSlot markers for %s; %d places made up %.0f m out from that side's start (the two starts are %.0f m apart)"),
				*World->GetMapName(), *F.ToString(), Count, ForwardCm / 100.f, Across.Size2D() / 100.f);
		}
		for (int32 i = 0; i < Count; ++i)
		{
			FVector At = Slots[i].GetLocation();
			FHitResult Hit;
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PA250Guns), false);
			if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 600.f), At - FVector(0.f, 0.f, 1500.f), ECC_WorldStatic, TraceParams)) At.Z = Hit.ImpactPoint.Z;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APA250FieldGun* Gun = World->SpawnActor<APA250FieldGun>(At, FRotator(0.f, Slots[i].Rotator().Yaw, 0.f), Params);
			if (!Gun) continue;
			Gun->Place(F);
			Guns.Add(Gun);
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 guns: %s brings %d field %s (%s)"), *F.ToString(), Count, Count == 1 ? TEXT("gun") : TEXT("guns"),
			bMarked ? TEXT("marked places") : TEXT("made-up places"));
	}
}

APA250FieldGun* APA250BattleGameMode::GunFor(ARFCharacter* Gunner, int32& OutPost) const
{
	OutPost = INDEX_NONE;
	if (!Gunner || Gunner->IsDead()) return nullptr;
	// The place he already has.
	for (APA250FieldGun* Gun : Guns)
	{
		if (!Gun || Gun->GetFaction() != Gunner->FactionId) continue;
		if (const int32 Post = Gun->PostOf(Gunner); Post != INDEX_NONE)
		{
			OutPost = Post;
			return Gun;
		}
	}
	// Else the nearest gun of his side with a place free, which he takes.
	APA250FieldGun* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (APA250FieldGun* Gun : Guns)
	{
		if (!Gun || Gun->GetFaction() != Gunner->FactionId || !Gun->HasFreePost()) continue;
		const float D = FVector::DistSquared2D(Gun->GetActorLocation(), Gunner->GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = Gun;
		}
	}
	if (Best) OutPost = Best->ClaimPost(Gunner);
	return OutPost != INDEX_NONE ? Best : nullptr;
}

int32 APA250BattleGameMode::SpawnGunCrews(FName Faction, int32 MostMen)
{
	const FName CrewClass(UTF8_TO_TCHAR(Rules.guns_crew_class.c_str()));
	int32 Spawned = 0;
	for (APA250FieldGun* Gun : Guns)
	{
		if (!Gun || Gun->GetFaction() != Faction) continue;
		for (int32 i = 0; i < Rules.cannon_crew_men && Spawned < MostMen; ++i)
		{
			if (ClassMenInReserve(Faction, CrewClass) <= 0) break;
			const FVector At = Gun->GetCrewPost(i);
			ARFCharacter* Man = SpawnBotAt(Faction, CrewClass, FTransform(Gun->GetActorRotation(), At));
			if (!Man) break;
			Gun->ClaimPost(Man);
			++Spawned;
		}
	}
	if (Spawned > 0) UE_LOG(LogTemp, Display, TEXT("PA250 guns: %d %s gunners come on at their guns"), Spawned, *Faction.ToString());
	return Spawned;
}

int32 APA250BattleGameMode::EnsureGunCrew(APA250FieldGun* Gun, ARFCharacter* Player)
{
	if (!Gun || !Player) return 0;
	int32 Kept = 0;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() || *It == Player || It->FactionId != Gun->GetFaction()) continue;
		if (FVector::Dist2D(It->GetActorLocation(), Gun->GetActorLocation()) > Rules.cannon_crew_radius_cm) continue;
		if (Gun->PostOf(*It) != INDEX_NONE) continue;
		if (Gun->ClaimPost(*It) != INDEX_NONE) ++Kept;
	}
	Gun->RecountCrew();
	int32 Summoned = 0;
	while (Gun->GetCrew() < Rules.cannon_min_crew && Summoned < Rules.cannon_min_crew)
	{
		const int32 Slot = Gun->FreePostIndex();
		if (Slot == INDEX_NONE) break;
		ARFCharacter* Mate = SummonGunMate(Gun, Slot);
		if (!Mate) break;
		Gun->ClaimPost(Mate);
		++Summoned;
		Gun->RecountCrew();
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 guns: crew %d at the piece (need %d), kept %d, summoned %d"),
		Gun->GetCrew(), Rules.cannon_min_crew, Kept, Summoned);
	return Gun->GetCrew();
}

ARFCharacter* APA250BattleGameMode::SummonGunMate(APA250FieldGun* Gun, int32 Post)
{
	if (!Gun || !GetWorld()) return nullptr;
	const FName CrewClass(UTF8_TO_TCHAR(Rules.guns_crew_class.c_str()));
	const FTransform At(Gun->GetActorRotation(), Gun->GetCrewPost(Post));
	if (ClassMenInReserve(Gun->GetFaction(), CrewClass) > 0)
	{
		if (ARFCharacter* Man = SpawnBotAt(Gun->GetFaction(), CrewClass, At))
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 guns: a %s gunner from the reserve takes a post"), *Gun->GetFaction().ToString());
			return Man;
		}
	}
	// The army has no gunner left. A mate stands the post so the piece can be worked. He is not a
	// ticket and he is not a man of the army (CountAlive skips the tag).
	ARFCharacter* C = GetWorld()->SpawnActorDeferred<ARFCharacter>(ARFCharacter::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!C) return nullptr;
	C->SetClothingSeed(0x6C11u + static_cast<uint32>(Post));
	C->AIControllerClass = APA250BotController::StaticClass();
	C->FinishSpawning(At);
	C->SpawnDefaultController();
	ConfigureSoldier(C, Gun->GetFaction(), CrewClass);
	C->Tags.AddUnique(TEXT("PA250GunMate"));
	if (AController* Ctl = C->GetController())
	{
		ControllerFaction.Add(Ctl, Gun->GetFaction());
		ControllerClass.Add(Ctl, CrewClass);
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 guns: summoned a mate at a %s gun (no gunner left in reserve)"), *Gun->GetFaction().ToString());
	return C;
}

FString APA250BattleGameMode::GeneralFallWords(bool bOurs, const FString& RankName, const FString& Fate, int32 Variant, FString& OutKey)
{
	const bool bKilled = Fate == TEXT("killed");
	const bool bCaptured = Fate == TEXT("captured") || Fate == TEXT("taken");
	const TCHAR* FateWord = bKilled ? TEXT("killed") : (bCaptured ? TEXT("taken") : TEXT("wounded"));
	OutKey = bOurs
		? FString(bKilled ? TEXT("general_killed") : (bCaptured ? TEXT("general_captured") : TEXT("general_wounded")))
		: FString(bKilled ? TEXT("enemy_general_killed") : (bCaptured ? TEXT("enemy_general_captured") : TEXT("enemy_general_wounded")));
	FString Surname = RankName;
	int32 Space = INDEX_NONE;
	if (RankName.FindLastChar(TEXT(' '), Space) && Space + 1 < RankName.Len()) Surname = RankName.Mid(Space + 1);
	const int32 V = Variant & 1;
	if (bOurs)
	{
		return V == 0
			? FString::Printf(TEXT("%s is %s!"), *RankName, FateWord)
			: FString::Printf(TEXT("%s! %s is %s!"), *Surname, *RankName, FateWord);
	}
	return V == 0
		? FString::Printf(TEXT("%s is %s!"), *RankName, FateWord)
		: FString::Printf(TEXT("%s! Their general, %s, is %s!"), *Surname, *RankName, FateWord);
}

int32 APA250BattleGameMode::GeneralFallVariant(uint64 Seed, uint32 FactionSalt)
{
	return static_cast<int32>((Seed + static_cast<uint64>(FactionSalt)) % 2ull);
}

APA250FieldGun* APA250BattleGameMode::GunAt(const ARFCharacter* Soldier) const
{
	if (!Soldier) return nullptr;
	for (APA250FieldGun* Gun : Guns)
	{
		if (Gun && Gun->GetFaction() == Soldier->FactionId
			&& FVector::Dist2D(Gun->GetActorLocation(), Soldier->GetActorLocation()) <= Rules.cannon_crew_radius_cm) return Gun;
	}
	return nullptr;
}

bool APA250BattleGameMode::PlayerFiresGun(ARFCharacter* Soldier)
{
	APA250FieldGun* Gun = GunAt(Soldier);
	if (!Gun || Gun->GetPlayerGunner() != Soldier) return false;
	if (Gun->IsFollowingPlayer())
	{
		ShowNotice(TEXT("The gun is being run up: press 3 to stand it before you fire"), 3.f);
		return true;
	}
	if (!Gun->IsLoaded())
	{
		ShowNotice(TEXT("The gun is not loaded"), 2.f);
		return true;
	}
	// Fire itself says why: too few men, or grape / shell / round too far.
	Gun->PlayerFire(LeaderAim);
	return true;
}

bool APA250BattleGameMode::PlayerWorksGun(ARFCharacter* Soldier, EActiveReloadHit& OutHit)
{
	OutHit = EActiveReloadHit::None;
	APA250FieldGun* Gun = GunAt(Soldier);
	if (!Gun || Gun->GetPlayerGunner() != Soldier) return false;
	OutHit = Gun->PlayerCommit();
	return true;
}

int32 APA250BattleGameMode::GetGunsTakenBy(FName Faction) const
{
	int32 N = 0;
	for (const APA250FieldGun* Gun : Guns) N += (Gun && Gun->IsTaken() && Gun->GetFaction() == Faction) ? 1 : 0;
	return N;
}

void APA250BattleGameMode::NotifyGunTaken(APA250FieldGun* Gun, FName From)
{
	if (!Gun) return;
	++GunCaptures;
	const bool bOurs = Gun->GetFaction() == PlayerFaction;
	PushKillFeed(bOurs ? TEXT("We have taken a gun!") : TEXT("THE ENEMY HAS TAKEN A GUN"));
	// A side that sees its guns go feels it.
	if (Volleys) Volleys->PressSide(From, Rules.cannon_pressure, Rules.cannon_pressure_seconds, TEXT("GunLost"));
}

void APA250BattleGameMode::NotifyGunFired(APA250FieldGun* Gun, EPA250Shot Shot)
{
	if (!Gun) return;
	GunShots.FindOrAdd(Gun->GetFaction())++;
	if (Shot == EPA250Shot::Grape) CanisterShots.FindOrAdd(Gun->GetFaction())++;
}

bool APA250BattleGameMode::PlacePlayerAtGun(ARFCharacter* Soldier)
{
	if (!Soldier || Soldier->IsDead() || !PA250UnitClasses::KitFor(Soldier->FactionId, Soldier->UnitClass).bServesGuns) return false;
	APA250FieldGun* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (APA250FieldGun* Gun : Guns)
	{
		if (!Gun || Gun->GetFaction() != Soldier->FactionId) continue;
		const float D = FVector::DistSquared2D(Gun->GetActorLocation(), Soldier->GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = Gun;
		}
	}
	if (!Best) return false;
	const int32 Post = Best->ForceClaimPost(Soldier);
	if (Post == INDEX_NONE) return false;
	Soldier->TeleportTo(Best->GetCrewPost(Post) + FVector(0.f, 0.f, 10.f), Best->GetActorRotation(), false, true);
	Best->SetPlayerGunner(Soldier);
	const int32 Crew = EnsureGunCrew(Best, Soldier);
	const FString Chip = Best->LoadChip();
	ShowNotice(Crew >= Rules.cannon_min_crew
		? FString::Printf(TEXT("You serve the gun (crew %d). %s"), Crew, *Chip)
		: FString::Printf(TEXT("You serve the gun, but only %d men are at it"), Crew), 6.f);
	UE_LOG(LogTemp, Display, TEXT("PA250 guns: player placed at a %s gun as %s, crew %d"), *Soldier->FactionId.ToString(), *Soldier->UnitClass.ToString(), Crew);
	return true;
}

bool APA250BattleGameMode::PlayerCycleGunShot(ARFCharacter* Soldier, int32 Delta)
{
	APA250FieldGun* Gun = GunAt(Soldier);
	if (!Gun || Gun->GetPlayerGunner() != Soldier) return false;
	Gun->CycleShot(Delta);
	return true;
}

void APA250BattleGameMode::NotifyRoundShot(ARFCharacter* Man, bool bStruck)
{
	(bStruck ? RoundShotStruck : RoundShotKnockedDown)++;
	if (Man) PA250Intensity::RoundShotStrike(GetWorld(), Man->GetActorLocation());
}

void APA250BattleGameMode::ShowDirt(float Seconds)
{
	if (Hud) Hud->ShowDirt(Seconds);
}

void APA250BattleGameMode::TickIntensityProbe()
{
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("PA250IntensityProbe"));
	if (!bProbe || IntensityProbeStep > 3) return;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	const float T = GetWorld()->GetTimeSeconds() - StartTime;
	if (!Me || Me->IsDead()) return;
	if (IntensityProbeStep == 0 && T > 12.f)
	{
		++IntensityProbeStep;
		FVector Ear = Me->GetActorLocation();
		FPA250BattleAudio::GetListener(this, Ear);
		const float FovBefore = Me->GetCurrentFOV();
		const bool bCounted = PA250Intensity::NearMiss(Me, Ear + Me->GetActorForwardVector() * 3000.f, Ear + Me->GetActorRightVector() * 100.f, Ear);
		UE_LOG(LogTemp, Display, TEXT("PA250 intensity probe: near miss counted=%d fov %.1f -> %.1f shakes=%d music layers=%d missing=%s"), bCounted ? 1 : 0, FovBefore,
			Me->GetCurrentFOV(), PA250Intensity::ShakeCount(), Music.IsValid() ? Music->NumPlaying() : 0, Music.IsValid() ? *FString::Join(Music->GetMissing(), TEXT(",")) : TEXT("-"));
	}
	else if (IntensityProbeStep == 1 && T > 14.f)
	{
		++IntensityProbeStep;
		const int32 Shakes = PA250Intensity::ShakeCount();
		PA250Intensity::RoundShotStrike(GetWorld(), Me->GetActorLocation() + Me->GetActorForwardVector() * 300.f);
		PA250Intensity::RoundShotStrike(GetWorld(), Me->GetActorLocation() + Me->GetActorForwardVector() * 20000.f);
		UE_LOG(LogTemp, Display, TEXT("PA250 intensity probe: round shot 3 m off and 200 m off: shakes +%d dirt=%d hud dirt alpha=%.2f"), PA250Intensity::ShakeCount() - Shakes,
			PA250Intensity::DirtCount(), Hud ? Hud->GetDirtAlpha() : -1.f);
	}
	else if (IntensityProbeStep == 2 && T > 14.4f)
	{
		++IntensityProbeStep;
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview/intensity_dirt.png"))), true, false);
		// Three men near him are shot down, so the fallen can be looked at once they have settled.
		TArray<ARFCharacter*> Near;
		for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != Me && !It->IsDead() && !IsGeneral(*It) && FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < 1500.f) Near.Add(*It);
		}
		for (int32 i = 0; i < FMath::Min(3, Near.Num()); ++i) UGameplayStatics::ApplyDamage(Near[i], 100000.f, nullptr, nullptr, UDamageType::StaticClass());
		if (PC) PC->SetControlRotation(FRotator(-25.f, PC->GetControlRotation().Yaw + 180.f, 0.f));
	}
	else if (IntensityProbeStep == 3 && T > 24.f)
	{
		++IntensityProbeStep;
		int32 Fallen = 0, Frozen = 0;
		for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsDead()) continue;
			++Fallen;
			Frozen += It->IsCorpseFrozen() ? 1 : 0;
		}
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview/intensity_fallen.png"))), true, false);
		UE_LOG(LogTemp, Display, TEXT("PA250 intensity probe: fallen on the field %d (budget %d), frozen %d; shouts charge=%d rout=%d officer_down=%d"), Fallen, ARFCharacter::GetMaxCorpses(), Frozen,
			Volleys ? Volleys->ShoutEventCount(TEXT("charge")) : 0, Volleys ? Volleys->ShoutEventCount(TEXT("rout")) : 0, Volleys ? Volleys->ShoutEventCount(TEXT("officer_down")) : 0);
		FTimerHandle Quit;
		GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateLambda([]() { FPlatformMisc::RequestExit(false); }), 1.5f, false);
	}
}

void APA250BattleGameMode::TickMusic(float DeltaSeconds)
{
	TickIntensityProbe();
	if (!Music.IsValid() || Music->NumPlaying() == 0 || !Volleys) return;
	MusicUpdateTimer -= DeltaSeconds;
	if (MusicUpdateTimer > 0.f) return;
	MusicUpdateTimer = 1.f;
	int32 Initial = 0;
	if (const TMap<FName, int32>* Pool = InitialPool.Find(PlayerFaction))
	{
		for (const auto& KV : *Pool) Initial += KV.Value;
	}
	const float Reserve = Initial > 0 ? float(TicketsRemaining(PlayerFaction)) / Initial : 1.f;
	Music->Update(Volleys->SteadyShare(PlayerFaction), Reserve);
}

void APA250BattleGameMode::NotifyShove(ARFCharacter* By, ARFCharacter* Victim)
{
	++Shoves;
}

void APA250BattleGameMode::NotifyWorkBattered(APA250FieldWork* Work, bool bBroken, FName ByFaction)
{
	if (!Work || !bBroken) return;
	WorksBroken.FindOrAdd(ByFaction)++;
	NotifyWorksChanged();
	PushKillFeed(ByFaction == PlayerFaction ? TEXT("Our gun has battered down a breastwork") : TEXT("Their gun has battered down one of our works"));
	UE_LOG(LogTemp, Display, TEXT("PA250 works: a %s breastwork is battered below cover by %s round shot (%d so far)"), *Work->GetFaction().ToString(), *ByFaction.ToString(),
		WorksBroken.FindRef(ByFaction));
}

void APA250BattleGameMode::MaybeRaiseColours(ARFCharacter* Soldier)
{
	if (!Soldier || Soldier->IsDead() || !GetWorld()) return;
	const FName F = Soldier->FactionId;
	if (!PA250UnitClasses::KitFor(F, Soldier->UnitClass).bCarriesColours) return;
	APA250Colours* Have = Colours.FindRef(F);
	if (Have && Have->GetState() != EPA250ColoursState::Taken)
	{
		// Colours already up (or lying on the field for him to recover): no second stand.
		return;
	}
	// A side has as many colours as its army brought ensign units, less those it has lost.
	const int32 Brought = InitialPool.FindRef(F).FindRef(Soldier->UnitClass);
	if (ColoursLost.FindRef(F) >= Brought) return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APA250Colours* Flag = GetWorld()->SpawnActor<APA250Colours>(Soldier->GetActorLocation(), Soldier->GetActorRotation(), Params);
	if (!Flag) return;
	Flag->Raise(F, Soldier);
	Colours.Add(F, Flag);
	UE_LOG(LogTemp, Display, TEXT("PA250 colours: the %s colours take the field with %s"), *F.ToString(), *Soldier->GetName());
}

APA250Colours* APA250BattleGameMode::FallenColoursNear(const FVector& From, float SeekCm) const
{
	APA250Colours* Best = nullptr;
	float BestD = SeekCm;
	for (const auto& Kv : Colours)
	{
		APA250Colours* Flag = Kv.Value;
		if (!Flag || Flag->GetState() != EPA250ColoursState::Fallen) continue;
		const float D = FVector::Dist2D(Flag->GetActorLocation(), From);
		if (D <= BestD)
		{
			BestD = D;
			Best = Flag;
		}
	}
	return Best;
}

int32 APA250BattleGameMode::NearestSavable(FName Faction, const FVector& From, float SeekCm) const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Window = PA250UnitClasses::Verbs().SurgeonWindowSeconds;
	int32 Best = INDEX_NONE;
	float BestD = SeekCm;
	for (int32 i = 0; i < FallenMen.Num(); ++i)
	{
		const FFallenMan& M = FallenMen[i];
		if (M.bSaved || M.Faction != Faction || !PA250Support::CanStillBeSaved(M.FellAt, Now, Window)) continue;
		const float D = FVector::Dist2D(M.Where, From);
		if (D <= BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	return Best;
}

void APA250BattleGameMode::TreatFallen(int32 Index, float Seconds, const ARFCharacter* Surgeon)
{
	if (!FallenMen.IsValidIndex(Index) || !Surgeon || Seconds <= 0.f) return;
	FFallenMan& M = FallenMen[Index];
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	if (M.bSaved || !PA250Support::CanStillBeSaved(M.FellAt, GetWorld()->GetTimeSeconds(), Verbs.SurgeonWindowSeconds)) return;
	M.Treated += Seconds;
	if (M.Treated < Verbs.SurgeonTreatSeconds) return;
	M.bSaved = true;
	SavedBySurgeon.FindOrAdd(M.Faction).FindOrAdd(M.UnitClass)++;
	if (M.Faction == PlayerFaction) PushKillFeed(TEXT("The surgeon has reached a fallen man: he will live"));
	UE_LOG(LogTemp, Display, TEXT("PA250 surgeon: a %s %s is carried off wounded, not left for dead (%d saved by that side)"), *M.Faction.ToString(), *M.UnitClass.ToString(),
		GetSavedBySurgeon(M.Faction));
}

int32 APA250BattleGameMode::GetSavedBySurgeon(FName Faction) const
{
	int32 N = 0;
	if (const TMap<FName, int32>* Side = SavedBySurgeon.Find(Faction))
	{
		for (const auto& CK : *Side) N += CK.Value;
	}
	return N;
}

void APA250BattleGameMode::TickSupport(float DeltaSeconds)
{
	SupportTimer += DeltaSeconds;
	if (SupportTimer < 0.25f) return;
	const float Step = SupportTimer;
	SupportTimer = 0.f;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Reach = Verbs.ColoursPickupM * 100.f;
	for (auto& Kv : Colours)
	{
		const FName F = Kv.Key;
		APA250Colours* Flag = Kv.Value;
		if (!Flag) continue;
		if (Volleys) Volleys->SetColours(F, Flag->GetState() == EPA250ColoursState::Carried, Flag->GetActorLocation());
		if (Flag->GetState() != EPA250ColoursState::Fallen) continue;
		// On the ground: a friend who reaches them first takes them up; an enemy who stands over
		// them for the capture time carries them off.
		ARFCharacter* Friend = nullptr;
		ARFCharacter* Foe = nullptr;
		for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsDead() || It->IsMounted() || It->FactionId.IsNone()) continue;
			if (FVector::Dist2D(It->GetActorLocation(), Flag->GetActorLocation()) > Reach) continue;
			(It->FactionId == F ? Friend : Foe) = *It;
		}
		if (Friend && !Foe)
		{
			Flag->TakeUp(Friend);
			PushKillFeed(F == PlayerFaction ? TEXT("Our colours are up again") : TEXT("Their colours are up again"));
			UE_LOG(LogTemp, Display, TEXT("PA250 colours: the %s colours are taken up again by %s"), *F.ToString(), *Friend->GetName());
			continue;
		}
		Flag->EnemyOverSeconds = (Foe && !Friend) ? Flag->EnemyOverSeconds + Step : 0.f;
		if (Foe && Flag->EnemyOverSeconds >= Verbs.ColoursCaptureSeconds)
		{
			Flag->CarryOff();
			ColoursLost.FindOrAdd(F)++;
			ColoursTaken.FindOrAdd(Foe->FactionId)++;
			if (Volleys && F == PlayerFaction) Volleys->ShoutEvent(TEXT("colours_taken"), F, Flag->GetActorLocation());
			if (Volleys)
			{
				Volleys->SetColours(F, false, FVector::ZeroVector);
				Volleys->PressSide(F, Verbs.ColoursFallPressure, Verbs.ColoursFallSeconds, TEXT("ColoursLost"));
			}
			PushKillFeed(F == PlayerFaction ? TEXT("THE ENEMY HAS OUR COLOURS") : TEXT("We have taken their colours!"));
			UE_LOG(LogTemp, Display, TEXT("PA250 colours: the %s colours are carried off by the %s; morale check on the side that lost them"), *F.ToString(), *Foe->FactionId.ToString());
		}
	}
}

bool APA250BattleGameMode::SideHoldsGroundAt(FName Faction, const FVector& Where) const
{
	const APA250ControlPoint* Nearest = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (!WP.IsValid()) continue;
		const float D = FVector::DistSquared2D(WP->GetActorLocation(), Where);
		if (D < BestD)
		{
			BestD = D;
			Nearest = WP.Get();
		}
	}
	return Nearest && Nearest->GetHolder() == Faction;
}

void APA250BattleGameMode::LabourOnWork(APA250FieldWork* Work, float Seconds, const ARFCharacter* Sapper)
{
	if (!Work || !Sapper || Seconds <= 0.f) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	// A breastwork is the measure; a redoubt is as many breastworks' labour as it is stronger.
	const float Strength = Work->GetKind() == EPA250WorkKind::Redoubt
		? FMath::Max(1.f, Rules.works_redoubt_health / FMath::Max(1.f, Rules.works_breastwork_health)) : 1.f;
	const bool bCoverBefore = Work->GivesCover();
	Work->AddWork(Seconds / (FMath::Max(1.f, Verbs.SapperBuildSeconds) * Strength), Sapper->FactionId);
	if (!bCoverBefore && Work->GivesCover())
	{
		WorksRaised.FindOrAdd(Sapper->FactionId)++;
		NotifyWorksChanged();
		PushKillFeed(Sapper->FactionId == PlayerFaction ? TEXT("Our sappers have raised a breastwork") : TEXT("Their pioneers have raised a breastwork"));
		UE_LOG(LogTemp, Display, TEXT("PA250 works: a %s sapper raised a %s to cover (%d by that side so far)"), *Sapper->FactionId.ToString(),
			Work->GetKind() == EPA250WorkKind::Redoubt ? TEXT("redoubt") : TEXT("breastwork"), WorksRaised.FindRef(Sapper->FactionId));
	}
}

void APA250BattleGameMode::NotifyWorksChanged()
{
	if (Volleys) Volleys->RefreshCoverAnchors();
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
	if (bDebugBattle && Setup.valid)
	{
		// A level opened directly fights the scenario it was built for (the debug campaign's first
		// contact is at Concord, which used to put the bridge fight on whatever map was open).
		// -PA250Scenario=<preset> overrides. Only this local copy of the setup changes.
		FString Want = GetWorld()->GetMapName().Contains(TEXT("L_Concord")) ? TEXT("concord_bridge") : TEXT("lexington_common");
		FParse::Value(FCommandLine::Get(), TEXT("PA250Scenario="), Want);
		if (Want != UTF8_TO_TCHAR(Setup.scenario_preset.c_str()))
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 debug battle: scenario %s on %s (the campaign's contact was %s)"),
				*Want, *GetWorld()->GetMapName(), UTF8_TO_TCHAR(Setup.scenario_preset.c_str()));
			Setup.scenario_preset = TCHAR_TO_UTF8(*Want);
			Setup.terrain_tags.erase(std::remove_if(Setup.terrain_tags.begin(), Setup.terrain_tags.end(),
				[](const std::string& T) { return T == "bridge_crossing" || T == "return_march"; }), Setup.terrain_tags.end());
			if (Want == TEXT("concord_bridge")) Setup.terrain_tags.push_back("bridge_crossing");
			if (Want == TEXT("battle_road")) Setup.terrain_tags.push_back("return_march");
		}
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

	Rules = pa250::battle_rules();
	// One source for soldiers per unit: the sim's battle_rules.json, carried by the setup.
	if (Setup.valid && Setup.soldiers_per_unit > 0) SoldiersPerUnit = Setup.soldiers_per_unit;
	int32 Cli = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250Bots="), Cli) && Cli >= 0)
	{
		BotsPerSide = Cli;
		bBotsOverride = true; // a fixed count a side, whatever the campaign says
	}
	float F = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250BattleTime="), F) && F > 0.f) TimeLimitSeconds = F;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250SoldiersPerUnit="), Cli) && Cli >= 1) SoldiersPerUnit = Cli;
	SoldiersPerUnit = FMath::Max(1, SoldiersPerUnit);

	PlayerFaction = ToName(Setup.player_faction);
	// Debug battles only (launched straight into the map): -PA250ExtraUnits=continental:dragoon:2,british:dragoon:2
	// adds units to this fight's tickets. Never a campaign battle, so no campaign state is touched.
	if (FString Extra; bDebugBattle && FParse::Value(FCommandLine::Get(), TEXT("PA250ExtraUnits="), Extra, /*bShouldStopOnSeparator=*/false))
	{
		TArray<FString> Items;
		Extra.ParseIntoArray(Items, TEXT(","));
		for (const FString& Item : Items)
		{
			TArray<FString> Parts;
			Item.ParseIntoArray(Parts, TEXT(":"));
			const auto SideIt = Parts.Num() == 3 ? Setup.sides.find(TCHAR_TO_UTF8(*Parts[0])) : Setup.sides.end();
			if (SideIt == Setup.sides.end() || FCString::Atoi(*Parts[2]) <= 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("PA250 battle: -PA250ExtraUnits item '%s' ignored (side:class:units)"), *Item);
				continue;
			}
			SideIt->second.tickets[TCHAR_TO_UTF8(*Parts[1])] += FCString::Atoi(*Parts[2]);
			UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug extra units %s %s +%s"), *Parts[0], *Parts[1], *Parts[2]);
		}
	}
	if (bDebugBattle && Setup.valid)
	{
		// Debug battles only: -PA250Morale=continental:15,british:80 sets a side's campaign morale and
		// -PA250Rounds=6 the cartridges in every box. Only this local copy of the setup changes.
		if (FString Arg; FParse::Value(FCommandLine::Get(), TEXT("PA250Morale="), Arg, /*bShouldStopOnSeparator=*/false))
		{
			TArray<FString> Items;
			Arg.ParseIntoArray(Items, TEXT(","));
			for (const FString& Item : Items)
			{
				FString Side, Value;
				const auto SideIt = Item.Split(TEXT(":"), &Side, &Value) ? Setup.sides.find(TCHAR_TO_UTF8(*Side)) : Setup.sides.end();
				if (SideIt == Setup.sides.end()) continue;
				SideIt->second.morale = FMath::Clamp(FCString::Atoi(*Value), 0, 100);
				UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug morale %s = %d"), *Side, SideIt->second.morale);
			}
		}
		// -PA250Mode=assault|landing|conquest: the kind of fight, whatever the campaign would choose.
		// -PA250Attacker=british|continental: which side attacks (the other defends).
		if (FString Arg; FParse::Value(FCommandLine::Get(), TEXT("PA250Attacker="), Arg) && Setup.sides.count(TCHAR_TO_UTF8(*Arg)))
		{
			Setup.attacker_faction = TCHAR_TO_UTF8(*Arg);
			Setup.defender_faction = Arg == TEXT("british") ? "continental" : "british";
			UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug attacker %s"), *Arg);
		}
		if (FString Arg; FParse::Value(FCommandLine::Get(), TEXT("PA250Mode="), Arg) && (Arg == TEXT("assault") || Arg == TEXT("landing") || Arg == TEXT("conquest")))
		{
			Setup.mode = TCHAR_TO_UTF8(*Arg);
			UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug mode %s"), *Arg);
		}
		// -PA250Guns=british:1,continental:2: field guns for this fight, whatever the armies hold.
		if (FString Arg; FParse::Value(FCommandLine::Get(), TEXT("PA250Guns="), Arg, /*bShouldStopOnSeparator=*/false))
		{
			TArray<FString> Items;
			Arg.ParseIntoArray(Items, TEXT(","));
			for (const FString& Item : Items)
			{
				FString Side, Value;
				const auto SideIt = Item.Split(TEXT(":"), &Side, &Value) ? Setup.sides.find(TCHAR_TO_UTF8(*Side)) : Setup.sides.end();
				if (SideIt == Setup.sides.end()) continue;
				SideIt->second.guns = FMath::Clamp(FCString::Atoi(*Value), 0, 8);
				UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug %s brings %d guns"), *Side, SideIt->second.guns);
			}
		}
		// -PA250Fort=N: the defender fights behind fortification level N (0 = in the open), whatever
		// the town has. Only this local copy of the setup changes.
		if (int32 Fort = 0; FParse::Value(FCommandLine::Get(), TEXT("PA250Fort="), Fort) && Fort >= 0)
		{
			Setup.fort_level = FMath::Clamp(Fort, 0, 3);
			const auto Def = Setup.sides.find(Setup.defender_faction);
			if (Def != Setup.sides.end()) Def->second.fortification_level = Setup.fort_level;
			if (Fort > 0) Setup.initial_post_holders[Setup.objective] = Setup.defender_faction;
			else Setup.initial_post_holders.erase(Setup.objective);
			UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug fortification level %d for %s"), Setup.fort_level, UTF8_TO_TCHAR(Setup.defender_faction.c_str()));
		}
		if (int32 Rounds = 0; FParse::Value(FCommandLine::Get(), TEXT("PA250Rounds="), Rounds) && Rounds >= 0)
		{
			for (auto& KV : Setup.sides) KV.second.rounds_per_man = Rounds;
		}
		// Class matchups: -PA250OnlyClass=continental:rifleman,british:line_infantry fields one class a side.
		if (FString Arg; FParse::Value(FCommandLine::Get(), TEXT("PA250OnlyClass="), Arg, /*bShouldStopOnSeparator=*/false))
		{
			TArray<FString> Items;
			Arg.ParseIntoArray(Items, TEXT(","));
			for (const FString& Item : Items)
			{
				FString Side, Class;
				const auto SideIt = Item.Split(TEXT(":"), &Side, &Class) ? Setup.sides.find(TCHAR_TO_UTF8(*Side)) : Setup.sides.end();
				if (SideIt == Setup.sides.end() || Class.IsEmpty()) continue;
				int32 Units = 0;
				for (const auto& T : SideIt->second.tickets) Units += FMath::Max(0, T.second);
				SideIt->second.tickets.clear();
				SideIt->second.tickets[TCHAR_TO_UTF8(*Class)] = FMath::Max(6, Units);
				UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug %s fields only %s"), *Side, *Class);
			}
		}
		// Extra units change the odds, so the field is shared out again by the sim's own rule.
		if (FParse::Param(FCommandLine::Get(), TEXT("PA250FieldFromTickets")) || FCString::Strifind(FCommandLine::Get(), TEXT("PA250ExtraUnits=")))
		{
			auto Men = [this](const char* Side) {
				int32 Units = 0;
				const auto It = Setup.sides.find(Side);
				if (It != Setup.sides.end()) for (const auto& T : It->second.tickets) Units += FMath::Max(0, T.second);
				return Units * SoldiersPerUnit;
			};
			const pa250::FieldSplit Split = pa250::split_field_strength(Men("british"), Men("continental"));
			if (Setup.sides.count("british")) Setup.sides.at("british").field_strength = Split.a;
			if (Setup.sides.count("continental")) Setup.sides.at("continental").field_strength = Split.b;
			UE_LOG(LogTemp, Display, TEXT("PA250 battle: debug field strength british %d, continental %d"), Split.a, Split.b);
		}
	}
	for (const auto& KV : Setup.sides)
	{
		const FName Faction = ToName(KV.first);
		for (const auto& T : KV.second.tickets)
		{
			InitialPool.FindOrAdd(Faction).Add(ToName(T.first), T.second);
		}
	}
	// Ammunition: each side's magazine, in rounds, and never less than one opening box a man.
	for (const auto& KV : Setup.sides)
	{
		if (KV.second.rounds_per_man <= 0) continue;
		int32 Units = 0;
		for (const auto& T : KV.second.tickets) Units += FMath::Max(0, T.second);
		const int32 Men = FMath::Max(Units * SoldiersPerUnit, KV.second.field_strength);
		const FName Faction = ToName(KV.first);
		Ammo.Init(Faction, FPA250AmmoPool::OpeningPool(KV.second.cartridge_budget, Rules.rounds_per_cartridge, Men, KV.second.rounds_per_man));
		UE_LOG(LogTemp, Display, TEXT("PA250 ammo %s: %d rounds a man, %d rounds on the field (%d cartridges from %s)"),
			*Faction.ToString(), KV.second.rounds_per_man, Ammo.Remaining(Faction), KV.second.cartridge_budget,
			KV.second.magazine_province.empty() ? TEXT("what the men carry") : UTF8_TO_TCHAR(KV.second.magazine_province.c_str()));
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
			else if (Tag == TEXT("PA250Attacker") || Tag == TEXT("PA250Defender"))
			{
				// A level built for either side to attack (L_Harbor) marks its starts by role.
				const std::string& Side = Tag == TEXT("PA250Attacker") ? Setup.attacker_faction : Setup.defender_faction;
				if (!Side.empty()) Starts.FindOrAdd(ToName(Side)).Add(*It);
			}
			else if (Tag.ToString().EndsWith(TEXT("_muster")))
			{
				FName Side(*Tag.ToString().LeftChop(7));
				if (Side == TEXT("PA250Attacker")) Side = ToName(Setup.attacker_faction);
				else if (Side == TEXT("PA250Defender")) Side = ToName(Setup.defender_faction);
				MusterPoints.FindOrAdd(Side).Add(*It);
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
		FPA250BattleAudio::StartAmbience(this);
		Music = MakeShared<FPA250BattleMusic>();
		Music->Start(this);
	}
	// The level's light for the battle's hour, and this platform's rendering tier (battle_look.json).
	// A debug battle's clock is not a battle's hour: it keeps the light the level was built with unless -PA250Hour says one.
	PA250BattleLook::Apply(GetWorld(), Setup.hour_of_day, !bDebugBattle);
	StartTime = GetWorld()->GetTimeSeconds();
	// The story screen gives way to the field: it fades in from black (a played battle only).
	if (!bAutoplayBots && FApp::CanEverRender())
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, StoryFadeInSeconds, FLinearColor::Black, false, false);
			UE_LOG(LogTemp, Display, TEXT("PA250 story: the field fades in over %.1f s"), StoryFadeInSeconds);
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("PA250StoryProbe")))
	{
		// The story probe's second half: the field after the fade, then quit.
		UE_LOG(LogTemp, Display, TEXT("STORY_PROBE: the battle level is open: %s"), *GetWorld()->GetMapName());
		FTimerHandle Still, QuitHandle;
		GetWorldTimerManager().SetTimer(Still, FTimerDelegate::CreateLambda([this]() {
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("Shot showui"));
			UE_LOG(LogTemp, Display, TEXT("STORY_PROBE: shot battle_story_field"));
		}), 10.f, false);
		GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda([this]() {
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), 13.f, false);
	}
	Banner = FString::Printf(TEXT("Battle of %s"), UTF8_TO_TCHAR(Setup.province_name.c_str()));
	if (PA250Modes::IsOrdered(ScenarioFight.Mode))
	{
		Banner += FString::Printf(TEXT(" — %s"), PA250Modes::ModeWords(ScenarioFight.Mode));
	}
	else if (ScenarioFight.bContestSingleCrossing)
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
		UE_LOG(LogTemp, Display, TEXT("PA250 field: %s puts %d men on the field%s"), *KV.Key.ToString(), GetFieldStrength(KV.Key),
			bBotsOverride ? TEXT(" (-PA250Bots)") : TEXT(""));
		SpawnInitialForces(KV.Key, GetFieldStrength(KV.Key));
	}
	SpawnGenerals();
	SetUpLanding();
	// Debug matchups: -PA250HoldAt=<metres> draws the two sides up that far apart on open ground and
	// has them hold it; -PA250NoReserve fights with the men on the field only.
	if (float HoldAt = 0.f; bDebugBattle && FParse::Value(FCommandLine::Get(), TEXT("PA250HoldAt="), HoldAt) && HoldAt > 0.f)
	{
		ArrangeMatchup(HoldAt);
	}
	// -PA250LedVolley=<side>: one man of that side leads a file as the player would (see TickLedVolleyProbe).
	if (FString Side; bDebugBattle && FParse::Value(FCommandLine::Get(), TEXT("PA250LedVolley="), Side) && !Side.IsEmpty())
	{
		ProbeSide = FName(*Side);
	}
	if (bDebugBattle && FParse::Param(FCommandLine::Get(), TEXT("PA250NoReserve")))
	{
		for (TPair<FName, int32>& KV : Ledger.Pool) KV.Value = 0;
	}
	// A full box of the player's side is kept for the player (FPA250AmmoPool::Held), as one ticket is.
	if (!bAutoplayBots && GetRoundsPerMan(PlayerFaction) > 0)
	{
		Ammo.Held.Add(PlayerFaction, FMath::Max(0, Rules.resupply_held_boxes) * GetRoundsPerMan(PlayerFaction));
	}
	// One ticket of the player's side is kept for the player: bot deaths and bleed cannot spend it and
	// bots are not reinforced from it, so he can always rejoin once (see FPA250TicketLedger::Held).
	// Set after the opening deployment, which is not a reinforcement.
	if (!bAutoplayBots && Ledger.Remaining(PlayerFaction) > 0)
	{
		Ledger.Held.Add(PlayerFaction, 1);
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
		Volleys->bHoldGround = bMatchupHold;
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([this]() { if (!bBattleOver) Banner.Reset(); }), 4.f, false);
	// Engine debug text (renderer warnings in yellow) does not belong over the battle. -PA250ScreenMessages keeps it.
	if (GAreScreenMessagesEnabled && !FParse::Param(FCommandLine::Get(), TEXT("PA250ScreenMessages")))
	{
		GAreScreenMessagesEnabled = false;
		bRestoreScreenMessages = true;
	}
	MaybeStartPreviewCapture();
	MaybeStartFpsSample();
	MaybeStartMeleeProbe();
	MaybeStartClassPortraits();
	MaybeStartGunStill();
	MaybeStartGunServe();
	MaybeStartLandingStill();
	// A moment after BeginPlay, so the player's pawn and its musket exist whatever the spawn order.
	FTimerHandle ClassPick;
	GetWorldTimerManager().SetTimer(ClassPick, FTimerDelegate::CreateWeakLambda(this, [this]() {
		if (!bBattleOver && GetWorld()) MaybeOpenClassPick(GetWorld()->GetFirstPlayerController());
	}), 0.3f, false);
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
		// -PA250SpectateSide=british: watch the other side (to check its soldiers in a real window).
		FString Side;
		const FName Watch = FParse::Value(FCommandLine::Get(), TEXT("PA250SpectateSide="), Side) && !Side.IsEmpty() ? FName(*Side) : PlayerFaction;
		if (It->FactionId == Watch)
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
					Bank->AddPanFlash(M - FVector(110.f, 0.f, 0.f), FVector(1.f, 0.f, 0.03f), false);
				}
			}), 1.f + 3.f * V, false);
		}
		// Just before the "fresh" still: balls going to ground ahead of the line, and two men hit.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			FTimerHandle Strikes;
			GetWorldTimerManager().SetTimer(Strikes, FTimerDelegate::CreateWeakLambda(this, [this, Pass]() {
				UPA250SmokeSubsystem* Bank = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>();
				const APA250ControlPoint* Green = nullptr;
				for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
					if (WP.IsValid() && WP->PostId == TEXT("green")) Green = WP.Get();
				if (!Bank || !Green) return;
				for (int32 i = 0; i < (Pass == 0 ? 8 : 2); ++i)
				{
					FVector At = Green->GetActorLocation() + (Pass == 0
						? FVector(-1350.f + 60.f * (i % 3), -560.f + i * 160.f, 0.f)
						: FVector(-1250.f, -200.f + i * 400.f, 0.f));
					FHitResult Hit;
					if (GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 500), At - FVector(0, 0, 2000), ECC_WorldStatic))
						At.Z = Hit.ImpactPoint.Z + (Pass == 0 ? 0.f : 120.f);
					Bank->AddImpact(At, Pass == 0 ? FVector::UpVector : FVector(-1.f, 0.f, 0.f), Pass == 1);
				}
			}), Pass == 0 ? 5.0f : 5.3f, false);
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
	// Stills are named for the level they show (a third level once wrote over Lexington's).
	const FString MapName = GetWorld()->GetMapName();
	const FString MapStem = MapName.Contains(TEXT("L_Concord")) ? TEXT("concord") : MapName.Contains(TEXT("L_Harbor")) ? TEXT("harbor") : TEXT("lexington");
	const FString FinalName = FString::Printf(TEXT("%s_%s_%s.png"), *MapStem, *PreviewLabel, *OutStem);
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
	// -PA250BattleShots=20,45,70: stills of the running battle (with HUD) at those game-seconds,
	// written to Saved/PA250/battle_shot_<n>.png. For checking how the fight looks in a real window.
	FString ShotTimes;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250BattleShots="), ShotTimes, /*bShouldStopOnSeparator=*/false))
	{
		TArray<FString> Parts;
		ShotTimes.ParseIntoArray(Parts, TEXT(","));
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const float At = FCString::Atof(*Parts[i]);
			if (At <= 0.f) continue;
			FTimerHandle Shot;
			GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [this, i]() {
				const FString Path = FPaths::ConvertRelativePathToFull(
					FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), FString::Printf(TEXT("battle_shot_%d.png"), i + 1)));
				int32 Fallen = 0;
				for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It) Fallen += It->IsDead() ? 1 : 0;
				const UPA250SmokeSubsystem* Bank = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>();
				UE_LOG(LogTemp, Display, TEXT("PA250 battle shot %d: fallen_on_field=%d puffs=%d misfires=%d -> %s"),
					i + 1, Fallen, Bank ? Bank->NumPuffs() : 0, Misfires, *Path);
				FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			}), At, false);
		}
	}
	// -PA250HurtProbe=<s>: the player takes a 60-point wound at that game-second (to see the wounded view with -PA250BattleShots).
	float HurtAt = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250HurtProbe="), HurtAt) && HurtAt > 0.f && !bAutoplayBots)
	{
		FTimerHandle Hurt;
		GetWorldTimerManager().SetTimer(Hurt, FTimerDelegate::CreateWeakLambda(this, [this]() {
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
			if (Me) UGameplayStatics::ApplyDamage(Me, 60.f, nullptr, nullptr, nullptr);
			UE_LOG(LogTemp, Display, TEXT("PA250 hurt probe: player health now %.0f"), Me && Me->HealthComponent ? Me->HealthComponent->CurrentHealth : -1.f);
		}), HurtAt, false);
	}
	// -PA250TakeoverProbe: at 8 s the player's side loses its reserves and the player is killed; the log
	// then says whether he came back as one of the men still standing. Real window, no autoplay.
	if ((FParse::Param(FCommandLine::Get(), TEXT("PA250TakeoverProbe")) || FString(FCommandLine::Get()).Contains(TEXT("PA250TakeoverProbe="))) && !bAutoplayBots)
	{
		FTimerHandle Kill;
		GetWorldTimerManager().SetTimer(Kill, FTimerDelegate::CreateWeakLambda(this, [this]() {
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
			// -PA250TakeoverProbe=held leaves the one ticket held for the player (he gets the spawn screen instead).
			FString ProbeMode;
			FParse::Value(FCommandLine::Get(), TEXT("PA250TakeoverProbe="), ProbeMode);
			Ledger.Pool.FindOrAdd(PlayerFaction) = ProbeMode == TEXT("held") ? Ledger.HeldFor(PlayerFaction) : 0;
			UE_LOG(LogTemp, Display, TEXT("PA250 takeover probe: reserves set to 0, killing player pawn %s"), Me ? *Me->GetName() : TEXT("(none)"));
			if (Me) UGameplayStatics::ApplyDamage(Me, 10000.f, nullptr, nullptr, nullptr);
		}), 8.f, false);
		// Held mode: the spawn screen must be up after the death cam; press its button.
		FTimerHandle Press;
		GetWorldTimerManager().SetTimer(Press, FTimerDelegate::CreateWeakLambda(this, [this]() {
			const bool bUp = SpawnScreen && SpawnScreen->IsInViewport();
			UE_LOG(LogTemp, Display, TEXT("PA250 takeover probe: after death cam spawn_screen=%d remaining=%d held=%d"),
				bUp, Ledger.Remaining(PlayerFaction), Ledger.HeldFor(PlayerFaction));
			// What the screen's button does (UPA250SpawnScreen::Confirm).
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			if (bUp && PC && RequestSpawn(PC, SpawnScreen->SelectedClass, nullptr))
			{
				SpawnScreen->RemoveFromParent();
				PC->SetInputMode(FInputModeGameOnly());
				PC->bShowMouseCursor = false;
				if (Hud) Hud->SetVisibility(ESlateVisibility::Visible);
			}
		}), 8.f + DeathCamSeconds + 1.f, false);
		FTimerHandle Report;
		GetWorldTimerManager().SetTimer(Report, FTimerDelegate::CreateWeakLambda(this, [this]() {
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
			const bool bOk = Me && !Me->IsDead() && Me->IsPlayerControlled() && Me->FactionId == PlayerFaction
				&& Me->InputComponent != nullptr && PC->GetViewTarget() == Me && !PC->bShowMouseCursor;
			UE_LOG(LogTemp, Display, TEXT("PA250 takeover probe tickets: remaining=%d held=%d"), Ledger.Remaining(PlayerFaction), Ledger.HeldFor(PlayerFaction));
			UE_LOG(LogTemp, Display, TEXT("PA250 takeover probe %s: pawn=%s alive=%d player=%d input=%d view=%d cursor=%d spawn_screen=%d notice='%s'"),
				bOk ? TEXT("PASSED") : TEXT("FAILED"), Me ? *Me->GetName() : TEXT("(none)"), Me && !Me->IsDead(), Me && Me->IsPlayerControlled(),
				Me && Me->InputComponent != nullptr, PC && Me && PC->GetViewTarget() == Me, PC && PC->bShowMouseCursor,
				SpawnScreen && SpawnScreen->IsInViewport(), *GetBannerText());
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("takeover_probe.png")));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
		}), 8.f + DeathCamSeconds + 2.f, false);
		FTimerHandle Quit;
		GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateLambda([]() { FPlatformMisc::RequestExit(false); }), 8.f + DeathCamSeconds + 5.f, false);
	}
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

bool APA250BattleGameMode::PostIsSafeSpawn(const APA250ControlPoint* Post, FName Faction) const
{
	if (!Post || Post->GetHolder() != Faction)
	{
		return false;
	}
	// Nobody comes on in the middle of the crossing being fought for.
	if (ScenarioFight.bContestSingleCrossing && Post->PostId == ScenarioFight.CrossingPostId)
	{
		return false;
	}
	// Nor under the enemy's muskets: no living enemy within SafeSpawnEnemyCm of the post. Light
	// infantry (forward_spawn) come on with the enemy much closer: that is their verb.
	const float SafeCm = (!SpawnClassHint.IsNone() && PA250UnitClasses::KitFor(Faction, SpawnClassHint).bForwardSpawn)
		? FMath::Min(SafeSpawnEnemyCm, PA250UnitClasses::Verbs().ForwardSpawnEnemyM * 100.f) : SafeSpawnEnemyCm;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->FactionId != Faction && !It->FactionId.IsNone()
			&& FVector::Dist2D(It->GetActorLocation(), Post->GetActorLocation()) < SafeCm)
		{
			return false;
		}
	}
	return true;
}

FTransform APA250BattleGameMode::PickSpawnTransform(FName Faction, APA250ControlPoint* PreferredPost) const
{
	auto AtPost = [this](const APA250ControlPoint* Post) {
		FVector Loc = Post->GetActorLocation();
		Loc += FVector(FMath::FRandRange(-Post->Radius * 0.35f, Post->Radius * 0.35f),
			FMath::FRandRange(-Post->Radius * 0.35f, Post->Radius * 0.35f), 0.f);
		// On ground a man can stand on: the navmesh under the point, else the post itself.
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(Loc, OnNav, FVector(250.f, 250.f, 400.f)))
			{
				Loc = OnNav.Location + FVector(0.f, 0.f, 100.f);
			}
			else
			{
				Loc = Post->GetActorLocation();
			}
		}
		return FTransform(FRotator::ZeroRotator, ClearSpawnLocation(GetWorld(), Loc + FVector(0.f, 0.f, 40.f)));
	};
	if (PostIsSafeSpawn(PreferredPost, Faction))
	{
		return AtPost(PreferredPost);
	}
	TArray<APA250ControlPoint*> Safe;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (APA250ControlPoint* P = WP.Get(); P && PostIsSafeSpawn(P, Faction))
		{
			Safe.Add(P);
		}
	}
	if (Safe.Num() > 0)
	{
		return AtPost(Safe[FMath::RandRange(0, Safe.Num() - 1)]);
	}
	// No post is ours and quiet: the side's own start ground at the edge of the field.
	if (AActor* Start = PickStart(Faction))
	{
		FTransform T = Start->GetActorTransform();
		T.AddToTranslation(FVector(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f));
		T.SetLocation(ClearSpawnLocation(GetWorld(), T.GetLocation()));
		return T;
	}
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (APA250ControlPoint* P = WP.Get(); P && P->GetHolder() == Faction)
		{
			return AtPost(P);
		}
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
	// Some classes take the field one or two at a time (the ensign with the colours, a musician, a
	// surgeon: unit_classes.json max_on_field), whatever the army has of them in reserve.
	auto Room = [this, Faction](FName Class) {
		return PA250Support::ClassHasFieldRoom(CountAlive(Faction, Class), PA250UnitClasses::KitFor(Faction, Class).MaxOnField);
	};
	// The class asked for, while the army still has a man of it off the field.
	if (ClassMenInReserve(Faction, Preferred) > 0 && Room(Preferred))
	{
		return Preferred;
	}
	// Otherwise the class with the most men left (name order breaks ties, so it is deterministic).
	TArray<FName> Names;
	P->GetKeys(Names);
	Names.Sort(FNameLexicalLess());
	FName Best;
	int32 BestLeft = 0;
	for (const FName& N : Names)
	{
		const int32 Left = Room(N) ? ClassMenInReserve(Faction, N) : 0;
		if (Left > BestLeft)
		{
			BestLeft = Left;
			Best = N;
		}
	}
	if (!Best.IsNone())
	{
		return Best;
	}
	// Every class is spent on paper (bleed and the ticket floor can leave a man to field): keep the
	// request if the army ever had that class, else its first class.
	if (P->FindRef(Preferred) > 0)
	{
		return Preferred;
	}
	for (const FName& N : Names)
	{
		if (P->FindRef(N) > 0) return N;
	}
	return P->Contains(Preferred) || Names.Num() == 0 ? Preferred : Names[0];
}

int32 APA250BattleGameMode::ClassMenLeft(FName Faction, FName UnitClass) const
{
	const TMap<FName, int32>* P = InitialPool.Find(Faction);
	const int32 Start = (P ? FMath::Max(0, P->FindRef(UnitClass)) : 0) * FMath::Max(1, SoldiersPerUnit);
	const TMap<FName, int32>* Dead = Ledger.Deaths.Find(Faction);
	return FMath::Max(0, Start - (Dead ? Dead->FindRef(UnitClass) : 0));
}

int32 APA250BattleGameMode::ClassMenInReserve(FName Faction, FName UnitClass) const
{
	const TMap<FName, int32>* Gave = Prisoners.Find(Faction);
	return FMath::Max(0, ClassMenLeft(Faction, UnitClass) - (Gave ? Gave->FindRef(UnitClass) : 0) - CountAlive(Faction, UnitClass));
}

TArray<APA250BattleGameMode::FClassChoice> APA250BattleGameMode::GetClassChoices(FName Faction) const
{
	TArray<FClassChoice> Out;
	const TMap<FName, int32>* P = InitialPool.Find(Faction);
	if (!P)
	{
		return Out;
	}
	TArray<FName> Names;
	P->GetKeys(Names);
	const TArray<FName>& Order = PA250UnitClasses::DisplayOrder();
	Names.Sort([&Order](const FName& A, const FName& B) {
		const int32 IA = Order.IndexOfByKey(A), IB = Order.IndexOfByKey(B);
		if (IA != IB) return (IA < 0 ? MAX_int32 : IA) < (IB < 0 ? MAX_int32 : IB);
		return A.LexicalLess(B);
	});
	for (const FName& N : Names)
	{
		if (P->FindRef(N) <= 0) continue; // the army has none of these
		FClassChoice C;
		C.Id = N;
		C.MenLeft = ClassMenLeft(Faction, N);
		C.Alive = CountAlive(Faction, N);
		Out.Add(C);
	}
	return Out;
}

FName APA250BattleGameMode::GetControllerClass(const AController* Controller) const
{
	for (const auto& KV : ControllerClass)
	{
		if (KV.Key.Get() == Controller) return KV.Value;
	}
	return NAME_None;
}

bool APA250BattleGameMode::RekitPlayer(APlayerController* PC, FName UnitClass)
{
	ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (bBattleOver || !Me || Me->IsDead() || Me->FactionId != PlayerFaction || ClassMenLeft(PlayerFaction, UnitClass) <= 0)
	{
		return false;
	}
	ControllerClass.Add(PC, UnitClass);
	ConfigureSoldier(Me, PlayerFaction, UnitClass);
	UE_LOG(LogTemp, Display, TEXT("PA250 class: player takes the field as %s (%s)"), *UnitClass.ToString(),
		PA250UnitClasses::KitFor(PlayerFaction, UnitClass).Label);
	PlacePlayerAtGun(Me);
	return true;
}

void APA250BattleGameMode::MaybeOpenClassPick(APlayerController* PC)
{
	// Battlefront-style: choose your class before the fight when the army has more than one. Scripted
	// runs (previews, probes, FPS samples) keep the default soldier; -PA250ClassPick forces the screen.
	if (!PC || bAutoplayBots || FApp::IsUnattended())
	{
		return;
	}
	const FString Cmd(FCommandLine::Get());
	const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("PA250ClassPick"));
	if (!bForce)
	{
		for (const TCHAR* Probe : {TEXT("PA250NoClassPick"), TEXT("PA250Preview="), TEXT("PA250BattleShots="), TEXT("PA250HurtProbe"),
				 TEXT("PA250TakeoverProbe"), TEXT("PA250FpsSample"), TEXT("PA250MeleeProbe"), TEXT("PA250ClassPortraits"), TEXT("PA250GunStill"), TEXT("PA250GunServe"), TEXT("PA250LandingStill"), TEXT("PA250OrderProbe")})
		{
			if (Cmd.Contains(Probe)) return;
		}
	}
	int32 Open = 0;
	for (const FClassChoice& C : GetClassChoices(PlayerFaction)) Open += C.MenLeft > 0 ? 1 : 0;
	if (Open < 2 || !Cast<ARFCharacter>(PC->GetPawn()))
	{
		return;
	}
	bOpeningClassPick = true;
	// He cannot move while he chooses, so he cannot be hurt either (at most 30 s of it).
	if (APawn* Me = PC->GetPawn()) Me->SetCanBeDamaged(false);
	FTimerHandle Guard;
	GetWorldTimerManager().SetTimer(Guard, FTimerDelegate::CreateWeakLambda(this, [this]() {
		// Only the protection lapses; the picker stays an opening pick until he deploys or falls.
		const APlayerController* P = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (APawn* Me = P ? P->GetPawn() : nullptr) Me->SetCanBeDamaged(true);
	}), 30.f, false);
	ShowSpawnScreen(PC);
	UE_LOG(LogTemp, Display, TEXT("PA250 class: opening class pick shown (%d classes)"), Open);
	if (bForce)
	{
		// Still of the picker with HUD chrome hidden; quit after the shot.
		FTimerHandle Still, QuitHandle;
		GetWorldTimerManager().SetTimer(Still, FTimerDelegate::CreateWeakLambda(this, [this]() {
			if (APlayerController* P = GetWorld()->GetFirstPlayerController()) P->ConsoleCommand(TEXT("Shot showui"));
			UE_LOG(LogTemp, Display, TEXT("PA250 class: shot class_picker; hud_visible=%d picker=%d"),
				   (Hud && Hud->GetVisibility() != ESlateVisibility::Collapsed) ? 1 : 0, IsClassPickerOpen() ? 1 : 0);
		}), 3.f, false);
		GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]() {
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), 5.f, false);
	}
}

void APA250BattleGameMode::EndOpeningClassPick()
{
	bOpeningClassPick = false;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (APawn* Me = PC ? PC->GetPawn() : nullptr) Me->SetCanBeDamaged(true);
}

int32 APA250BattleGameMode::CountAlive(FName Faction) const
{
	int32 N = 0;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->FactionId == Faction && !It->ActorHasTag(TEXT("PA250GunMate"))) ++N;
	}
	return N;
}

void APA250BattleGameMode::TickCoverProbe(float DeltaSeconds)
{
	if (CoverProbeStage == 0 || CoverProbeStage > 4) return;
	const ARFCharacter* Man = CoverProbeMan.Get();
	CoverProbeTimer += DeltaSeconds;
	if (!Man || Man->IsDead())
	{
		// He fell before both stills were taken: start again with the next man at a wall.
		CoverProbeStage = 0;
		CoverProbeMan.Reset();
		return;
	}
	FString Tag;
	FParse::Value(FCommandLine::Get(), TEXT("PA250CoverProbeTag="), Tag);
	auto Still = [&Tag](const TCHAR* Name)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
			FString::Printf(TEXT("cover_%s_%s.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag, Name)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTemp, Display, TEXT("PA250 cover probe: still %s"), *Path);
	};
	const bool bDown = Man->GetCharacterMovement() && Man->GetCharacterMovement()->IsCrouching();
	if (CoverProbeStage == 1 && CoverProbeTimer > 1.5f && bDown)
	{
		Still(TEXT("down"));
		CoverProbeStage = 2;
		CoverProbeTimer = 0.f;
	}
	else if (CoverProbeStage == 2 && CoverProbeTimer > 0.5f && !bDown)
	{
		Still(TEXT("up"));
		CoverProbeStage = 3;
		CoverProbeTimer = 0.f;
	}
	else if (CoverProbeStage == 3 && CoverProbeTimer > 1.5f)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 cover probe: done"));
		CoverProbeStage = 5;
		FPlatformMisc::RequestExit(false);
	}
	else if (CoverProbeTimer > 90.f)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 cover probe: gave up at stage %d (the man never %s)"), CoverProbeStage,
			CoverProbeStage == 1 ? TEXT("went down") : TEXT("rose to fire"));
		if (CoverProbeStage == 2) Still(TEXT("up_never"));
		CoverProbeStage = 5;
		FPlatformMisc::RequestExit(false);
	}
}

void APA250BattleGameMode::NoteCoverThink(const ARFCharacter* Man, bool bAtCover, bool bDucked)
{
	if (!Man) return;
	const FName Faction = Man->FactionId;
	if (CoverProbeStage == 0 && bAtCover && FParse::Param(FCommandLine::Get(), TEXT("PA250CoverProbe")))
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			// A camera behind and to one side of him, looking along his wall.
			const FVector Fwd = Man->GetActorForwardVector().GetSafeNormal2D();
			const FVector Right(-Fwd.Y, Fwd.X, 0.f);
			const FVector At = Man->GetActorLocation() - Fwd * 420.f + Right * 260.f + FVector(0.f, 0.f, 90.f);
			const FRotator Look = (Man->GetActorLocation() + Fwd * 150.f + FVector(0.f, 0.f, 20.f) - At).Rotation();
			if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(At, Look))
			{
				Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
				PC->SetViewTarget(Cam);
				CoverProbeMan = Man;
				CoverProbeStage = 1;
				CoverProbeTimer = 0.f;
				UE_LOG(LogTemp, Display, TEXT("PA250 cover probe: watching %s (%s) at his wall"), *Man->GetName(), *Faction.ToString());
			}
		}
	}
	FCoverUse& U = CoverUse.FindOrAdd(Faction);
	++U.Thinks;
	U.AtCover += bAtCover ? 1 : 0;
	U.Ducked += bDucked ? 1 : 0;
}

void APA250BattleGameMode::NoteBotShot(FName Faction, bool bFromCover)
{
	FCoverUse& U = CoverUse.FindOrAdd(Faction);
	++U.Shots;
	U.ShotsFromCover += bFromCover ? 1 : 0;
}

void APA250BattleGameMode::TickAnimTiers(float DeltaSeconds)
{
	AnimTierTimer -= DeltaSeconds;
	if (AnimTierTimer > 0.f) return;
	AnimTierTimer = 0.5f;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	// Debug runs: -PA250MotionMatching=1 (or 0) overrides the data flag, so the node can be tried
	// while battle_feel.json still says false. -PA250AnimProbe logs what the tiers are doing.
	int32 Override = -1;
	bMotionMatchingOn = FParse::Value(FCommandLine::Get(), TEXT("PA250MotionMatching="), Override) ? Override != 0 : Feel.bMotionMatching;
	bAnimProbe = FParse::Param(FCommandLine::Get(), TEXT("PA250AnimProbe"));
	FPA250AnimBudget Budget = Feel.Budget;
	FParse::Value(FCommandLine::Get(), TEXT("PA250AnimFullMax="), Budget.FullQualityMax);
	FParse::Value(FCommandLine::Get(), TEXT("PA250AnimSwitchM="), Budget.SwitchDistanceM);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
	TArray<ARFCharacter*> Men;
	TArray<PA250BattleFeel::FTierInput> Inputs;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() || !It->IsUsingPeriodSoldier()) continue;
		PA250BattleFeel::FTierInput In;
		In.Key = It->GetName();
		In.bPlayer = *It == Viewer;
		In.DistanceM = Viewer ? FVector::Dist(It->GetActorLocation(), Viewer->GetActorLocation()) / 100.f : 0.f;
		In.bWasFull = It->IsAnimFullQuality();
		Men.Add(*It);
		Inputs.Add(In);
	}
	// With nobody to look from (a headless bot battle) there is no "near": everyone is cheap.
	const bool bOn = bMotionMatchingOn && Viewer && PA250AnimClips::LocomotionReady();
	const TArray<int32> Full = PA250BattleFeel::PickFullQuality(bOn, Budget, Feel.SwitchHysteresisM, Inputs);
	TSet<int32> FullSet(Full);
	int32 Showing = 0, Searches = 0, Jumps = 0, NoHistory = 0;
	const FPA250BattleLookData& Look = PA250BattleLook::Get();
	for (int32 i = 0; i < Men.Num(); ++i)
	{
		Men[i]->SetAnimFullQuality(FullSet.Contains(i));
		// The soldier budget (battle_look.json soldier_lod): no shadow from a man far from the viewer.
		if (Look.bLoaded) Men[i]->SetLookBudget(!Viewer || Look.ShadowCastM <= 0.f || Inputs[i].DistanceM <= Look.ShadowCastM, Look.bUpdateRateOptimisation);
		if (!bAnimProbe) continue;
		if (const UPA250SoldierAnimInstance* Inst = Cast<UPA250SoldierAnimInstance>(Men[i]->GetMesh()->GetAnimInstance()))
		{
			const UPA250SoldierAnimInstance::FLocomotionReport& R = Inst->GetLocomotionReport();
			Showing += R.bShowingFull ? 1 : 0;
			Searches += R.Searches;
			Jumps += R.Jumps;
			NoHistory += (FullSet.Contains(i) && !R.bHadHistory) ? 1 : 0;
		}
	}
	AnimProbeTimer -= 0.5f;
	if (bAnimProbe && AnimProbeTimer <= 0.f)
	{
		AnimProbeTimer = 2.f;
		UE_LOG(LogTemp, Display, TEXT("PA250 anim: full=%d cheap=%d showing_full=%d searches=%d jumps=%d no_history=%d (motion matching %s, budget %d within %.0f m, %d databases)"),
			Full.Num(), Men.Num() - Full.Num(), Showing, Searches, Jumps, NoHistory, bMotionMatchingOn ? TEXT("on") : TEXT("off"),
			Budget.FullQualityMax, Budget.SwitchDistanceM, PA250AnimClips::LocomotionDatabaseCount());
	}
}

void APA250BattleGameMode::TickOfficers(float DeltaSeconds)
{
	// Volley fire under an officer's eye (GAMEPLAY SETTING): men near a living officer of their side
	// aim steadier and load faster. Twice a second is plenty; the values live in PA250UnitClasses.
	OfficerTimer -= DeltaSeconds;
	if (OfficerTimer > 0.f)
	{
		return;
	}
	OfficerTimer = 0.5f;
	TArray<ARFCharacter*> Men, Officers, Musicians;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	int32 CadenceMen = 0;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead()) continue;
		Men.Add(*It);
		if (PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bOfficer) Officers.Add(*It);
		if (PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bRelaysOrders) Musicians.Add(*It);
	}
	for (ARFCharacter* C : Men)
	{
		bool bNear = false;
		for (const ARFCharacter* O : Officers)
		{
			// A general's aura reaches further than a company officer's (BattleSetup commander.aura_radius_m).
			const float GeneralCm = IsGeneral(O) ? GeneralAuraCm(O->FactionId) : 0.f;
			const bool bInReach = GeneralCm > 0.f ? FVector::DistSquared(C->GetActorLocation(), O->GetActorLocation()) <= FMath::Square(GeneralCm)
												  : PA250UnitClasses::InOfficerRange(C->GetActorLocation(), O->GetActorLocation());
			if (O != C && O->FactionId == C->FactionId && bInReach)
			{
				bNear = true;
				if (GeneralCm > 0.f) ++GeneralAuraChecks;
				break;
			}
		}
		C->bNearOfficer = bNear;
		C->OfficerAccuracyScale = bNear ? PA250UnitClasses::OfficerSpreadScale : 1.f;
		if (C->Musket)
		{
			// The whole kit, not only the officer bonus: a soldier configured before his musket was
			// spawned (the player's first pawn) still gets his class's firelock here.
			const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(C->FactionId, C->UnitClass);
			C->Musket->ClassSpreadScale = Kit.SpreadScale;
			C->Musket->ClassRangeScale = Kit.RangeScale;
			C->Musket->ClassAccurateRangeCm = Kit.AccurateM * 100.f;
			C->Musket->ClassSpreadGrowth = Kit.SpreadGrowth;
			// The musician's verb: men who fire by volley load in cadence within earshot of him.
			bool bCadence = false;
			for (const ARFCharacter* M : Musicians)
			{
				bCadence |= M != C && M->FactionId == C->FactionId
					&& FVector::DistSquared2D(M->GetActorLocation(), C->GetActorLocation()) <= FMath::Square(Verbs.MusicianCadenceM * 100.f);
			}
			const bool bVolleyRole = CombatProfilesAsset ? !CombatProfilesAsset->RoleFor(C->FactionId, C->UnitClass).bIndividualFire : true;
			C->Musket->ReloadTimeScale = Kit.ReloadScale * (bNear ? PA250UnitClasses::OfficerReloadScale : 1.f)
				* PA250Support::CadenceReloadScale(bVolleyRole, bCadence, Verbs.MusicianCadenceScale);
			CadenceMen += (bVolleyRole && bCadence) ? 1 : 0;
		}
	}
	CadenceManChecks += CadenceMen;
}

bool APA250BattleGameMode::CanSpawn(FName Faction) const
{
	return PoolTotal(Faction) > 0;
}

void APA250BattleGameMode::SpendDeathTicket(FName Faction, FName UnitClass)
{
	// A class may cost less than a whole ticket (militia: half; unit_classes.json ticket_cost).
	Ledger.SpendDeath(Faction, UnitClass, PA250UnitClasses::KitFor(Faction, UnitClass).TicketCost);
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
	// The class's kit: health, pace, his firelock and how hard he hits. Bots and the player alike.
	const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(Faction, UnitClass);
	// (Until 2026-11-13 a bot spawned more than 70 m from the first post was moved to 55 m from it.
	// The levels now place every start and post at the distance meant, and on L_Concord that rule
	// dropped the militia beside the bridge and into the river, 10 m from the regulars.)
	if (Soldier->HealthComponent)
	{
		Soldier->HealthComponent->MaxHealth = Kit.MaxHealth;
		Soldier->HealthComponent->CurrentHealth = Soldier->HealthComponent->MaxHealth;
	}
	Soldier->WeaponTuning = WeaponTuningAsset;
	Soldier->FactionVisuals = FactionVisualsAsset;
	Soldier->ApplyFactionVisuals();
	if (Soldier->MeleeComponent)
	{
		Soldier->MeleeComponent->Tuning = MeleeTuningAsset;
		Soldier->MeleeComponent->DamageScale = Kit.MeleeScale;
	}
	if (Soldier->Musket)
	{
		if (Tuning) Soldier->Musket->Tuning = Tuning;
		Soldier->Musket->WeaponTuning = WeaponTuningAsset;
		Soldier->Musket->SetReloadPreset(ReloadPreset == 1 ? EPA250ReloadPreset::Authentic : EPA250ReloadPreset::Arcade);
		Soldier->Musket->ApplyWeaponTuning();
		Soldier->Musket->ClassSpreadScale = Kit.SpreadScale;
		Soldier->Musket->ClassRangeScale = Kit.RangeScale;
		Soldier->Musket->ClassAccurateRangeCm = Kit.AccurateM * 100.f;
		Soldier->Musket->ClassSpreadGrowth = Kit.SpreadGrowth;
		Soldier->Musket->ReloadTimeScale = Kit.ReloadScale; // TickOfficers adds the officer bonus
	}
	Soldier->bNearOfficer = false;
	Soldier->OfficerAccuracyScale = 1.f;
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
			// Training, not class or side; then each man is a slightly better or worse shot than the average.
			// The firelock matters too: a rifleman's aim cone is a fraction of a musket man's.
			Bot->AimErrorDegrees = CombatProfilesAsset->TrainingFor(Faction, UnitClass).AimErrorDegrees * Bot->GetAimScale() * Kit.SpreadScale;
		}
		Bot->ConfigureCrowd(BotTuningAsset);
		// The class's preferred range (unit_classes.json): he opens fire inside the far edge and
		// closes to the near one; he has to see at least that far.
		Bot->BandMinCm = FMath::Max(0.f, Kit.PreferMinM * 100.f);
		Bot->BandMaxCm = FMath::Max(500.f, Kit.PreferMaxM * 100.f);
		Bot->SightRange = FMath::Max(Bot->SightRange, Bot->BandMaxCm + 1000.f);
	}
	{
		const ARFCharacter* Defaults = GetDefault<ARFCharacter>(Soldier->GetClass());
		Soldier->WalkSpeed = Defaults->WalkSpeed * Kit.SpeedScale;
		Soldier->SprintSpeed = Defaults->SprintSpeed * Kit.SpeedScale;
	}
	Soldier->GetCharacterMovement()->MaxWalkSpeed = 380.f * Kit.SpeedScale;
	if (Soldier->IsPlayerControlled())
	{
		Soldier->GetCharacterMovement()->MaxWalkSpeed = Soldier->WalkSpeed;
	}
	MaybeRaiseColours(Soldier);
	// Dragoons (GAME INVENTION for 1775) take the field in the saddle; anyone else leaves his horse.
	if (UnitClass == TEXT("dragoon"))
	{
		if (!Soldier->IsMounted()) Soldier->MountHorse(nullptr, /*bForce=*/true);
	}
	else if (Soldier->IsMounted() || Soldier->GetOwnHorse())
	{
		Soldier->ReleaseOwnHorse();
	}
}

ARFCharacter* APA250BattleGameMode::SpawnBot(FName Faction, FName UnitClass, APA250ControlPoint* PreferredPost, uint32 ClothingSeed)
{
	if (HoldsLandingSpawn(Faction))
	{
		++HeldLandingSpawns; // he comes ashore when the first boat is in (TickLanding)
		return nullptr;
	}
	const FName Charged = ChooseClassFor(Faction, UnitClass);
	if (!Ledger.CanReinforce(Faction) || MenInReserve(Faction) <= 0) // the ticket held for the human is not for bots; nor a man the army does not have
	{
		return nullptr;
	}
	SpawnClassHint = Charged;
	const FTransform At = PickSpawnTransform(Faction, PreferredPost);
	SpawnClassHint = NAME_None;
	return SpawnBotAt(Faction, Charged, At, ClothingSeed);
}

int32 APA250BattleGameMode::SideOrder(FName Faction, const char* Label) const
{
	const auto Side = Setup.sides.find(ToStd(Faction));
	if (Side == Setup.sides.end()) return 0;
	for (const pa250::SideFactor& F : Side->second.factors)
	{
		if (F.label == Label) return F.value;
	}
	return 0;
}

bool APA250BattleGameMode::IsGeneral(const ARFCharacter* Man) const
{
	if (!Man) return false;
	const TWeakObjectPtr<ARFCharacter>* G = Generals.Find(Man->FactionId);
	return G && G->Get() == Man;
}

bool APA250BattleGameMode::GeneralStation(const ARFCharacter* Man, FVector& OutStation, float& OutRadiusCm) const
{
	if (!IsGeneral(Man)) return false;
	const FVector* At = GeneralStations.Find(Man->FactionId);
	if (!At) return false;
	OutStation = *At;
	OutRadiusCm = pa250::commander_rules().home_post_radius_m * 100.f;
	return true;
}

float APA250BattleGameMode::GeneralAuraCm(FName Faction) const
{
	const TWeakObjectPtr<ARFCharacter>* G = Generals.Find(Faction);
	if (!G || !G->IsValid() || G->Get()->IsDead()) return 0.f;
	const auto Side = Setup.sides.find(ToStd(Faction));
	if (Side == Setup.sides.end()) return 0.f;
	// While the player fights as him, his presence is the hero kit's.
	if (Hero.bPlaying && Faction == PlayerFaction && Side->second.has_hero) return Side->second.hero_aura_radius_m * 100.f;
	return Side->second.commander_aura_radius_m * 100.f;
}

void APA250BattleGameMode::SpawnGenerals()
{
	// The general is not one of the army's men in the ranks: he costs no ticket and is no part of the
	// reserve or the casualty ledger. What becomes of him goes into the result on its own line.
	for (const auto& Sk : Setup.sides)
	{
		if (Sk.second.commander_id.empty()) continue;
		const FName Faction(UTF8_TO_TCHAR(Sk.first.c_str()));
		SpawnClassHint = TEXT("officer");
		FTransform At = PickSpawnTransform(Faction, nullptr);
		SpawnClassHint = NAME_None;
		// A few paces behind where the men come on, so he stands at the rear of his own post.
		At.SetLocation(At.GetLocation() - At.GetRotation().GetForwardVector() * 250.f);
		ARFCharacter* C = GetWorld()->SpawnActorDeferred<ARFCharacter>(ARFCharacter::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!C) continue;
		C->SetClothingSeed(HashCombine(GetTypeHash(static_cast<uint64>(Setup.seed)), GetTypeHash(Faction)));
		C->AIControllerClass = APA250BotController::StaticClass();
		C->FinishSpawning(At);
		C->SpawnDefaultController();
		ConfigureSoldier(C, Faction, TEXT("officer"));
		C->Tags.AddUnique(TEXT("PA250General"));
		if (AController* Ctl = C->GetController())
		{
			ControllerFaction.Add(Ctl, Faction);
			ControllerClass.Add(Ctl, TEXT("officer"));
		}
		Generals.Add(Faction, C);
		GeneralStations.Add(Faction, C->GetActorLocation());
		UE_LOG(LogTemp, Display, TEXT("PA250 general: %s commands the %s side from his post (aura %.0f m, a company officer's is %.0f m)"),
			UTF8_TO_TCHAR(Sk.second.commander_name.c_str()), *Faction.ToString(), Sk.second.commander_aura_radius_m, PA250UnitClasses::OfficerRadiusCm / 100.f);
	}
}

ARFCharacter* APA250BattleGameMode::SpawnBotAt(FName Faction, FName Charged, const FTransform& T, uint32 ClothingSeed)
{
	if (!Ledger.CanReinforce(Faction) || MenInReserve(Faction) <= 0)
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
	// Gun crews first, at their guns; they are part of the side's field strength.
	Count -= SpawnGunCrews(Faction, Count);
	// Militia muster (GAME INVENTION): only part of a side's militia is on the field at the start;
	// the rest arrive in companies (TickMuster). Everyone else deploys as before. Tickets are not
	// touched (they are spent on death), so BattleResult losses still never exceed the pools.
	const TMap<FName, int32>* P = InitialPool.Find(Faction);
	const UPA250BotTuning* BT = BotTuningAsset;
	const int32 MilitiaUnits = P ? P->FindRef(TEXT("militia")) : 0;
	const FString DefenderFaction = UTF8_TO_TCHAR(Setup.defender_faction.c_str());
	const bool bDefenderSide = Faction.ToString() == DefenderFaction;
	// Province Militia Muster adds defender companies; scenarios that already muster keep doing so
	// when the building is absent (no regression).
	const bool bBuildingMuster = Setup.militia_muster && bDefenderSide;
	const bool bDoMuster = P && BT && MilitiaUnits > 0 && (BT->Values.bMilitiaMuster || bBuildingMuster);
	if (!bDoMuster)
	{
		SpawnBots(Faction, Count);
		return;
	}
	int32 Total = 0;
	for (const auto& KV : *P) Total += KV.Value;
	int32 Others = 0, Initial = 0, Cap = 0;
	MusterPlan(Count, MilitiaUnits, Total, BT->Values.MusterInitialFraction, BT->Values.MusterMaxAliveScale, Others, Initial, Cap);
	if (bBuildingMuster)
	{
		Cap += FMath::Max(1, BT->Values.MusterWaveSize);
		UE_LOG(LogTemp, Display, TEXT("PA250 muster: Militia Muster building adds +%d cap for %s"),
			   BT->Values.MusterWaveSize, *Faction.ToString());
	}
	// An agent raised the alarm (BattleSetup alarmed): nobody is still mustering when the enemy comes.
	if (const auto AlarmSide = Setup.sides.find(ToStd(Faction)); AlarmSide != Setup.sides.end() && AlarmSide->second.alarmed)
	{
		AlarmMusteredAtOnce += FMath::Max(0, (Count - Others) - Initial);
		UE_LOG(LogTemp, Display, TEXT("PA250 muster: the alarm was raised: all %d militia places of the %s side are filled at the start (%d would have come later)"),
			   Count - Others, *Faction.ToString(), FMath::Max(0, (Count - Others) - Initial));
		Initial = Count - Others;
	}
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
		const int32 N = MusterWave(KV.Value, CountAlive(F, TEXT("militia")), PoolTotal(F) - Ledger.HeldFor(F), AliveSide, BT->Values.MusterWaveSize);
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
	if (Controller->IsA<APlayerController>() && Ledger.DrawHeld(Faction))
	{
		ShowNotice(TEXT("Last reserve - fall again and you fight on as one of the men still standing"), 6.f);
	}

	SpawnClassHint = Cls;
	FTransform T = PickSpawnTransform(Faction, PostOrNull);
	SpawnClassHint = NAME_None;
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
		if (Controller->IsA<APlayerController>()) PlacePlayerAtGun(C);
	}
	FrameHumanCamera(Cast<APlayerController>(Controller), NewPawn);
	return true;
}

bool APA250BattleGameMode::IsClassPickerOpen() const
{
	return SpawnScreen && SpawnScreen->IsInViewport();
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
		if (Hud) Hud->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void APA250BattleGameMode::CloseSpawnScreenToSpectate(APlayerController* PC)
{
	if (SpawnScreen)
	{
		SpawnScreen->RemoveFromParent();
	}
	if (Hud) Hud->SetVisibility(ESlateVisibility::Visible);
	if (!PC) return;
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
	// Out of reserves is not out of the fight: while any of his side still stand, the player becomes one of them.
	if (!bAutoplayBots && !bBattleOver)
	{
		const ARFCharacter* Mine = Cast<ARFCharacter>(PC->GetPawn());
		if ((!Mine || Mine->IsDead()) && TakeOverSoldier(PC))
		{
			return;
		}
		if (!Mine || Mine->IsDead())
		{
			ShowNotice(TEXT("No reserves and no men left standing - you can only watch"), 8.f);
		}
	}
	SpectateNextSoldier(PC, PC->GetPawn());
}

void APA250BattleGameMode::ShowNotice(const FString& Text, float Seconds)
{
	Notice = Text;
	NoticeUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Seconds : -1.f;
	UE_LOG(LogTemp, Display, TEXT("PA250 notice: %s"), *Text);
}

bool APA250BattleGameMode::TakeOverSoldier(APlayerController* PC)
{
	if (!PC || bBattleOver || !GetWorld())
	{
		return false;
	}
	const APawn* Old = PC->GetPawn();
	// The next living man of his own file first; with none, the nearest man of his side.
	const ARFCharacter* Fallen = Cast<ARFCharacter>(Old);
	ARFCharacter* Best = (Volleys && Fallen) ? Volleys->PickSuccessor(Fallen) : nullptr;
	const bool bFromFile = Best != nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (TActorIterator<ARFCharacter> It(GetWorld()); It && !bFromFile; ++It)
	{
		ARFCharacter* C = *It;
		// Never the general: he is taken only through the hero screen, with his kit and his meter.
		if (C->IsDead() || C->FactionId != PlayerFaction || C->IsMounted() || !Cast<APA250BotController>(C->GetController()) || IsGeneral(C)) continue;
		const float D = Old ? FVector::DistSquared(C->GetActorLocation(), Old->GetActorLocation()) : 0.f;
		if (D < BestD)
		{
			BestD = D;
			Best = C;
		}
	}
	if (!Best)
	{
		return false;
	}
	const FName Cls = Best->UnitClass;
	if (AController* BotCtl = Best->GetController())
	{
		BotCtl->StopMovement();
		BotCtl->UnPossess();
		ControllerFaction.Remove(BotCtl);
		ControllerClass.Remove(BotCtl);
		BotCtl->Destroy();
	}
	PC->Possess(Best); // leaves the body behind
	ControllerFaction.Add(PC, PlayerFaction);
	ControllerClass.Add(PC, Cls);
	// He is the player's man now: none of the bot's habits stay with him.
	Best->SetDucking(false);
	Best->MisfireChance = 0.f;
	Best->MisfireStress = 0.f;
	Best->SquadId = -1;
	Best->bInLine = false;
	Best->GetCharacterMovement()->MaxWalkSpeed = 380.f;
	if (Best->MeleeComponent) Best->MeleeComponent->SetGuardHeld(false);
	const FRotator Facing = Best->GetActorRotation();
	FrameHumanCamera(PC, Best);
	PC->SetControlRotation(FRotator(-10.f, Facing.Yaw, 0.f));
	PC->SetViewTarget(Best);
	if (bFromFile)
	{
		// His file is his still: the man he has become leads it.
		Volleys->TransferLeadership(Fallen, Best);
		int32 Men = 0, Loaded = 0, Presented = 0;
		EPA250SquadOrder Order = EPA250SquadOrder::None;
		Volleys->GetFileStatus(Best, Men, Loaded, Presented, Order);
		ShowNotice(FString::Printf(TEXT("You are down - you fight on as the next man of your file (%d with you)"), Men), 6.f);
		UE_LOG(LogTemp, Display, TEXT("PA250 file: the player takes the place of %s; %d men left in the file"), *Best->GetName(), Men);
		return true;
	}
	const int32 Left = CountAlive(PlayerFaction);
	ShowNotice(FString::Printf(TEXT("No reserves left - you take the place of a man still standing (%d left)"), Left), 6.f);
	return true;
}

FString APA250BattleGameMode::FileLineText(int32 Men, int32 Loaded, int32 Presented, const TCHAR* OrderWords, bool bVolleyRole, int32 WithYou)
{
	if (Men <= 0) return TEXT("Your file: no men left");
	// How many are at his side now: the proof an order is being obeyed (or that they are still coming up).
	const FString With = WithYou < 0 ? FString() : (WithYou >= Men ? FString(TEXT(" (all with you)")) : FString::Printf(TEXT(" (%d with you, %d coming up)"), WithYou, Men - WithYou));
	FString S = FString::Printf(TEXT("Your file: %d %s%s, %d loaded  -  %s"), Men, Men == 1 ? TEXT("man") : TEXT("men"), *With, Loaded, OrderWords);
	if (Presented > 0) S += FString::Printf(TEXT("  -  %d PRESENTED: your shot fires them"), Presented);
	else S += bVolleyRole ? TEXT("  -  hold aim to make ready") : TEXT("  -  hold aim: they fire at will on your man");
	return S + TEXT("\n1 follow   2 line here   3 hold   4 cover   5 charge   6 fall back");
}

FVector APA250BattleGameMode::ResolveLeaderAim(const FVector& Eye, const FVector& HitPoint, const TArray<FVector>& Enemies, float RayCm)
{
	FVector Best = HitPoint;
	float BestAlong = TNumericLimits<float>::Max();
	const FVector Dir = (HitPoint - Eye).GetSafeNormal();
	for (const FVector& E : Enemies)
	{
		if (FMath::PointDistToSegment(E, Eye, HitPoint) > RayCm) continue;
		const float Along = FVector::DotProduct(E - Eye, Dir);
		if (Along > 0.f && Along < BestAlong)
		{
			BestAlong = Along;
			Best = E;
		}
	}
	return Best;
}

FString APA250BattleGameMode::GetFileLine(const ARFCharacter* Soldier) const
{
	int32 Men = 0, Loaded = 0, Presented = 0;
	EPA250SquadOrder Order = EPA250SquadOrder::None;
	int32 WithYou = 0;
	if (!Volleys || !Soldier || !Volleys->GetFileStatus(Soldier, Men, Loaded, Presented, Order, &WithYou)) return FString();
	const bool bVolleyRole = CombatProfilesAsset ? !CombatProfilesAsset->RoleFor(Soldier->FactionId, Soldier->UnitClass).bIndividualFire : true;
	return FileLineText(Men, Loaded, Presented, PA250Squad::OrderWords(Order), bVolleyRole, WithYou);
}

void APA250BattleGameMode::GivePlayerOrder(ARFCharacter* Soldier, int32 OrderIndex)
{
	static const EPA250SquadOrder Keys[] = {EPA250SquadOrder::FollowMe, EPA250SquadOrder::FormLine, EPA250SquadOrder::Hold,
		EPA250SquadOrder::TakeCover, EPA250SquadOrder::Charge, EPA250SquadOrder::FallBack};
	if (bBattleOver || !Volleys || !Soldier || Soldier->IsDead() || OrderIndex < 1 || OrderIndex > 6) return;
	const EPA250SquadOrder Order = Keys[OrderIndex - 1];
	// At a gun he serves, "follow me" has its crew run it up after him and "hold" stands it again.
	if (APA250FieldGun* Gun = GunAt(Soldier); Gun && Gun->GetPlayerGunner() == Soldier && (Order == EPA250SquadOrder::FollowMe || Order == EPA250SquadOrder::Hold))
	{
		Gun->SetFollowPlayer(Order == EPA250SquadOrder::FollowMe);
		ShowNotice(Order == EPA250SquadOrder::FollowMe ? TEXT("Run her up! Walk ahead and the crew pushes the gun after you")
			: TEXT("The gun stands"), 3.f);
	}
	const int32 Took = Volleys->GiveOrder(Soldier, Order, LeaderAim);
	if (Took <= 0)
	{
		ShowNotice(TEXT("You have no men to order"), 2.5f);
		return;
	}
	ShowNotice(Took > 1 ? FString::Printf(TEXT("%s!  (%d squads obey)"), PA250Squad::OrderWords(Order), Took)
		: FString::Printf(TEXT("%s!"), PA250Squad::OrderWords(Order)), 2.5f);
	UE_LOG(LogTemp, Display, TEXT("PA250 order: %s -> %d squad(s)"), PA250Squad::OrderWords(Order), Took);
}

void APA250BattleGameMode::TickPlayerFile(float DeltaSeconds)
{
	if (!Volleys || bAutoplayBots || bOpeningClassPick) return;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (!Me || Me->IsDead() || Me->FactionId != PlayerFaction || Me->IsMounted()) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	FileTimer -= DeltaSeconds;
	if (FileTimer <= 0.f)
	{
		FileTimer = FMath::Max(0.2f, Verbs.SquadEnsureSeconds);
		Volleys->EnsureLeaderFile(Me);
	}
	// Where he is looking: the man on his line of sight, else the ground or wall it strikes.
	FVector Eye;
	FRotator Rot;
	PC->GetPlayerViewPoint(Eye, Rot);
	const FVector End = Eye + Rot.Vector() * 20000.f;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(PA250LeaderAim), false, Me);
	if (Me->Musket) Q.AddIgnoredActor(Me->Musket);
	const FVector Struck = GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Q) ? Hit.ImpactPoint : End;
	TArray<FVector> Enemies;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->FactionId != PlayerFaction && !It->FactionId.IsNone()) Enemies.Add(It->GetActorLocation());
	}
	LeaderAim = ResolveLeaderAim(Eye, Struck, Enemies, Verbs.SquadAimRayM * 100.f);
	Volleys->UpdateLeader(Me, Me->IsAiming(), LeaderAim);
}

void APA250BattleGameMode::TickLedVolleyProbe(float DeltaSeconds)
{
	if (ProbeSide.IsNone() || !Volleys) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Now = GetWorld()->GetTimeSeconds();
	ARFCharacter* L = ProbeLeader.Get();
	if (!L || L->IsDead())
	{
		// A new leader: a man of the side whose class volleys.
		L = nullptr;
		for (TActorIterator<ARFCharacter> It(GetWorld()); It && !L; ++It)
		{
			if (It->IsDead() || It->FactionId != ProbeSide || !Cast<APA250BotController>(It->GetController())) continue;
			if (PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bVolleyShock) L = *It;
		}
		if (!L) return;
		ProbeLeader = L;
		ProbeHold = 0.f;
		if (APA250BotController* Bot = Cast<APA250BotController>(L->GetController())) Bot->bScriptedHold = true;
		const int32 Men = Volleys->EnsureLeaderFile(L);
		Volleys->GiveOrder(L, EPA250SquadOrder::Hold, L->GetActorLocation() + L->GetActorForwardVector() * 1000.f);
		UE_LOG(LogTemp, Display, TEXT("PA250 led volley probe: %s (%s) leads %d men"), *L->GetName(), *ProbeSide.ToString(), Men);
	}
	else
	{
		Volleys->EnsureLeaderFile(L);
		// He repeats his order now and then, as a leader does (and so a musician who has come up can carry it on).
		if (Now >= ProbeOrderAt)
		{
			ProbeOrderAt = Now + 20.f;
			Volleys->GiveOrder(L, EPA250SquadOrder::Hold, L->GetActorLocation() + L->GetActorForwardVector() * 1000.f);
		}
	}
	// Did the squad he fired on waver?
	if (ProbeTargetSquad >= 0)
	{
		const EPA250Morale M = Volleys->GetSquadMorale(ProbeTargetSquad);
		if (M != EPA250Morale::Steady)
		{
			++ProbeShook;
			UE_LOG(LogTemp, Display, TEXT("PA250 led volley probe: squad %d fired on is %s (%d of %d volleys shook their target)"), ProbeTargetSquad, MoraleName(M), ProbeShook, ProbeVolleys);
			ProbeTargetSquad = -1;
		}
		else if (Now > ProbeWatchUntil)
		{
			ProbeTargetSquad = -1;
		}
	}
	// His aim: the nearest living enemy within a volley's reach.
	ARFCharacter* Foe = nullptr;
	float BestD = Verbs.VolleyReachM * 100.f;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() || It->FactionId == ProbeSide || It->FactionId.IsNone()) continue;
		const float D = FVector::Dist(It->GetActorLocation(), L->GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Foe = *It;
		}
	}
	if (!Foe)
	{
		Volleys->UpdateLeader(L, false, L->GetActorLocation());
		ProbeHold = 0.f;
		return;
	}
	L->SetActorRotation(FRotator(0.f, (Foe->GetActorLocation() - L->GetActorLocation()).Rotation().Yaw, 0.f));
	Volleys->UpdateLeader(L, true, Foe->GetActorLocation());
	int32 Men = 0, Loaded = 0, Presented = 0;
	EPA250SquadOrder Order = EPA250SquadOrder::None;
	Volleys->GetFileStatus(L, Men, Loaded, Presented, Order);
	ProbeHold = Presented >= Verbs.VolleyMinShooters ? ProbeHold + DeltaSeconds : 0.f;
	if (ProbeHold >= 0.5f && Now >= ProbeNextAt && ProbeTargetSquad < 0)
	{
		const int32 TargetSquad = Foe->SquadId;
		const bool bSteady = Volleys->GetSquadMorale(TargetSquad) == EPA250Morale::Steady;
		if (L->Musket && L->Musket->CanFire()) L->FireMusketAt(Foe->GetActorLocation() + FVector(0.f, 0.f, 20.f));
		if (Volleys->LeaderFired(L))
		{
			++ProbeVolleys;
			// Only a squad that stood steady can be seen to waver.
			if (bSteady && TargetSquad >= 0)
			{
				ProbeTargetSquad = TargetSquad;
				ProbeWatchUntil = Now + Verbs.VolleyShockSeconds + 2.f;
			}
			UE_LOG(LogTemp, Display, TEXT("PA250 led volley probe: volley %d, %d presented, target squad %d at %.0f m (%s)"), ProbeVolleys, Presented, TargetSquad, BestD / 100.f,
				bSteady ? TEXT("steady") : TEXT("already wavering"));
		}
		ProbeHold = 0.f;
		ProbeNextAt = Now + 8.f;
	}
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
		// RFGameMode expects an actor. Its own marker, not a level start: moving a PlayerStart here
		// (as this did) walked the side's start ground onto whatever post was last spawned at.
		if (!RespawnAnchor.IsValid())
		{
			RespawnAnchor = GetWorld()->SpawnActor<ATargetPoint>(T.GetLocation(), T.Rotator());
		}
		if (AActor* Anchor = RespawnAnchor.Get())
		{
			Anchor->SetActorTransform(T);
			return Anchor;
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

int32 APA250BattleGameMode::MenInReserve(FName Faction) const
{
	int32 Down = 0;
	for (const auto& CK : Ledger.Deaths.FindRef(Faction)) Down += CK.Value;
	for (const auto& CK : Prisoners.FindRef(Faction)) Down += CK.Value;
	return FMath::Max(0, InitialSoldiers(Faction) - Down - CountAlive(Faction));
}

int32 APA250BattleGameMode::PoolTotal(FName Faction) const
{
	// Tickets, but never more than the men the army still has off the field.
	return FMath::Min(Ledger.Remaining(Faction), MenInReserve(Faction));
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
		if (!It->IsDead() && It->FactionId == Faction && It->UnitClass == UnitClass && !It->ActorHasTag(TEXT("PA250GunMate")))
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
		// A post closed by the assault or the landing is still somebody's.
		if (WP.IsValid() && (!WP->IsContestLocked() || PA250Modes::IsOrdered(ScenarioFight.Mode)) && WP->GetHolder() == Faction) ++N;
	}
	return N;
}

bool APA250BattleGameMode::HoldsLandingSpawn(FName Faction) const
{
	return Boats.Num() > 0 && FirstBoatInAt < 0.f && Faction == ToName(Setup.attacker_faction);
}

void APA250BattleGameMode::SetUpLanding()
{
	UWorld* World = GetWorld();
	if (!World || !PA250Modes::IsLanding(ScenarioFight.Mode)) return;
	const FName Attacker = ToName(Setup.attacker_faction);
	TArray<ARFCharacter*> Men;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		if (!It->IsDead() && It->FactionId == Attacker && !It->IsPlayerControlled() && !It->IsMounted()) Men.Add(*It);
	}
	const int32 Count = PA250Boats::BoatsFor(Men.Num(), Rules.landing_men_per_boat, Rules.landing_boats);
	if (Count <= 0) return;
	const FVector Rear = GetRearPoint(Attacker, FVector::ZeroVector);
	const float RowOut = Rules.landing_row_out_m * 100.f;
	// Courses: the level's landing places nearest the attacker's start, else abreast by his start.
	TArray<AActor*> Marks;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("PA250BoatLanding"))) Marks.Add(*It);
	}
	Marks.Sort([&Rear](const AActor& A, const AActor& B) {
		const float DA = FVector::DistSquared2D(A.GetActorLocation(), Rear), DB = FVector::DistSquared2D(B.GetActorLocation(), Rear);
		return DA != DB ? DA < DB : A.GetActorLocation().X < B.GetActorLocation().X;
	});
	const bool bMarked = Marks.Num() >= Count;
	FVector Seaward = FVector::ZeroVector;
	if (!bMarked)
	{
		const FVector PostLoc = (ModeOrder.Num() > 0 && Posts[ModeOrder[0]].IsValid()) ? Posts[ModeOrder[0]]->GetActorLocation() : Rear + FVector(1000.f, 0.f, 0.f);
		Seaward = (Rear - PostLoc).GetSafeNormal2D();
		if (Seaward.IsNearlyZero()) Seaward = FVector::BackwardVector;
		UE_LOG(LogTemp, Warning, TEXT("PA250 landing: %s has no PA250BoatLanding markers for %d boats; they land abreast by the attacker's start (over whatever ground is there)"),
			*World->GetMapName(), Count);
	}
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Start, Beach, Inland;
		if (bMarked)
		{
			Beach = Marks[i]->GetActorLocation();
			Inland = Marks[i]->GetActorForwardVector().GetSafeNormal2D();
			Start = Beach - Inland * RowOut;
		}
		else
		{
			PA250Modes::BoatCourse(i, Count, Rear + Seaward * 500.f - FVector(0.f, 0.f, 60.f), Seaward, Rules.landing_boat_spacing_cm, RowOut, Start, Beach);
			Inland = -Seaward;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APA250RowBoat* Boat = World->SpawnActor<APA250RowBoat>(Start, FRotator::ZeroRotator, Params);
		if (!Boat) continue;
		Boat->Launch(Attacker, Start, Beach, Inland, Rules.landing_men_per_boat);
		Boats.Add(Boat);
	}
	MenEmbarked = 0;
	int32 Ashore = 0;
	for (ARFCharacter* Man : Men)
	{
		bool bSeated = false;
		for (APA250RowBoat* Boat : Boats)
		{
			if (Boat && Boat->Embark(Man))
			{
				bSeated = true;
				break;
			}
		}
		(bSeated ? MenEmbarked : Ashore)++;
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 landing: %d %s men in %d boats lying %.0f m off (%s)%s"), MenEmbarked, *Attacker.ToString(), Boats.Num(), Rules.landing_row_out_m,
		bMarked ? TEXT("marked landing places") : TEXT("made-up landing places"), Ashore > 0 ? *FString::Printf(TEXT("; %d more than the boats hold start ashore"), Ashore) : TEXT(""));
}

void APA250BattleGameMode::TickLanding(float DeltaSeconds)
{
	if (Boats.Num() == 0 || bBattleOver) return;
	const FName Attacker = ToName(Setup.attacker_faction);
	const bool bPlayerAttacks = !bAutoplayBots && PlayerFaction == Attacker;
	// The player, if he lands with them, takes a place in a boat that is still off.
	if (bPlayerAttacks && !bPlayerRode && FirstBoatInAt < 0.f)
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		if (Me && !Me->IsDead() && Me->FactionId == Attacker && !Me->bEmbarked && !Me->IsMounted() && !bOpeningClassPick)
		{
			for (APA250RowBoat* Boat : Boats)
			{
				if (Boat && Boat->Embark(Me))
				{
					bPlayerRode = true;
					ShowNotice(TEXT("You are in the boats. Nobody fires from a boat: you are set ashore when the keel touches."), 7.f);
					break;
				}
			}
			// Every boat full: the last man of the last boat gives him his place.
			if (!bPlayerRode) bPlayerRode = true;
		}
	}
	if (!bBoatsRowing)
	{
		BoatsWaited += DeltaSeconds;
		if (!bPlayerAttacks || bPlayerRode || BoatsWaited >= Rules.landing_player_wait_seconds)
		{
			bBoatsRowing = true;
			for (APA250RowBoat* Boat : Boats)
			{
				if (Boat) Boat->StartRowing();
			}
			PushKillFeed(bPlayerAttacks ? TEXT("Give way together! The boats pull for the shore") : TEXT("BOATS ARE PULLING FOR THE SHORE"));
			UE_LOG(LogTemp, Display, TEXT("PA250 landing: %d boats give way at %.0f s"), Boats.Num(), GetWorld()->GetTimeSeconds() - StartTime);
		}
	}
	int32 Ashore = 0;
	bool bAnyIn = false;
	for (const APA250RowBoat* Boat : Boats)
	{
		if (!Boat) continue;
		Ashore += Boat->GetSetAshore();
		bAnyIn = bAnyIn || Boat->GetState() == EPA250BoatState::Empty;
	}
	MenLanded = Ashore;
	if (bAnyIn && FirstBoatInAt < 0.f)
	{
		FirstBoatInAt = GetWorld()->GetTimeSeconds() - StartTime;
		PushKillFeed(bPlayerAttacks ? TEXT("We are ashore!") : TEXT("THEY ARE ASHORE"));
		UE_LOG(LogTemp, Display, TEXT("PA250 landing: the first boat is in at %.0f s; %d replacements held back now come ashore"), FirstBoatInAt, HeldLandingSpawns);
		const int32 Held = HeldLandingSpawns;
		HeldLandingSpawns = 0;
		for (int32 i = 0; i < Held; ++i) SpawnBot(Attacker, TEXT("line_infantry"), nullptr);
	}
}

APA250ControlPoint* APA250BattleGameMode::GetModeOpenPost() const
{
	if (!PA250Modes::IsOrdered(ScenarioFight.Mode) || ModeOrder.Num() != Posts.Num()) return nullptr;
	const FName Attacker = ToName(Setup.attacker_faction);
	if (PA250Modes::IsLanding(ScenarioFight.Mode))
	{
		return (!bLanded && Posts.IsValidIndex(ModeOrder[0])) ? Posts[ModeOrder[0]].Get() : nullptr;
	}
	TArray<FName> Holders;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts) Holders.Add(WP.IsValid() ? WP->GetHolder() : NAME_None);
	const int32 Open = PA250Modes::OpenPost(ModeOrder, Holders, Attacker);
	return Posts.IsValidIndex(Open) ? Posts[Open].Get() : nullptr;
}

void APA250BattleGameMode::TickBattleMode()
{
	if (!PA250Modes::IsOrdered(ScenarioFight.Mode) || ModeOrder.Num() != Posts.Num() || bBattleOver) return;
	const FName Attacker = ToName(Setup.attacker_faction);
	const FName Defender = ToName(Setup.defender_faction);
	TArray<FName> Holders;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts) Holders.Add(WP.IsValid() ? WP->GetHolder() : NAME_None);
	const bool bOurAttack = Attacker == PlayerFaction;
	if (PA250Modes::IsLanding(ScenarioFight.Mode) && !bLanded && Holders.IsValidIndex(ModeOrder[0]) && Holders[ModeOrder[0]] == Attacker)
	{
		bLanded = true;
		PushKillFeed(bOurAttack ? TEXT("The landing place is ours: push inland") : TEXT("THEY HOLD THE LANDING PLACE"));
		UE_LOG(LogTemp, Display, TEXT("PA250 mode: landing made good at %.0f s; every post is open"), GetWorld()->GetTimeSeconds() - StartTime);
	}
	const int32 Carried = PA250Modes::PostsCarried(Holders, Attacker);
	if (PA250Modes::IsAssault(ScenarioFight.Mode) && Carried > ModeCarried)
	{
		// Each post carried buys the attack more time.
		const float Add = Rules.assault_seconds_per_post * (Carried - ModeCarried);
		TimeLimitSeconds += Add;
		ModeClockAdded += Add;
		PushKillFeed(bOurAttack ? FString::Printf(TEXT("Post carried! (%d of %d)"), Carried, Posts.Num()) : FString::Printf(TEXT("THEY HAVE CARRIED A POST (%d of %d)"), Carried, Posts.Num()));
		UE_LOG(LogTemp, Display, TEXT("PA250 mode: assault carries post %d of %d at %.0f s; clock +%.0f s"), Carried, Posts.Num(), GetWorld()->GetTimeSeconds() - StartTime, Add);
	}
	ModeCarried = Carried;
	const TArray<bool> Locked = PA250Modes::LockedPosts(ScenarioFight.Mode, ModeOrder, Holders, Attacker, bLanded);
	for (int32 i = 0; i < Posts.Num(); ++i)
	{
		if (Posts[i].IsValid()) Posts[i]->SetContestLocked(Locked.IsValidIndex(i) && Locked[i]);
	}
	if (PA250Modes::IsAssault(ScenarioFight.Mode) && Posts.Num() > 0 && Carried >= Posts.Num())
	{
		EndBattle(Attacker, TEXT("posts_swept"));
	}
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
	if (PA250Modes::IsLanding(ScenarioFight.Mode) && !bLanded) return 0;
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
	// A landing is not worn down for posts it has had no chance to reach.
	if (PA250Modes::IsLanding(ScenarioFight.Mode) && !bLanded) return;
	// Battlefront-style: the bleed grows with the post advantage, fastest when one side holds them all.
	Ledger.Bleed(DeltaSeconds, BleedIntervalSeconds,
		TEXT("british"), PostsOwned(TEXT("british")), TEXT("continental"), PostsOwned(TEXT("continental")), CountPosts());
}

void APA250BattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FPA250BattleAudio::StopAmbience();
	if (bRestoreScreenMessages)
	{
		GAreScreenMessagesEnabled = true;
		bRestoreScreenMessages = false;
	}
	Super::EndPlay(EndPlayReason);
}

bool APA250BattleGameMode::SideIsOut(int32 Pool, int32 InitialUnits, bool bEverFielded, int32 Alive, bool bHumanAliveOnSide)
{
	// Every man standing is a ticket (user decision 2026-11-13): a side is beaten only when its
	// reserve is spent AND nobody of it is left on the field. InitialUnits no longer decides.
	(void)InitialUnits;
	if (bHumanAliveOnSide || Pool > 0 || Alive > 0) return false;
	return bEverFielded;
}

bool APA250BattleGameMode::RemnantBreaks(int32 Pool, int32 Alive, bool bHumanAliveOnSide, int32 RemnantMen)
{
	return !bHumanAliveOnSide && Pool <= 0 && Alive > 0 && Alive <= RemnantMen;
}

bool APA250BattleGameMode::IsMarkedRemnant(FName Faction) const
{
	if (bBattleOver || PoolTotal(Faction) > 0) return false;
	const int32 Alive = CountAlive(Faction);
	return Alive > 0 && Alive <= Rules.remnant_mark_men;
}

float APA250BattleGameMode::RemnantCountdown(FName Faction) const
{
	if (bBattleOver) return -1.f; // the bar must not go on counting under the victory banner
	const float* T = RemnantTime.Find(Faction);
	return (T && *T > 0.f) ? FMath::Max(0.f, Rules.remnant_seconds - *T) : -1.f;
}

void APA250BattleGameMode::TickRemnants(float DeltaSeconds)
{
	// The last handful of a side with no reserve does not fight to the last man in a hedge: it
	// breaks. Without this a battle whose last enemies were out of sight (or stuck) ran to the clock.
	bool bHumanAlive = false;
	if (!bAutoplayBots)
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		bHumanAlive = Me && !Me->IsDead() && Me->FactionId == PlayerFaction;
	}
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		float& T = RemnantTime.FindOrAdd(F);
		if (!RemnantBreaks(PoolTotal(F), CountAlive(F), bHumanAlive && F == PlayerFaction, Rules.remnant_men))
		{
			T = 0.f;
			continue;
		}
		if (T <= 0.f)
		{
			PushKillFeed(FString::Printf(TEXT("The last %s are breaking"), F == TEXT("british") ? TEXT("regulars") : TEXT("provincials")));
			UE_LOG(LogTemp, Display, TEXT("PA250 remnant: %s has %d men and no reserve; breaks in %.0f s"), *F.ToString(), CountAlive(F), Rules.remnant_seconds);
		}
		T += DeltaSeconds;
		if (T >= Rules.remnant_seconds)
		{
			EndBattle(F == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")), TEXT("routed"));
			return;
		}
	}
}

int32 APA250BattleGameMode::GetPrisonersTaken(FName Faction) const
{
	// Prisoners[f] are men OF f taken; "taken by Faction" are the other side's.
	int32 N = 0;
	if (const TMap<FName, int32>* Side = Prisoners.Find(OtherFaction(Faction)))
	{
		for (const TPair<FName, int32>& KV : *Side) N += KV.Value;
	}
	return N;
}

void APA250BattleGameMode::TakePrisoner(ARFCharacter* Prisoner, const ARFCharacter* Captor)
{
	// Destroy() waits until the end of the frame, so a second dragoon in the same pass would
	// count this man again. The tag is the mark that he has already surrendered.
	if (!Prisoner || Prisoner->IsDead() || Prisoner->Tags.Contains(TEXT("PA250Prisoner"))) return;
	Prisoner->Tags.Add(TEXT("PA250Prisoner"));
	const FName F = Prisoner->FactionId;
	Prisoners.FindOrAdd(F).FindOrAdd(Prisoner->UnitClass)++;
	// He leaves the field alive: a man gone from the side's strength, not a death, and nobody takes his place.
	if (int32* Pool = Ledger.Pool.Find(F); Pool && *Pool > Ledger.HeldFor(F)) --(*Pool);
	PushKillFeed(F == PlayerFaction
		? FString::Printf(TEXT("One of ours surrenders to %s"), Captor ? PA250UnitClasses::KitFor(Captor->FactionId, Captor->UnitClass).Label : TEXT("the enemy"))
		: FString::Printf(TEXT("A routing %s surrenders"), PA250UnitClasses::KitFor(F, Prisoner->UnitClass).Label));
	if (AController* C = Prisoner->GetController())
	{
		ControllerFaction.Remove(C);
		ControllerClass.Remove(C);
		C->UnPossess();
		C->Destroy();
	}
	Prisoner->Destroy();
}

void APA250BattleGameMode::TickClassVerbs()
{
	// Twice a second, with the officers' steadying (TickOfficers sets OfficerTimer to 0.5 just before).
	if (!Volleys || !FMath::IsNearlyEqual(OfficerTimer, 0.5f)) return;
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Now = GetWorld()->GetTimeSeconds();
	TArray<ARFCharacter*> Men;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && !It->FactionId.IsNone()) Men.Add(*It);
	}
	for (ARFCharacter* C : Men)
	{
		if (!IsValid(C) || C->IsDead() || C->Tags.Contains(TEXT("PA250Prisoner"))) continue;
		const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(C->FactionId, C->UnitClass);
		// The officer's verb, for the bots: rally the nearest routing squad of his side.
		if (Kit.bRallies && !C->IsPlayerControlled() && Now >= RallyReadyAt.FindRef(C)
			&& Volleys->RallyNearestRouting(C->FactionId, C->GetActorLocation(), Verbs.RallyRadiusM * 100.f))
		{
			RallyReadyAt.Add(C, Now + Verbs.RallyCooldownSeconds);
			if (C->FactionId == PlayerFaction) PushKillFeed(TEXT("An officer rallies the men"));
		}
		// The dragoon's verb: in the saddle, a routing enemy he overtakes surrenders.
		if (Kit.bTakesPrisoners && C->IsMounted())
		{
			for (ARFCharacter* Foe : Men)
			{
				if (!IsValid(Foe) || Foe->IsDead() || Foe->FactionId == C->FactionId || Foe->IsPlayerControlled() || Foe->IsMounted()) continue;
				if (FVector::DistSquared2D(Foe->GetActorLocation(), C->GetActorLocation()) > FMath::Square(Verbs.SurrenderRadiusM * 100.f)) continue;
				if (Volleys->IsRouting(Foe)) TakePrisoner(Foe, C);
			}
		}
	}
}

void APA250BattleGameMode::UseClassAbility(ARFCharacter* Soldier)
{
	if (!Soldier || Soldier->IsDead() || bBattleOver || !Volleys) return;
	const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(Soldier->FactionId, Soldier->UnitClass);
	const FPA250ClassVerbs& Verbs = PA250UnitClasses::Verbs();
	const float Now = GetWorld()->GetTimeSeconds();
	if (APA250FieldGun* Gun = GunAt(Soldier))
	{
		// Anyone may serve a gun of his side (a gunner serves it at full drill): T takes his place at
		// it or leaves it. At the gun his trigger fires it and his reload key works the drill.
		if (Gun->GetPlayerGunner() == Soldier)
		{
			Gun->SetPlayerGunner(nullptr);
			ShowNotice(TEXT("You leave the gun"), 2.f);
		}
		else
		{
			Gun->ForceClaimPost(Soldier);
			Gun->SetPlayerGunner(Soldier);
			const int32 Crew = EnsureGunCrew(Gun, Soldier);
			ShowNotice(FString::Printf(TEXT("You serve the gun (crew %d). %s"), Crew, *Gun->LoadChip()), 5.f);
		}
		return;
	}
	if (Kit.bBuildsWorks)
	{
		// The sapper's verb: a spell of labour on the work he stands at, feet planted.
		APA250FieldWork* Work = NearestWorkNeedingLabour(Soldier->GetActorLocation(), Verbs.SapperReachM * 100.f);
		if (!Work)
		{
			ShowNotice(TEXT("No work within reach needs you: stand at a marked place or a battered bank"), 3.f);
			return;
		}
		if (!SideHoldsGroundAt(Soldier->FactionId, Work->GetActorLocation()))
		{
			ShowNotice(TEXT("You can only dig on ground your side holds"), 3.f);
			return;
		}
		if (Now < PlayerAbilityReadyAt) return;
		PlayerAbilityReadyAt = Now + Verbs.SapperBurstSeconds;
		Soldier->LockMovement(Verbs.SapperBurstSeconds);
		LabourOnWork(Work, Verbs.SapperBurstSeconds, Soldier);
		ShowNotice(FString::Printf(TEXT("You dig: the work stands to %d %% of its height%s"), FMath::RoundToInt(Work->GetStandingShare() * 100.f),
			Work->GivesCover() ? TEXT(" and stops a ball") : TEXT("")), Verbs.SapperBurstSeconds);
		return;
	}
	const bool bOrders = Kit.bRallies || Kit.bVolleyShock || Kit.bChargeRouts;
	if (!bOrders)
	{
		// A passive verb: say what it is.
		ShowNotice(FString(Kit.Verb), 5.f);
		return;
	}
	// As the general the player rallies every routing squad within the hero's radius (its own cooldown);
	// with none routing, or the rally not ready, T is the officer's ordinary word below.
	if (IsPlayedGeneral(Soldier) && UseHeroRally(Soldier)) return;
	if (Now < PlayerAbilityReadyAt)
	{
		ShowNotice(FString::Printf(TEXT("Not yet: %.0f s"), PlayerAbilityReadyAt - Now), 2.f);
		return;
	}
	const FVector Here = Soldier->GetActorLocation();
	const float Reach = Verbs.OrderRadiusM * 100.f;
	if (Kit.bRallies && Volleys->RallyNearestRouting(Soldier->FactionId, Here, Verbs.RallyRadiusM * 100.f))
	{
		PlayerAbilityReadyAt = Now + Verbs.RallyCooldownSeconds;
		ShowNotice(TEXT("You rally the men: they stop and re-form"), 4.f);
		NoteHeroEvent(EPA250HeroEvent::Rally);
		return;
	}
	// An officer may order either; a regular calls his line's volley; a grenadier leads the charge.
	const FString Order = Volleys->OrderNearestLine(Soldier->FactionId, Here, Reach, Kit.bRallies || Kit.bVolleyShock, Kit.bRallies || Kit.bChargeRouts);
	if (Order == TEXT("volley"))
	{
		PlayerAbilityReadyAt = Now + 8.f;
		ShowNotice(TEXT("\"Present... FIRE!\" - the line volleys on your word"), 3.f);
	}
	else if (Order == TEXT("charge"))
	{
		PlayerAbilityReadyAt = Now + 12.f;
		ShowNotice(TEXT("\"Charge bayonets!\" - the line goes in"), 3.f);
	}
	else
	{
		ShowNotice(Kit.bRallies ? TEXT("No routing squad to rally and no line within 30 m with an enemy in front of it")
			: TEXT("No line within 30 m that can obey: it needs an enemy in front of it"), 3.f);
	}
}

FString APA250BattleGameMode::GetAbilityLine(const ARFCharacter* Soldier) const
{
	if (!Soldier) return FString();
	const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(Soldier->FactionId, Soldier->UnitClass);
	if (FString(Kit.Verb).IsEmpty()) return FString();
	const float Left = PlayerAbilityReadyAt - (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
	if (const APA250FieldGun* Gun = GunAt(Soldier))
	{
		return Gun->GetPlayerGunner() == Soldier
			? Gun->StatusLine()
			: FString(TEXT("T  Serve this gun (look lays · [ ] load · fire · R drill)"));
	}
	if (Kit.bServesGuns) return TEXT("Gunner: Deploy places you at a gun; or go to one and press T to serve it");
	if (Kit.bBuildsWorks) return TEXT("T  Dig: stand at a marked place or a battered bank and raise the work");
	if (Kit.bRallies) return Left > 0.f ? FString::Printf(TEXT("T  Rally / order: %.0f s"), Left) : FString(TEXT("T  Rally a routing squad, or order the nearest line to volley or charge"));
	if (Kit.bVolleyShock) return Left > 0.f ? FString::Printf(TEXT("T  Call a volley: %.0f s"), Left) : FString(TEXT("T  Call your line's volley: 3 muskets together shake the squad they hit"));
	if (Kit.bChargeRouts) return Left > 0.f ? FString::Printf(TEXT("T  Order the charge: %.0f s"), Left) : FString(TEXT("T  Order the charge: a squad it reaches checks or breaks"));
	if (Kit.bSteadyInCover) return TEXT("Home ground: steady and truer behind walls and fences, quickly shaken in the open");
	if (Kit.bOfficerHunter) return TEXT("Pick off an officer: his squad is shaken");
	if (Kit.bForwardSpawn) return TEXT("Takes posts fastest; may come on at a post close to the enemy");
	if (Kit.bTakesPrisoners) return Soldier->IsMounted() ? FString(TEXT("In the saddle: ride down routers and they surrender")) : FString(TEXT("Mount (H) to ride down routers"));
	return FString(Kit.Verb);
}

void APA250BattleGameMode::ArrangeMatchup(float MetersApart)
{
	// Two lines facing each other, each man two metres from the next, the given distance apart, on
	// ground where each line can see the other: the field is searched for an open, level axis
	// (walls, houses and the rolling ground away from the Green block a 150 m line of sight).
	// The squads then keep that ground (UPA250VolleyCoordinator::bHoldGround).
	UWorld* World = GetWorld();
	const float Half = MetersApart * 50.f;
	auto Ground = [World](const FVector& At, FVector& Out) {
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250MatchupGround), false);
		for (TActorIterator<ARFCharacter> It(World); It; ++It) Params.AddIgnoredActor(*It);
		if (!World->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, 20000.f), FVector(At.X, At.Y, -20000.f), ECC_Visibility, Params)) return false;
		Out = Hit.ImpactPoint;
		return true;
	};
	auto Clear = [World](const FVector& From, const FVector& To) {
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250MatchupSight), false);
		for (TActorIterator<ARFCharacter> It(World); It; ++It) Params.AddIgnoredActor(*It);
		return !World->LineTraceSingleByChannel(Hit, From + FVector(0.f, 0.f, 150.f), To + FVector(0.f, 0.f, 100.f), ECC_Visibility, Params);
	};
	TArray<FVector> Centers;
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (WP.IsValid()) Centers.Add(WP->GetActorLocation());
	}
	if (Centers.Num() == 0) Centers.Add(FVector::ZeroVector);
	const int32 NumPosts = Centers.Num();
	for (int32 i = 0; i < NumPosts; ++i)
	{
		for (const FVector& Off : {FVector(6000.f, 0.f, 0.f), FVector(-6000.f, 0.f, 0.f), FVector(0.f, 6000.f, 0.f), FVector(0.f, -6000.f, 0.f)}) Centers.Add(Centers[i] + Off);
	}
	FVector BestA = Centers[0] - FVector(Half, 0.f, 0.f), BestB = Centers[0] + FVector(Half, 0.f, 0.f);
	float BestRise = TNumericLimits<float>::Max();
	for (const FVector& Center : Centers)
	{
		for (int32 Deg = 0; Deg < 180; Deg += 15)
		{
			const FVector Dir(FMath::Cos(FMath::DegreesToRadians(float(Deg))), FMath::Sin(FMath::DegreesToRadians(float(Deg))), 0.f);
			const FVector Side(-Dir.Y, Dir.X, 0.f);
			FVector A, B;
			if (!Ground(Center - Dir * Half, A) || !Ground(Center + Dir * Half, B)) continue;
			// The whole width of both lines must see across, both ways.
			bool bOpen = true;
			for (float Lat : {-900.f, -450.f, 0.f, 450.f, 900.f})
			{
				FVector LA, LB;
				bOpen = bOpen && Ground(A + Side * Lat, LA) && Ground(B + Side * Lat, LB) && Clear(LA, LB) && Clear(LB, LA)
					&& FMath::Abs(LA.Z - A.Z) < 150.f && FMath::Abs(LB.Z - B.Z) < 150.f;
			}
			const float Rise = FMath::Abs(A.Z - B.Z);
			if (bOpen && Rise < BestRise)
			{
				BestRise = Rise;
				BestA = A;
				BestB = B;
			}
		}
	}
	const bool bFound = BestRise < TNumericLimits<float>::Max();
	const FVector Axis = (BestB - BestA).GetSafeNormal2D();
	const FVector Side(-Axis.Y, Axis.X, 0.f);
	TMap<FName, int32> Placed;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		if (It->IsDead() || It->IsPlayerControlled()) continue;
		// A gunner with a place at a gun goes with his gun (below), not into the line.
		bool bAtGun = false;
		for (const APA250FieldGun* Gun : Guns) bAtGun = bAtGun || (Gun && Gun->PostOf(*It) != INDEX_NONE);
		if (bAtGun) continue;
		const bool bBrit = It->FactionId == TEXT("british");
		const int32 Index = Placed.FindOrAdd(It->FactionId)++;
		const float Along = (Index % 2 == 0 ? 1.f : -1.f) * 200.f * ((Index + 1) / 2);
		FVector Spot = (bBrit ? BestB : BestA) + Side * Along;
		FVector OnGround;
		if (Ground(Spot, OnGround)) Spot = OnGround;
		const FRotator Face = (bBrit ? -Axis : Axis).Rotation();
		It->TeleportTo(Spot + FVector(0.f, 0.f, 95.f), Face, false, true);
		if (AController* C = It->GetController()) C->SetControlRotation(Face);
	}
	// Guns: on the right flank of their side's line, clear of it, fronting the other line.
	TMap<FName, int32> GunsPlaced;
	for (APA250FieldGun* Gun : Guns)
	{
		if (!Gun) continue;
		const bool bBrit = Gun->GetFaction() == TEXT("british");
		const int32 Index = GunsPlaced.FindOrAdd(Gun->GetFaction())++;
		const float Flank = 200.f * (Placed.FindRef(Gun->GetFaction()) / 2 + 1) + 700.f * (Index + 1);
		FVector Spot = (bBrit ? BestB : BestA) + Side * Flank;
		FVector OnGround;
		if (Ground(Spot, OnGround)) Spot = OnGround;
		Gun->SetActorLocationAndRotation(Spot, FRotator(0.f, (bBrit ? -Axis : Axis).Rotation().Yaw, 0.f));
		for (TActorIterator<ARFCharacter> It(World); It; ++It)
		{
			const int32 Post = Gun->PostOf(*It);
			if (Post == INDEX_NONE || It->IsDead()) continue;
			It->TeleportTo(Gun->GetCrewPost(Post) + FVector(0.f, 0.f, 10.f), Gun->GetActorRotation(), false, true);
		}
	}
	// Works (with -PA250Fort): the standing banks go two metres in front of their side's line,
	// opposite the other side's guns when it has any, so the men those guns fire at are behind them.
	{
		TMap<FName, int32> WorksPlaced;
		int32 Moved = 0;
		for (APA250FieldWork* Work : Works)
		{
			if (!Work || !Work->GivesCover()) continue;
			const FName F = Work->GetFaction();
			const bool bBrit = F == TEXT("british");
			float Opposite = 0.f;
			int32 EnemyGuns = 0;
			for (const APA250FieldGun* Gun : Guns)
			{
				if (!Gun || Gun->GetFaction() == F) continue;
				Opposite += FVector::DotProduct(Gun->GetActorLocation() - (bBrit ? BestA : BestB), Side);
				++EnemyGuns;
			}
			if (EnemyGuns > 0) Opposite /= EnemyGuns;
			const int32 Index = WorksPlaced.FindOrAdd(F)++;
			const float Along = Opposite + (Index % 2 == 0 ? 1.f : -1.f) * (Rules.works_breastwork_length_cm + 30.f) * ((Index + 1) / 2);
			const FVector Toward = bBrit ? -Axis : Axis;
			FVector Spot = (bBrit ? BestB : BestA) + Side * Along + Toward * 200.f;
			FVector OnGround;
			if (Ground(Spot, OnGround)) Spot = OnGround;
			Work->SetActorLocationAndRotation(Spot, FRotator(0.f, Toward.Rotation().Yaw, 0.f));
			++Moved;
		}
		if (Moved > 0)
		{
			NotifyWorksChanged();
			UE_LOG(LogTemp, Display, TEXT("PA250 matchup: %d standing works set in front of their side's line"), Moved);
		}
	}
	bMatchupHold = true;
	UE_LOG(LogTemp, Display, TEXT("PA250 matchup: british %d and continental %d drawn up %.0f m apart, holding their ground (%s, rise %.0f cm)"),
		Placed.FindRef(TEXT("british")), Placed.FindRef(TEXT("continental")), MetersApart, bFound ? TEXT("open axis found") : TEXT("NO OPEN AXIS: lines of sight may be blocked"),
		bFound ? BestRise : -1.f);
}

int32 APA250BattleGameMode::GetFieldStrength(FName Faction) const
{
	if (bBotsOverride) return BotsPerSide;
	const auto It = Setup.sides.find(TCHAR_TO_UTF8(*Faction.ToString()));
	return (It != Setup.sides.end() && It->second.field_strength > 0) ? It->second.field_strength : BotsPerSide;
}

int32 APA250BattleGameMode::GetRoundsPerMan(FName Faction) const
{
	const auto It = Setup.sides.find(TCHAR_TO_UTF8(*Faction.ToString()));
	return It != Setup.sides.end() ? FMath::Max(0, It->second.rounds_per_man) : 0;
}

APA250ControlPoint* APA250BattleGameMode::NearestHeldPost(FName Faction, const FVector& From) const
{
	APA250ControlPoint* Best = nullptr;
	float BestSq = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		APA250ControlPoint* P = WP.Get();
		if (!P || P->GetHolder() != Faction) continue;
		const float Sq = FVector::DistSquared2D(P->GetActorLocation(), From);
		if (Sq < BestSq)
		{
			BestSq = Sq;
			Best = P;
		}
	}
	return Best;
}

float APA250BattleGameMode::PostsSweptCountdown(FName& OutFaction) const
{
	for (const TPair<FName, float>& KV : SweepTime)
	{
		if (KV.Value > 0.f)
		{
			OutFaction = KV.Key;
			return FMath::Max(0.f, Rules.posts_swept_seconds - KV.Value);
		}
	}
	return -1.f;
}

float APA250BattleGameMode::ArmyBrokeCountdown(FName Faction) const
{
	const float* T = BrokeTime.Find(Faction);
	return (T && *T > 0.f) ? FMath::Max(0.f, Rules.army_broke_seconds - *T) : -1.f;
}

void APA250BattleGameMode::TickEndConditions(float DeltaSeconds)
{
	const float Elapsed = GetWorld()->GetTimeSeconds() - StartTime;
	auto SideName = [](FName F) { return F == TEXT("british") ? TEXT("regulars") : TEXT("provincials"); };
	// posts_swept: one side holds every post for the sweep time. The single-crossing fight (Concord)
	// has one post to contest, so holding it is the fight itself, not a sweep.
	if (!ScenarioFight.bContestSingleCrossing && !PA250Modes::IsAssault(ScenarioFight.Mode))
	{
		const int32 Total = CountPosts();
		for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
		{
			if (PostsOwned(F) > 0) EverHeldAPost.FindOrAdd(F) = true;
		}
		for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
		{
			const int32 Owned = PostsOwned(F);
			float& T = SweepTime.FindOrAdd(F);
			if (!PA250EndRules::PostsAreSwept(Owned, Total, EverHeldAPost.FindRef(OtherFaction(F)), Elapsed, Rules))
			{
				T = 0.f;
				continue;
			}
			if (T <= 0.f)
			{
				PushKillFeed(FString::Printf(TEXT("The %s hold every post"), SideName(F)));
				UE_LOG(LogTemp, Display, TEXT("PA250 sweep: %s hold all %d posts; the field is theirs in %.0f s"), *F.ToString(), Total, Rules.posts_swept_seconds);
			}
			T += DeltaSeconds;
			if (T >= Rules.posts_swept_seconds)
			{
				EndBattle(F, TEXT("posts_swept"));
				return;
			}
		}
	}
	// A letter's orders: a side told to withdraw early quits the field when that share of the clock
	// has run, and the ground is the enemy's (the sim then saves what the orders said of the magazine).
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		const int32 Pct = SideOrder(F, "withdraw_early");
		if (Pct > 0 && Elapsed >= TimeLimitSeconds * FMath::Clamp(Pct, 1, 100) / 100.f)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 orders: the %s side withdraws early as ordered (%.0f s of %.0f)"), *F.ToString(), Elapsed, TimeLimitSeconds);
			PushKillFeed(F == PlayerFaction ? TEXT("Your company falls back as ordered.") : TEXT("The enemy falls back."));
			EndBattle(F == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")), TEXT("time_limit"));
			return;
		}
	}
	// army_broke: most of a side's squads are withdrawing or routing. Either side, the player's too
	// (user decision 2026-11-14); guarded against the opening minutes and against a handful of squads.
	if (!Volleys) return;
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		int32 Squads = 0, Broken = 0;
		Volleys->CountSquads(F, Squads, Broken);
		int32& Peak = PeakSquads.FindOrAdd(F);
		Peak = FMath::Max(Peak, Squads);
		float& T = BrokeTime.FindOrAdd(F);
		if (SideOrder(F, "stand_fast") > 0)
		{
			T = 0.f; // ordered to stand to the end: this side does not break off
			continue;
		}
		if (!PA250EndRules::ArmyIsBreaking(Squads, Broken, Peak, Elapsed, Rules))
		{
			// A squad that steadies for a moment does not wipe the slate: the clock runs back down at
			// the rate it ran up. (Squad states last a few seconds each, so a clock that snapped to
			// zero never reached 15 s even in an army with morale 5: low-morale autoplay, 2026-11-14.)
			T = FMath::Max(0.f, T - DeltaSeconds);
			continue;
		}
		if (T <= 0.f)
		{
			PushKillFeed(FString::Printf(TEXT("The %s are breaking!"), SideName(F)));
			UE_LOG(LogTemp, Display, TEXT("PA250 broke: %s has %d of %d squads falling back; breaks in %.0f s"), *F.ToString(), Broken, Squads, Rules.army_broke_seconds);
		}
		T += DeltaSeconds;
		float& Longest = BrokePeak.FindOrAdd(F);
		Longest = FMath::Max(Longest, T);
		if (T >= Rules.army_broke_seconds)
		{
			EndBattle(OtherFaction(F), TEXT("army_broke"));
			return;
		}
	}
}

void APA250BattleGameMode::TickAmmo(float DeltaSeconds)
{
	// Twice a second is plenty: an opening box for each man as he takes the field, and a refill for a
	// man standing inside a post his side holds, out of what the side has left.
	AmmoTimer += DeltaSeconds;
	if (AmmoTimer < 0.5f) return;
	const float Step = AmmoTimer;
	AmmoTimer = 0.f;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		ARFCharacter* C = *It;
		if (C->IsDead() || !C->Musket) continue;
		const int32 Box = GetRoundsPerMan(C->FactionId);
		if (Box <= 0) continue; // a setup without rounds: boxes are not counted in this fight
		const bool bHuman = PC && C->GetController() == PC;
		if (!AmmoIssued.Contains(C))
		{
			AmmoIssued.Add(C);
			// The round in the barrel is one of them.
			C->Musket->RoundsInBox = FMath::Max(0, Ammo.Draw(C->FactionId, Box, bHuman) - 1);
			continue;
		}
		const bool bDry = C->Musket->IsDry();
		const int32 Want = (Box - 1) - FMath::Max(0, C->Musket->RoundsInBox) + (bDry ? 1 : 0);
		const APA250ControlPoint* Post = Want > 0 ? NearestHeldPost(C->FactionId, C->GetActorLocation()) : nullptr;
		const bool bAtPost = Post && FVector::DistSquared2D(Post->GetActorLocation(), C->GetActorLocation()) <= FMath::Square(Post->Radius);
		if (!bAtPost || Ammo.Available(C->FactionId, bHuman) <= 0)
		{
			ResupplyClock.Remove(C);
			continue;
		}
		float& Clock = ResupplyClock.FindOrAdd(C);
		Clock += Step;
		if (Clock < Rules.resupply_seconds) continue;
		Clock = 0.f;
		const int32 Granted = Ammo.Refill(C->FactionId, Want, bHuman);
		C->Musket->Resupply(Granted);
		if (bHuman && Granted > 0)
		{
			ShowNotice(FString::Printf(TEXT("Cartridge box filled at the %s (+%d)"), *Post->PostId.ToString(), Granted), 2.5f);
		}
	}
}

void APA250BattleGameMode::CheckTicketExhaustion()
{
	// A side loses when its reserve is spent and its last man is down (SideIsOut). Bleed alone can
	// no longer end a fight, so the old two-minute guard is gone: it only made a player whose side
	// had been wiped early watch the field until the clock allowed the result.
	// A side that started with no reinforcement tickets only loses after it has fielded at
	// least once and been wiped — never on the first Tick before spawn (stores gating used
	// to leave Continental at 0 tickets and kick Concord back to the map immediately).
	// The player's side is never counted out while the player himself is alive on the field
	// (user decision 2026-11-10): the battle is not lost under a man who is still fighting.
	bool bHumanAlive = false;
	if (!bAutoplayBots)
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		bHumanAlive = Me && !Me->IsDead() && Me->FactionId == PlayerFaction;
	}
	auto Out = [this, bHumanAlive](FName F) {
		int32 Initial = 0;
		for (const auto& CK : InitialPool.FindRef(F)) Initial += CK.Value;
		const int32 Alive = CountAlive(F);
		return SideIsOut(PoolTotal(F), Initial, EverFielded.FindRef(F), Alive, bHumanAlive && F == PlayerFaction);
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
		// While a man of his file stands he fights on as that man: no spawn screen. (His fall has
		// cost its ticket like any other death; stepping into a living man costs no more.)
		if (Volleys && Volleys->PickSuccessor(Cast<ARFCharacter>(C->GetPawn())) && TakeOverSoldier(C)) return;
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
	// The general falls: no ticket and no place in the casualty ledger (he is not one of the men),
	// but his side takes a morale check and the result says what became of him.
	if (HasAuthority() && IsGeneral(Victim))
	{
		const pa250::CommanderRules GeneralRules = pa250::commander_rules();
		const std::string Fate = pa250::commander_fate(Setup.seed, ToStd(Victim->FactionId));
		GeneralFate.Add(Victim->FactionId, UTF8_TO_TCHAR(Fate.c_str()));
		if (Volleys) Volleys->PressSide(Victim->FactionId, GeneralRules.fall_pressure, GeneralRules.fall_pressure_seconds, TEXT("GeneralFell"));
		const auto Side = Setup.sides.find(ToStd(Victim->FactionId));
		const FString Name = Side != Setup.sides.end() && !Side->second.commander_name.empty()
			? FString(UTF8_TO_TCHAR(Side->second.commander_name.c_str()))
			: FString(TEXT("The general"));
		const bool bOurs = Victim->FactionId == PlayerFaction;
		const FString FateText(UTF8_TO_TCHAR(Fate.c_str()));
		FString Key;
		const int32 Variant = GeneralFallVariant(Setup.seed, GetTypeHash(Victim->FactionId));
		const FString Words = GeneralFallWords(bOurs, Name, FateText, Variant, Key);
		const FName Voice = bOurs ? Victim->FactionId : PlayerFaction;
		PushKillFeed(Words);
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool bHold = GeneralCalloutUntil >= 0.0 && Now < GeneralCalloutUntil;
		if (!bHold)
		{
			ShowNotice(Words, 6.f);
			GeneralCalloutUntil = Now + 6.0;
			if (Volleys)
			{
				if (bOurs) Volleys->CallOut(Voice, *Key, *Words, Victim->GetActorLocation(), 6.f);
				else Volleys->CallOutNearListener(*Key, *Words);
			}
			UE_LOG(LogTemp, Display, TEXT("PA250 general shout: voice %s key %s variant %d \"%s\""),
				*Voice.ToString(), *Key, Variant, *Words);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 general shout: held (callout still up) voice %s key %s variant %d \"%s\""),
				*Voice.ToString(), *Key, Variant, *Words);
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 general: %s falls (%s); morale check on the %s side"), *Name, *FateText, *Victim->FactionId.ToString());
		// The player was fighting as him: he is given a body again (his own, the next man of his file,
		// any man standing, or the spawn screen). Before 2026-11-15 this branch returned with the
		// player left in the corpse.
		if (Victim->FactionId == PlayerFaction && (Hero.bPlaying || (VictimController && VictimController->IsA<APlayerController>())))
		{
			HeroFell(Cast<APlayerController>(VictimController));
		}
		return;
	}
	// M6: tickets spent on death (and bleed), not spawn.
	if (HasAuthority())
	{
		// A surgeon may still reach him (the surgeon's verb).
		{
			FFallenMan Down;
			Down.Where = Victim->GetActorLocation();
			Down.FellAt = GetWorld()->GetTimeSeconds();
			Down.Faction = Victim->FactionId;
			Down.UnitClass = Victim->UnitClass;
			FallenMen.Add(Down);
		}
		// The ensign is down: the colours fall with him and his side feels it.
		if (APA250Colours* Flag = Colours.FindRef(Victim->FactionId); Flag && Flag->GetBearer() == Victim)
		{
			const FPA250ClassVerbs& SV = PA250UnitClasses::Verbs();
			Flag->Drop();
			ColoursFell.FindOrAdd(Victim->FactionId)++;
			if (Volleys) Volleys->PressSide(Victim->FactionId, SV.ColoursFallPressure, SV.ColoursFallSeconds, TEXT("ColoursFell"));
			PushKillFeed(Victim->FactionId == PlayerFaction ? TEXT("Our colours are down!") : TEXT("Their colours are down!"));
			UE_LOG(LogTemp, Display, TEXT("PA250 colours: the %s colours fall with their bearer; morale check on that side"), *Victim->FactionId.ToString());
		}
		// An officer of the player's side is down: the men near say so (intensity.shouts.officer_down).
		if (Volleys && Victim->FactionId == PlayerFaction && PA250UnitClasses::KitFor(Victim->FactionId, Victim->UnitClass).bOfficer)
		{
			Volleys->ShoutEvent(TEXT("officer_down"), Victim->FactionId, Victim->GetActorLocation());
		}
		if (!Victim->ActorHasTag(TEXT("PA250GunMate"))) SpendDeathTicket(Victim->FactionId, Victim->UnitClass);
		else UE_LOG(LogTemp, Display, TEXT("PA250 guns: a summoned mate is down (no ticket)"));
		// The rifleman's verb: an officer he picks off leaves that officer's squad shaken.
		if (const ARFCharacter* Marksman = Killer ? Cast<ARFCharacter>(Killer->GetPawn()) : nullptr)
		{
			if (Marksman->FactionId != Victim->FactionId && PA250UnitClasses::KitFor(Victim->FactionId, Victim->UnitClass).bOfficer
				&& PA250UnitClasses::KitFor(Marksman->FactionId, Marksman->UnitClass).bOfficerHunter && Volleys && Volleys->ShakeSquadOf(Victim))
			{
				PushKillFeed(Victim->FactionId == PlayerFaction ? TEXT("Our officer is down: his men waver") : TEXT("Their officer is down: his men waver"));
			}
		}
		if (Killer)
		{
			if (const ARFCharacter* KillerChar = Cast<ARFCharacter>(Killer->GetPawn()))
			{
				// Personal kills are enemy kills by the player's own hand (musket, bayonet or
				// taken-over soldier alike); a friendly death at his hand is not one.
				if (Killer->IsLocalPlayerController() && KillerChar->FactionId != Victim->FactionId)
				{
					++PersonalKills;
					NoteHeroEvent(EPA250HeroEvent::Kill);
				}
				auto Label = [](const ARFCharacter* C, bool bYou) {
					if (bYou) return FString(TEXT("You"));
					return FString(PA250UnitClasses::KitFor(C->FactionId, C->UnitClass).Label);
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
		bOpeningClassPick = false; // shot while still choosing: from here it is an ordinary respawn
		BeginDeathCamThenSpawn(Cast<APlayerController>(VictimController));
		return;
	}
	TWeakObjectPtr<AController> OldCtl = VictimController;
	// His replacement may come on at the colours if he stood in the ensign's squad (the ensign's verb).
	bool bOnColours = false;
	if (const APA250Colours* Flag = Colours.FindRef(F))
	{
		const ARFCharacter* Bearer = Flag->GetBearer();
		bOnColours = Bearer && !Bearer->IsDead() && Bearer != Victim && Bearer->SquadId >= 0 && Bearer->SquadId == Victim->SquadId;
	}
	const FName Cls = Victim->UnitClass;
	const uint32 Clothes = Victim->GetClothingSeed();
	float Delay = RespawnDelay;
	{
		const auto Sit = Setup.sides.find(TCHAR_TO_UTF8(*F.ToString()));
		if (Sit != Setup.sides.end() && Sit->second.reinforcement_scale > 0.f)
		{
			Delay = FMath::Max(3.f, RespawnDelay * Sit->second.reinforcement_scale);
		}
		Delay *= HeroReinforceDelayScale(F); // the played general's huzza: the reserve comes up faster for its time
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, OldCtl, F, Cls, Clothes, bOnColours]() {
		if (bBattleOver) return;
		if (AController* C = OldCtl.Get())
		{
			ControllerFaction.Remove(C);
			ControllerClass.Remove(C);
			C->Destroy();
		}
		// On the colours: beside the ensign, unless the enemy is on top of him.
		const APA250Colours* Flag = bOnColours ? Colours.FindRef(F).Get() : nullptr;
		const ARFCharacter* Bearer = Flag ? Flag->GetBearer() : nullptr;
		bool bSafe = Bearer && !Bearer->IsDead();
		for (TActorIterator<ARFCharacter> It(GetWorld()); It && bSafe; ++It)
		{
			if (!It->IsDead() && It->FactionId != F && !It->FactionId.IsNone()
				&& FVector::DistSquared2D(It->GetActorLocation(), Bearer->GetActorLocation()) < FMath::Square(SafeSpawnEnemyCm * 0.5f)) bSafe = false;
		}
		if (bSafe)
		{
			const FVector At = ClearSpawnLocation(GetWorld(), Bearer->GetActorLocation() - Bearer->GetActorForwardVector() * 350.f
				+ FVector(FMath::FRandRange(-200.f, 200.f), FMath::FRandRange(-200.f, 200.f), 0.f));
			if (SpawnBotAt(F, ChooseClassFor(F, Cls), FTransform(Bearer->GetActorRotation(), At), Clothes))
			{
				++SpawnedOnColours;
				CheckTicketExhaustion();
				return;
			}
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
			const FString FpsMap = GetWorld()->GetMapName();
			const FString Path = FPaths::Combine(Dir, FpsMap.Contains(TEXT("L_Concord")) ? TEXT("concord_fps.json") : FpsMap.Contains(TEXT("L_Harbor")) ? TEXT("harbor_fps.json") : TEXT("lexington_fps.json"));
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
			// The bar is the data file's (battle_rules.json field.frame_rate_bar), not a number here.
			const bool bHolds = AvgFps >= Rules.fps_bar;
			const FString Json = FString::Printf(
				TEXT("{\"avg_fps\":%.2f,\"frames\":%d,\"seconds\":%.3f,\"bar\":%.0f,\"pass\":%s,\"bots_per_side\":%d,\"soldiers_alive\":%d,\"puffs\":%d,\"gi_method\":%d,\"reflection_method\":%d,\"time_dilation\":%.1f,\"game_ms\":%.2f,\"render_ms\":%.2f,\"gpu_ms\":%.2f,\"res\":\"%dx%d\"}\n"),
				AvgFps, FpsSampleFrames, FpsSampleAccum, Rules.fps_bar, bHolds ? TEXT("true") : TEXT("false"),
				BotsPerSide, Soldiers, Smoke ? Smoke->NumPuffs() : 0, GI ? GI->GetInt() : -1, Refl ? Refl->GetInt() : -1,
				UGameplayStatics::GetGlobalTimeDilation(this),
				FpsGameMs / FMath::Max(1, FpsSampleFrames), FpsRenderMs / FMath::Max(1, FpsSampleFrames),
				FpsGpuMs / FMath::Max(1, FpsSampleFrames), ViewSize.X, ViewSize.Y);
			FFileHelper::SaveStringToFile(Json, *Path);
			UE_LOG(LogTemp, Display, TEXT("PA250 FPS sample avg=%.1f bar=%.0f pass=%s at %dx%d with %d soldiers -> %s"),
				AvgFps, Rules.fps_bar, bHolds ? TEXT("true") : TEXT("false"), ViewSize.X, ViewSize.Y, Soldiers, *Path);
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
			if (FirstFlipAt < 0.f) FirstFlipAt = GetWorld()->GetTimeSeconds() - StartTime;
			// A post taken with the player standing on it counts toward leading the general.
			if (WP->GetHolder() == PlayerFaction)
			{
				const APlayerController* FlipPC = GetWorld()->GetFirstPlayerController();
				const ARFCharacter* Taker = FlipPC ? Cast<ARFCharacter>(FlipPC->GetPawn()) : nullptr;
				if (Taker && !Taker->IsDead() && FVector::Dist2D(Taker->GetActorLocation(), WP->GetActorLocation()) <= WP->Radius)
				{
					NoteHeroEvent(EPA250HeroEvent::Capture);
				}
			}
		}
	}
	TickHero(DeltaSeconds);
	TickMusic(DeltaSeconds);
	// How long each side kept half its opening men standing (battle trace: holding behind works).
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		const int32 Alive = CountAlive(F);
		int32& Opening = OpeningField.FindOrAdd(F);
		Opening = FMath::Max(Opening, Alive);
		if (Opening > 0 && !HalfStrengthAt.Contains(F) && Alive * 2 <= Opening && PoolTotal(F) <= 0)
		{
			HalfStrengthAt.Add(F, GetWorld()->GetTimeSeconds() - StartTime);
		}
	}
	BleedTickets(DeltaSeconds);
	TickMuster(DeltaSeconds);
	TickOfficers(DeltaSeconds);
	TickAnimTiers(DeltaSeconds);
	TickCoverProbe(DeltaSeconds);
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		if (CountAlive(F) > 0)
		{
			EverFielded.FindOrAdd(F) = true;
		}
	}
	TickAmmo(DeltaSeconds);
	TickClassVerbs();
	TickSupport(DeltaSeconds);
	TickPlayerFile(DeltaSeconds);
	TickLedVolleyProbe(DeltaSeconds);
	TickOrderProbe(DeltaSeconds);
	TickLanding(DeltaSeconds);
	TickBattleMode();
	CheckTicketExhaustion();
	if (!bBattleOver)
	{
		TickRemnants(DeltaSeconds);
	}
	if (!bBattleOver)
	{
		TickEndConditions(DeltaSeconds);
	}
	if (SpawnScreen && SpawnScreen->IsInViewport() && !bOpeningClassPick && !CanSpawn(PlayerFaction))
	{
		CloseSpawnScreenToSpectate(SpawnScreen->GetOwningPlayer());
	}
	if (bBattleOver)
	{
		return;
	}
	if (TimeRemaining() <= 0.f)
	{
		// Every man standing counts, as it does for exhaustion: reserve plus the men on the field.
		const int32 TB = MenRemaining(TEXT("british"));
		const int32 TC = MenRemaining(TEXT("continental"));
		FName Winner;
		if (PA250Modes::IsAssault(ScenarioFight.Mode))
		{
			// The clock is the defender's: he has held.
			EndBattle(PA250Modes::AssaultWinnerAtTimeLimit(ToName(Setup.defender_faction)), TEXT("time_limit"));
		}
		else if (ScenarioFight.bRewardForcePreservation)
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
	UE_LOG(LogTemp, Display, TEXT("PA250 battle: %d AI misfires (flash in the pan)"), Misfires);

	Result = pa250::BattleResult{};
	Result.province_id = Setup.province_id;
	Result.winner = ToStd(Winner);
	Result.end_reason = TCHAR_TO_UTF8(*Reason);
	const FName Plurality = PluralityHolder();
	Result.objective_holder = Plurality.IsNone() ? std::string() : ToStd(Plurality);
	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		if (!WP.IsValid() || WP->GetHolder().IsNone()) continue;
		Result.objectives_held[ToStd(WP->PostId)] = ToStd(WP->GetHolder());
	}
	// Casualties in men, from the battle's own ledger (until 2026-11-14 they were rounded to units
	// of SoldiersPerUnit and capped at the ticket pool, so nineteen men could fall for a reported
	// loss of one unit). The sim turns men into units and keeps the men left over on the army.
	Result.casualties = CasualtiesFromLedger(Ledger.Deaths, Prisoners, SavedBySurgeon);
	for (const auto& Kv : ColoursTaken)
	{
		if (Kv.Value > 0) Result.colours_captured[ToStd(Kv.Key)] = Kv.Value;
	}
	// Guns that have changed hands and are still held by their captor at the end.
	for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
	{
		if (const int32 N = GetGunsTakenBy(F); N > 0) Result.guns_taken[ToStd(F)] = N;
	}
	// What became of a general who fell (the sim ignores a side that had none).
	for (const auto& Kv : GeneralFate)
	{
		Result.commander_events[ToStd(Kv.Key)] = TCHAR_TO_UTF8(*Kv.Value);
	}
	Result.personal_kills = PersonalKills;
	// The hero report (battle_result.yaml hero): only when the player fought as his general. It
	// changes nothing in the campaign; commander_events above says what became of him.
	if (Hero.Takeovers > 0)
	{
		if (const auto HeroSideIt = Setup.sides.find(ToStd(PlayerFaction)); HeroSideIt != Setup.sides.end() && !HeroSideIt->second.commander_id.empty())
		{
			Result.hero_faction = ToStd(PlayerFaction);
			Result.hero_person_id = HeroSideIt->second.commander_id;
			Result.hero_takeovers = Hero.Takeovers;
			Result.hero_seconds_played = Hero.SecondsPlayed;
			Result.hero_fell_while_played = Hero.bFellWhilePlayed;
		}
	}
	for (const auto& FK : Result.casualties)
	{
		int32 Killed = 0, Wounded = 0, Taken = 0;
		for (const auto& CK : FK.second)
		{
			Killed += CK.second.killed;
			Wounded += CK.second.wounded;
			Taken += CK.second.prisoners;
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 ledger: %s fell %d (%d killed, %d wounded), %d taken prisoner"), UTF8_TO_TCHAR(FK.first.c_str()),
			Killed + Wounded, Killed, Wounded, Taken);
	}
	// Cartridges from live shots when available (rounds_per_cartridge rounds to a magazine cartridge,
	// rounded up); else fielded×2. Never more than the side's own magazine held.
	int32 TotalShots = 0;
	for (const auto& Kv : ShotsFiredByFaction) TotalShots += Kv.Value;
	for (const auto& Side : Setup.sides)
	{
		const FName SideName = ToName(Side.first);
		const int32 Fielded = [&]() {
			int32 N = 0;
			for (const auto& T : Side.second.tickets) N += std::max(0, T.second);
			return N;
		}();
		int32 Spend = TotalShots > 0 ? FMath::DivideAndRoundUp(ShotsFiredByFaction.FindRef(SideName), FMath::Max(1, Rules.rounds_per_cartridge)) : Fielded * 2;
		if (ShotsFiredByFaction.FindRef(SideName) > 0)
		{
			Result.shots_fired[Side.first] = ShotsFiredByFaction.FindRef(SideName);
		}
		Spend = FMath::Clamp(Spend, 0, FMath::Max(0, Side.second.cartridge_budget));
		Result.cartridges_spent[Side.first] = Spend;
	}
	// Powder horn waste from misfires (flash in the pan).
	int32 RemainingPowder = Setup.province_powder;
	for (const auto& Side : Setup.sides)
	{
		const FName SideName = ToName(Side.first);
		int32 Pw = MisfiresByFaction.FindRef(SideName);
		Pw = FMath::Clamp(Pw, 0, RemainingPowder);
		RemainingPowder -= Pw;
		if (Pw > 0) Result.powder_spent[Side.first] = Pw;
	}
	++BattleResultEmitCount;
	const std::string Loser = Winner == TEXT("british") ? "continental" : "british";
	auto It = Setup.sides.find(Loser);
	if (It != Setup.sides.end())
	{
		Result.retreat_province = It->second.retreat_province;
	}
	Banner = FString::Printf(TEXT("%s victory: %s"), *Winner.ToString().ToUpper(), PA250EndRules::EndReasonWords(Reason, ScenarioFight.Mode));
	UE_LOG(LogTemp, Display, TEXT("PA250 battle over: %s [%s] flips=%d bleed=%d"), *Banner, *Reason, PostFlips, Ledger.BleedTicks);
	WriteBattleFile(Reason);

	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &APA250BattleGameMode::FinishAndReturn), FMath::Max(0.1f, EndDelaySeconds), false);
}

std::map<std::string, std::map<std::string, pa250::MenDown>> APA250BattleGameMode::CasualtiesFromLedger(
	const TMap<FName, TMap<FName, int32>>& Deaths, const TMap<FName, TMap<FName, int32>>& Surrendered,
	const TMap<FName, TMap<FName, int32>>& SavedBySurgeon)
{
	std::map<std::string, std::map<std::string, pa250::MenDown>> Out;
	for (const auto& FK : Deaths)
	{
		for (const auto& CK : FK.Value)
		{
			if (CK.Value <= 0) continue;
			// A man the surgeon reached in time is wounded whatever the ratio says; the ratio shares
			// out the rest.
			const TMap<FName, int32>* SavedSide = SavedBySurgeon.Find(FK.Key);
			const int32 Saved = FMath::Clamp(SavedSide ? SavedSide->FindRef(CK.Key) : 0, 0, CK.Value);
			const pa250::MenDown Split = pa250::split_fallen(CK.Value - Saved);
			pa250::MenDown& M = Out[ToStd(FK.Key)][ToStd(CK.Key)];
			M.killed += Split.killed;
			M.wounded += Split.wounded + Saved;
		}
	}
	for (const auto& FK : Surrendered)
	{
		for (const auto& CK : FK.Value)
		{
			if (CK.Value > 0) Out[ToStd(FK.Key)][ToStd(CK.Key)].prisoners += CK.Value;
		}
	}
	return Out;
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
	Json += FString::Printf(TEXT(",\"field_strength\":{\"british\":%d,\"continental\":%d},\"squads_peak\":{\"british\":%d,\"continental\":%d}"),
		GetFieldStrength(TEXT("british")), GetFieldStrength(TEXT("continental")), PeakSquads.FindRef(TEXT("british")), PeakSquads.FindRef(TEXT("continental")));
	Json += FString::Printf(TEXT(",\"broke_peak_seconds\":{\"british\":%.1f,\"continental\":%.1f}"), BrokePeak.FindRef(TEXT("british")), BrokePeak.FindRef(TEXT("continental")));
	Json += FString::Printf(TEXT(",\"prisoners_taken\":{\"british\":%d,\"continental\":%d}"), GetPrisonersTaken(TEXT("british")), GetPrisonersTaken(TEXT("continental")));
	{
		int32 StandingWorks = 0;
		for (const APA250FieldWork* W : Works) StandingWorks += (W && W->GivesCover()) ? 1 : 0;
		Json += FString::Printf(TEXT(",\"works\":{\"places\":%d,\"standing\":%d,\"raised\":{\"british\":%d,\"continental\":%d}}"), Works.Num(), StandingWorks,
			WorksRaised.FindRef(TEXT("british")), WorksRaised.FindRef(TEXT("continental")));
	}
	{
		int32 Loaded = 0;
		for (const APA250FieldGun* Gun : Guns) Loaded += (Gun && Gun->IsLoaded()) ? 1 : 0;
		Json += FString::Printf(TEXT(",\"guns\":{\"on_field\":%d,\"loaded_at_end\":%d,\"shots\":{\"british\":%d,\"continental\":%d},\"canister\":{\"british\":%d,\"continental\":%d},\"men_struck\":%d,\"men_knocked_down\":%d,\"works_broken\":{\"british\":%d,\"continental\":%d},\"changed_hands\":%d,\"held_by_captor\":{\"british\":%d,\"continental\":%d}}"),
			Guns.Num(), Loaded, GunShots.FindRef(TEXT("british")), GunShots.FindRef(TEXT("continental")), CanisterShots.FindRef(TEXT("british")), CanisterShots.FindRef(TEXT("continental")),
			RoundShotStruck, RoundShotKnockedDown, WorksBroken.FindRef(TEXT("british")), WorksBroken.FindRef(TEXT("continental")), GunCaptures,
			GetGunsTakenBy(TEXT("british")), GetGunsTakenBy(TEXT("continental")));
	}
	{
		const UPA250SmokeSubsystem* Smoke = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>();
		// How much each side used cover: shares of the formed bots' thinks, and of their shots.
		auto CoverJson = [this](const TCHAR* Faction)
		{
			const FCoverUse& U = GetCoverUse(Faction);
			return FString::Printf(TEXT("\"%s\":{\"thinks\":%d,\"at_cover\":%d,\"ducked\":%d,\"shots\":%d,\"shots_from_cover\":%d}"),
				Faction, U.Thinks, U.AtCover, U.Ducked, U.Shots, U.ShotsFromCover);
		};
		FString Search;
		if (Volleys)
		{
			const UPA250VolleyCoordinator::FCoverSearch& S = Volleys->GetCoverSearch();
			Search = FString::Printf(TEXT(",\"search\":{\"wall_places_in_level\":%d,\"squad_queries\":%d,\"squad_queries_not_seeking\":%d,\"men\":%d,\"skipped\":%d,\"no_place_near\":%d,\"place_off_nav\":%d,\"place_not_cover\":%d,\"wall_places\":%d}"),
				Volleys->NumCoverSlots(), S.SquadQueries, S.SquadQueriesNot, S.Men, S.Skipped, S.NoSlotNear, S.SlotOffNav, S.SlotNotCover, S.WallPlaces);
		}
		Json += FString::Printf(TEXT(",\"cover\":{%s,%s%s}"), *CoverJson(TEXT("british")), *CoverJson(TEXT("continental")), *Search);
		Json += FString::Printf(TEXT(",\"feel\":{\"smoke_sight_checks\":%d,\"smoke_sight_blocked\":%d,\"puffs_at_end\":%d,\"shoves\":%d}"),
			Smoke ? Smoke->GetSightChecks() : 0, Smoke ? Smoke->GetSightBlocks() : 0, Smoke ? Smoke->NumPuffs() : 0, Shoves);
	}
	Json += FString::Printf(TEXT(",\"mode\":{\"name\":\"%s\",\"posts_carried\":%d,\"landed\":%s,\"clock_added_seconds\":%.0f,\"boats\":%d,\"men_embarked\":%d,\"men_set_ashore\":%d,\"first_boat_in_seconds\":%.0f}"),
		*ScenarioFight.Mode.ToString(), ModeCarried, bLanded ? TEXT("true") : TEXT("false"), ModeClockAdded, Boats.Num(), MenEmbarked, MenLanded, FirstBoatInAt);
	Json += FString::Printf(TEXT(",\"support\":{\"colours_fell\":{\"british\":%d,\"continental\":%d},\"colours_taken_by\":{\"british\":%d,\"continental\":%d},\"saved_by_surgeon\":{\"british\":%d,\"continental\":%d},\"spawned_on_colours\":%d,\"cadence_man_checks\":%d}"),
		ColoursFell.FindRef(TEXT("british")), ColoursFell.FindRef(TEXT("continental")), ColoursTaken.FindRef(TEXT("british")), ColoursTaken.FindRef(TEXT("continental")),
		GetSavedBySurgeon(TEXT("british")), GetSavedBySurgeon(TEXT("continental")), SpawnedOnColours, CadenceManChecks);
	{
		auto Who = [this](const TCHAR* F) {
			const auto Side = Setup.sides.find(TCHAR_TO_UTF8(F));
			return Side == Setup.sides.end() ? FString() : FString(UTF8_TO_TCHAR(Side->second.commander_id.c_str()));
		};
		auto Alarmed = [this](const TCHAR* F) {
			const auto Side = Setup.sides.find(TCHAR_TO_UTF8(F));
			return Side != Setup.sides.end() && Side->second.alarmed;
		};
		Json += FString::Printf(TEXT(",\"generals\":{\"british\":\"%s\",\"continental\":\"%s\",\"fell\":{\"british\":\"%s\",\"continental\":\"%s\"},\"aura_checks\":%d,\"alarmed\":{\"british\":%s,\"continental\":%s},\"muster_at_once\":%d}"),
			*Who(TEXT("british")), *Who(TEXT("continental")), *GeneralFate.FindRef(TEXT("british")), *GeneralFate.FindRef(TEXT("continental")), GeneralAuraChecks,
			Alarmed(TEXT("british")) ? TEXT("true") : TEXT("false"), Alarmed(TEXT("continental")) ? TEXT("true") : TEXT("false"), AlarmMusteredAtOnce);
	}
	Json += HeroTraceJson();
	Json += FString::Printf(TEXT(",\"look\":\"%s\""), *PA250BattleLook::Applied());
	Json += FString::Printf(TEXT(",\"intensity\":{\"near_misses\":%d,\"shakes\":%d,\"dirt\":%d,\"shouts\":{\"charge\":%d,\"rout\":%d,\"officer_down\":%d,\"colours_taken\":%d},\"music_layers\":%d}"),
		PA250Intensity::NearMissCount(), PA250Intensity::ShakeCount(), PA250Intensity::DirtCount(), Volleys ? Volleys->ShoutEventCount(TEXT("charge")) : 0,
		Volleys ? Volleys->ShoutEventCount(TEXT("rout")) : 0, Volleys ? Volleys->ShoutEventCount(TEXT("officer_down")) : 0,
		Volleys ? Volleys->ShoutEventCount(TEXT("colours_taken")) : 0, Music.IsValid() ? Music->NumPlaying() : 0);
	Json += FString::Printf(TEXT(",\"held\":{\"first_flip_seconds\":%.0f,\"half_strength_seconds\":{\"british\":%.0f,\"continental\":%.0f}}"), FirstFlipAt,
		HalfStrengthAt.Contains(TEXT("british")) ? HalfStrengthAt.FindRef(TEXT("british")) : -1.f,
		HalfStrengthAt.Contains(TEXT("continental")) ? HalfStrengthAt.FindRef(TEXT("continental")) : -1.f);
	Json += FString::Printf(TEXT(",\"led_volley\":{\"side\":\"%s\",\"volleys\":%d,\"shook\":%d}"), ProbeSide.IsNone() ? TEXT("") : *ProbeSide.ToString(), ProbeVolleys, ProbeShook);
	Json += FString::Printf(TEXT(",\"standing\":{\"british\":%d,\"continental\":%d},\"deaths\":{\"british\":%d,\"continental\":%d}"),
		CountAlive(TEXT("british")), CountAlive(TEXT("continental")),
		[this]() { int32 N = 0; for (const auto& CK : Ledger.Deaths.FindRef(TEXT("british"))) N += CK.Value; return N; }(),
		[this]() { int32 N = 0; for (const auto& CK : Ledger.Deaths.FindRef(TEXT("continental"))) N += CK.Value; return N; }());
	Json += TEXT(",\"verbs\":{");
	if (Volleys)
	{
		bool bFirstVerb = true;
		for (const TCHAR* Side : {TEXT("british"), TEXT("continental")})
		{
			for (const TCHAR* Verb : {TEXT("VolleyShock"), TEXT("ChargeRout"), TEXT("OfficerDown"), TEXT("Rallied"), TEXT("OrderedVolley"), TEXT("LedVolley"), TEXT("FileFormed"), TEXT("Order"), TEXT("OrderRelayed"),
				 TEXT("ColoursFell"), TEXT("ColoursLost"), TEXT("CannonShock"), TEXT("GunLost")})
			{
				Json += FString::Printf(TEXT("%s\"%s.%s\":%d"), bFirstVerb ? TEXT("") : TEXT(","), Side, Verb, Volleys->CountOf(FString::Printf(TEXT("%s.%s"), Side, Verb)));
				bFirstVerb = false;
			}
		}
	}
	Json += TEXT("}");
	Json += FString::Printf(TEXT(",\"unit_classes_from_data\":%s,\"unit_class_fallback_uses\":%d"), PA250UnitClasses::LoadedFromData() ? TEXT("true") : TEXT("false"), PA250UnitClasses::FallbackUses());
	auto AmmoJson = [this](const TCHAR* F) {
		return FString::Printf(TEXT("{\"rounds_per_man\":%d,\"start\":%d,\"left\":%d,\"refills\":%d,\"refill_rounds\":%d}"),
			GetRoundsPerMan(F), Ammo.Start.FindRef(F), Ammo.Remaining(F), Ammo.Refills.FindRef(F), Ammo.RefillRounds.FindRef(F));
	};
	Json += TEXT(",\"ammo\":{\"british\":") + AmmoJson(TEXT("british")) + TEXT(",\"continental\":") + AmmoJson(TEXT("continental")) + TEXT("}");
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
		// The result is the ledger, to the man: killed + wounded are the deaths, prisoners the surrendered.
		bool bLedgerOk = true;
		int32 Fell = 0;
		for (const FName F : {FName(TEXT("british")), FName(TEXT("continental"))})
		{
			int32 Dead = 0, Gave = 0, OnResult = 0, OnResultTaken = 0;
			for (const auto& CK : Ledger.Deaths.FindRef(F)) Dead += CK.Value;
			for (const auto& CK : Prisoners.FindRef(F)) Gave += CK.Value;
			const auto Side = Result.casualties.find(ToStd(F));
			if (Side != Result.casualties.end())
			{
				for (const auto& CK : Side->second)
				{
					OnResult += CK.second.killed + CK.second.wounded;
					OnResultTaken += CK.second.prisoners;
				}
			}
			bLedgerOk &= OnResult == Dead && OnResultTaken == Gave;
			Fell += Dead + Gave;
		}
		Check(Sub, bLedgerOk, TEXT("the result's casualties are the battle's ledger, to the man"));
		// And the campaign took them as reported (a debug battle that fields units the campaign army
		// does not have is the one case where the sim has nobody to take them from).
		const bool bDebugUnits = FCString::Strifind(FCommandLine::Get(), TEXT("PA250ExtraUnits=")) || FCString::Strifind(FCommandLine::Get(), TEXT("PA250OnlyClass="));
		if (bApplied && !bDebugUnits)
		{
			int32 OnCard = 0;
			for (const auto& FK : Sub->GetState().progress.last_battle_report.casualties)
			{
				for (const auto& CK : FK.second) OnCard += CK.second.total();
			}
			Check(Sub, OnCard == Fell, FString::Printf(TEXT("the after-battle card's men equal the battle's ledger (%d on the card, %d fell)"), OnCard, Fell));
		}
		Check(Sub, Sub->GetState().progress.last_battle_report.personal_kills <= FMath::Max(0, PersonalKills), TEXT("by your own hand never exceeds what the battle counted"));
		const auto Prov = Before.provinces.find(Setup.province_id);
		const bool bAdj = Prov != Before.provinces.end() &&
			(Result.retreat_province.empty() || std::find(Prov->second.adjacent.begin(), Prov->second.adjacent.end(), Result.retreat_province) != Prov->second.adjacent.end());
		Check(Sub, bAdj, TEXT("retreat province is adjacent to the battle province"));
		Check(Sub, bApplied && Sub->GetFlow() == EPA250Flow::Campaign && !Sub->IsBattlePending(), TEXT("BattleResult applied; flow back to campaign"));
		{
			// The winner takes the town whatever ended the fight; a fort turns that into a siege, and
			// on Battle Road (force_preserved) the posts still decide. The sim's report says which.
			const pa250::Province& Town = Sub->GetState().provinces.at(Setup.province_id);
			const std::string& OwnerBefore = Before.provinces.at(Setup.province_id).owner;
			const std::string& Why = Sub->GetState().progress.last_battle_report.ownership_reason;
			const bool bOwnerOk = Why == "siege_begun" ? (Town.owner == OwnerBefore && Town.siege_by == Result.winner)
				: Why == "unchanged" ? Town.owner == OwnerBefore
				: Town.owner == Result.winner;
			Check(Sub, bApplied && bOwnerOk, FString::Printf(TEXT("province ownership follows the result (%s, %s)"), UTF8_TO_TCHAR(Why.c_str()), *EndReason));
			Check(Sub, Sub->GetState().progress.last_battle_report.end_reason == Result.end_reason, TEXT("the report carries the end reason"));
		}
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
