#include "PA250BotController.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MusketWeapon.h"
#include "PA250CombatProfiles.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "PA250BattleGameMode.h"
#include "PA250BotTuning.h"
#include "PA250ControlPoint.h"
#include "PA250MeleeComponent.h"
#include "PA250VolleyCoordinator.h"
#include "RFCharacter.h"
#include "Navigation/PathFollowingComponent.h"

APA250BotController::APA250BotController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = false;
}

void APA250BotController::ConfigureCrowd(UPA250BotTuning* Tuning)
{
	BotTuning = Tuning;
	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		const float Sep = Tuning ? Tuning->Values.SeparationWeight : 1.5f;
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(Sep);
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good);
		Crowd->SetCrowdObstacleAvoidance(true);
	}
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

void APA250BotController::AssignSlot(APA250ControlPoint* Post, int32 SlotIndex, int32 InSquadId, int32 InFileIndex)
{
	if (ClaimedPost.IsValid())
	{
		ClaimedPost->ReleaseSlot(GetPawn());
	}
	// Claim can fail if the slot was taken between the search and the call. Don't record a slot we don't own.
	ClaimedPost = nullptr;
	ClaimedSlot = INDEX_NONE;
	SquadId = InSquadId;
	FileIndex = InFileIndex;
	if (Post && SlotIndex != INDEX_NONE && GetPawn() && Post->ClaimSlot(SlotIndex, GetPawn()))
	{
		ClaimedPost = Post;
		ClaimedSlot = SlotIndex;
	}
	if (ARFCharacter* Me = Cast<ARFCharacter>(GetPawn()))
	{
		Me->SquadId = InSquadId;
	}
}

void APA250BotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ConfigureCrowd(BotTuning);
}

void APA250BotController::OnUnPossess()
{
	if (ClaimedPost.IsValid())
	{
		ClaimedPost->ReleaseSlot(GetPawn());
	}
	ClaimedSlot = INDEX_NONE;
	Super::OnUnPossess();
}

ARFCharacter* APA250BotController::FindNearestEnemy(const ARFCharacter* Me, float& OutDist) const
{
	// Perception: the nearest enemy he can actually SEE (line of sight), not one behind a wall.
	// Nearest first; at most 4 traces per think.
	TArray<TPair<float, ARFCharacter*>> Candidates;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		ARFCharacter* Other = *It;
		if (Other == Me || Other->IsDead() || Other->FactionId == Me->FactionId || Other->FactionId.IsNone()) continue;
		const float D = FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation());
		if (D < SightRange) Candidates.Emplace(D, Other);
	}
	Candidates.Sort([](const TPair<float, ARFCharacter*>& A, const TPair<float, ARFCharacter*>& B) { return A.Key < B.Key; });
	OutDist = SightRange;
	for (int32 i = 0; i < FMath::Min(4, Candidates.Num()); ++i)
	{
		if (HasLineOfSight(Me, Candidates[i].Value))
		{
			OutDist = Candidates[i].Key;
			return Candidates[i].Value;
		}
	}
	return nullptr;
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
	float AttackScore = TNumericLimits<float>::Max();

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
		else
		{
			// Weighted: prefer contested/neutral over distant enemy-held; spread via squad id.
			const float ContestedBias = (P->IsContested() || P->GetHolder().IsNone()) ? 0.65f : 1.f;
			const float SquadBias = 1.f + 0.08f * ((SquadId >= 0 ? SquadId : 0) % 5);
			const float Score = D * ContestedBias * SquadBias;
			if (Score < AttackScore)
			{
				AttackScore = Score;
				Attack = P;
			}
		}
	}
	return Defend ? Defend : Attack;
}

void APA250BotController::EnsureSlot(ARFCharacter* Me, APA250ControlPoint* Goal)
{
	if (!Me) return;
	// A live claim belongs to the squad assignment. Re-picking the nearest post every
	// think tick was releasing that slot and marching the squad back through the centroid.
	if (ClaimedPost.IsValid() && ClaimedSlot != INDEX_NONE
		&& ClaimedPost->GetSlots().IsValidIndex(ClaimedSlot)
		&& ClaimedPost->GetSlots()[ClaimedSlot].Claimant.Get() == Me)
	{
		return;
	}
	ClaimedSlot = INDEX_NONE;
	if (!Goal) return;
	const int32 Sector = UPA250VolleyCoordinator::SectorForSquad(SquadId >= 0 ? SquadId : 0);
	const int32 Slot = Goal->FindFreeSlot(Sector);
	if (Slot != INDEX_NONE)
	{
		AssignSlot(Goal, Slot, SquadId, FileIndex);
	}
}

FVector APA250BotController::DesiredSlotLocation(const ARFCharacter* Me, APA250ControlPoint* Goal) const
{
	if (ClaimedSlot != INDEX_NONE && ClaimedPost.IsValid())
	{
		return ClaimedPost->GetSlotLocation(ClaimedSlot);
	}
	if (!Goal) return Me->GetActorLocation();
	return Goal->GetActorLocation() + LooseSlotOffset;
}

	// Bayonet: close in when an enemy is inside charge range (or reloading within 20 m),
	// thrust once inside bayonet reach, lunge once inside charge distance.
bool APA250BotController::TryBayonet(ARFCharacter* Me, ARFCharacter* Enemy, float Dist, const FPA250BotTuningValues& T)
{
	if (Enemy && Me->MeleeComponent)
	{
		const bool bEnemyReloading = Enemy->Musket && Enemy->Musket->IsReloading();
		if (Dist < T.ChargeRangeCm || (bEnemyReloading && Dist < T.ChargeVsReloadRangeCm))
		{
			const FPA250MeleeTuningValues& MV = Me->MeleeComponent->GetValues();
			State = EBotState::Charge;
			SetFocus(Enemy);
			MoveToward(Me, Enemy->GetActorLocation(), 80.f);
			if (Dist <= MV.ThrustReachCm + MV.ThrustRadiusCm)
			{
				Me->MeleeComponent->DoThrust();
			}
			else if (Dist <= MV.ChargeLungeCm && Me->MeleeComponent->GetChargeCooldownRemaining() <= 0.f
				&& FVector::DotProduct(Me->GetActorForwardVector().GetSafeNormal2D(),
					(Enemy->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D()) > 0.94f) // dash goes where he faces
			{
				Me->MeleeComponent->DoCharge();
			}
			return true;
		}
	}
	return false;
}


void APA250BotController::ApplyCombatProfile(const FPA250TrainingProfile& InTraining, const FPA250TacticalRole& InRole, int32 Seed)
{
	Training = InTraining;
	Role = InRole;
	ReloadParams = InTraining.ToReloadParams();
	ReloadRng.Initialize(Seed);
	bHasCombatProfile = true;
}

float APA250BotController::ComputeStress01(const ARFCharacter* Me) const
{
	// Existing pressure signals only: being under fire (volley suppression) and squad losses.
	float Stress = 0.f;
	if (Me->SuppressionTimer > 0.f)
	{
		Stress += 0.5f + FMath::Min(0.3f, Me->SuppressionSwayDegrees / 10.f);
	}
	const APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr;
	if (const UPA250VolleyCoordinator* V = GM ? GM->GetVolleys() : nullptr)
	{
		if (SquadId >= 0) Stress += 0.5f * (1.f - V->GetSquadStrength(SquadId));
		Stress += V->GetSquadMoraleStress(SquadId);
	}
	return FMath::Clamp(Stress, 0.f, 1.f);
}

void APA250BotController::TickReload(ARFCharacter* Me)
{
	AMusketWeapon* Musket = Me->Musket;
	if (!Musket) return;
	if (Musket->IsReloading())
	{
		FPA250AIReloadConditions C;
		C.Stress01 = ComputeStress01(Me);
		C.bMoving = Me->GetVelocity().Size2D() > 80.f;
		Musket->TickAIReload(ReloadParams, C, ReloadRng);
	}
	if (Musket->GetReloadsCompleted() != SeenReloads)
	{
		SeenReloads = Musket->GetReloadsCompleted();
		if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
		{
			GM->RecordAIReload(Training.Name, Musket->GetLastReloadSeconds(), Musket->GetLastReloadFumbles());
		}
	}
}

void APA250BotController::SetFormationOrder(const FPA250FormationOrder& Order)
{
	Form = Order;
	Form.Facing = Order.Facing.GetSafeNormal2D();
	FormOrderAge = 0.f;
}

void APA250BotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FormOrderAge += DeltaSeconds;
	ARFCharacter* Me = Cast<ARFCharacter>(GetPawn());
	if (!Me || Me->IsDead())
	{
		return;
	}
	// AI reload runs every frame (not in the 0.2-0.35 s think gate) so its press timing is exact.
	TickReload(Me);

	ThinkTimer -= DeltaSeconds;
	ReactionTimer = FMath::Max(0.f, ReactionTimer - DeltaSeconds);
	VolleyJitter = FMath::Max(0.f, VolleyJitter - DeltaSeconds);
	if (ThinkTimer > 0.f)
	{
		return;
	}
	ThinkTimer = 0.2f + FMath::FRand() * 0.15f;

	const FPA250BotTuningValues& T = BotTuning ? BotTuning->Values : FPA250BotTuningValues();

	float Dist = 0.f;
	ARFCharacter* Enemy = FindNearestEnemy(Me, Dist);
	const bool bLoaded = Me->Musket && Me->Musket->CanFire();

	// In a formed squad: the squad decides where to stand, when to fire and whether to run.
	if (HasFormationOrder())
	{
		{
			const float ArriveForDuck = Form.ArriveCm > 0.f ? Form.ArriveCm : T.FormationArriveCm;
			const bool bAtSpotNow = FVector::Dist2D(Me->GetActorLocation(), Form.Target) <= ArriveForDuck;
			Me->SetDucking(PA250ShouldDuck(Form.bInCover, bAtSpotNow, bLoaded, Enemy != nullptr && Dist <= T.EngageMaxCm, Form.bRout, Form.bCharge));
		}
		if (Form.bRout)
		{
			// Routing: run for the rear. No shots, no bayonet; the reload carries on (slowed by stress).
			State = EBotState::Rout;
			ClearFocus(EAIFocusPriority::Gameplay);
			MoveToward(Me, Form.Target, 200.f);
			return;
		}
		UCharacterMovementComponent* MoveComp = Me->GetCharacterMovement();
		if (BaseWalkSpeed < 0.f) BaseWalkSpeed = MoveComp->MaxWalkSpeed;
		if (Form.bCharge)
		{
			// Squad bayonet charge: run at the nearest enemy in sight (else the squad's contact), lunge, stab.
			ARFCharacter* Target = Enemy ? Enemy : Form.Contact.Get();
			if (Target && Me->MeleeComponent)
			{
				const float D = FVector::Dist(Target->GetActorLocation(), Me->GetActorLocation());
				const FPA250MeleeTuningValues& MV = Me->MeleeComponent->GetValues();
				State = EBotState::Charge;
				SetFocus(Target);
				MoveComp->MaxWalkSpeed = Me->SprintSpeed;
				MoveToward(Me, Target->GetActorLocation(), 60.f);
				if (D <= MV.ThrustReachCm + MV.ThrustRadiusCm) Me->MeleeComponent->DoThrust();
				else if (D <= MV.ChargeLungeCm && Me->MeleeComponent->GetChargeCooldownRemaining() <= 0.f) Me->MeleeComponent->DoCharge();
				return;
			}
		}
		MoveComp->MaxWalkSpeed = BaseWalkSpeed;
		if (Form.bMayCharge && TryBayonet(Me, Enemy, Dist, T)) return;
		const float ArriveCm = Form.ArriveCm > 0.f ? Form.ArriveCm : T.FormationArriveCm;
		const bool bAtSpot = FVector::Dist2D(Me->GetActorLocation(), Form.Target) <= ArriveCm;
		if (Form.bPresent && Form.Contact.IsValid())
		{
			SetFocus(Form.Contact.Get());
			State = EBotState::Aim;
		}
		else if (Form.bFireAtWill && Enemy && Dist <= T.EngageMaxCm)
		{
			SetFocus(Enemy);
			State = bLoaded ? EBotState::Aim : EBotState::Reload;
		}
		else
		{
			ClearFocus(EAIFocusPriority::Gameplay);
			SetFocalPoint(Me->GetActorLocation() + Form.Facing * 5000.f);
			State = Form.bEngaged ? (bLoaded ? EBotState::FormLine : EBotState::Reload) : EBotState::Advance;
		}
		if (bAtSpot) StopMovement();
		else MoveToward(Me, Form.Target, ArriveCm * 0.5f);

		// Skirmish fire: only once in position (repositioning costs the shot), loaded, target in sight.
		if (Form.bFireAtWill && bAtSpot && bLoaded && Enemy && Dist <= T.EngageMaxCm)
		{
			if (!bReactionRolled)
			{
				ReactionTimer = FMath::FRandRange(T.ReactionMinSeconds, T.ReactionMaxSeconds);
				bReactionRolled = true;
			}
			if (ReactionTimer <= 0.f)
			{
				const FVector Muzzle = Me->Musket->GetMuzzleLocation()->GetComponentLocation();
				const FVector Aim = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f)
					+ Enemy->GetVelocity() * (Dist / FMath::Max(Me->Musket->GetProjectileSpeed(), 1.f));
				const FVector Dir = FMath::VRandCone((Aim - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErrorDegrees));
				if (Me->FireMusketAt(Muzzle + Dir * Dist))
				{
					State = EBotState::Fire;
					if (APA250BattleGameMode* GM = GetWorld()->GetAuthGameMode<APA250BattleGameMode>())
					{
						if (UPA250VolleyCoordinator* V = GM->GetVolleys()) V->RecordEngagement(Dist);
					}
				}
				bReactionRolled = false;
			}
		}
		return;
	}

	Me->SetDucking(false);
	APA250ControlPoint* Goal = PickGoal(Me);
	if (!Goal && Objective.IsValid()) Goal = Objective.Get();
	EnsureSlot(Me, Goal);
	if (ClaimedSlot == INDEX_NONE)
	{
		APA250ControlPoint* Anchor = Goal;
		if (LooseGoal.Get() != Anchor)
		{
			LooseGoal = Anchor;
			const FVector Rand = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
			LooseSlotOffset = Rand * FMath::FRandRange(700.f, 1600.f);
		}
	}
	const FVector SlotLoc = DesiredSlotLocation(Me, Goal);
	const bool bAtSlot = FVector::Dist2D(Me->GetActorLocation(), SlotLoc) < 180.f;

	APA250BattleGameMode* GM = GetWorld()->GetAuthGameMode<APA250BattleGameMode>();
	UPA250VolleyCoordinator* Volleys = GM ? GM->GetVolleys() : nullptr;

	// Cover when the squad is below half strength (live / peak members, from the coordinator).
	const float Strength = (Volleys && SquadId >= 0) ? Volleys->GetSquadStrength(SquadId) : 1.f;
	if (Strength < T.CoverSquadStrength && Goal)
	{
		State = EBotState::SeekCover;
		const FVector Cover = SlotLoc + (SlotLoc - Goal->GetActorLocation()).GetSafeNormal2D() * 400.f;
		if (Enemy) SetFocus(Enemy);
		MoveToward(Me, Cover, 150.f);
		// Still return fire from cover.
		if (Enemy && bLoaded && Dist <= T.EngageMaxCm && HasLineOfSight(Me, Enemy) && ReactionTimer <= 0.f)
		{
			const FVector Muzzle = Me->Musket->GetMuzzleLocation()->GetComponentLocation();
			const FVector Aim = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f);
			const FVector Dir = FMath::VRandCone((Aim - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErrorDegrees * 1.5f));
			if (Me->FireMusketAt(Muzzle + Dir * Dist) && Volleys) Volleys->RecordEngagement(Dist);
			ReactionTimer = FMath::FRandRange(T.ReactionMinSeconds, T.ReactionMaxSeconds);
		}
		return;
	}

	if (TryBayonet(Me, Enemy, Dist, T)) return;

	// Inside the engage band with a clear line: halt, react, take the volley window, fire.
	if (Enemy && Dist <= T.EngageMaxCm && HasLineOfSight(Me, Enemy))
	{
		SetFocus(Enemy);
		if (!bLoaded)
		{
			// Reload on the move while still outside the near edge of the band; hold once inside it.
			State = EBotState::Reload;
			bReactionRolled = false;
			if (Dist > T.EngageMinCm && !bAtSlot) MoveToward(Me, SlotLoc, 120.f);
			else StopMovement();
			return;
		}

		StopMovement();
		if (!bReactionRolled)
		{
			ReactionTimer = FMath::FRandRange(T.ReactionMinSeconds, T.ReactionMaxSeconds);
			VolleyJitter = FMath::FRandRange(0.f, T.VolleyWindowSeconds);
			bReactionRolled = true;
			State = EBotState::FormLine;
		}
		if (ReactionTimer > 0.f || VolleyJitter > 0.f)
		{
			State = EBotState::Aim;
			return;
		}
		State = EBotState::Fire;
		const float Speed = Me->Musket->GetProjectileSpeed();
		const FVector Lead = Enemy->GetVelocity() * (Dist / FMath::Max(Speed, 1.f));
		const FVector Aim = Enemy->GetActorLocation() + FVector(0.f, 0.f, 20.f) + Lead;
		const FVector Muzzle = Me->Musket->GetMuzzleLocation()->GetComponentLocation();
		const FVector Dir = FMath::VRandCone((Aim - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(AimErrorDegrees));
		if (Me->FireMusketAt(Muzzle + Dir * Dist) && Volleys)
		{
			Volleys->RecordEngagement(Dist);
		}
		bReactionRolled = false;
		return;
	}

	bReactionRolled = false;
	ClearFocus(EAIFocusPriority::Gameplay);
	if (bAtSlot)
	{
		APA250ControlPoint* FacePost = ClaimedPost.Get() ? ClaimedPost.Get() : Goal;
		State = (FacePost && FacePost->GetHolder() == Me->FactionId) ? EBotState::Defend : EBotState::HoldObjective;
		StopMovement();
		if (FacePost)
		{
			FVector Face = FacePost->GetActorLocation() - Me->GetActorLocation();
			if (Enemy) Face = Enemy->GetActorLocation() - Me->GetActorLocation();
			Face.Z = 0.f;
			if (!Face.IsNearlyZero()) Me->SetActorRotation(Face.Rotation());
		}
		return;
	}
	State = bLoaded ? EBotState::Advance : EBotState::Reload;
	MoveToward(Me, SlotLoc, 120.f);
}
