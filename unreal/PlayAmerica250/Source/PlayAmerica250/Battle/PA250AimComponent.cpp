#include "PA250AimComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MusketWeapon.h"
#include "RFCharacter.h"

UPA250AimComponent::UPA250AimComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FPA250AimResult UPA250AimComponent::ResolveAim(
	UWorld* World,
	const FVector& CameraLocation,
	const FVector& CameraDirection,
	const FVector& MuzzleLocation,
	float SpreadDegrees,
	float MaxRangeCm,
	const AActor* IgnoreActor,
	const AActor* IgnoreMuzzleOwner,
	FName FriendlyFaction)
{
	FPA250AimResult R;
	R.SpreadDegrees = SpreadDegrees;
	if (!World)
	{
		R.CameraAimPoint = CameraLocation + CameraDirection * MaxRangeCm;
		R.MuzzleHitPoint = R.CameraAimPoint;
		R.FireDirection = CameraDirection.GetSafeNormal();
		return R;
	}

	FCollisionQueryParams CamParams(SCENE_QUERY_STAT(PA250AimCam), true, IgnoreActor);
	if (IgnoreMuzzleOwner) CamParams.AddIgnoredActor(IgnoreMuzzleOwner);

	const FVector CamEnd = CameraLocation + CameraDirection.GetSafeNormal() * MaxRangeCm;
	FHitResult CamHit;
	const bool bCamHit = World->LineTraceSingleByChannel(CamHit, CameraLocation, CamEnd, ECC_Visibility, CamParams);
	R.CameraAimPoint = bCamHit ? CamHit.ImpactPoint : CamEnd;
	R.bCameraHit = bCamHit;

	if (bCamHit)
	{
		if (const ARFCharacter* HitChar = Cast<ARFCharacter>(CamHit.GetActor()))
		{
			R.bHostileUnderReticle = !FriendlyFaction.IsNone() && HitChar->FactionId != FriendlyFaction && !HitChar->IsDead();
		}
	}

	// Block test uses the unspread muzzle→aim ray. Spread is applied only to FireDirection,
	// otherwise the reticle X flickers and a clear shot can be treated as obstructed.
	const FVector ToAim = (R.CameraAimPoint - MuzzleLocation).GetSafeNormal();
	FCollisionQueryParams MuzzleParams(SCENE_QUERY_STAT(PA250AimMuzzle), true, IgnoreActor);
	if (IgnoreMuzzleOwner) MuzzleParams.AddIgnoredActor(IgnoreMuzzleOwner);

	const FVector MuzzleEnd = MuzzleLocation + ToAim * MaxRangeCm;
	FHitResult MuzzleHit;
	const bool bMuzzleHit = World->LineTraceSingleByChannel(MuzzleHit, MuzzleLocation, MuzzleEnd, ECC_Visibility, MuzzleParams);
	R.MuzzleHitPoint = bMuzzleHit ? MuzzleHit.ImpactPoint : MuzzleEnd;

	const float DistCam = FVector::Dist(MuzzleLocation, R.CameraAimPoint);
	const float DistHit = FVector::Dist(MuzzleLocation, R.MuzzleHitPoint);
	R.bBlocked = bMuzzleHit && DistHit + 5.f < DistCam;
	// A trace that starts inside cover returns no hit. Treat an overlapping muzzle as blocked.
	if (!R.bBlocked && World->OverlapBlockingTestByChannel(
		MuzzleLocation, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8.f), MuzzleParams))
	{
		R.bBlocked = true;
		R.MuzzleHitPoint = MuzzleLocation + ToAim * 40.f;
	}

	FVector Fired = ToAim;
	if (SpreadDegrees > 0.01f)
	{
		Fired = FMath::VRandCone(ToAim, FMath::DegreesToRadians(SpreadDegrees));
	}
	R.FireDirection = Fired;
	return R;
}

FPA250AimResult UPA250AimComponent::ResolveFromOwner(float SpreadDegrees, float MaxRangeCm)
{
	const ARFCharacter* OwnerChar = Cast<ARFCharacter>(GetOwner());
	FVector CamLoc = GetOwner() ? GetOwner()->GetActorLocation() + FVector(0, 0, 60) : FVector::ZeroVector;
	FVector CamDir = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;
	FVector Muzzle = CamLoc + CamDir * 65.f;
	FName Faction = NAME_None;
	const AActor* IgnoreMusket = nullptr;

	if (OwnerChar)
	{
		Faction = OwnerChar->FactionId;
		if (OwnerChar->Camera)
		{
			CamLoc = OwnerChar->Camera->GetComponentLocation();
			CamDir = OwnerChar->Camera->GetForwardVector();
		}
		else if (const AController* C = OwnerChar->GetController())
		{
			FRotator Rot;
			C->GetPlayerViewPoint(CamLoc, Rot);
			CamDir = Rot.Vector();
		}
		if (OwnerChar->Musket && OwnerChar->Musket->GetMuzzleLocation())
		{
			Muzzle = OwnerChar->Musket->GetMuzzleLocation()->GetComponentLocation();
			IgnoreMusket = OwnerChar->Musket;
		}
	}

	LastResult = ResolveAim(GetWorld(), CamLoc, CamDir, Muzzle, SpreadDegrees, MaxRangeCm, OwnerChar, IgnoreMusket, Faction);
	return LastResult;
}
