#include "PA250VisualsCommandlet.h"
#include "RFCharacter.h"
#include "MusketWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AnimationRuntime.h"
#include "Animation/Skeleton.h"
#include "Components/CapsuleComponent.h"

UPA250VisualsCommandlet::UPA250VisualsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UPA250VisualsCommandlet::Main(const FString& Params)
{
	int32 Failed = 0;
	auto Check = [&Failed](bool OK, const TCHAR* Message) {
		UE_LOG(LogTemp, Display, TEXT("PA250.Visuals %s: %s"), OK ? TEXT("PASS") : TEXT("FAIL"), Message);
		if (!OK) ++Failed;
	};
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PA250Visuals"));
	if (!World || !GEngine) return 1;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	ARFCharacter* British = World->SpawnActor<ARFCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
	ARFCharacter* Continental = World->SpawnActor<ARFCharacter>(FVector(0, 500, 100), FRotator::ZeroRotator);
	Check(British && Continental, TEXT("both factions spawn"));
	if (British && Continental)
	{
		British->SetTeam(TEXT("british"), TEXT("line_infantry"));
		Continental->SetTeam(TEXT("continental"), TEXT("line_infantry"));
		for (ARFCharacter* Soldier : {British, Continental})
		{
			// A bare commandlet world has no GameMode to dispatch BeginPlay.
			if (!Soldier->HasActorBegunPlay()) Soldier->DispatchBeginPlay();
			Check(Soldier->IsUsingPeriodSoldier() && Soldier->GetMesh()->GetSkeletalMeshAsset()->GetPathName().StartsWith(TEXT("/Game/american_soldier/")), TEXT("spawned pawn uses Fab soldier"));
			UAnimSingleNodeInstance* Anim = Soldier->GetMesh()->GetSingleNodeInstance();
			Check(Anim && Anim->GetCurrentAsset() && Anim->GetCurrentAsset()->GetPathName().Contains(TEXT("ThirdPersonIdle")), TEXT("compatible pack idle replaces Manny AnimBP"));
			Check(!Soldier->HatMesh->IsVisible() && !Soldier->TeamMarker->IsVisible(), TEXT("primitive hat and floating cone hidden"));
			AMusketWeapon* Weapon = Soldier->Musket.Get();
			Check(Weapon && (Weapon->GetAnimatedMesh() ? Weapon->GetAnimatedMesh()->GetSkeletalMeshAsset()->GetPathName() : Weapon->GetMusketMesh()->GetStaticMesh()->GetPathName()).StartsWith(TEXT("/Game/HS_WeaponPack_01/")), TEXT("spawned weapon uses Horizon musket"));
			Check(Weapon && Weapon->GetAttachParentActor() == Soldier && Weapon->GetRootComponent()->GetAttachSocketName() == TEXT("hand_r"), TEXT("musket attached to soldier hand"));
		}
		// One soldier faces +Y to prove the barrel follows the owner, not the hand bone.
		Continental->SetActorRotation(FRotator(0.f, 90.f, 0.f));
		// Let the pack idle pose the skeleton (no renderer here, so tick and refresh bones by hand).
		for (int32 i = 0; i < 30; ++i)
		{
			for (ARFCharacter* Soldier : {British, Continental})
			{
				USkeletalMeshComponent* M = Soldier->GetMesh();
				M->TickAnimation(1.f / 30.f, false);
				M->RefreshBoneTransforms();
				M->FinalizeBoneTransform();
				M->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::TeleportPhysics);
				if (AMusketWeapon* W = Soldier->Musket.Get()) W->UpdateVisuals(1.f / 30.f);
			}
		}
		for (ARFCharacter* Soldier : {British, Continental})
		{
			AMusketWeapon* Weapon = Soldier->Musket.Get();
			if (!Weapon) continue;
			const FTransform Actor = Soldier->GetActorTransform();
			const FVector Muzzle = Actor.InverseTransformPosition(Weapon->GetMuzzleLocation()->GetComponentLocation());
			const float Along = FVector::DotProduct(Weapon->GetActorForwardVector(), Soldier->GetActorForwardVector());
			const float MuzzleHeight = Muzzle.Z + Soldier->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			UE_LOG(LogTemp, Display, TEXT("PA250.Visuals measure %s: barrel.facing=%.3f muzzle(actor space)=%s height=%.0f cm"),
				*Soldier->FactionId.ToString(), Along, *Muzzle.ToCompactString(), MuzzleHeight);
			Check(Along > 0.97f, TEXT("barrel points where the soldier faces (not the hand bone axis)"));
			Check(Muzzle.X > 50.f && MuzzleHeight > 60.f && MuzzleHeight < 170.f, TEXT("muzzle is in front of the body at a carrying height"));

			// Pack animations: lock falls on the shot, then the reload is scrubbed by the sequencer.
			USkeletalMeshComponent* Gun = Weapon->GetAnimatedMesh();
			Check(Gun && Weapon->GetFireAnim() && Weapon->GetReloadAnim()
				&& Weapon->GetFireAnim()->GetSkeleton() == Gun->GetSkeletalMeshAsset()->GetSkeleton(), TEXT("pack Firing/Reloading animations match the skeletal musket"));
			if (!Gun) continue;
			const FVector Hammer = Weapon->GetActorTransform().InverseTransformPosition(Gun->GetSocketLocation(TEXT("b_gun_Hammer")));
			const FVector Strap = Weapon->GetActorTransform().InverseTransformPosition(Gun->GetSocketLocation(TEXT("b_gun_FrontStrapGrip")));
			UE_LOG(LogTemp, Display, TEXT("PA250.Visuals measure gun-space hammer=%s front_strap=%s"), *Hammer.ToCompactString(), *Strap.ToCompactString());
			Check(Hammer.Z > 0.f && Hammer.X < 30.f, TEXT("musket upright: lock hammer above the barrel near the grip"));
			Weapon->Fire(Soldier->GetActorForwardVector());
			UAnimSingleNodeInstance* GunAnim = Gun->GetSingleNodeInstance();
			Check(GunAnim && GunAnim->GetCurrentAsset() == Weapon->GetFireAnim(), TEXT("shot plays the pack Firing animation"));
			Weapon->UpdateVisuals(Weapon->GetFireAnim() ? Weapon->GetFireAnim()->GetPlayLength() + 0.05f : 0.5f);
			Weapon->UpdateVisuals(0.f);
			GunAnim = Gun->GetSingleNodeInstance();
			Check(Weapon->IsReloading() && GunAnim && GunAnim->GetCurrentAsset() == Weapon->GetReloadAnim(), TEXT("reload poses the pack Reloading animation"));
		}
		{
			using P = EActiveReloadPhase;
			const float A0 = AMusketWeapon::ReloadAnimAlpha(P::PrimePan, 0.f), A1 = AMusketWeapon::ReloadAnimAlpha(P::PrimePan, 1.f);
			const float A2 = AMusketWeapon::ReloadAnimAlpha(P::RamCharge, 0.5f), A3 = AMusketWeapon::ReloadAnimAlpha(P::ShoulderAim, 1.f);
			Check(A0 == 0.f && A1 <= A2 && A2 < A3 && A3 == 1.f && AMusketWeapon::ReloadAnimAlpha(P::Jammed, 0.5f) < 0.f
				&& AMusketWeapon::ReloadAnimAlpha(P::MissDelay, 0.5f) < 0.f, TEXT("reload animation follows the drill phases and holds on fumble/jam"));
		}
		auto* Red = Cast<UMaterialInstanceDynamic>(British->GetMesh()->GetMaterial(1));
		auto* Blue = Cast<UMaterialInstanceDynamic>(Continental->GetMesh()->GetMaterial(1));
		Check(Red && Blue && Red->K2_GetVectorParameterValue(TEXT("CoatColor")).R > Red->K2_GetVectorParameterValue(TEXT("CoatColor")).B
			&& Blue->K2_GetVectorParameterValue(TEXT("CoatColor")).B > Blue->K2_GetVectorParameterValue(TEXT("CoatColor")).R, TEXT("British red and Continental blue use distinct coat materials"));
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	UE_LOG(LogTemp, Display, TEXT("PA250.Visuals failures=%d"), Failed);
	return Failed ? 1 : 0;
}
