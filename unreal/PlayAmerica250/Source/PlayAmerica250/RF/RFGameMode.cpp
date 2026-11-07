#include "RFGameMode.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerStart.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"
#include "TimerManager.h"

ARFGameMode::ARFGameMode()
{
	DefaultPawnClass = ARFCharacter::StaticClass();
}

void ARFGameMode::RegisterCharacter(ARFCharacter* Character)
{
	if (Character && Character->HealthComponent)
	{
		Character->HealthComponent->OnDied.AddUniqueDynamic(this, &ARFGameMode::HandleCharacterDied);
	}
}

void ARFGameMode::HandleCharacterDied(AActor* Victim)
{
	ARFCharacter* Char = Cast<ARFCharacter>(Victim);
	if (!Char)
	{
		return;
	}
	AController* Killer = Char->HealthComponent ? Char->HealthComponent->LastInstigator.Get() : nullptr;
	OnCharacterDied(Char, Char->GetController(), Killer);
}

void ARFGameMode::OnCharacterDied(ARFCharacter* Victim, AController* VictimController, AController* Killer)
{
	if (VictimController)
	{
		ScheduleRespawn(VictimController, RespawnDelay);
	}
}

void ARFGameMode::ScheduleRespawn(AController* Controller, float Delay)
{
	if (!Controller || !GetWorld())
	{
		return;
	}
	FTimerHandle& Handle = RespawnTimers.FindOrAdd(Controller);
	GetWorldTimerManager().ClearTimer(Handle);
	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &ARFGameMode::RespawnController, Controller);
	GetWorldTimerManager().SetTimer(Handle, Delegate, FMath::Max(Delay, 0.01f), false);
}

AActor* ARFGameMode::ChooseRespawnPoint(AController* Controller)
{
	TArray<APlayerStart*> Starts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		Starts.Add(*It);
	}
	if (Starts.Num() == 0)
	{
		return nullptr;
	}
	return Starts[FMath::RandRange(0, Starts.Num() - 1)];
}

TSubclassOf<APawn> ARFGameMode::ChooseRespawnPawnClass(AController* Controller)
{
	return DefaultPawnClass;
}

void ARFGameMode::RespawnController(AController* Controller)
{
	if (!Controller || !GetWorld())
	{
		return;
	}
	RespawnTimers.Remove(Controller);

	if (APawn* Old = Controller->GetPawn())
	{
		Controller->UnPossess();
		Old->SetLifeSpan(0.1f);
	}
	TSubclassOf<APawn> PawnClass = ChooseRespawnPawnClass(Controller);
	if (!PawnClass)
	{
		return;
	}
	const AActor* Point = ChooseRespawnPoint(Controller);
	const FTransform T = Point ? Point->GetActorTransform() : FTransform::Identity;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (APawn* NewPawn = GetWorld()->SpawnActor<APawn>(PawnClass, T, Params))
	{
		Controller->Possess(NewPawn);
		OnControllerRespawned(Controller, NewPawn);
	}
}
