#include "PA250Blast.h"
#include "PA250BattleAudio.h"
#include "PA250BattleGameMode.h"
#include "PA250FieldGun.h"
#include "PA250FieldWork.h"
#include "PA250Smoke.h"
#include "PA250VolleyCoordinator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "RFCharacter.h"
#include "pa250/sim.hpp"

namespace PA250Blast
{
namespace
{
/** Cover is ground, a wall or a work between the blast and the man. Men are not cover for one
 *  another (the round shot's rule too), and neither is the thing that blew up. */
bool HasCoverToBlast(UWorld* World, const FVector& Origin, const ARFCharacter* Man, const FCollisionQueryParams& Params)
{
	if (!World || !Man) return false;
	const FVector Chest = Man->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, Origin + FVector(0.f, 0.f, 40.f), Chest, ECC_Visibility, Params);
}
} // namespace

void SpawnBlastFX(UWorld* World, const FVector& Origin, EPA250BlastSize Size)
{
	if (!World) return;
	const pa250::BattleRules& R = pa250::battle_rules();
	const float Scale = Size == EPA250BlastSize::Chest ? 2.2f : (Size == EPA250BlastSize::Premature ? 0.7f : 1.5f);
	if (UPA250SmokeSubsystem* Bank = World->GetSubsystem<UPA250SmokeSubsystem>())
	{
		Bank->AddDirtBurst(Origin, FVector::UpVector, R.cannon_dirt_burst_scale * Scale);
		const int32 Puffs = Size == EPA250BlastSize::Chest ? 10 : 6;
		for (int32 i = 0; i < Puffs; ++i)
		{
			Bank->AddShot(Origin, FMath::VRandCone(FVector::UpVector, FMath::DegreesToRadians(50.f)));
		}
	}
	FPA250BattleAudio::PlayAtOr(World, TEXT("cannon_fire"), TEXT("musket_distant"), Origin, EPA250SoundRange::Gunfire,
		Size == EPA250BlastSize::Chest ? 1.1f : 0.85f, 0.05f);
}

void BatterWorksInRadius(UWorld* World, const FVector& Origin, float RadiusCm, float Damage, FName Faction, APA250BattleGameMode* GM)
{
	if (!World || RadiusCm <= 0.f || Damage <= 0.f) return;
	for (TActorIterator<APA250FieldWork> It(World); It; ++It)
	{
		APA250FieldWork* Work = *It;
		if (!Work || FVector::Dist(Work->GetActorLocation(), Origin) > RadiusCm) continue;
		const bool bBroken = Work->Batter(Damage);
		if (GM) GM->NotifyWorkBattered(Work, bBroken, Faction);
	}
}

FPA250BlastResult ApplyBlast(
	UWorld* World,
	const FVector& Origin,
	EPA250BlastSize Size,
	FName SourceFaction,
	ARFCharacter* Instigator,
	AActor* Causer,
	uint32 /*Seed*/,
	const TSet<ARFCharacter*>* SkipMen)
{
	FPA250BlastResult Out;
	if (!World) return Out;
	const pa250::BattleRules& R = pa250::battle_rules();
	const float Outer = OuterRadiusCm(Size, R);
	const float Impulse = ImpulseCm(Size, R);
	APA250BattleGameMode* GM = World->GetAuthGameMode<APA250BattleGameMode>();
	SpawnBlastFX(World, Origin, Size);

	const float WorksDamage = Size == EPA250BlastSize::Premature ? 0.f
		: (Size == EPA250BlastSize::Chest ? R.cannon_shell_works_damage * 1.5f : R.cannon_shell_works_damage);
	BatterWorksInRadius(World, Origin, Outer, WorksDamage, SourceFaction, GM);

	// The men are gathered first: a death in the loop below can destroy actors (the corpse cap),
	// and nobody is to be judged twice.
	FCollisionQueryParams CoverParams(SCENE_QUERY_STAT(PA250BlastCover), false, Causer);
	TArray<ARFCharacter*> Men;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		CoverParams.AddIgnoredActor(*It);
		ARFCharacter* Man = *It;
		if (!Man || Man->IsDead() || Man->FactionId.IsNone()) continue;
		if (SkipMen && SkipMen->Contains(Man)) continue;
		if (FVector::Dist(Man->GetActorLocation(), Origin) > Outer) continue;
		Men.Add(Man);
	}
	for (ARFCharacter* Man : Men)
	{
		if (!IsValid(Man) || Man->IsDead()) continue;
		const float D = FVector::Dist(Man->GetActorLocation(), Origin);
		const bool bCovered = HasCoverToBlast(World, Origin, Man, CoverParams);
		const EPA250BlastRing Ring = ClassifyRing(D, Outer, bCovered, R);
		if (Ring == EPA250BlastRing::None) continue;

		const bool bFriendly = !SourceFaction.IsNone() && Man->FactionId == SourceFaction;
		++Out.Hit;
		if (bFriendly) ++Out.FriendlyHit;

		FVector Away = (Man->GetActorLocation() - Origin).GetSafeNormal2D();
		if (Away.IsNearlyZero()) Away = FVector::ForwardVector;
		const float Falloff = 1.f - (D / FMath::Max(1.f, Outer));
		const EPA250BlastDir Dir = ReactionDir(Man->GetActorForwardVector(), Origin - Man->GetActorLocation());

		switch (Ring)
		{
		case EPA250BlastRing::Killed:
		{
			FHitResult Hit;
			Hit.ImpactPoint = Man->GetActorLocation();
			Hit.Location = Hit.ImpactPoint;
			AController* Credit = (!bFriendly && Instigator) ? Instigator->GetController() : nullptr;
			UGameplayStatics::ApplyPointDamage(Man, 999.f, Away, Hit, Credit, Causer, nullptr);
			// Counted only if it put him down: the totals have to agree with the ticket ledger.
			if (!Man->IsDead()) break;
			++Out.Killed;
			if (bFriendly) ++Out.FriendlyKilled;
			if (GM) GM->TryBlastRagdoll(Man, Away * Impulse * Falloff, /*bDead*/ true);
			break;
		}
		case EPA250BlastRing::Wounded:
			if (GM) GM->WoundOut(Man, bFriendly ? nullptr : Instigator, Causer);
			if (!Man->ActorHasTag(TEXT("PA250Wounded"))) break; // no battle mode, or he stayed up
			++Out.Wounded;
			if (bFriendly) ++Out.FriendlyWounded;
			break;
		case EPA250BlastRing::Knockdown:
			if (GM) GM->ApplyBlastKnockdown(Man, Away * Impulse * Falloff, Dir);
			else if (!Man->KnockDown(Away * Impulse * Falloff)) Man->LockMovement(R.blast_settle_max_seconds);
			++Out.Knocked;
			break;
		case EPA250BlastRing::Stagger:
			if (GM) GM->ApplyBlastStagger(Man, Dir);
			else Man->LockMovement(R.blast_settle_max_seconds * 0.6f);
			Man->ApplySuppression(R.cannon_knockdown_seconds, 4.f);
			++Out.Staggered;
			break;
		case EPA250BlastRing::Flinch:
			if (GM) GM->ApplyBlastFlinch(Man, Dir);
			else Man->ApplySuppression(R.blast_player_control_loss_seconds, 2.f);
			++Out.Flinched;
			break;
		default:
			break;
		}

		if (GM && GM->GetVolleys() && Man->SquadId >= 0 && !bFriendly)
		{
			GM->GetVolleys()->PressSquad(Man->SquadId, R.cannon_pressure * Falloff, R.cannon_pressure_seconds, SourceFaction, TEXT("Blast"));
		}
	}

	if (GM)
	{
		GM->NoteFriendlyBlastCasualties(SourceFaction, Out.FriendlyKilled, Out.FriendlyWounded);
	}
	return Out;
}
} // namespace PA250Blast
