#include "PA250VisualsCommandlet.h"
#include "RFCharacter.h"
#include "MusketWeapon.h"
#include "PA250SoldierAnimInstance.h"
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
#include "PA250FactionVisuals.h"
#include "PA250Smoke.h"

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
			const UPA250SoldierAnimInstance* Anim = Cast<UPA250SoldierAnimInstance>(Soldier->GetMesh()->GetAnimInstance());
			Check(Anim && Anim->GetLocomotion() && Anim->GetLocomotion()->GetPathName().Contains(TEXT("ThirdPersonIdle")), TEXT("compatible pack idle replaces Manny AnimBP"));
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
				if (AMusketWeapon* W = Soldier->Musket.Get())
				{
					W->UpdateVisuals(1.f / 30.f);
					W->PoseSupportHand();
				}
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

			// A steep upward aim must raise the barrel with the view. The old ±45° clamp left it short.
			{
				const FRotator Level = Soldier->GetActorRotation();
				Soldier->SetActorRotation(FRotator(-70.f, Level.Yaw, 0.f));
				Weapon->UpdateVisuals(0.f);
				const float UpDot = FVector::DotProduct(Weapon->GetActorForwardVector(), Soldier->GetActorForwardVector());
				const float Raised = Weapon->GetMuzzleLocation()->GetComponentLocation().Z - Soldier->GetActorLocation().Z;
				UE_LOG(LogTemp, Display, TEXT("PA250.Visuals measure %s: upward aim barrel.forward=%.3f muzzle above actor=%.0f cm"),
					*Soldier->FactionId.ToString(), UpDot, Raised);
				Check(UpDot > 0.97f, TEXT("barrel follows a 70 degree upward aim"));
				Soldier->SetActorRotation(Level);
				Weapon->UpdateVisuals(0.f);
			}

			// Left hand solved onto the stock inside pose evaluation (the wrist sits 10 cm behind the spot).
			if (USkeletalMeshComponent* GunMesh = Weapon->GetAnimatedMesh(); GunMesh && GunMesh->GetBoneIndex(TEXT("b_gun_LeftHandSpot")) != INDEX_NONE)
			{
				const UPA250SoldierAnimInstance* BodyAnim = Cast<UPA250SoldierAnimInstance>(Soldier->GetMesh()->GetAnimInstance());
				const FVector Spot = GunMesh->GetBoneLocation(TEXT("b_gun_LeftHandSpot"), EBoneSpaces::WorldSpace);
				const float Gap = FVector::Dist(Soldier->GetMesh()->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace), Spot);
				bool bBonesValid = true;
				for (const FTransform& B : Soldier->GetMesh()->GetComponentSpaceTransforms())
				{
					bBonesValid &= !B.ContainsNaN() && B.IsRotationNormalized();
				}
				USkeletalMeshComponent* BodyMesh = Soldier->GetMesh();
				const FVector Sh = BodyMesh->GetBoneLocation(TEXT("upperarm_l"), EBoneSpaces::WorldSpace);
				const FVector El = BodyMesh->GetBoneLocation(TEXT("lowerarm_l"), EBoneSpaces::WorldSpace);
				const FVector Wr = BodyMesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace);
				const float Reach = FVector::Dist(Sh, El) + FVector::Dist(El, Wr);
				UE_LOG(LogTemp, Display, TEXT("PA250.Visuals measure %s: left wrist to stock spot=%.1f cm (arm reach %.1f cm, shoulder to spot %.1f cm)"),
					*Soldier->FactionId.ToString(), Gap, Reach, FVector::Dist(Sh, Spot));
				// In reach: the wrist lands on its target (10 cm behind the spot). Out of reach (the unarmed
				// idle carries the gun low and far forward): the straightened arm points right at the spot.
				const float Short = FMath::Max(0.f, FVector::Dist(Sh, Spot) - 10.f - Reach);
				Check(BodyAnim && BodyAnim->GetSupportHandTarget().bEnabled && Gap < 10.f + Short + 3.f,
					TEXT("left arm solved onto the musket stock (anim-thread IK)"));
				Check(bBonesValid, TEXT("every body bone is finite with a unit rotation after the hand IK"));
			}

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
		// Melee pose layer on the real skeleton: a full lunge, then a blow from the front.
		{
			USkeletalMeshComponent* M = British->GetMesh();
			UPA250SoldierAnimInstance* Anim = Cast<UPA250SoldierAnimInstance>(M->GetAnimInstance());
			Check(Anim != nullptr, TEXT("melee pose: soldier runs the native body anim"));
			if (Anim)
			{
				auto Refresh = [M]() { M->TickAnimation(1.f / 30.f, false); M->RefreshBoneTransforms(); M->FinalizeBoneTransform(); };
				auto At = [British, M](const TCHAR* Bone) { return British->GetActorTransform().InverseTransformPosition(M->GetBoneLocation(Bone, EBoneSpaces::WorldSpace)); };
				auto BonesValid = [M]() {
					bool bOK = true;
					for (const FTransform& B : M->GetComponentSpaceTransforms()) bOK &= !B.ContainsNaN() && B.IsRotationNormalized();
					return bOK;
				};
				FPA250MeleePose Dirs;
				Dirs.ForwardCS = M->GetComponentTransform().InverseTransformVectorNoScale(British->GetActorForwardVector());
				Dirs.RightCS = M->GetComponentTransform().InverseTransformVectorNoScale(British->GetActorRightVector());
				Anim->SetMeleePose(FPA250MeleePose());
				Refresh();
				const FVector Hand0 = At(TEXT("hand_r")), FootL0 = At(TEXT("foot_l")), FootR0 = At(TEXT("foot_r")), Pelvis0 = At(TEXT("pelvis")), Head0 = At(TEXT("head"));

				FPA250MeleePose Lunge = Dirs;
				Lunge.Lunge = 1.f;
				Lunge.RearFootLagCm = 30.f;
				Anim->SetMeleePose(Lunge);
				Refresh();
				const FVector Hand1 = At(TEXT("hand_r")), FootL1 = At(TEXT("foot_l")), FootR1 = At(TEXT("foot_r")), Pelvis1 = At(TEXT("pelvis")), Head1 = At(TEXT("head"));
				UE_LOG(LogTemp, Display, TEXT("PA250.Visuals measure lunge (cm, actor space, vs idle): hand_r %s  foot_l %s  foot_r %s  pelvis %s  head %s"),
					*(Hand1 - Hand0).ToCompactString(), *(FootL1 - FootL0).ToCompactString(), *(FootR1 - FootR0).ToCompactString(),
					*(Pelvis1 - Pelvis0).ToCompactString(), *(Head1 - Head0).ToCompactString());
				Check(Hand1.X - Hand0.X > 40.f, TEXT("lunge: the right hand (and the musket on it) is driven over 40 cm forward"));
				Check(FootL1.X - FootL0.X > 30.f && FMath::Abs(FootL1.Z - FootL0.Z) < 4.f, TEXT("lunge: the front foot steps out and lands on the ground"));
				// Asked to trail 30 cm; the straight rear leg only reaches so far with the hips this low, and the foot never lifts to cheat.
				Check(FootR0.X - FootR1.X > 10.f && FMath::Abs(FootR1.Z - FootR0.Z) < 4.f, TEXT("lunge: the rear foot trails behind as far as the leg reaches and stays down"));
				Check(Pelvis0.Z - Pelvis1.Z > 8.f && Pelvis1.X > Pelvis0.X, TEXT("lunge: hips drop and go forward"));
				Check(BonesValid(), TEXT("lunge: every body bone is finite with a unit rotation"));

				FPA250MeleePose Flinch = Dirs;
				Flinch.FlinchCS = -Dirs.ForwardCS; // struck from the front
				Anim->SetMeleePose(Flinch);
				Refresh();
				const FVector Head2 = At(TEXT("head")), FootL2 = At(TEXT("foot_l")), FootR2 = At(TEXT("foot_r"));
				UE_LOG(LogTemp, Display, TEXT("PA250.Visuals measure flinch (cm, vs idle): head %s  foot_l %s  foot_r %s"),
					*(Head2 - Head0).ToCompactString(), *(FootL2 - FootL0).ToCompactString(), *(FootR2 - FootR0).ToCompactString());
				Check(Head0.X - Head2.X > 15.f && FVector::Dist(FootL2, FootL0) < 4.f && FVector::Dist(FootR2, FootR0) < 4.f,
					TEXT("hit reaction: struck from the front, the body rocks back over planted feet"));
				Check(BonesValid(), TEXT("hit reaction: every body bone is finite with a unit rotation"));

				Anim->SetMeleePose(FPA250MeleePose());
				Refresh();
				Check(FVector::Dist(At(TEXT("hand_r")), Hand0) < 3.f && FVector::Dist(At(TEXT("pelvis")), Pelvis0) < 3.f, TEXT("melee pose: no weights leaves the clip pose alone"));
			}
		}
		{
			using P = EActiveReloadPhase;
			const float A0 = AMusketWeapon::ReloadAnimAlpha(P::PrimePan, 0.f), A1 = AMusketWeapon::ReloadAnimAlpha(P::PrimePan, 1.f);
			const float A2 = AMusketWeapon::ReloadAnimAlpha(P::RamCharge, 0.5f), A3 = AMusketWeapon::ReloadAnimAlpha(P::ShoulderAim, 1.f);
			Check(A0 == 0.f && A1 <= A2 && A2 < A3 && A3 == 1.f && AMusketWeapon::ReloadAnimAlpha(P::Jammed, 0.5f) < 0.f
				&& AMusketWeapon::ReloadAnimAlpha(P::MissDelay, 0.5f) < 0.f, TEXT("reload animation follows the drill phases and holds on fumble/jam"));
		}
		{
			auto* Coat = Cast<UMaterialInstanceDynamic>(Continental->GetMesh()->GetMaterial(1));
			auto* Under = Cast<UMaterialInstanceDynamic>(Continental->GetMesh()->GetMaterial(0));
			const FLinearColor C = Coat ? Coat->K2_GetVectorParameterValue(TEXT("CoatColor")) : FLinearColor::Black;
			Check(Coat && C.R >= C.B && C.R < 1.2f && Under && Under->GetMaterial()->GetName() == TEXT("M_PA250_SoldierUnder")
				&& !Cast<UMaterialInstanceDynamic>(British->GetMesh()->GetMaterial(0)),
				TEXT("Patriots wear homespun (earth-tone coat, own waistcoat/breeches); regulars keep the pack's whites"));
			FLinearColor A, B, A2, B2;
			int32 Distinct = 0;
			TSet<uint32> Seen;
			for (uint32 Seed = 0; Seed < 40; ++Seed)
			{
				UPA250FactionVisuals::HomespunFor(Seed, A, B);
				Seen.Add(GetTypeHash(A.ToFColor(false)));
			}
			Distinct = Seen.Num();
			UPA250FactionVisuals::HomespunFor(7, A, B);
			UPA250FactionVisuals::HomespunFor(7, A2, B2);
			Check(Distinct >= 12 && A == A2 && B == B2 && A.R < 0.6f, TEXT("homespun: varied per man, stable per seed, never bright"));
		}
		auto* Red = Cast<UMaterialInstanceDynamic>(British->GetMesh()->GetMaterial(1));
		bool bNeverRed = true;
		for (uint32 Seed = 0; Seed < 200; ++Seed)
		{
			FLinearColor Coat, Under;
			UPA250FactionVisuals::HomespunFor(Seed, Coat, Under);
			bNeverRed &= Coat.R < 1.6f * Coat.G;
		}
		const FLinearColor R = Red ? Red->K2_GetVectorParameterValue(TEXT("CoatColor")) : FLinearColor::Black;
		Check(Red && R.R > 2.f * R.G && R.R > 2.f * R.B && bNeverRed, TEXT("British wear red; Patriot homespun is never red (friend/foe at a glance)"));
	}
	{
		// Smoke puff: blown forward, then drifts downwind, swells, and fades out by Life.
		FPA250SmokePuff P;
		P.Pos = FVector::ZeroVector; P.Vel = FVector(260.f, 0.f, 0.f); P.Life = 25.f; P.Size0 = 60.f; P.Size1 = 500.f; P.Peak = 0.5f;
		float Peak = 0.f, SizeAt3 = 0.f;
		FVector PosAt2;
		for (int32 i = 0; i < 30 * 26; ++i)
		{
			P.Step(1.f / 30.f, UPA250SmokeSubsystem::Wind());
			Peak = FMath::Max(Peak, P.Opacity());
			if (i == 60) PosAt2 = P.Pos;
			if (i == 90) SizeAt3 = P.Size();
		}
		Check(PosAt2.X > 100.f && PosAt2.X < 300.f && P.Pos.Z > 0.f && P.Pos.X > 1000.f, TEXT("smoke: blown a metre or two out, then drifts downwind and rises"));
		Check(Peak > 0.45f && P.IsDead() && P.Opacity() == 0.f && SizeAt3 > 250.f, TEXT("smoke: thick at first, billows within seconds, gone after its life"));
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	UE_LOG(LogTemp, Display, TEXT("PA250.Visuals failures=%d"), Failed);
	return Failed ? 1 : 0;
}
