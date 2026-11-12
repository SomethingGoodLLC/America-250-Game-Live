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
		{TEXT("continental"), TEXT("rifleman")}, {TEXT("continental"), TEXT("officer")}};

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
	for (int32 i = 0; i < UE_ARRAY_COUNT(Subjects); ++i)
	{
		const FSubject Subject = Subjects[i];
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
	}), First + Each * UE_ARRAY_COUNT(Subjects) + 1.f, false);
	UE_LOG(LogTemp, Display, TEXT("PA250 portraits: %d classes, stage %s"), static_cast<int32>(UE_ARRAY_COUNT(Subjects)), *Stage.ToCompactString());
}
