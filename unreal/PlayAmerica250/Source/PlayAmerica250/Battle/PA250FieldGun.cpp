#include "PA250FieldGun.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "MusketProjectile.h"
#include "MusketWeapon.h"
#include "PA250BattleAudio.h"
#include "PA250BattleGameMode.h"
#include "PA250BotController.h"
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
}

FVector APA250FieldGun::GetMuzzle() const
{
	if (Mesh && Mesh->DoesSocketExist(TEXT("Muzzle"))) return Mesh->GetSocketLocation(TEXT("Muzzle"));
	return GetActorTransform().TransformPosition(MuzzleLocal);
}

FVector APA250FieldGun::GetCrewPost(int32 Index) const
{
	// The asset's own crew sockets (Scripts/setup_cannon_asset.py) when it has them.
	const FName Socket(*FString::Printf(TEXT("Crew_%d"), FMath::Clamp(Index, 0, 3) + 1));
	if (Mesh && Mesh->DoesSocketExist(Socket)) return Mesh->GetSocketLocation(Socket) + FVector(0.f, 0.f, 90.f);
	const float Half = pa250::battle_rules().cannon_length_cm * 0.5f;
	static const FVector2D Share[] = {FVector2D(-0.25f, 1.f), FVector2D(-0.25f, -1.f), FVector2D(-1.05f, 0.7f), FVector2D(-1.05f, -0.7f)};
	const FVector2D S = Share[FMath::Clamp(Index, 0, 3)];
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
	PlayerGunner = Player;
	if (!Player)
	{
		bPlayerChoseShot = false;
		SelectedShot = EPA250Shot::RoundShot;
		bFollowPlayer = false;
		if (AimMark) AimMark->SetVisibility(false);
		return;
	}
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
	int32 Trained = 0;
	CountMen(OwnNear, Trained, EnemiesNear);
	CrewNow = FMath::Min(OwnNear, R.cannon_crew_men);
	TrainedNow = FMath::Min(Trained, CrewNow);
}

float APA250FieldGun::LayTo(const FVector& AimAt)
{
	const pa250::BattleRules& R = pa250::battle_rules();
	const FVector Muzzle = GetMuzzle();
	const float Horiz = FMath::Max(100.f, FVector::Dist2D(AimAt, Muzzle));
	const float Speed = (SelectedShot == EPA250Shot::Shell ? R.cannon_shell_speed_m : R.cannon_round_speed_m) * 100.f;
	const float Drop = PA250Gunnery::RoundShotDrop(Horiz, Speed);
	const float Elev = PA250Gunnery::BallisticElevationDeg(Horiz, Drop, AimAt.Z - Muzzle.Z);
	LayPitch = FMath::Clamp(Elev, R.cannon_elevation_min_deg, R.cannon_elevation_max_deg);
	// One mesh for barrel, carriage and wheels. Pitching it lifts the wheels, so the actor stays
	// level and LayPitch is only the angle the shot is aimed above the mark.
	if (!bLoggedLay)
	{
		bLoggedLay = true;
		UE_LOG(LogTemp, Display, TEXT("PA250 gun: one-piece mesh stays level (actor pitch %.1f); elevation %.1f deg is ballistic compensation onto the aim mark"),
			GetActorRotation().Pitch, LayPitch);
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
	if (bLoaded)
	{
		return FString::Printf(TEXT("%s\ncrew %d of %d  ·  look lays  ·  fire  ·  [ ] load"),
			*Chip, CrewNow, R.cannon_crew_men);
	}
	return FString::Printf(TEXT("%s\n%s %d%%  ·  crew %d of %d  ·  R on the mark"),
		*Chip, PA250Gunnery::DrillWords(Drill.GetPhase()), FMath::RoundToInt(Drill.GetPhaseProgress() * 100.f),
		CrewNow, R.cannon_crew_men);
}

void APA250FieldGun::ChangeHands(FName NewFaction)
{
	const FName Was = Faction;
	Faction = NewFaction;
	for (TWeakObjectPtr<ARFCharacter>& P : Posts) P = nullptr;
	SetPlayerGunner(nullptr);
	OverrunSeconds = 0.f;
	UE_LOG(LogTemp, Display, TEXT("PA250 gun: taken by %s from %s (%s)"), *NewFaction.ToString(), *Was.ToString(), NewFaction == OriginalFaction ? TEXT("retaken") : TEXT("captured"));
	if (APA250BattleGameMode* GM = BattleOf(this)) GM->NotifyGunTaken(this, Was);
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
	FVector Dir = (AimPoint - Muzzle).GetSafeNormal();
	if (Dir.IsNearlyZero()) Dir = GetActorForwardVector();
	bLoaded = false;
	Drill.Reset();
	++ShotsFired;
	// Smoke, flash and report: a gun makes ten muskets' worth.
	if (UPA250SmokeSubsystem* Bank = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>())
	{
		for (int32 i = 0; i < 10; ++i)
		{
			Bank->AddShot(Muzzle + Dir * (40.f * i), FMath::VRandCone(Dir, FMath::DegreesToRadians(10.f)));
		}
	}
	FPA250BattleAudio::PlayAtOr(this, TEXT("cannon_fire"), TEXT("musket_distant"), Muzzle, EPA250SoundRange::Gunfire, 1.f, 0.04f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (Shot == EPA250Shot::RoundShot)
	{
		const float Speed = R.cannon_round_speed_m * 100.f;
		FVector Laid = (AimPoint + FVector(0.f, 0.f, PA250Gunnery::RoundShotDrop(Range, Speed)) - Muzzle).GetSafeNormal();
		if (!Laid.IsNearlyZero() && R.cannon_round_spread_degrees > 0.f) Laid = FMath::VRandCone(Laid, FMath::DegreesToRadians(R.cannon_round_spread_degrees));
		if (APA250CannonBall* Ball = GetWorld()->SpawnActor<APA250CannonBall>(Muzzle, Laid.Rotation(), Params))
		{
			Ball->Launch((Laid.IsNearlyZero() ? Dir : Laid) * Speed, Faction, Gunner, this);
		}
	}
	else if (Shot == EPA250Shot::Shell)
	{
		const float Speed = R.cannon_shell_speed_m * 100.f;
		const float Fuse = FMath::Clamp(Range, R.cannon_shell_fuse_min_m * 100.f, R.cannon_shell_range_m * 100.f);
		FVector Laid = (AimPoint + FVector(0.f, 0.f, PA250Gunnery::RoundShotDrop(Range, Speed)) - Muzzle).GetSafeNormal();
		if (!Laid.IsNearlyZero() && R.cannon_shell_spread_degrees > 0.f) Laid = FMath::VRandCone(Laid, FMath::DegreesToRadians(R.cannon_shell_spread_degrees));
		if (APA250CannonShell* Ball = GetWorld()->SpawnActor<APA250CannonShell>(Muzzle, Laid.Rotation(), Params))
		{
			Ball->Launch((Laid.IsNearlyZero() ? Dir : Laid) * Speed, Fuse, Faction, Gunner, this);
		}
	}
	else
	{
		// Grape: a tin of musket balls (projectiles, not hit-scan).
		for (int32 i = 0; i < FMath::Max(1, R.cannon_grape_balls); ++i)
		{
			const FVector D = FMath::VRandCone(Dir, FMath::DegreesToRadians(FMath::Max(0.5f, R.cannon_grape_cone_degrees)));
			const FTransform At(D.Rotation(), Muzzle + D * 30.f);
			AMusketProjectile* Ball = GetWorld()->SpawnActorDeferred<AMusketProjectile>(AMusketProjectile::StaticClass(), At, this, Gunner,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Ball) continue;
			if (Gunner && Gunner->Musket)
			{
				Ball->WeaponTuning = Gunner->Musket->WeaponTuning;
				if (Ball->WeaponTuning) Ball->Damage = Ball->WeaponTuning->Values.TorsoDamageNear;
			}
			Ball->FinishSpawning(At);
			Ball->SpawnLocation = Muzzle;
			if (UProjectileMovementComponent* Move = Ball->GetProjectileMovement())
			{
				const float Speed = (Gunner && Gunner->Musket) ? Gunner->Musket->GetProjectileSpeed() : 20000.f;
				Move->Velocity = D * Speed;
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 gun: %s gun fires %s at %.0f m (crew %d, %d gunners)"), *Faction.ToString(),
		PA250Gunnery::ShotWords(Shot), Range / 100.f, CrewNow, TrainedNow);
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
	return (!bLoaded && Drill.IsReloading()) ? Drill.TryCommit() : EActiveReloadHit::None;
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
	// The drill: as fast as the men at the gun can make it.
	const float Speed = PA250Gunnery::DrillSpeed(CrewNow, TrainedNow, R);
	if (!bLoaded && Speed > 0.f)
	{
		if (!Drill.IsReloading() && !Drill.IsComplete()) Drill.BeginReload();
		Drill.Tick(DeltaSeconds * Speed);
		if (Drill.IsComplete())
		{
			bLoaded = true;
			Drill.Reset();
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
		const float Step = FMath::Clamp(FMath::FindDeltaAngleDegrees(Now, Want), -R.cannon_traverse_deg_per_second * DeltaSeconds, R.cannon_traverse_deg_per_second * DeltaSeconds);
		// One-piece mesh: yaw on the actor. Elevation is LayTo, used as aim compensation, not actor pitch.
		SetActorRotation(FRotator(0.f, Now + Step, 0.f));
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
	const pa250::BattleRules& R = pa250::battle_rules();
	UWorld* World = GetWorld();
	if (!World || Velocity.IsNearlyZero()) return;
	APA250BattleGameMode* GM = BattleOf(this);
	Velocity.Z -= 490.f * DeltaSeconds; // it drops a little: a flat, grazing shot
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;
	Travelled += FVector::Dist(From, To);
	// Men beside its path are knocked off their feet; the squads it ploughs through take a morale check.
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		ARFCharacter* Man = *It;
		if (Man->IsDead() || Man->FactionId == Faction || Man->FactionId.IsNone() || Struck.Contains(Man)) continue;
		// The ball against the man's body (a standing man from knee to head), this step of its flight.
		const FVector Body = Man->GetActorLocation();
		FVector OnPath, OnBody;
		FMath::SegmentDistToSegment(From, To, Body - FVector(0.f, 0.f, 60.f), Body + FVector(0.f, 0.f, 80.f), OnPath, OnBody);
		const float Off = FVector::Dist(OnPath, OnBody);
		if (Off > R.cannon_knockdown_radius_cm) continue;
		const bool bHit = Off <= 45.f && MenLeft > 0;
		// A man it only passes is knocked down once; it may still go through him a step later.
		if (!bHit && KnockedDown.Contains(Man)) continue;
		if (bHit)
		{
			Struck.Add(Man);
			--MenLeft;
			FHitResult Hit;
			Hit.ImpactPoint = Man->GetActorLocation();
			Hit.Location = Hit.ImpactPoint;
			UGameplayStatics::ApplyPointDamage(Man, R.cannon_round_damage, Velocity.GetSafeNormal(), Hit, Gunner.IsValid() ? Gunner->GetController() : nullptr, this, nullptr);
			if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>()) Bank->AddImpact(Hit.ImpactPoint, -Velocity.GetSafeNormal(), true);
		}
		else
		{
			// Knocked down, not hit: thrown off his feet away from the shot's path, his aim gone for a while.
			KnockedDown.Add(Man);
			FVector Away = (Body - OnPath).GetSafeNormal2D();
			if (Away.IsNearlyZero()) Away = FVector::CrossProduct(Velocity.GetSafeNormal2D(), FVector::UpVector);
			if (!Man->KnockDown(Away * R.cannon_knockdown_push_cm)) Man->LockMovement(R.cannon_knockdown_seconds);
			Man->ApplySuppression(R.cannon_knockdown_seconds * 2.f, 6.f);
		}
		if (GM) GM->NotifyRoundShot(Man, bHit);
		if (GM && GM->GetVolleys() && Man->SquadId >= 0 && !SquadsShaken.Contains(Man->SquadId))
		{
			SquadsShaken.Add(Man->SquadId);
			GM->GetVolleys()->PressSquad(Man->SquadId, R.cannon_pressure, R.cannon_pressure_seconds, Faction, TEXT("CannonShock"));
		}
	}
	// What it strikes: a field work is battered; the ground or a wall stops it in a burst of dirt.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250RoundShot), false, this);
	if (Gun.IsValid()) Params.AddIgnoredActor(Gun.Get());
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
	{
		if (APA250FieldWork* Work = Cast<APA250FieldWork>(Hit.GetActor()))
		{
			const bool bBroken = Work->Batter(R.cannon_works_damage);
			if (GM) GM->NotifyWorkBattered(Work, bBroken, Faction);
		}
		if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
		{
			Bank->AddDirtBurst(Hit.ImpactPoint, FVector(Hit.ImpactNormal), R.cannon_dirt_burst_scale);
		}
		FPA250BattleAudio::PlayAt(this, TEXT("impact_ground"), Hit.ImpactPoint, EPA250SoundRange::Gunfire, 1.f, 0.1f);
		Destroy();
		return;
	}
	if (MenLeft <= 0 || Travelled > R.cannon_round_range_m * 100.f * 1.3f)
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
	const pa250::BattleRules& R = pa250::battle_rules();
	UWorld* World = GetWorld();
	if (!World) { Destroy(); return; }
	APA250BattleGameMode* GM = BattleOf(this);
	if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
	{
		Bank->AddDirtBurst(At, FVector::UpVector, R.cannon_dirt_burst_scale * 1.5f);
		for (int32 i = 0; i < 6; ++i)
		{
			Bank->AddShot(At, FMath::VRandCone(FVector::UpVector, FMath::DegreesToRadians(50.f)));
		}
	}
	FPA250BattleAudio::PlayAtOr(this, TEXT("cannon_fire"), TEXT("musket_distant"), At, EPA250SoundRange::Gunfire, 0.85f, 0.05f);
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		ARFCharacter* Man = *It;
		if (!Man || Man->IsDead() || Man->FactionId == Faction || Man->FactionId.IsNone()) continue;
		const float D = FVector::Dist(Man->GetActorLocation(), At);
		if (D > R.cannon_shell_blast_radius_cm) continue;
		const float Falloff = 1.f - (D / FMath::Max(1.f, R.cannon_shell_blast_radius_cm));
		FHitResult Hit;
		Hit.ImpactPoint = Man->GetActorLocation();
		Hit.Location = Hit.ImpactPoint;
		UGameplayStatics::ApplyPointDamage(Man, R.cannon_shell_damage * Falloff, (Man->GetActorLocation() - At).GetSafeNormal(), Hit,
			Gunner.IsValid() ? Gunner->GetController() : nullptr, this, nullptr);
		FVector Away = (Man->GetActorLocation() - At).GetSafeNormal2D();
		if (Away.IsNearlyZero()) Away = FVector::ForwardVector;
		if (!Man->KnockDown(Away * R.cannon_knockdown_push_cm * Falloff)) Man->LockMovement(R.cannon_knockdown_seconds);
		Man->ApplySuppression(R.cannon_knockdown_seconds * 2.f, 6.f);
		if (GM && GM->GetVolleys() && Man->SquadId >= 0)
		{
			GM->GetVolleys()->PressSquad(Man->SquadId, R.cannon_pressure * Falloff, R.cannon_pressure_seconds, Faction, TEXT("ShellBurst"));
		}
	}
	for (TActorIterator<APA250FieldWork> It(World); It; ++It)
	{
		APA250FieldWork* Work = *It;
		if (!Work || FVector::Dist(Work->GetActorLocation(), At) > R.cannon_shell_blast_radius_cm) continue;
		const bool bBroken = Work->Batter(R.cannon_shell_works_damage);
		if (GM) GM->NotifyWorkBattered(Work, bBroken, Faction);
	}
	Destroy();
}

void APA250CannonShell::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || Velocity.IsNearlyZero()) return;
	Velocity.Z -= 490.f * DeltaSeconds;
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;
	Travelled += FVector::Dist(From, To);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Shell), false, this);
	if (Gun.IsValid()) Params.AddIgnoredActor(Gun.Get());
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
	{
		Burst(Hit.ImpactPoint);
		return;
	}
	if (Travelled >= FuseCm)
	{
		Burst(To);
		return;
	}
	const pa250::BattleRules& R = pa250::battle_rules();
	if (Travelled > R.cannon_shell_range_m * 100.f * 1.4f)
	{
		Destroy();
		return;
	}
	SetActorLocation(To);
}
