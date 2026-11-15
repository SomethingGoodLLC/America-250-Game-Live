// PA250ClassPortraits.cpp — -PA250ClassPortraits: photographs one soldier of every class in a real
// window, as the battle draws him, to Saved/PA250/portraits/<faction>_<class>.png (no HUD). The class
// picker's card images are cut from these stills (tools/art/build_class_portraits.py), so a card always
// shows what that class looks like on the field. Run with -PA250Bots=0 so nobody walks through the shot.
#include "PA250BattleGameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "MusketWeapon.h"
#include "PA250ControlPoint.h"
#include "PA250FieldGun.h"
#include "PA250RowBoat.h"
#include "PA250UnitClasses.h"
#include "RFCharacter.h"
#include "TimerManager.h"
#include "UnrealClient.h"

void APA250BattleGameMode::MaybeStartClassPortraits()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250ClassPortraits")) || !GetWorld())
	{
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	struct FSubject { FName Faction, Class; };
	static const FSubject Subjects[] = {
		{TEXT("british"), TEXT("line_infantry")}, {TEXT("british"), TEXT("grenadier")}, {TEXT("british"), TEXT("light_infantry")},
		{TEXT("british"), TEXT("officer")}, {TEXT("continental"), TEXT("line_infantry")}, {TEXT("continental"), TEXT("militia")},
		{TEXT("continental"), TEXT("rifleman")}, {TEXT("continental"), TEXT("officer")},
		// The battle pass's classes (2026-11-14).
		{TEXT("british"), TEXT("sapper")},
		{TEXT("british"), TEXT("ensign")},
		{TEXT("british"), TEXT("musician")},
		{TEXT("british"), TEXT("surgeon")},
		{TEXT("british"), TEXT("artilleryman")},
		{TEXT("continental"), TEXT("sapper")},
		{TEXT("continental"), TEXT("ensign")},
		{TEXT("continental"), TEXT("musician")},
		{TEXT("continental"), TEXT("surgeon")},
		{TEXT("continental"), TEXT("artilleryman")}};
	// -PA250PortraitClasses=sapper,ensign shoots only those classes (both sides), so the other cards
	// are not shot again.
	TArray<FSubject> Wanted;
	{
		FString Only;
		TArray<FString> Names;
		if (FParse::Value(FCommandLine::Get(), TEXT("PA250PortraitClasses="), Only, /*bShouldStopOnSeparator=*/false)) Only.ParseIntoArray(Names, TEXT(","));
		for (const FSubject& S : Subjects)
		{
			if (Names.Num() == 0 || Names.Contains(S.Class.ToString())) Wanted.Add(S);
		}
	}

	// The stage: open ground near the first post. The camera looks the way the sunlight travels, so the
	// man who faces it is lit from the front.
	FVector Stage = Posts.Num() > 0 && Posts[0].IsValid() ? Posts[0]->GetActorLocation() + FVector(900.f, 600.f, 0.f) : FVector(0.f, 0.f, 200.f);
	FHitResult Ground;
	if (GetWorld()->LineTraceSingleByChannel(Ground, Stage + FVector(0.f, 0.f, 3000.f), Stage - FVector(0.f, 0.f, 6000.f), ECC_WorldStatic))
	{
		Stage = Ground.ImpactPoint;
	}
	FVector Sun(1.f, 0.3f, 0.f);
	if (TActorIterator<ADirectionalLight> It(GetWorld()); It)
	{
		Sun = It->GetActorForwardVector();
	}
	Sun.Z = 0.f;
	Sun = Sun.GetSafeNormal(UE_SMALL_NUMBER, FVector(1.f, 0.f, 0.f));

	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Stage - Sun * 560.f + FVector(0.f, 0.f, 105.f), Sun.Rotation());
	if (!Cam)
	{
		return;
	}
	Cam->GetCameraComponent()->SetFieldOfView(40.f);
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (APawn* Me = PC->GetPawn())
	{
		Me->SetActorHiddenInGame(true);
		if (const ARFCharacter* C = Cast<ARFCharacter>(Me); C && C->Musket) C->Musket->SetActorHiddenInGame(true);
		Me->SetCanBeDamaged(false);
	}
	PC->SetViewTarget(Cam);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("portraits")), true);

	const float First = 3.f, Each = 3.f;
	TSharedRef<TWeakObjectPtr<ARFCharacter>> Model = MakeShared<TWeakObjectPtr<ARFCharacter>>();
	for (int32 i = 0; i < Wanted.Num(); ++i)
	{
		const FSubject Subject = Wanted[i];
		FTimerHandle Pose, Shot;
		GetWorldTimerManager().SetTimer(Pose, FTimerDelegate::CreateWeakLambda(this, [this, Model, Subject, Stage, Sun]() {
			if (ARFCharacter* Old = Model->Get())
			{
				if (Old->Musket) Old->Musket->Destroy();
				Old->Destroy();
			}
			// Three-quarter view: he faces the camera, turned a little to his left.
			const FRotator Facing((-Sun).Rotation() + FRotator(0.f, 28.f, 0.f));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ARFCharacter* C = GetWorld()->SpawnActor<ARFCharacter>(DefaultPawnClass, Stage + FVector(0.f, 0.f, 95.f), Facing, Params);
			if (!C) return;
			C->SetClothingSeed(HashCombine(GetTypeHash(Subject.Class), 0x5EEDu));
			ConfigureSoldier(C, Subject.Faction, Subject.Class);
			C->SetActorLocationAndRotation(Stage + FVector(0.f, 0.f, 95.f), Facing, false, nullptr, ETeleportType::TeleportPhysics);
			C->SetCanBeDamaged(false);
			*Model = C;
		}), First + Each * i, false);
		GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [Subject]() {
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("portraits"),
				FString::Printf(TEXT("%s_%s.png"), *Subject.Faction.ToString(), *Subject.Class.ToString())));
			UE_LOG(LogTemp, Display, TEXT("PA250 portrait: %s %s (%s) -> %s"), *Subject.Faction.ToString(), *Subject.Class.ToString(),
				PA250UnitClasses::KitFor(Subject.Faction, Subject.Class).Label, *Path);
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		}), First + Each * i + 2.2f, false);
	}
	FTimerHandle Done;
	GetWorldTimerManager().SetTimer(Done, FTimerDelegate::CreateWeakLambda(this, []() {
		UE_LOG(LogTemp, Display, TEXT("PA250 portraits done"));
	}), First + Each * Wanted.Num() + 1.f, false);
	UE_LOG(LogTemp, Display, TEXT("PA250 portraits: %d classes, stage %s"), Wanted.Num(), *Stage.ToCompactString());
}

// -PA250GunStill: the first field gun on the field with one soldier standing by its muzzle, from the
// side and from three-quarters front, to Saved/PA250/gun_still_side.png and gun_still_front.png (no
// HUD). Run with -PA250Bots=0 -PA250Guns=continental:1 so the gun stands alone. First still at 12 s.
void APA250BattleGameMode::MaybeStartGunStill()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250GunStill")) || !GetWorld())
	{
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APA250FieldGun* Gun = Guns.Num() > 0 ? Guns[0].Get() : nullptr;
	if (!PC || !Gun)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 gun still: no gun on the field (run with -PA250Guns=continental:1)"));
		return;
	}
	const FVector At = Gun->GetActorLocation();
	const FVector Fwd = Gun->GetActorForwardVector();
	FVector Right = Gun->GetActorRightVector();
	FVector Sun(1.f, 0.3f, 0.f);
	if (TActorIterator<ADirectionalLight> It(GetWorld()); It)
	{
		Sun = It->GetActorForwardVector();
	}
	// The camera stands on the sunlit side.
	if (FVector::DotProduct(-Right, Sun) < 0.f) Right = -Right;
	if (APawn* Me = PC->GetPawn())
	{
		Me->SetActorHiddenInGame(true);
		if (const ARFCharacter* C = Cast<ARFCharacter>(Me); C && C->Musket) C->Musket->SetActorHiddenInGame(true);
		Me->SetCanBeDamaged(false);
	}
	// A soldier of the gun's side a pace in front of the muzzle, facing the camera side.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Stand = At + Fwd * 250.f + FVector(0.f, 0.f, 95.f);
	if (ARFCharacter* Man = GetWorld()->SpawnActor<ARFCharacter>(DefaultPawnClass, Stand, Right.Rotation(), Params))
	{
		Man->SetClothingSeed(0x6A11u);
		ConfigureSoldier(Man, Gun->GetFaction(), TEXT("line_infantry"));
		Man->SetCanBeDamaged(false);
	}
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(At, FRotator::ZeroRotator);
	if (!Cam) return;
	Cam->GetCameraComponent()->SetFieldOfView(50.f);
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	PC->SetViewTarget(Cam);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250")), true);
	struct FView { const TCHAR* Name; FVector From; FVector LookAt; float Seconds; };
	const FView Views[] = {
		{TEXT("side"), At + Fwd * 45.f + Right * 640.f + FVector(0.f, 0.f, 135.f), At + Fwd * 45.f + FVector(0.f, 0.f, 90.f), 12.f},
		{TEXT("front"), At + Fwd * 560.f + Right * 380.f + FVector(0.f, 0.f, 170.f), At + Fwd * 60.f + FVector(0.f, 0.f, 85.f), 15.f}};
	for (const FView& V : Views)
	{
		const FString Name = V.Name;
		const FVector From = V.From, LookAt = V.LookAt;
		FTimerHandle Pose, Shot;
		GetWorldTimerManager().SetTimer(Pose, FTimerDelegate::CreateWeakLambda(this, [Cam, From, LookAt]() {
			Cam->SetActorLocationAndRotation(From, (LookAt - From).Rotation());
		}), V.Seconds - 1.5f, false);
		GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(this, [Name]() {
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), FString::Printf(TEXT("gun_still_%s.png"), *Name)));
			UE_LOG(LogTemp, Display, TEXT("PA250 gun still: %s -> %s"), *Name, *Path);
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		}), V.Seconds, false);
	}
	FTimerHandle Done;
	GetWorldTimerManager().SetTimer(Done, FTimerDelegate::CreateWeakLambda(this, []() {
		UE_LOG(LogTemp, Display, TEXT("PA250 gun still done"));
		FPlatformMisc::RequestExit(false);
	}), 18.f, false);
}

// -PA250LandingStill: a landing seen from the shore. Run on L_Harbor with -PA250Mode=landing
// -PA250Attacker=british. Stills (no HUD) to Saved/PA250/landing_boats.png at 12 s (the boats on the
// way in) and landing_ashore.png three seconds after the first boat is in.
void APA250BattleGameMode::MaybeStartLandingStill()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250LandingStill")) || !GetWorld())
	{
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || Boats.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 landing still: no boats (run with -PA250Mode=landing)"));
		return;
	}
	// The middle of the landing places and the way inland, from the attacker's start and the boats.
	FVector Sea = FVector::ZeroVector;
	for (const APA250RowBoat* Boat : Boats) Sea += Boat ? Boat->GetActorLocation() / Boats.Num() : FVector::ZeroVector;
	const FVector Rear = GetRearPoint(FName(UTF8_TO_TCHAR(Setup.attacker_faction.c_str())), Sea);
	const FVector Inland = (Rear - Sea).GetSafeNormal2D();
	const FVector Along(-Inland.Y, Inland.X, 0.f);
	const FVector Shore = Rear - Inland * 900.f;
	if (APawn* Me = PC->GetPawn())
	{
		Me->SetActorHiddenInGame(true);
		if (const ARFCharacter* C = Cast<ARFCharacter>(Me); C && C->Musket) C->Musket->SetActorHiddenInGame(true);
		Me->SetCanBeDamaged(false);
	}
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Shore, FRotator::ZeroRotator);
	if (!Cam) return;
	Cam->GetCameraComponent()->SetFieldOfView(60.f);
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	PC->SetViewTarget(Cam);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250")), true);
	auto Shoot = [](const TCHAR* Name) {
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), FString::Printf(TEXT("landing_%s.png"), Name)));
		UE_LOG(LogTemp, Display, TEXT("PA250 landing still: %s -> %s"), Name, *Path);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	};
	// The boats on the way in, from the water's edge to one side of them.
	FTimerHandle Pose1, Shot1, Poll;
	GetWorldTimerManager().SetTimer(Pose1, FTimerDelegate::CreateWeakLambda(this, [this, Cam, Shore, Along]() {
		FVector Mid = FVector::ZeroVector;
		for (const APA250RowBoat* Boat : Boats) Mid += Boat ? Boat->GetActorLocation() / Boats.Num() : FVector::ZeroVector;
		const FVector From = Shore + Along * 2600.f + FVector(0.f, 0.f, 260.f);
		Cam->SetActorLocationAndRotation(From, (Mid + FVector(0.f, 0.f, 80.f) - From).Rotation());
	}), 10.5f, false);
	GetWorldTimerManager().SetTimer(Shot1, FTimerDelegate::CreateWeakLambda(this, [Shoot]() { Shoot(TEXT("boats")); }), 12.f, false);
	// The men ashore: three seconds after the first boat is in, from above the beach.
	TSharedRef<bool> bDone = MakeShared<bool>(false);
	GetWorldTimerManager().SetTimer(Poll, FTimerDelegate::CreateWeakLambda(this, [this, Cam, Shore, Inland, Along, Shoot, bDone]() {
		if (*bDone || FirstBoatInAt < 0.f || GetWorld()->GetTimeSeconds() - StartTime < FirstBoatInAt + 1.5f) return;
		*bDone = true;
		const FVector From = Shore + Inland * 1500.f + Along * 1700.f + FVector(0.f, 0.f, 420.f);
		Cam->SetActorLocationAndRotation(From, (Shore + Inland * 250.f + FVector(0.f, 0.f, 60.f) - From).Rotation());
		FTimerHandle Shot2, Done;
		GetWorldTimerManager().SetTimer(Shot2, FTimerDelegate::CreateWeakLambda(this, [Shoot]() { Shoot(TEXT("ashore")); }), 1.5f, false);
		GetWorldTimerManager().SetTimer(Done, FTimerDelegate::CreateWeakLambda(this, []() {
			UE_LOG(LogTemp, Display, TEXT("PA250 landing still done"));
			FPlatformMisc::RequestExit(false);
		}), 4.f, false);
	}), 0.5f, true);
}
