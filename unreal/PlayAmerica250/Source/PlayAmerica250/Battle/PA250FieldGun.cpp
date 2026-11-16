#include "PA250FieldGun.h"
#include "Battle/PA250Intensity.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "MusketProjectile.h"
#include "MusketWeapon.h"
#include "PA250BattleAudio.h"
#include "PA250BattleGameMode.h"
#include "PA250AnimClips.h"
#include "PA250Blast.h"
#include "PA250BotController.h"
#include "PA250ControlPoint.h"
#include "PA250FieldWork.h"
#include "PA250Smoke.h"
#include "PA250UnitClasses.h"
#include "PA250VolleyCoordinator.h"
#include "RFCharacter.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "pa250/sim.hpp"

namespace
{
/** The purchased Fab pack "CANNON - GAME READY" (PTM-MODELS). */
const TCHAR* GCannonMesh = TEXT("/Game/ThirdParty/PTM_Cannon/Mesh.Mesh");
const TCHAR* GCannonMaterial = TEXT("/Game/PA250/Materials/M_PA250_Cannon.M_PA250_Cannon");

APA250BattleGameMode* BattleOf(const AActor* A)
{
	return (A && A->GetWorld()) ? A->GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr;
}
}

APA250FieldGun::APA250FieldGun()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	AimMark = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AimMark"));
	AimMark->SetupAttachment(RootComponent);
	AimMark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AimMark->SetCastShadow(false);
	AimMark->SetVisibility(false);
	AimMark->SetRelativeScale3D(FVector(0.35f));
	Posts.SetNum(4);
}

void APA250FieldGun::FitMesh()
{
	const pa250::BattleRules& R = pa250::battle_rules();
	UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, GCannonMesh);
	if (!Asset)
	{
		// The pack is not in this checkout: say so loudly and stand a placeholder barrel in its place.
		UE_LOG(LogTemp, Error, TEXT("PA250 gun: cannon mesh %s is missing; a plain cylinder stands in for it"), GCannonMesh);
		Asset = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		if (!Asset) return;
		Mesh->SetStaticMesh(Asset);
		Mesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
		Mesh->SetRelativeScale3D(FVector(0.3f, 0.3f, R.cannon_length_cm / 100.f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
		MuzzleLocal = FVector(R.cannon_length_cm * 0.5f, 0.f, 80.f);
		return;
	}
	Mesh->SetStaticMesh(Asset);
	// The pack's material instance binds none of its textures (it renders flat grey), so the gun wears
	// the project's material over the pack's own textures (Scripts/build_cannon_material.py).
	if (UMaterialInterface* Skin = LoadObject<UMaterialInterface>(nullptr, GCannonMaterial))
	{
		Mesh->SetMaterial(0, Skin);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250 gun: %s is missing (run Scripts/build_cannon_material.py); the gun shows the pack's untextured material"), GCannonMaterial);
	}
	// The pack's own axes are not ours. The piece's longest horizontal extent is its length; the
	// "Muzzle" socket (added by Scripts/setup_cannon_asset.py) says which end fires. The mesh is
	// turned so the muzzle points along the actor's +X, scaled to the gun's length, and set on its wheels.
	const FBoxSphereBounds B = Asset->GetBounds();
	const bool bLongX = B.BoxExtent.X >= B.BoxExtent.Y;
	FVector Long = bLongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
	FVector MuzzleMesh = B.Origin + Long * (bLongX ? B.BoxExtent.X : B.BoxExtent.Y) + FVector(0.f, 0.f, B.BoxExtent.Z * 0.2f);
	if (const UStaticMeshSocket* Socket = Asset->FindSocket(TEXT("Muzzle")))
	{
		MuzzleMesh = Socket->RelativeLocation;
		const FVector Toward = (MuzzleMesh - B.Origin) * FVector(1.f, 1.f, 0.f);
		if (!Toward.IsNearlyZero()) Long = Toward.GetSafeNormal();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250 gun: the cannon mesh has no Muzzle socket; the +%s end is taken for the muzzle"), bLongX ? TEXT("X") : TEXT("Y"));
	}
	const float LengthMesh = 2.f * FMath::Max(B.BoxExtent.X, B.BoxExtent.Y);
	const float Scale = LengthMesh > 1.f ? R.cannon_length_cm / LengthMesh : 1.f;
	const FRotator Turn(0.f, -Long.Rotation().Yaw, 0.f);
	Mesh->SetRelativeScale3D(FVector(Scale));
	Mesh->SetRelativeRotation(Turn);
	// Centre of the piece over the actor, lowest point on the ground.
	const FVector CentreTurned = Turn.RotateVector(B.Origin * Scale);
	Mesh->SetRelativeLocation(FVector(-CentreTurned.X, -CentreTurned.Y, -(B.Origin.Z - B.BoxExtent.Z) * Scale));
	MuzzleLocal = Mesh->GetRelativeLocation() + Turn.RotateVector(MuzzleMesh * Scale);
	if (!bLoggedWholePiece)
	{
		bLoggedWholePiece = true;
		// One mesh for barrel, carriage and wheels: the whole piece is laid and turned as one. The
		// barrel does not elevate apart from the carriage and the wheels do not turn.
		UE_LOG(LogTemp, Display, TEXT("PA250 gun: one-piece cannon mesh (%d sections): the whole piece turns to lay; scaled x%.3f to %.0f cm, %.0f cm high"),
			Asset->GetNumSections(0), Scale, R.cannon_length_cm, 2.f * B.BoxExtent.Z * Scale);
	}
}

void APA250FieldGun::Place(FName InFaction)
{
	Faction = InFaction;
	OriginalFaction = InFaction;
	bLoaded = true;
	Drill.Configure(PA250Gunnery::DrillPreset(pa250::battle_rules()));
	Drill.Reset();
	FitMesh();
	Tags.AddUnique(TEXT("PA250FieldGun"));
	if (!Chest.IsValid() && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;
		const FVector At = GetActorLocation() + GetActorRightVector() * 90.f + FVector(0.f, 0.f, 20.f);
		if (APA250AmmoChest* Box = GetWorld()->SpawnActor<APA250AmmoChest>(At, GetActorRotation(), Params))
		{
			Box->AttachToGun(this);
			Chest = Box;
		}
	}
}

bool APA250FieldGun::IsChestEmpty() const
{
	// A blown chest is an empty one that never refills. A gun with no chest at all (none could be
	// spawned) loads as it did before chests.
	const APA250AmmoChest* Box = Chest.Get();
	return Box && (Box->IsDetonated() || Box->ReadyRounds() <= 0);
}

bool APA250FieldGun::IsChestBlown() const
{
	const APA250AmmoChest* Box = Chest.Get();
	return Box && Box->IsDetonated();
}

int32 APA250FieldGun::RoundsHeld() const
{
	const APA250AmmoChest* Box = Chest.Get();
	return (Box ? Box->ReadyRounds() : 0) + ((bLoaded || bRoundInHand) ? 1 : 0);
}

FVector APA250FieldGun::GetMuzzle() const
{
	if (Mesh && Mesh->DoesSocketExist(TEXT("Muzzle"))) return Mesh->GetSocketLocation(TEXT("Muzzle"));
	return GetActorTransform().TransformPosition(MuzzleLocal);
}

FVector APA250FieldGun::GetCrewPost(int32 Index) const
{
	// Role sockets (Artillery pass): sponger, loader, ventsman/firer, handspike/captain.
	static const FName RoleSockets[] = {
		FName(TEXT("Crew_1")), FName(TEXT("Crew_2")), FName(TEXT("Vent")), FName(TEXT("Linstock")),
		FName(TEXT("TrailL")), FName(TEXT("Trail")), FName(TEXT("Crew_3")), FName(TEXT("Crew_4"))
	};
	const int32 I = FMath::Clamp(Index, 0, 3);
	// Tool sockets sit on the barrel or trail. A man's capsule belongs on the ground,
	// alongside the vent, rather than 90 cm above the tool socket.
	auto StandAt = [&](FName Socket) {
		FVector At = Mesh->GetSocketLocation(Socket);
		if (Socket == TEXT("Vent")) At += GetActorRightVector() * 110.f;
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250CrewGround), false, this);
		if (GetWorld()->LineTraceSingleByObjectType(Ground, At + FVector(0.f, 0.f, 300.f),
			At - FVector(0.f, 0.f, 1000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params)) At.Z = Ground.ImpactPoint.Z + 90.f;
		else At.Z = GetActorLocation().Z + 90.f;
		return At;
	};
	if (Mesh)
	{
		// Crew sockets are separated foot positions. Vent and Linstock are tool
		// attachment points and would overlap the sponger if used as foot positions.
		const FName Legacy(*FString::Printf(TEXT("Crew_%d"), I + 1));
		if (Mesh->DoesSocketExist(Legacy)) return StandAt(Legacy);
		const FName Prefer = RoleSockets[I];
		if (Mesh->DoesSocketExist(Prefer)) return StandAt(Prefer);
		// Fallbacks if the mesh has not been regenerated yet.
		const FName Alt = RoleSockets[I + 4];
		if (Mesh->DoesSocketExist(Alt)) return StandAt(Alt);
	}
	const float Half = pa250::battle_rules().cannon_length_cm * 0.5f;
	static const FVector2D Share[] = {FVector2D(-0.25f, 1.f), FVector2D(-0.25f, -1.f), FVector2D(-1.05f, 0.7f), FVector2D(-1.05f, -0.7f)};
	const FVector2D S = Share[I];
	return GetActorTransform().TransformPosition(FVector(S.X * Half, S.Y * 110.f, 90.f));
}

int32 APA250FieldGun::ClaimPost(ARFCharacter* Gunner)
{
	if (!Gunner || Gunner->IsDead() || Gunner->FactionId != Faction) return INDEX_NONE;
	const int32 Men = FMath::Min(Posts.Num(), pa250::battle_rules().cannon_crew_men);
	for (int32 i = 0; i < Men; ++i)
	{
		if (Posts[i].Get() == Gunner) return i;
	}
	for (int32 i = 0; i < Men; ++i)
	{
		const ARFCharacter* Held = Posts[i].Get();
		if (!Held || Held->IsDead() || Held->FactionId != Faction)
		{
			Posts[i] = Gunner;
			return i;
		}
	}
	return INDEX_NONE;
}

int32 APA250FieldGun::ForceClaimPost(ARFCharacter* Gunner)
{
	if (!Gunner || Gunner->IsDead() || Gunner->FactionId != Faction) return INDEX_NONE;
	if (const int32 Have = ClaimPost(Gunner); Have != INDEX_NONE) return Have;
	const int32 Men = FMath::Min(Posts.Num(), pa250::battle_rules().cannon_crew_men);
	if (Men <= 0) return INDEX_NONE;
	Posts[0] = Gunner;
	return 0;
}

void APA250FieldGun::SetPlayerGunner(ARFCharacter* Player)
{
	// Restore a previous gunner's musket when he leaves the piece.
	if (ARFCharacter* Was = PlayerGunner.Get())
	{
		if (Was != Player && Was->Musket) Was->Musket->SetActorHiddenInGame(false);
	}
	PlayerGunner = Player;
	if (!Player)
	{
		bPlayerChoseShot = false;
		SelectedShot = EPA250Shot::RoundShot;
		bFollowPlayer = false;
		if (AimMark) AimMark->SetVisibility(false);
		return;
	}
	// Player artilleryman: kit stays; while serving, weapon holstered (tools in hand).
	if (Player->Musket) Player->Musket->SetActorHiddenInGame(true);
	if (!bPlayerChoseShot) SelectedShot = EPA250Shot::RoundShot;
}

void APA250FieldGun::CycleShot(int32 Delta)
{
	if (!PlayerGunner.IsValid() || Delta == 0) return;
	const int32 N = 3;
	int32 I = static_cast<int32>(SelectedShot);
	I = (I + Delta % N + N) % N;
	SelectedShot = static_cast<EPA250Shot>(I);
	bPlayerChoseShot = true;
	FString Line = LoadChip();
	if (APA250BattleGameMode* GM = BattleOf(this))
	{
		const float Range = AimMarkWorld.IsNearlyZero() ? 0.f : FVector::Dist(GetMuzzle(), AimMarkWorld);
		if (Range > 1.f)
		{
			if (const TCHAR* Why = PA250Gunnery::RefuseShot(SelectedShot, Range, pa250::battle_rules()))
			{
				Line = FString::Printf(TEXT("%s — %s"), *LoadChip(), Why);
			}
		}
		GM->ShowNotice(Line, 2.5f);
	}
}

void APA250FieldGun::UpdateAimMark(const FVector& AimAt)
{
	AimMarkWorld = AimAt;
	if (!AimMark) return;
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		if (AimMark->GetStaticMesh() != Sphere) AimMark->SetStaticMesh(Sphere);
	}
	AimMark->SetWorldLocation(AimAt);
	AimMark->SetVisibility(PlayerGunner.IsValid());
}

int32 APA250FieldGun::PostOf(const ARFCharacter* Gunner) const
{
	const int32 Men = FMath::Min(Posts.Num(), pa250::battle_rules().cannon_crew_men);
	for (int32 i = 0; i < Men; ++i)
	{
		if (Gunner && Posts[i].Get() == Gunner) return i;
	}
	return INDEX_NONE;
}

bool APA250FieldGun::HasFreePost() const
{
	return FreePostIndex() != INDEX_NONE;
}

int32 APA250FieldGun::FreePostIndex() const
{
	const int32 Men = FMath::Min(Posts.Num(), pa250::battle_rules().cannon_crew_men);
	for (int32 i = 0; i < Men; ++i)
	{
		const ARFCharacter* Held = Posts[i].Get();
		if (!Held || Held->IsDead() || Held->FactionId != Faction) return i;
	}
	return INDEX_NONE;
}

void APA250FieldGun::RecountCrew()
{
	const pa250::BattleRules& R = pa250::battle_rules();
	int32 TrainedNear = 0;
	CountMen(OwnNear, TrainedNear, EnemiesNear);
	(void)TrainedNear;
	// Serving crew is a claimed post or an artilleryman inside the crew radius.
	// Infantry walking through the radius used to count, so a short crew never
	// abandoned and the piece could still fire.
	TArray<const ARFCharacter*, TInlineAllocator<8>> Serving;
	int32 ServeTrained = 0;
	auto Consider = [&](const ARFCharacter* M)
	{
		if (!M || M->IsDead() || M->IsMounted() || M->FactionId != Faction || Serving.Contains(M)
			|| M->ActorHasTag(TEXT("PA250Prisoner")) || M->ActorHasTag(TEXT("PA250CrewFallingBack"))) return;
		if (FVector::Dist2D(M->GetActorLocation(), GetActorLocation()) > R.cannon_crew_radius_cm) return;
		Serving.Add(M);
		if (PA250UnitClasses::KitFor(M->FactionId, M->UnitClass).bServesGuns) ++ServeTrained;
	};
	for (const TWeakObjectPtr<ARFCharacter>& P : Posts) Consider(P.Get());
	if (const ARFCharacter* Player = PlayerGunner.Get()) Consider(Player);
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (!PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bServesGuns) continue;
		Consider(*It);
	}
	CrewNow = FMath::Min(Serving.Num(), R.cannon_crew_men);
	TrainedNow = FMath::Min(ServeTrained, CrewNow);
}

float APA250FieldGun::LayTo(const FVector& AimAt)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	const FVector Muzzle = GetMuzzle();
	const float Horiz = FMath::Max(100.f, FVector::Dist2D(AimAt, Muzzle));
	const bool bCanister = SelectedShot == EPA250Shot::Grape;
	const float Speed = (bCanister ? R.cannon_grape_speed_m
		: SelectedShot == EPA250Shot::Shell ? R.cannon_shell_speed_m : R.cannon_round_speed_m) * 100.f;
	const float Gravity = bCanister ? FMath::Abs(GetWorld()->GetGravityZ()) * 0.3f : R.cannon_shot_gravity_cm;
	const float Drag = bCanister ? 0.f : R.cannon_shot_drag_k;
	// Fire the notch that lands closest. Rounding the continuous pitch onto a coarse quoin
	// (six steps, nearest angle) missed a 300 m mark by several metres.
	int32 Notch = 0;
	const int32 Steps = FMath::Max(2, R.cannon_quoin_steps);
	LayPitch = PA250Gunnery::BestQuoinElevationDeg(Horiz, Speed, AimAt.Z - Muzzle.Z,
		R.cannon_elevation_min_deg, R.cannon_elevation_max_deg, Steps, Notch,
		Gravity, Drag);
	QuoinNotch = Notch;
	if (!bLoggedLay)
	{
		bLoggedLay = true;
		UE_LOG(LogTemp, Display, TEXT("PA250 gun: one-piece mesh stays level; quoin notch %d of %d lays %.1f°"),
			QuoinNotch, Steps, LayPitch);
	}
	return LayPitch;
}

bool APA250FieldGun::DischargeAt(const FVector& AimPoint, ARFCharacter* Gunner)
{
	if (Gunner) PlayerGunner = Gunner;
	bPlayerChoseShot = true;
	RecountCrew();
	return Fire(AimPoint, Gunner ? Gunner : PlayerGunner.Get(), true);
}

void APA250FieldGun::CountMen(int32& OutOwn, int32& OutTrained, int32& OutEnemies) const
{
	const pa250::BattleRules& R = pa250::battle_rules();
	OutOwn = OutTrained = OutEnemies = 0;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() || It->FactionId.IsNone() || It->IsMounted()) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (It->FactionId == Faction)
		{
			if (D > R.cannon_crew_radius_cm) continue;
			++OutOwn;
			OutTrained += PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bServesGuns ? 1 : 0;
		}
		else if (D <= R.cannon_capture_radius_cm)
		{
			++OutEnemies;
		}
	}
}

FString APA250FieldGun::LoadChip() const
{
	return FString::Printf(TEXT("LOAD: %s"), *FString(PA250Gunnery::ShotWords(SelectedShot)).ToUpper());
}

FString APA250FieldGun::StatusLine() const
{
	const pa250::BattleRules& R = pa250::battle_rules();
	const FString Chip = LoadChip();
	if (CrewNow < R.cannon_min_crew)
	{
		return FString::Printf(TEXT("%s\n%d at the gun — needs %d to fire"), *Chip, CrewNow, R.cannon_min_crew);
	}
	if (!bLoaded && !bRoundInHand && IsChestEmpty())
	{
		return FString::Printf(TEXT("%s\n%s  ·  crew %d of %d"), *Chip,
			IsChestBlown() ? TEXT("Chest blown") : TEXT("Chest empty"), CrewNow, R.cannon_crew_men);
	}
	if (bLoaded)
	{
		return FString::Printf(TEXT("%s\ncrew %d of %d  ·  look lays  ·  fire  ·  [ ] load"),
			*Chip, CrewNow, R.cannon_crew_men);
	}
	const FString Step = Drill.GetStepLabel();
	return FString::Printf(TEXT("%s\n%s %d%%  ·  crew %d of %d  ·  R on the mark"),
		*Chip, *Step, FMath::RoundToInt(Drill.GetPhaseProgress() * 100.f),
		CrewNow, R.cannon_crew_men);
}

void APA250FieldGun::ChangeHands(FName NewFaction)
{
	const FName Was = Faction;
	TArray<ARFCharacter*> Leaving;
	for (TWeakObjectPtr<ARFCharacter>& P : Posts)
	{
		if (ARFCharacter* M = P.Get())
		{
			if (!M->IsDead() && M->FactionId == Was) Leaving.Add(M);
		}
		P = nullptr;
	}
	Faction = NewFaction;
	SetPlayerGunner(nullptr);
	OverrunSeconds = 0.f;
	UE_LOG(LogTemp, Display, TEXT("PA250 gun: taken by %s from %s (%s)"), *NewFaction.ToString(), *Was.ToString(), NewFaction == OriginalFaction ? TEXT("retaken") : TEXT("captured"));
	if (APA250BattleGameMode* GM = BattleOf(this))
	{
		GM->NotifyGunTaken(this, Was);
		GM->FallBackGunCrew(Leaving, Was, this);
	}
}

bool APA250FieldGun::PickTarget(FVector& OutPoint, AActor*& OutActor) const
{
	const pa250::BattleRules& R = pa250::battle_rules();
	const FVector Muzzle = GetMuzzle();
	const float Range = R.cannon_round_range_m * 100.f;
	ARFCharacter* Best = nullptr;
	float BestD = Range;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() || It->FactionId == Faction || It->FactionId.IsNone()) continue;
		const float D = FVector::Dist(It->GetActorLocation(), Muzzle);
		if (D < BestD)
		{
			BestD = D;
			Best = *It;
		}
	}
	if (!Best) return false;
	OutPoint = Best->GetActorLocation();
	OutActor = Best;
	// What stands between the gun and him? A field work he shelters behind is the target: round
	// shot batters it down. Anything else in the way and the crew holds its fire.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250GunLay), false, this);
	Params.AddIgnoredActor(Best);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, OutPoint + FVector(0.f, 0.f, 20.f), ECC_Visibility, Params))
	{
		APA250FieldWork* Work = Cast<APA250FieldWork>(Hit.GetActor());
		if (!Work) return false;
		OutPoint = Hit.ImpactPoint;
		OutActor = Work;
	}
	return true;
}

bool APA250FieldGun::Fire(const FVector& AimPoint, ARFCharacter* Gunner, bool bPlayerChose)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	RecountCrew();
	const FVector Muzzle = GetMuzzle();
	const float Range = FVector::Dist(Muzzle, AimPoint);
	EPA250Shot Shot = SelectedShot;
	const TCHAR* Refuse = nullptr;
	if (!bPlayerChose || !bPlayerChoseShot)
	{
		if (!PA250Gunnery::ChooseShot(Range, R, Shot)) Refuse = TEXT("out of range");
	}
	else
	{
		Refuse = PA250Gunnery::RefuseShot(Shot, Range, R);
	}
	if (!bLoaded || CrewNow < R.cannon_min_crew)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 gun: refused %s — %d at the piece, needs %d"),
			PA250Gunnery::ShotWords(Shot), CrewNow, R.cannon_min_crew);
		if (bPlayerChose)
		{
			if (APA250BattleGameMode* GM = BattleOf(this))
			{
				GM->ShowNotice(FString::Printf(TEXT("Too few men at the gun (%d, needs %d)"), CrewNow, R.cannon_min_crew), 3.f);
			}
		}
		return false;
	}
	if (Refuse)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 gun: refused %s at %.0f m — %s"), PA250Gunnery::ShotWords(Shot), Range / 100.f, Refuse);
		if (bPlayerChose)
		{
			if (APA250BattleGameMode* GM = BattleOf(this)) GM->ShowNotice(FString(Refuse), 3.f);
		}
		return false;
	}
	// Both AI and player fire use an absolute elevation above horizontal.
	// Starting with muzzle-to-mark pitch and adding LayPitch counted height twice.
	SelectedShot = Shot;
	LayTo(AimPoint);
	// Windage + muzzle-velocity σ (Artillery pass M6). No ball wind-drift / wet-powder this pass.
	FRandomStream AimRng(HashCombine(GetTypeHash(static_cast<uint64>(ShotsFired + 1)), GetTypeHash(Faction.GetComparisonIndex())));
	const float WindageDeg = AimRng.FRandRange(-0.4f, 0.4f);
	const float MvSigma = AimRng.FRandRange(0.97f, 1.03f);
	const FVector Dir = FRotator(LayPitch, GetActorRotation().Yaw + WindageDeg, 0.f).Vector();
	bLoaded = false;
	Drill.Reset();
	DrillStepHeard = -1;
	bRoundInHand = false;
	++ShotsFired;
	BarrelHeat = FMath::Min(1.f, BarrelHeat + 0.18f);
	RecoilLeft = 0.55f;
	SettleWobble = AimRng.FRandRange(0.8f, 1.4f);
	// Smoke hangs and drifts with PA250Wind (visual only; blocks the gunner's view).
	if (UPA250SmokeSubsystem* Bank = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>())
	{
		const int32 Clouds = 10 + FMath::RoundToInt(BarrelHeat * 6.f);
		for (int32 i = 0; i < Clouds; ++i)
		{
			Bank->AddShot(Muzzle + Dir * (40.f * i), FMath::VRandCone(Dir, FMath::DegreesToRadians(12.f)));
		}
	}
	FPA250BattleAudio::PlayAtOr(this, TEXT("cannon_fire"), TEXT("musket_distant"), Muzzle, EPA250SoundRange::Gunfire, 1.f, 0.04f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (Shot == EPA250Shot::RoundShot)
	{
		const float Speed = R.cannon_round_speed_m * 100.f * MvSigma;
		FVector Laid = Dir;
		if (R.cannon_round_spread_degrees > 0.f) Laid = FMath::VRandCone(Laid, FMath::DegreesToRadians(R.cannon_round_spread_degrees));
		if (APA250CannonBall* Ball = GetWorld()->SpawnActor<APA250CannonBall>(Muzzle, Laid.Rotation(), Params))
		{
			Ball->Launch(Laid * Speed, Faction, Gunner, this);
		}
	}
	else if (Shot == EPA250Shot::Shell)
	{
		const float Speed = R.cannon_shell_speed_m * 100.f * MvSigma;
		const float Fuse = FMath::Clamp(Range, R.cannon_shell_fuse_min_m * 100.f, R.cannon_shell_range_m * 100.f);
		FVector Laid = Dir;
		if (R.cannon_shell_spread_degrees > 0.f) Laid = FMath::VRandCone(Laid, FMath::DegreesToRadians(R.cannon_shell_spread_degrees));
		if (APA250CannonShell* Ball = GetWorld()->SpawnActor<APA250CannonShell>(Muzzle, Laid.Rotation(), Params))
		{
			Ball->Launch(Laid * Speed, Fuse, Faction, Gunner, this);
		}
	}
	else
	{
		// Canister (enum grape): ~40 pellets, ~4° half-angle, falloff past 150 m.
		const float HalfAngle = FMath::Max(0.5f, R.cannon_grape_cone_degrees);
		for (int32 i = 0; i < FMath::Max(1, R.cannon_grape_balls); ++i)
		{
			const FVector D = FMath::VRandCone(Dir, FMath::DegreesToRadians(HalfAngle));
			const FTransform At(D.Rotation(), Muzzle + D * 30.f);
			AMusketProjectile* Ball = GetWorld()->SpawnActorDeferred<AMusketProjectile>(AMusketProjectile::StaticClass(), At, this, Gunner,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Ball) continue;
			// Flat canister damage. Falloff is by how far the pellet flies, applied in OnHit.
			// Scaling by the aim mark here made every pellet in the cone share the mark's range.
			Ball->Damage = R.cannon_grape_damage;
			Ball->FalloffStartCm = R.cannon_grape_falloff_start_m * 100.f;
			Ball->FalloffEndCm = R.cannon_grape_range_m * 100.f;
			Ball->FalloffFloor = R.cannon_grape_falloff_floor;
			Ball->FinishSpawning(At);
			Ball->SpawnLocation = Muzzle;
			if (UProjectileMovementComponent* Move = Ball->GetProjectileMovement())
			{
				const float Speed = R.cannon_grape_speed_m * 100.f * MvSigma;
				Move->Velocity = D * Speed;
				Move->InitialSpeed = Speed;
				Move->MaxSpeed = Speed;
			}
			Ball->SetLifeSpan(FMath::Max(1.f, R.cannon_grape_range_m / FMath::Max(1.f, R.cannon_grape_speed_m) + 0.5f));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 gun: %s gun fires %s at %.0f m (crew %d, %d gunners, heat %.2f)"), *Faction.ToString(),
		PA250Gunnery::ShotWords(Shot), Range / 100.f, CrewNow, TrainedNow, BarrelHeat);
	if (APA250BattleGameMode* GM = BattleOf(this)) GM->NotifyGunFired(this, Shot);
	return true;
}

bool APA250FieldGun::PlayerFire(const FVector& AimPoint)
{
	ARFCharacter* Me = PlayerGunner.Get();
	if (!Me || Me->IsDead()) return false;
	// The mesh stays level. The shot is laid onto the ground mark; Fire adds the drop.
	const FVector Mark = AimMarkWorld.IsNearlyZero() ? AimPoint : AimMarkWorld;
	return Fire(Mark, Me, true);
}

EActiveReloadHit APA250FieldGun::PlayerCommit()
{
	if (bLoaded || !Drill.IsReloading()) return EActiveReloadHit::None;
	// Load with no round in hand (the chest is empty): there is nothing to commit.
	if (Drill.GetStepIndex() == 1 && !bRoundInHand) return EActiveReloadHit::None;
	return Drill.TryCommit();
}

void APA250FieldGun::DischargePremature(FRandomStream& Hot)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	UE_LOG(LogTemp, Warning, TEXT("PA250 gun: premature discharge (heat %.2f)"), BarrelHeat);
	BarrelHeat = 0.f;
	bLoaded = false;
	bRoundInHand = false;
	Drill.Reset();
	DrillStepHeard = -1;
	// The sponger at the muzzle is the one sure casualty; the blast then takes its chance of
	// everyone else, and he is left out of it so he is never counted twice.
	TSet<ARFCharacter*> SpongerSkip;
	ARFCharacter* Sponger = Posts.Num() > 0 ? Posts[0].Get() : nullptr;
	if (Sponger && !Sponger->IsDead() && Sponger->FactionId == Faction
		&& FVector::Dist2D(Sponger->GetActorLocation(), GetActorLocation()) <= R.cannon_crew_radius_cm)
	{
		SpongerSkip.Add(Sponger);
		APA250BattleGameMode* GM = BattleOf(this);
		if (GM && Hot.FRand() < 0.55f) GM->WoundOut(Sponger, nullptr, this);
		else
		{
			FHitResult Hit;
			Hit.ImpactPoint = Sponger->GetActorLocation();
			Hit.Location = Hit.ImpactPoint;
			UGameplayStatics::ApplyPointDamage(Sponger, 999.f, FVector::UpVector, Hit, nullptr, this, nullptr);
		}
		if (GM) GM->NoteFriendlyBlastCasualties(Faction, Sponger->IsDead() && !Sponger->ActorHasTag(TEXT("PA250Wounded")) ? 1 : 0,
			Sponger->ActorHasTag(TEXT("PA250Wounded")) ? 1 : 0);
	}
	PA250Blast::ApplyBlast(GetWorld(), GetMuzzle(), EPA250BlastSize::Premature, Faction, nullptr, this,
		static_cast<uint32>(Hot.GetCurrentSeed()), &SpongerSkip);
}

void APA250FieldGun::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const pa250::BattleRules& R = pa250::battle_rules();
	APA250BattleGameMode* GM = BattleOf(this);
	if (Faction.IsNone() || !GM || GM->IsBattleOver()) return;
	// Who stands by the gun (four times a second is plenty).
	CountTimer -= DeltaSeconds;
	if (CountTimer <= 0.f)
	{
		CountTimer = 0.25f;
		RecountCrew();
	}
	// Overrun: nobody of its own side by it and the enemy standing over it.
	OverrunSeconds = PA250Gunnery::IsOverrun(OwnNear, EnemiesNear) ? OverrunSeconds + DeltaSeconds : 0.f;
	if (OverrunSeconds >= R.cannon_capture_seconds)
	{
		ChangeHands(Faction == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british")));
		return;
	}
	ARFCharacter* Player = PlayerGunner.Get();
	if (Player && (Player->IsDead() || Player->FactionId != Faction || FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) > R.cannon_crew_radius_cm))
	{
		SetPlayerGunner(nullptr);
		Player = nullptr;
	}
	if (!Player) bFollowPlayer = false;
	// Heat cools; recoil settles with a short wobble on traverse.
	BarrelHeat = FMath::Max(0.f, BarrelHeat - DeltaSeconds * 0.04f);
	if (RecoilLeft > 0.f)
	{
		RecoilLeft = FMath::Max(0.f, RecoilLeft - DeltaSeconds);
	}
	SettleWobble = FMath::Max(0.f, SettleWobble - DeltaSeconds);
	// Premature discharge (M6): sponger at the muzzle is the guaranteed casualty (55% WIA / 45% KIA),
	// then ApplyBlast(Premature) for everyone else.
	if (!bLoaded && Drill.IsReloading() && BarrelHeat > 0.72f && Drill.GetStepIndex() == 0)
	{
		FRandomStream Hot(HashCombine(GetTypeHash(static_cast<uint64>(FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 10.f))), GetTypeHash(Faction.GetComparisonIndex())));
		if (Hot.FRand() < 0.002f * BarrelHeat)
		{
			DischargePremature(Hot);
		}
	}
	// The drill: as fast as the men at the gun can make it. Missing roles cover +25% step time.
	// Load (step 1) begins by taking one round from the chest. With none to take the drill
	// stands at Load ("Chest empty") and a commit does nothing, so no round is loaded for free.
	float Speed = PA250Gunnery::DrillSpeed(CrewNow, TrainedNow, R);
	if (CrewNow > 0 && CrewNow < R.cannon_crew_men) Speed /= 1.25f;
	if (!bLoaded && Speed > 0.f)
	{
		if (!Drill.IsReloading() && !Drill.IsComplete())
		{
			Drill.BeginReload();
			bRoundInHand = false;
		}
		if (Drill.IsReloading() && Drill.GetStepIndex() == 1 && !bRoundInHand)
		{
			APA250AmmoChest* Box = Chest.Get();
			bRoundInHand = !Box || Box->TakeOneForLoad();
		}
		const bool bStalled = Drill.IsReloading() && Drill.GetStepIndex() == 1 && !bRoundInHand;
		if (!bStalled)
		{
			// One step at a time: a long frame must not carry the drill through Load unpaid.
			const int32 StepBefore = Drill.GetStepIndex();
			Drill.Tick(DeltaSeconds * Speed);
			if (Drill.IsReloading() && StepBefore == 0 && Drill.GetStepIndex() > 1 && !bRoundInHand)
			{
				APA250AmmoChest* Box = Chest.Get();
				bRoundInHand = !Box || Box->TakeOneForLoad();
				if (!bRoundInHand)
				{
					UE_LOG(LogTemp, Warning, TEXT("PA250 gun: drill passed Load with an empty chest; the drill starts again"));
					Drill.Reset();
					DrillStepHeard = -1;
				}
			}
			if (Drill.IsReloading() && Drill.GetStepIndex() != DrillStepHeard)
			{
				if (DrillStepHeard >= 0)
				{
					FPA250BattleAudio::PlayAt(this, TEXT("ramrod"), GetActorLocation(), EPA250SoundRange::Near, 0.7f, 0.08f);
				}
				DrillStepHeard = Drill.GetStepIndex();
			}
			if (Drill.IsComplete())
			{
				bLoaded = true;
				bRoundInHand = false;
				Drill.Reset();
				DrillStepHeard = -1;
			}
		}
	}
	CrewAbandonHold = FMath::Max(0.f, CrewAbandonHold - DeltaSeconds);
	// Serving crew under two, and the player is not at the piece: those men fall back.
	// Gunners standing in the radius count as crew even after their posts are cleared, so
	// they have to leave too. The hold stops a man who cannot be moved from being ordered every tick.
	if (CrewNow < R.cannon_min_crew && !Player && CrewAbandonHold <= 0.f)
	{
		TArray<ARFCharacter*> Leaving;
		auto AddLeaving = [&](ARFCharacter* M)
		{
			if (!M || M->IsDead() || M->FactionId != Faction || M->IsPlayerControlled() || Leaving.Contains(M)) return;
			Leaving.Add(M);
		};
		for (TWeakObjectPtr<ARFCharacter>& P : Posts)
		{
			AddLeaving(P.Get());
			P = nullptr;
		}
		for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
		{
			if (!PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bServesGuns) continue;
			if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) > R.cannon_crew_radius_cm) continue;
			AddLeaving(*It);
		}
		if (Leaving.Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 gun: crew below %d abandons the piece"), R.cannon_min_crew);
			if (GM) GM->FallBackGunCrew(Leaving, Faction, this);
			CrewAbandonHold = 2.f;
		}
	}
	if (CrewNow < R.cannon_min_crew) return;
	// Run up after the player: the crew pushes it toward him while he walks ahead of it.
	if (Player && bFollowPlayer)
	{
		if (!PA250Gunnery::CanPush(CrewNow, !bLoaded, R)) return;
		const FVector Gap = Player->GetActorLocation() - GetActorLocation();
		if (Gap.Size2D() < 220.f) return;
		const FVector Dir = Gap.GetSafeNormal2D();
		FVector To = GetActorLocation() + Dir * R.cannon_push_speed_cm * DeltaSeconds;
		FHitResult Ground;
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(PA250GunPushPlayer), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Ground, To + FVector(0.f, 0.f, 300.f), To - FVector(0.f, 0.f, 600.f), ECC_WorldStatic, GroundParams)) To.Z = Ground.ImpactPoint.Z;
		SetActorLocationAndRotation(To, FRotator(0.f, Dir.Rotation().Yaw, 0.f));
		return;
	}
	// Laying the piece: on the player's look (yaw + pitch) when he serves it, else on the crew's own target.
	FVector AimAt = FVector::ZeroVector;
	AActor* TargetActor = nullptr;
	bool bHasAim = false;
	if (Player)
	{
		if (const APlayerController* PC = Cast<APlayerController>(Player->GetController()))
		{
			const FRotator Look = PC->GetControlRotation();
			const float RangeCm = FMath::Clamp(
				SelectedShot == EPA250Shot::Shell ? R.cannon_shell_range_m * 100.f
					: (SelectedShot == EPA250Shot::Grape ? R.cannon_grape_range_m * 100.f : R.cannon_round_range_m * 100.f),
				2000.f, R.cannon_round_range_m * 100.f);
			// Ground mark along the look. Pitch chooses how far out the mark sits; the piece itself
			// stays level and the shot is lofted onto that mark.
			FHitResult Ground;
			const FVector Eye = Player->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			const FVector LookDir = Look.Vector();
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PA250GunAim), false, this);
			TraceParams.AddIgnoredActor(Player);
			if (GetWorld()->LineTraceSingleByChannel(Ground, Eye, Eye + LookDir * RangeCm, ECC_Visibility, TraceParams))
			{
				AimAt = Ground.ImpactPoint;
			}
			else
			{
				AimAt = Eye + LookDir * RangeCm;
				AimAt.Z = GetActorLocation().Z;
			}
			bHasAim = true;
			LayTo(AimAt);
			UpdateAimMark(AimAt);
		}
	}
	else
	{
		if (AimMark) AimMark->SetVisibility(false);
		bHasAim = PickTarget(AimAt, TargetActor);
	}
	if (bHasAim)
	{
		IdleSeconds = 0.f;
		const float Want = (AimAt - GetActorLocation()).Rotation().Yaw;
		const float Now = GetActorRotation().Yaw;
		float Rate = R.cannon_traverse_deg_per_second;
		if (RecoilLeft > 0.f) Rate *= 0.35f; // slow lay while settling after recoil
		if (SettleWobble > 0.f) Rate *= 0.7f;
		const float Step = FMath::Clamp(FMath::FindDeltaAngleDegrees(Now, Want), -Rate * DeltaSeconds, Rate * DeltaSeconds);
		const float Wobble = SettleWobble > 0.f ? FMath::Sin(GetWorld()->GetTimeSeconds() * 18.f) * 0.35f * SettleWobble : 0.f;
		SetActorRotation(FRotator(0.f, Now + Step + Wobble, 0.f));
		// The crew fires when the piece bears, with no friend in the path of the shot.
		if (!Player && bLoaded && FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Want)) < 1.5f)
		{
			const FVector Muzzle = GetMuzzle();
			bool bFriendInPath = false;
			for (TActorIterator<ARFCharacter> It(GetWorld()); It && !bFriendInPath; ++It)
			{
				if (It->IsDead() || It->FactionId != Faction) continue;
				if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= R.cannon_crew_radius_cm) continue; // the crew stand clear
				bFriendInPath = FMath::PointDistToSegment(It->GetActorLocation(), Muzzle, AimAt) < 250.f;
			}
			ARFCharacter* Gunner = nullptr;
			for (const TWeakObjectPtr<ARFCharacter>& P : Posts)
			{
				if (P.IsValid() && !P->IsDead()) Gunner = P.Get();
			}
			if (!bFriendInPath) Fire(AimAt + FVector(0.f, 0.f, 20.f), Gunner, false);
		}
		return;
	}
	// Nothing to fire at: a crew pushes its gun up after its own men (not while it is loading).
	IdleSeconds += DeltaSeconds;
	if (Player || IdleSeconds < 5.f || !PA250Gunnery::CanPush(CrewNow, !bLoaded, R)) return;
	const ARFCharacter* Ahead = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() || It->FactionId != Faction || PA250UnitClasses::KitFor(It->FactionId, It->UnitClass).bServesGuns) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (D < BestD && D > R.cannon_crew_radius_cm)
		{
			BestD = D;
			Ahead = *It;
		}
	}
	if (!Ahead || BestD < R.cannon_follow_gap_m * 100.f) return;
	const FVector Dir = (Ahead->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	FVector To = GetActorLocation() + Dir * R.cannon_push_speed_cm * DeltaSeconds;
	FHitResult Ground;
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(PA250GunPush), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Ground, To + FVector(0.f, 0.f, 300.f), To - FVector(0.f, 0.f, 600.f), ECC_WorldStatic, GroundParams)) To.Z = Ground.ImpactPoint.Z;
	SetActorLocationAndRotation(To, FRotator(0.f, Dir.Rotation().Yaw, 0.f));
}

// ---- Round shot -----------------------------------------------------------

APA250CannonBall::APA250CannonBall()
{
	PrimaryActorTick.bCanEverTick = true;
	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	RootComponent = Ball;
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetRelativeScale3D(FVector(0.12f)); // the engine sphere is 100 cm: a 12 cm ball (larger than life, to be seen)
	InitialLifeSpan = 8.f;
}

void APA250CannonBall::Launch(const FVector& InVelocity, FName InFaction, ARFCharacter* InGunner, const AActor* InGun)
{
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"))) Ball->SetStaticMesh(Sphere);
	Velocity = InVelocity;
	Faction = InFaction;
	Gunner = InGunner;
	Gun = InGun;
	MenLeft = FMath::Max(1, pa250::battle_rules().cannon_round_pierce);
}

void APA250CannonBall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Match the solver's integration step even on a slow rendered frame.
	float Left = DeltaSeconds;
	while (Left > SMALL_NUMBER && !IsActorBeingDestroyed())
	{
		const float Step = FMath::Min(Left, 0.01f);
		AdvanceFlight(Step);
		Left -= Step;
	}
}

void APA250CannonBall::AdvanceFlight(float DeltaSeconds)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	UWorld* World = GetWorld();
	if (!World || Velocity.IsNearlyZero()) return;
	APA250BattleGameMode* GM = BattleOf(this);
	PA250Gunnery::ApplyShotAccel(Velocity, DeltaSeconds, R.cannon_shot_gravity_cm, R.cannon_shot_drag_k);
	const FVector From = GetActorLocation();
	FVector To = From + Velocity * DeltaSeconds;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250RoundShot), false, this);
	if (Gun.IsValid()) Params.AddIgnoredActor(Gun.Get());
	for (TActorIterator<ARFCharacter> It(World); It; ++It) Params.AddIgnoredActor(*It);
	// No chest stops a round shot: it goes through the crate (and may set it off, below).
	for (TActorIterator<APA250AmmoChest> It(World); It; ++It) Params.AddIgnoredActor(*It);
	FHitResult SurfaceHit;
	const bool bBlocked = World->LineTraceSingleByChannel(SurfaceHit, From, To, ECC_Visibility, Params);
	if (bBlocked) To = SurfaceHit.ImpactPoint;
	Travelled += FVector::Dist(From, To);
	// Ammunition chest: struck squarely, or passed within a ball's width of it. One roll for
	// each chest for each shot, however many flight steps the ball spends beside it.
	for (TActorIterator<APA250AmmoChest> It(World); It; ++It)
	{
		APA250AmmoChest* Box = *It;
		if (!Box || Box->IsDetonated() || ChestsRolled.Contains(Box)) continue;
		if (Gun.IsValid() && Box->GetGun() == Gun.Get()) continue;
		FVector OnPath, OnBox;
		FMath::SegmentDistToSegment(From, To, Box->GetActorLocation(), Box->GetActorLocation() + FVector(0.f, 0.f, 40.f), OnPath, OnBox);
		if (FVector::Dist(OnPath, OnBox) > 55.f) continue;
		ChestsRolled.Add(Box);
		const uint32 Seed = HashCombine(GetTypeHash(static_cast<uint64>(Travelled)), GetTypeHash(Faction.GetComparisonIndex()));
		if (Box->TryDetonateFromHit(/*bShell*/ false, Seed, Faction, Gunner.Get()))
		{
			Destroy();
			return;
		}
	}
	// Men along this step, nearest first. Actor order is not path order, so a man behind
	// the shot used to take a pierce that belonged to the man in front.
	struct FInPath
	{
		ARFCharacter* Man = nullptr;
		float Along = 0.f;
		float Off = 0.f;
		bool bDirect = false;
	};
	TArray<FInPath> InPath;
	const FVector StepDir = (To - From).GetSafeNormal();
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		ARFCharacter* Man = *It;
		if (Man->IsDead() || Man->FactionId == Faction || Man->FactionId.IsNone() || Struck.Contains(Man)) continue;
		const FVector Body = Man->GetActorLocation();
		// Do not let the swept capsule's endpoint reach a man behind an opaque surface.
		if (bBlocked && FVector::DotProduct(Body - To, StepDir) > 0.f) continue;
		FVector OnPath, OnBody;
		FMath::SegmentDistToSegment(From, To, Body - FVector(0.f, 0.f, 60.f), Body + FVector(0.f, 0.f, 80.f), OnPath, OnBody);
		const float Off = FVector::Dist(OnPath, OnBody);
		const float ReactRadius = FMath::Max(R.cannon_knockdown_radius_cm,
			FMath::Max(R.blast_near_miss_path_cm, R.blast_dirt_spray_cm));
		if (Off > ReactRadius) continue;
		const float CentreAlong = FVector::DotProduct(Body - From, StepDir);
		// A near miss is beside this segment, not within a sphere at its end.
		// Knocking down men 2 m ahead moved the next pierce targets out of the shot.
		if (Off > 45.f && (CentreAlong < 0.f || CentreAlong > FVector::Dist(From, To))) continue;
		FInPath Row;
		Row.Man = Man;
		Row.Along = FVector::DotProduct(OnPath - From, StepDir);
		Row.Off = Off;
		Row.bDirect = Off <= 45.f;
		InPath.Add(Row);
	}
	InPath.Sort([](const FInPath& A, const FInPath& B) { return A.Along < B.Along; });
	float SpeedMps = Velocity.Size() / 100.f;
	for (const FInPath& Row : InPath)
	{
		ARFCharacter* Man = Row.Man;
		if (!Man || Man->IsDead()) continue;
		const bool bHit = Row.bDirect && MenLeft > 0;
		if (!bHit && KnockedDown.Contains(Man)) continue;
		const FVector Body = Man->GetActorLocation();
		if (bHit)
		{
			Struck.Add(Man);
			--MenLeft;
			const float Band = SpeedMps;
			Velocity *= 0.9f;
			SpeedMps = Velocity.Size() / 100.f;
			FHitResult FleshHit;
			FleshHit.ImpactPoint = Man->GetActorLocation();
			FleshHit.Location = FleshHit.ImpactPoint;
			if (Band >= R.cannon_round_kill_mps)
			{
				UGameplayStatics::ApplyPointDamage(Man, R.cannon_round_damage, Velocity.GetSafeNormal(), FleshHit,
					Gunner.IsValid() ? Gunner->GetController() : nullptr, this, nullptr);
			}
			else if (Band >= R.cannon_round_wound_mps)
			{
				// Leg wound (GAME INVENTION): fall, crawl, then recorded WIA.
				// Without a crawl clip, WoundOut is immediate (tests and current drop).
				const uint32 LegSeed = HashCombine(GetTypeHash(Man), GetTypeHash(static_cast<uint64>(Band * 100.f)));
				const float Crawl = FMath::Max(0.1f, R.blast_leg_wound_crawl_seconds);
				Man->LockMovement(Crawl);
				const float Played = PA250AnimClips::PlayCombatClip(Man, TEXT("AM_React_LegCrawl"), LegSeed);
				if (Played <= 0.f || !World)
				{
					if (GM) GM->WoundOut(Man, Gunner.Get(), this);
				}
				else
				{
					TWeakObjectPtr<ARFCharacter> WeakMan(Man);
					TWeakObjectPtr<ARFCharacter> WeakGunner(Gunner.Get());
					TWeakObjectPtr<APA250BattleGameMode> WeakGM(GM);
					TWeakObjectPtr<APA250CannonBall> WeakBall(this);
					FTimerHandle Handle;
					World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakMan, WeakGunner, WeakGM, WeakBall]()
					{
						// A battle that ended while he crawled has its ledger closed: he is not added to it.
						if (!WeakGM.IsValid() || WeakGM->IsBattleOver() || !WeakMan.IsValid() || WeakMan->IsDead()) return;
						WeakGM->WoundOut(WeakMan.Get(), WeakGunner.Get(), WeakBall.Get());
					}), Crawl, false);
				}
			}
			else
			{
				// Stun: no casualty. Shove him off the line of the shot.
				const FVector Lateral = FVector::VectorPlaneProject(Body - From, StepDir).GetSafeNormal2D();
				const FVector Away = Lateral.IsNearlyZero() ? FVector::CrossProduct(StepDir, FVector::UpVector).GetSafeNormal2D() : Lateral;
				if (!Man->KnockDown(Away * R.cannon_knockdown_push_cm))
					Man->LockMovement(R.cannon_knockdown_seconds);
			}
			if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
				Bank->AddImpact(FleshHit.ImpactPoint, -Velocity.GetSafeNormal(), true);
			if (GM) GM->NotifyRoundShot(Man, Band >= R.cannon_round_wound_mps);
		}
		else
		{
			KnockedDown.Add(Man);
			FVector Away = FVector::VectorPlaneProject(Body - From, StepDir).GetSafeNormal2D();
			if (Away.IsNearlyZero()) Away = FVector::CrossProduct(StepDir, FVector::UpVector).GetSafeNormal2D();
			const uint32 ReactSeed = HashCombine(GetTypeHash(Man), GetTypeHash(static_cast<uint64>(Row.Along)));
			// M5 rule kept: a man within knockdown_radius_cm of the path goes off his feet, is
			// counted as knocked down and his squad is shaken. M9 adds reactions around that and
			// changes none of it: a near-miss flinch for a man who keeps his feet, and dirt
			// thrown over men out to dirt_spray_cm, who are suppressed but not counted.
			const bool bKnock = Row.Off <= R.cannon_knockdown_radius_cm;
			if (bKnock)
			{
				if (!Man->KnockDown(Away * R.cannon_knockdown_push_cm))
				{
					Man->LockMovement(R.cannon_knockdown_seconds);
					PA250AnimClips::PlayCombatClip(Man, TEXT("AM_React_NearMiss"), ReactSeed);
				}
				Man->ApplySuppression(R.cannon_knockdown_seconds * 2.f, 6.f);
				if (GM) GM->NotifyRoundShot(Man, false);
			}
			else
			{
				Man->ApplySuppression(R.cannon_knockdown_seconds, 3.f);
				PA250AnimClips::PlayCombatClip(Man,
					Row.Off <= R.blast_near_miss_path_cm ? TEXT("AM_React_NearMiss") : TEXT("AM_React_DirtSpray"), ReactSeed);
			}
			if (Row.Off <= R.blast_dirt_spray_cm)
			{
				if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
					Bank->AddDirtBurst(Body, Away, R.cannon_dirt_burst_scale * (bKnock ? 0.35f : 0.25f));
			}
			if (!bKnock) continue; // sprayed, not struck or thrown: his squad is not shaken by it
		}
		if (GM && GM->GetVolleys() && Man->SquadId >= 0 && !SquadsShaken.Contains(Man->SquadId))
		{
			SquadsShaken.Add(Man->SquadId);
			GM->GetVolleys()->PressSquad(Man->SquadId, R.cannon_pressure, R.cannon_pressure_seconds, Faction, TEXT("CannonShock"));
		}
		if (MenLeft <= 0) break;
	}
	if (MenLeft <= 0)
	{
		Destroy();
		return;
	}
	if (bBlocked)
	{
		if (APA250FieldWork* Work = Cast<APA250FieldWork>(SurfaceHit.GetActor()))
		{
			const bool bBroken = Work->Batter(R.cannon_works_damage);
			if (GM) GM->NotifyWorkBattered(Work, bBroken, Faction);
			if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
				Bank->AddDirtBurst(SurfaceHit.ImpactPoint, FVector(SurfaceHit.ImpactNormal), R.cannon_dirt_burst_scale);
			PA250Intensity::RoundShotStrike(GetWorld(), SurfaceHit.ImpactPoint);
			Destroy();
			return;
		}
		// Graze vs bury (dirt/grass defaults: MaxGrazeAngle 18°, Restitution 0.35, GrazeRetention 0.75).
		const float IncidenceDeg = PA250Gunnery::GrazeAngleDeg(Velocity, SurfaceHit.ImpactNormal);
		const float MaxGraze = R.cannon_graze_max_deg;
		if (IncidenceDeg <= MaxGraze && SpeedMps >= R.cannon_round_wound_mps)
		{
			FVector V = Velocity;
			const FVector N = SurfaceHit.ImpactNormal;
			const float Vn = FVector::DotProduct(V, N);
			V -= (1.f + R.cannon_graze_restitution) * Vn * N;
			V = V.GetSafeNormal() * (V.Size() * R.cannon_graze_retention);
			FRandomStream Jitter(HashCombine(GetTypeHash(static_cast<uint64>(Travelled)), GetTypeHash(Faction.GetComparisonIndex())));
			V = V.RotateAngleAxis(Jitter.FRandRange(-3.f, 3.f), FVector::UpVector);
			Velocity = V;
			SetActorLocation(SurfaceHit.ImpactPoint + N * 5.f);
			if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
				Bank->AddDirtBurst(SurfaceHit.ImpactPoint, N, R.cannon_dirt_burst_scale * 0.6f);
			FPA250BattleAudio::PlayAt(this, TEXT("impact_ground"), SurfaceHit.ImpactPoint, EPA250SoundRange::Gunfire, 0.7f, 0.1f);
			PA250Intensity::RoundShotStrike(GetWorld(), SurfaceHit.ImpactPoint); // the player's camera shakes; close by, earth is thrown over his view
			return;
		}
		if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
			Bank->AddDirtBurst(SurfaceHit.ImpactPoint, FVector(SurfaceHit.ImpactNormal), R.cannon_dirt_burst_scale);
		FPA250BattleAudio::PlayAt(this, TEXT("impact_ground"), SurfaceHit.ImpactPoint, EPA250SoundRange::Gunfire, 1.f, 0.1f);
		PA250Intensity::RoundShotStrike(GetWorld(), SurfaceHit.ImpactPoint); // the player's camera shakes; close by, earth is thrown over his view
		Destroy();
		return;
	}
	if (SpeedMps < 2.f || Travelled > R.cannon_round_range_m * 100.f * 1.5f)
	{
		Destroy();
		return;
	}
	SetActorLocation(To);
}

// ---- Exploding shell ------------------------------------------------------

APA250CannonShell::APA250CannonShell()
{
	PrimaryActorTick.bCanEverTick = true;
	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	RootComponent = Shell;
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->SetRelativeScale3D(FVector(0.14f));
	InitialLifeSpan = 10.f;
}

void APA250CannonShell::Launch(const FVector& InVelocity, float InFuseCm, FName InFaction, ARFCharacter* InGunner, const AActor* InGun)
{
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"))) Shell->SetStaticMesh(Sphere);
	Velocity = InVelocity;
	FuseCm = FMath::Max(500.f, InFuseCm);
	Faction = InFaction;
	Gunner = InGunner;
	Gun = InGun;
}

void APA250CannonShell::Burst(const FVector& At)
{
	UWorld* World = GetWorld();
	if (!World) { Destroy(); return; }
	// Shared blast path (both sides). Outer radius from battle_rules.json blast.shell.
	PA250Blast::ApplyBlast(World, At, EPA250BlastSize::Shell, Faction, Gunner.Get(), this,
		HashCombine(GetTypeHash(static_cast<uint64>(Travelled)), GetTypeHash(Faction.GetComparisonIndex())));
	Destroy();
}

void APA250CannonShell::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Match the solver's integration step even on a slow rendered frame.
	float Left = DeltaSeconds;
	while (Left > SMALL_NUMBER && !IsActorBeingDestroyed())
	{
		const float Step = FMath::Min(Left, 0.01f);
		AdvanceFlight(Step);
		Left -= Step;
	}
}

void APA250CannonShell::AdvanceFlight(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || Velocity.IsNearlyZero()) return;
	const pa250::BattleRules& R = pa250::battle_rules();
	PA250Gunnery::ApplyShotAccel(Velocity, DeltaSeconds, R.cannon_shot_gravity_cm, R.cannon_shot_drag_k);
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;
	Travelled += FVector::Dist(From, To);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Shell), false, this);
	if (Gun.IsValid()) Params.AddIgnoredActor(Gun.Get());
	// The piece's own chest stands beside it: its own shell neither bursts on it nor sets it off.
	for (TActorIterator<APA250AmmoChest> It(World); It; ++It)
	{
		if (Gun.IsValid() && It->GetGun() == Gun.Get()) Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
	const FVector End = bBlocked ? FVector(Hit.ImpactPoint) : To;
	// One detonation roll for each chest it strikes or passes within 70 cm of.
	for (TActorIterator<APA250AmmoChest> It(World); It; ++It)
	{
		APA250AmmoChest* Box = *It;
		if (!Box || Box->IsDetonated() || ChestsRolled.Contains(Box)) continue;
		if (Gun.IsValid() && Box->GetGun() == Gun.Get()) continue;
		bool bStruck = bBlocked && Hit.GetActor() == Box;
		if (!bStruck)
		{
			FVector OnPath, OnBox;
			FMath::SegmentDistToSegment(From, End, Box->GetActorLocation(), Box->GetActorLocation() + FVector(0.f, 0.f, 40.f), OnPath, OnBox);
			bStruck = FVector::Dist(OnPath, OnBox) <= 70.f;
		}
		if (!bStruck) continue;
		ChestsRolled.Add(Box);
		const uint32 Seed = HashCombine(GetTypeHash(static_cast<uint64>(Travelled)), GetTypeHash(Faction.GetComparisonIndex()));
		if (Box->TryDetonateFromHit(/*bShell*/ true, Seed, Faction, Gunner.Get()))
		{
			Burst(Box->GetActorLocation());
			return;
		}
	}
	if (bBlocked)
	{
		Burst(Hit.ImpactPoint);
		return;
	}
	if (Travelled >= FuseCm)
	{
		Burst(To);
		return;
	}
	if (Travelled > R.cannon_shell_range_m * 100.f * 1.4f)
	{
		Destroy();
		return;
	}
	SetActorLocation(To);
}

// ---- Ammunition chest -----------------------------------------------------

APA250AmmoChest::APA250AmmoChest()
{
	PrimaryActorTick.bCanEverTick = true;
	Crate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Crate"));
	RootComponent = Crate;
	// A musket ball (and a canister ball) stops on it; sight and shell traces find it; a round
	// shot goes through it (APA250CannonBall ignores chests); the crew beside it are not blocked.
	// Its own object type, because a musket ball ignores WorldDynamic objects and ground traces
	// by WorldStatic must not find a crate to stand a man on.
	Crate->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Crate->SetCollisionObjectType(ECC_Destructible);
	Crate->SetCollisionResponseToAllChannels(ECR_Ignore);
	Crate->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Crate->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block); // AMusketProjectile's object type
	Crate->SetRelativeScale3D(FVector(0.55f, 0.4f, 0.35f));
	Tags.AddUnique(TEXT("PA250AmmoChest"));
}

void APA250AmmoChest::AttachToGun(APA250FieldGun* InGun)
{
	Gun = InGun;
	const pa250::BattleRules& R = pa250::battle_rules();
	Cap = FMath::Max(0, R.blast_chest_ready_rounds);
	Ready = Cap;
	bDetonated = false;
	LostOnDetonation = 0;
	RefillAccum = 0.f;
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Crate->SetStaticMesh(Cube);
	}
	if (InGun)
	{
		const FVector At = InGun->GetActorLocation() + InGun->GetActorRightVector() * 90.f + FVector(0.f, 0.f, 25.f);
		SetActorLocation(At);
		SetActorRotation(InGun->GetActorRotation());
		// It goes where the piece goes (the crew run the gun up, and traverse it).
		AttachToActor(InGun, FAttachmentTransformRules::KeepWorldTransform);
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 chest: ready_rounds=%d refill=%.1fs radius=%.0fcm (GAME INVENTION)"),
		Ready, R.blast_chest_refill_seconds, R.blast_chest_resupply_radius_cm);
}

bool APA250AmmoChest::TakeOneForLoad()
{
	if (Ready <= 0 || bDetonated) return false;
	--Ready;
	return true;
}

bool APA250AmmoChest::TryDetonateFromHit(bool bShell, uint32 Seed, FName ByFaction, ARFCharacter* By)
{
	if (bDetonated) return false;
	++Rolls;
	const pa250::BattleRules& R = pa250::battle_rules();
	FRandomStream Rng(Seed);
	const float Chance = bShell ? R.blast_chest_detonate_chance_shell : R.blast_chest_detonate_chance_round;
	if (Rng.FRand() >= Chance) return false;
	Detonate(ByFaction, By);
	return true;
}

void APA250AmmoChest::Detonate(FName ByFaction, ARFCharacter* By)
{
	if (bDetonated) return;
	bDetonated = true;
	LostOnDetonation = Ready;
	Ready = 0;
	const pa250::BattleRules& R = pa250::battle_rules();
	APA250FieldGun* Piece = Gun.Get();
	const FName Side = Piece ? Piece->GetFaction() : NAME_None;
	APA250BattleGameMode* GM = BattleOf(this);
	if (GM && LostOnDetonation > 0)
	{
		GM->AddChestRoundsLost(LostOnDetonation);
		// Rounds in a chest are not charged until fired: these never will be, so charge them now.
		GM->ChargeChestLossToMagazine(Side, LostOnDetonation);
	}
	if (GM && !Side.IsNone())
	{
		GM->ShockArmyResolve(Side, R.blast_chest_resolve_own);
		const FName Enemy = Side == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));
		GM->ShockArmyResolve(Enemy, R.blast_chest_resolve_enemy);
	}
	UE_LOG(LogTemp, Warning, TEXT("PA250 chest: detonated — %d rounds lost"), LostOnDetonation);
	// Out of the way first: the blast starts inside the crate, and a crate still solid would
	// stand between it and every man (cover for all of them).
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
	// The casualties belong to the side whose shot set it off. A chest that goes up with no
	// enemy shot behind it is its own side's accident.
	const FName Source = ByFaction.IsNone() ? Side : ByFaction;
	PA250Blast::ApplyBlast(GetWorld(), GetActorLocation(), EPA250BlastSize::Chest, Source, By, this,
		HashCombine(GetTypeHash(static_cast<uint64>(LostOnDetonation)), GetTypeHash(Side.GetComparisonIndex())));
}

void APA250AmmoChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDetonated || Ready >= Cap) return;
	APA250FieldGun* Piece = Gun.Get();
	APA250BattleGameMode* GM = BattleOf(this);
	if (!Piece || !GM || Piece->GetFaction().IsNone()) return;
	const pa250::BattleRules& R = pa250::battle_rules();
	const float Need = FMath::Max(0.1f, R.blast_chest_refill_seconds);
	const float Radius = R.blast_chest_resupply_radius_cm;
	bool bNearFriendlyPost = false;
	for (TActorIterator<APA250ControlPoint> It(GetWorld()); It; ++It)
	{
		APA250ControlPoint* Post = *It;
		if (!Post || Post->GetHolder() != Piece->GetFaction()) continue;
		if (FVector::Dist2D(Post->GetActorLocation(), Piece->GetActorLocation()) <= Radius)
		{
			bNearFriendlyPost = true;
			break;
		}
	}
	if (!bNearFriendlyPost) { RefillAccum = 0.f; return; }
	// A round in the chest is a claim on the magazine, charged when it is fired (NotifyGunFired)
	// or when the chest blows. Refilling draws nothing: it only claims a round not yet claimed.
	if (GM->MagazineSpareForChests(Piece->GetFaction()) <= 0) { RefillAccum = 0.f; return; }
	RefillAccum += DeltaSeconds;
	if (RefillAccum < Need) return;
	RefillAccum = 0.f;
	++Ready;
}
