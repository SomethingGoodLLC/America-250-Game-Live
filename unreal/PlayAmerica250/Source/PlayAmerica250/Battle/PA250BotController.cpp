#include "PA250BotController.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MusketWeapon.h"
#include "PA250ControlPoint.h"
#include "RFCharacter.h"
#include "Navigation/PathFollowingComponent.h"

APA250BotController::APA250BotController()
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = false;
}

ARFCharacter* APA250BotController::FindNearestEnemy(const ARFCharacter* Me, float& OutDist) const
{
	ARFCharacter* Best = nullptr;
	OutDist = SightRange;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		ARFCharacter* Other = *It;
		if (Other == Me || Other->IsDead() || Other->FactionId == Me->FactionId || Other->FactionId.IsNone())
		{
			continue;
		}
		const float D = FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation());
		if (D < OutDist)
		{
			OutDist = D;
			Best = Other;
		}
	}
	return Best;
}

bool APA250BotController::HasLineOfSight(const ARFCharacter* Me, const ARFCharacter* Enemy) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotLOS), false, Me);
	Params.AddIgnoredActor(Enemy);
	if (Me->Musket)
	{
		Params.AddIgnoredActor(Me->Musket);
	}
	const FVector From = Me->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	const FVector To = Enemy->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	return !GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

void APA250BotController::MoveToward(const ARFCharacter* Me, const FVector& Dest, float Acceptance)
{
	if (FVector::Dist2D(Me->GetActorLocation(), Dest) <= Acceptance)
	{
		StopMovement();
		return;
	}
	const EPathFollowingRequestResult::Type R = MoveToLocation(Dest, Acceptance, true, true, true, true);
	if (R == EPathFollowingRequestResult::Failed)
	{
		// No navmesh (or unreachable): steer straight; the Green is open ground.
		const FVector Dir = (Dest - Me->GetActorLocation()).GetSafeNormal2D();
		const_cast<ARFCharacter*>(Me)->AddMovementInput(Dir, 1.f);
	}
}

void APA250BotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ARFCharacter* Me = Cast<ARFCharacter>(GetPawn());
	if (!Me || Me->IsDead())
	{
		return;
	}
	ThinkTimer -= DeltaSeconds;
	if (ThinkTimer > 0.f)
	{
		return;
	}
	ThinkTimer = 0.25f + FMath::FRand() * 0.15f;

	float Dist = 0.f;
	ARFCharacter* Enemy = FindNearestEnemy(Me, Dist);
	const bool bLoaded = Me->Musket && Me->Musket->CanFire();
	const FVector ObjLoc = Objective.IsValid() ? Objective->GetActorLocation() : Me->GetActorLocation();
	const bool bAtObjective = Objective.IsValid() && FVector::Dist2D(Me->GetActorLocation(), ObjLoc) < Objective->Radius * 0.7f;

	if (Enemy && Dist <= EngageRange && HasLineOfSight(Me, Enemy))
	{
		if (bLoaded)
		{
			State = EBotState::Engage;
			StopMovement();
			SetFocus(Enemy);
			// Aim at the torso with an error cone; compensate muzzle travel time against a moving target.
			const float Speed = Me->Musket->GetTuning().ProjectileSpeed;
			const FVector Lead = Enemy->GetVelocity() * (Dist / FMath::Max(Speed, 1.f));
			const FVector Aim = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f) + Lead;
			const FVector Muzzle = Me->Musket->GetMuzzleLocation()->GetComponentLocation();
			const FVector Dir = FMath::VRandCone((Aim - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErrorDegrees));
			Me->FireMusketAt(Muzzle + Dir * Dist);
			return;
		}
		// Reloading with an enemy close: hold ground (stay on the objective if already there).
		State = EBotState::Reload;
		SetFocus(Enemy);
		if (!bAtObjective)
		{
			MoveToward(Me, ObjLoc, 300.f);
		}
		return;
	}

	ClearFocus(EAIFocusPriority::Gameplay);
	if (bAtObjective)
	{
		State = EBotState::HoldObjective;
		StopMovement();
		return;
	}
	State = bLoaded ? EBotState::Advance : EBotState::Reload;
	MoveToward(Me, ObjLoc + FVector(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f), 300.f);
}
