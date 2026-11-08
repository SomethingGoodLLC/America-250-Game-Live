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

void APA250BotController::SetObjective(APA250ControlPoint* InObjective)
{
	Objective = InObjective;
	if (InObjective)
	{
		Posts.Reset();
		Posts.Add(InObjective);
	}
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
		const FVector Dir = (Dest - Me->GetActorLocation()).GetSafeNormal2D();
		const_cast<ARFCharacter*>(Me)->AddMovementInput(Dir, 1.f);
	}
}

APA250ControlPoint* APA250BotController::PickGoal(const ARFCharacter* Me) const
{
	APA250ControlPoint* Defend = nullptr;
	float DefendDist = TNumericLimits<float>::Max();
	APA250ControlPoint* Attack = nullptr;
	float AttackDist = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<APA250ControlPoint>& WP : Posts)
	{
		APA250ControlPoint* P = WP.Get();
		if (!P) continue;
		const float D = FVector::Dist2D(Me->GetActorLocation(), P->GetActorLocation());
		if (P->GetHolder() == Me->FactionId)
		{
			const FName Enemy = Me->FactionId == TEXT("british") ? FName(TEXT("continental")) : FName(TEXT("british"));
			const bool UnderAttack = P->GetPresence(Enemy) > 0 || (P->GetCapturer() == Enemy && P->GetCaptureProgress() > 0.f);
			if (UnderAttack && D < DefendDist)
			{
				DefendDist = D;
				Defend = P;
			}
		}
		else if (D < AttackDist)
		{
			AttackDist = D;
			Attack = P;
		}
	}
	return Defend ? Defend : Attack;
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

	if (Me->Musket && Me->Musket->IsReloading())
	{
		FRandomStream Rng(FMath::Rand());
		Me->Musket->TickBotReload(ReloadSkill, Rng);
		if (Me->Musket->IsJammed())
		{
			Me->Musket->SetReloadHeld(true);
		}
	}

	float Dist = 0.f;
	ARFCharacter* Enemy = FindNearestEnemy(Me, Dist);
	const bool bLoaded = Me->Musket && Me->Musket->CanFire();
	APA250ControlPoint* Goal = PickGoal(Me);
	if (!Goal && Objective.IsValid())
	{
		Goal = Objective.Get();
	}
	const FVector ObjLoc = Goal ? Goal->GetActorLocation() : Me->GetActorLocation();
	const float GoalR = Goal ? Goal->Radius : 1200.f;
	const bool bAtObjective = Goal && FVector::Dist2D(Me->GetActorLocation(), ObjLoc) < GoalR * 0.7f;
	const bool bDefending = Goal && Goal->GetHolder() == Me->FactionId;

	if (Enemy && Dist <= EngageRange && HasLineOfSight(Me, Enemy))
	{
		if (bLoaded)
		{
			State = EBotState::Engage;
			StopMovement();
			SetFocus(Enemy);
			const float Speed = Me->Musket->GetTuning().ProjectileSpeed;
			const FVector Lead = Enemy->GetVelocity() * (Dist / FMath::Max(Speed, 1.f));
			const FVector Aim = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f) + Lead;
			const FVector Muzzle = Me->Musket->GetMuzzleLocation()->GetComponentLocation();
			const FVector Dir = FMath::VRandCone((Aim - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErrorDegrees));
			Me->FireMusketAt(Muzzle + Dir * Dist);
			return;
		}
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
		State = bDefending ? EBotState::Defend : EBotState::HoldObjective;
		StopMovement();
		return;
	}
	State = bLoaded ? EBotState::Advance : EBotState::Reload;
	MoveToward(Me, ObjLoc + FVector(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f), 300.f);
}
