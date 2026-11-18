#include "RFCharacter.h"
#include "PA250UnitClasses.h"
#include "Battle/PA250AnimClips.h"
#include "Misc/Paths.h"
#include "Battle/PA250BattleFeel.h"
#include "Battle/PA250BattleLook.h"
#include "Battle/PA250BattleWeather.h"
#include "Battle/PA250BattleRng.h"
#include "Battle/PA250MusketFeel.h"
#include "Battle/PA250Intensity.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Mounted/PA250MountedSoldier.h"
#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "PA250SoldierAnimInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "PA250Cover.h"
#include "pa250/sim.hpp"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MusketWeapon.h"
#include "PA250AimComponent.h"
#include "PA250BattleGameMode.h"
#include "PA250BattleAudio.h"
#include "PA250CombatProfiles.h"
#include "EngineUtils.h"
#include "PA250FactionVisuals.h"
#include "PA250MeleeComponent.h"
#include "Battle/PA250TraversalComponent.h"
#include "Battle/PA250TraversalLinks.h"
#include "MotionWarpingComponent.h"
#include "PA250ThirdPartyAssets.h"
#include "PA250VolleyCoordinator.h"
#include "PA250WeaponTuning.h"
#include "RFGameMode.h"
#include "RFHealthComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Blueprint/UserWidget.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Text/STextBlock.h"

ARFCharacter::ARFCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = HipArmLength;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bDoCollisionTest = false;
	SpringArm->SocketOffset = HipSocketOffset;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;
	// Rotation lag is off: its substepped quaternion blend stalls pitch, so the view (and the
	// musket, which follows the view) stops rising while yaw still works.
	SpringArm->bEnableCameraRotationLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;
	Camera->FieldOfView = DefaultFOV;

	HealthComponent = CreateDefaultSubobject<URFHealthComponent>(TEXT("HealthComponent"));
	AimComponent = CreateDefaultSubobject<UPA250AimComponent>(TEXT("AimComponent"));
	MeleeComponent = CreateDefaultSubobject<UPA250MeleeComponent>(TEXT("MeleeComponent"));
	MotionWarping = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));
	Traversal = CreateDefaultSubobject<UPA250TraversalComponent>(TEXT("Traversal"));

	TeamMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TeamMarker"));
	TeamMarker->SetupAttachment(RootComponent);
	TeamMarker->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	TeamMarker->SetRelativeScale3D(FVector(0.25f));
	TeamMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TeamMarker->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		TeamMarker->SetStaticMesh(ConeMesh.Object);
	}

	HatMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HatMesh"));
	HatMesh->SetupAttachment(GetMesh(), TEXT("head"));
	HatMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HatMesh->SetCastShadow(true);
	HatMesh->SetRelativeLocation(FVector(0.f, 0.f, 12.f));
	HatMesh->SetRelativeScale3D(FVector(0.35f, 0.45f, 0.2f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		HatMesh->SetStaticMesh(CubeMesh.Object);
	}

	Nameplate = CreateDefaultSubobject<UWidgetComponent>(TEXT("Nameplate"));
	Nameplate->SetupAttachment(RootComponent);
	Nameplate->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	Nameplate->SetWidgetSpace(EWidgetSpace::Screen);
	Nameplate->SetDrawSize(FVector2D(120.f, 24.f));
	Nameplate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Nameplate->SetVisibility(false);

	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	// Walk and sprint speed are data (battle_feel.json movement); the class's own values stand when the file gave none.
	if (const FPA250BattleFeel& MoveFeel = PA250BattleFeel::Get(); MoveFeel.WalkSpeedCm > 0.f && MoveFeel.SprintSpeedCm > 0.f)
	{
		WalkSpeed = MoveFeel.WalkSpeedCm;
		SprintSpeed = MoveFeel.SprintSpeedCm;
	}
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->JumpZVelocity = 450.f;
	// Ducking behind a 1.1 m wall: crouched capsule is 1.0 m tall (top below the wall).
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->SetCrouchedHalfHeight(CrouchedHalfHeightCm);
	GetCharacterMovement()->MaxWalkSpeedCrouched = 150.f;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Quinn(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (Quinn.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(Quinn.Object);
		GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	}
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimBP(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (AnimBP.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimBP.Class);
	}

	MusketClass = AMusketWeapon::StaticClass();
	CurrentArmLength = HipArmLength;
	CurrentSocketOffset = HipSocketOffset;
	CurrentFOV = TargetFOV = DefaultFOV;
}

void ARFCharacter::SetTeam(FName InFaction, FName InUnitClass)
{
	FactionId = InFaction;
	UnitClass = InUnitClass;
	ApplyTeamMarker();
	ApplyFactionVisuals();
}

void ARFCharacter::SetClothingSeed(uint32 Seed)
{
	ClothingSeed = Seed;
	bClothingSeedSet = true;
}

void ARFCharacter::ApplyFactionVisuals()
{
	// Fab soldier uses its own skeleton and supplied animations; never run Manny AnimBP on it.
	if (!FactionVisuals)
	{
		FactionVisuals = UPA250FactionVisuals::MakeRuntimeDefaults();
	}
	const FPA250FactionLook& Look = FactionVisuals->LookFor(FactionId, UnitClass);
	if (USkeletalMeshComponent* MeshComp = GetMesh(); MeshComp && !bUsesPeriodSoldier)
	{
		if (UMaterialInstanceDynamic* MID = MeshComp->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Look.CoatColor);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Look.CoatColor);
			MID->SetVectorParameterValue(TEXT("Tint"), Look.CoatColor);
		}
		if (MeshComp->GetNumMaterials() > 1)
		{
			if (UMaterialInstanceDynamic* Facing = MeshComp->CreateAndSetMaterialInstanceDynamic(1))
			{
				Facing->SetVectorParameterValue(TEXT("Color"), Look.FacingColor);
				Facing->SetVectorParameterValue(TEXT("BaseColor"), Look.FacingColor);
			}
		}
	}
	if (bUsesPeriodSoldier)
	{
		UMaterialInterface* Coat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_PA250_SoldierUniform.M_PA250_SoldierUniform"));
		// 1775 Patriots (militia, minutemen and the new army alike) wore their own clothes:
		// every man gets his own homespun coat and waistcoat/breeches. Regulars keep red.
		const bool bHomespun = FactionId == TEXT("continental");
		FLinearColor CoatColor = Look.CoatColor, UnderColor = FLinearColor::White;
		if (bHomespun)
		{
			// Actor names change when a man is spawned again. The seed does not.
			const uint32 Seed = bClothingSeedSet ? ClothingSeed : GetTypeHash(GetFName());
			UPA250FactionVisuals::HomespunFor(Seed, CoatColor, UnderColor);
			// Riflemen wore the fringed linen hunting shirt; a Patriot officer a blue coat faced buff.
			if (UnitClass == TEXT("rifleman"))
			{
				CoatColor = FLinearColor(0.42f, 0.36f, 0.24f);
				UnderColor = FLinearColor(0.36f, 0.30f, 0.20f);
			}
			else if (UnitClass == TEXT("officer"))
			{
				CoatColor = FLinearColor(0.05f, 0.08f, 0.24f);
				UnderColor = FLinearColor(0.62f, 0.52f, 0.34f);
			}
			// Enlisted provincials keep their own clothes as well (user request 2026-11-14: not a
			// professional army yet, so nobody matches). Congress chose brown coats only in
			// November 1775 and few were issued before 1776; the uniform brown coat is gone.
		}
		else if (UnitClass == TEXT("officer"))
		{
			CoatColor = FLinearColor(0.72f, 0.05f, 0.04f); // scarlet, not the ranks' madder red
		}
		if (Coat)
		{
			// Slot 1 is the pack's outer clothing; leave skin, eyes, hair and kit untouched.
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Coat, this);
			// The coat material lifts the pack's near-black cloth to about 0.22 grey before this
			// tint (tools/ue/patch_soldier_coat_curve.py), so these boosts set the cloth's albedo:
			// about 0.4 x the colour for homespun, 0.5 x for the regulars' red. With the old
			// 3.5 / 3 (chosen when the cloth was still near black) every coat came out pastel in
			// sunlight: indigo as sky blue, madder red as salmon.
			MID->SetVectorParameterValue(TEXT("CoatColor"), CoatColor * (bHomespun ? 1.8f : 2.2f));
			GetMesh()->SetMaterial(1, MID);
		}
		// Slot 0 is the pack's pants, waistcoat and boots material (M_american_s_armor2skin).
		// Skin, face and hair are separate materials (body, head, eyes) and stay untinted.
		// Only militia change slot 0; regulars keep the pack's white/buff.
		static TWeakObjectPtr<UMaterialInterface> UnderMat;
		if (bHomespun && !UnderMat.IsValid())
			UnderMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_PA250_SoldierUnder.M_PA250_SoldierUnder"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (bHomespun && UnderMat.IsValid())
		{
			UMaterialInstanceDynamic* Under = UMaterialInstanceDynamic::Create(UnderMat.Get(), this);
			Under->SetVectorParameterValue(TEXT("CoatColor"), UnderColor * 2.f);
			GetMesh()->SetMaterial(0, Under);
		}
	}
	ApplyClassLook();
	if (HatMesh && !bUsesPeriodSoldier)
	{
		const bool bRound = Look.HatStyle == TEXT("round");
		HatMesh->SetRelativeScale3D(bRound ? FVector(0.4f, 0.4f, 0.15f) : FVector(0.35f, 0.5f, 0.18f));
		if (UMaterialInstanceDynamic* HatMid = HatMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			HatMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.05f, 0.05f));
		}
		HatMesh->SetVisibility(true);
	}
}

void ARFCharacter::ApplyClassLook()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	auto HideHats = [this]() { for (UStaticMeshComponent* P : ClassHatPieces) if (P) P->SetVisibility(false); };
	if (!MeshComp || !bUsesPeriodSoldier)
	{
		HideHats();
		return;
	}
	const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(FactionId, UnitClass);
	if (BaseMeshScale.IsZero()) BaseMeshScale = MeshComp->GetRelativeScale3D();
	MeshComp->SetRelativeScale3D(BaseMeshScale * Kit.HeightScale);

	// ---- The model's own cocked hat. It is part of the one-piece soldier mesh, but the pack also ships
	// the soldier in separate parts: a class with other headgear is drawn from those parts (all but the
	// hat), following the main mesh's pose, while the main mesh keeps animating unseen. ----
	const bool bOwnHeadgear = Kit.Hat == EPA250Hat::Bearskin || Kit.Hat == EPA250Hat::Cap || Kit.Hat == EPA250Hat::RoundHat;
	if (bOwnHeadgear && BodyParts.Num() == 0 && !bBodyPartsFailed)
	{
		static const TCHAR* const Parts[] = {TEXT("armor1"), TEXT("armor2"), TEXT("bag"), TEXT("belt1"), TEXT("belt2"), TEXT("belt3"),
			TEXT("body"), TEXT("boots"), TEXT("cloth"), TEXT("cloth2"), TEXT("cloth3"), TEXT("hair"), TEXT("head"), TEXT("pants"), TEXT("ring")};
		for (const TCHAR* Part : Parts)
		{
			const FString Path = FString::Printf(TEXT("/Game/american_soldier/base_mesh/separatedmesh/SK_AMERICAN_SOLDIER_%s.SK_AMERICAN_SOLDIER_%s"), Part, Part);
			USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Asset)
			{
				bBodyPartsFailed = true;
				break;
			}
			USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
			C->SetSkeletalMeshAsset(Asset);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCanEverAffectNavigation(false);
			C->SetupAttachment(MeshComp);
			C->RegisterComponent();
			C->SetLeaderPoseComponent(MeshComp);
			BodyParts.Add(C);
		}
		if (bBodyPartsFailed)
		{
			for (USkeletalMeshComponent* C : BodyParts) if (C) C->DestroyComponent();
			BodyParts.Reset();
			UE_LOG(LogTemp, Warning, TEXT("PA250 visuals: soldier part meshes missing; %s keeps the cocked hat"), *GetName());
		}
	}
	const bool bInParts = bOwnHeadgear && BodyParts.Num() > 0;
	MeshComp->SetVisibility(!bInParts, false);
	MeshComp->VisibilityBasedAnimTickOption = bInParts ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones : MeshComp->VisibilityBasedAnimTickOption;
	for (USkeletalMeshComponent* C : BodyParts)
	{
		if (!C) continue;
		C->SetVisibility(bInParts);
		if (!bInParts) continue;
		// The parts' cloth slots take the same tinted materials as the one-piece mesh (0 = smallclothes, 1 = coat).
		const TArray<FName> Slots = C->GetMaterialSlotNames();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FString Slot = Slots[i].ToString();
			if (Slot.Contains(TEXT("armor3"))) C->SetMaterial(i, MeshComp->GetMaterial(0));
			else if (Slot.Contains(TEXT("armor4"))) C->SetMaterial(i, MeshComp->GetMaterial(1));
		}
	}

	// ---- Headgear and feathers: basic shapes placed in the mesh's own space (Z up, cm; the head top is
	// at 184, the cocked hat's crown at 188) and fixed to the head bone's reference pose. ----
	struct FPiece { bool bSphere; float Diameter, Height, Z, X; FLinearColor Color; };
	TArray<FPiece, TInlineAllocator<3>> Pieces;
	const FLinearColor Fur(0.016f, 0.012f, 0.010f), Leather(0.015f, 0.015f, 0.016f), Felt(0.11f, 0.065f, 0.03f);
	switch (bInParts || Kit.Hat == EPA250Hat::CockedHat ? Kit.Hat : EPA250Hat::None)
	{
	case EPA250Hat::Bearskin: // tall fur cap with a rounded top
		Pieces.Add({false, 22.f, 22.f, 187.f, 0.f, Fur});
		Pieces.Add({true, 22.f, 16.f, 198.f, 0.f, Fur});
		break;
	case EPA250Hat::Cap: // low leather cap with a green feather on the left
		Pieces.Add({false, 21.f, 10.f, 180.f, 0.f, Leather});
		Pieces.Add({false, 4.5f, 15.f, 193.f, 7.f, FLinearColor(0.03f, 0.38f, 0.07f)});
		break;
	case EPA250Hat::RoundHat: // broad-brimmed round hat
		Pieces.Add({false, 44.f, 1.6f, 177.f, 0.f, Felt});
		Pieces.Add({false, 20.f, 9.f, 181.5f, 0.f, Felt});
		break;
	case EPA250Hat::CockedHat: // the model's own hat, with an officer's gold feather
		Pieces.Add({false, 4.5f, 17.f, 196.f, 0.f, FLinearColor(0.95f, 0.66f, 0.08f)});
		break;
	default: break;
	}
	const int32 Bone = MeshComp->GetBoneIndex(TEXT("head"));
	const USkeletalMesh* Asset = MeshComp->GetSkeletalMeshAsset();
	if (Pieces.Num() == 0 || Bone == INDEX_NONE || !Asset)
	{
		HideHats();
		return;
	}
	// Reference pose of the head bone in the mesh's own space, walked up the parent chain.
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	FTransform BoneCS = FTransform::Identity;
	for (int32 B = Bone; B != INDEX_NONE; B = Ref.GetParentIndex(B))
	{
		BoneCS = BoneCS * Ref.GetRefBonePose()[B];
	}
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		const FPiece& Pc = Pieces[i];
		if (!ClassHatPieces.IsValidIndex(i))
		{
			UStaticMeshComponent* Hat = NewObject<UStaticMeshComponent>(this);
			Hat->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Hat->SetCanEverAffectNavigation(false);
			Hat->SetCastShadow(true);
			Hat->SetupAttachment(MeshComp, TEXT("head"));
			Hat->RegisterComponent();
			ClassHatPieces.Add(Hat);
		}
		UStaticMeshComponent* Hat = ClassHatPieces[i];
		UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, Pc.bSphere ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		if (!Hat || !Shape) continue;
		Hat->SetStaticMesh(Shape);
		// Basic shapes are 100 cm across and 100 cm tall. The head sits 2 cm behind the mesh origin.
		const FTransform HatCS(FQuat::Identity, FVector(Pc.X, -2.f, Pc.Z), FVector(Pc.Diameter / 100.f, Pc.Diameter / 100.f, Pc.Height / 100.f));
		Hat->SetRelativeTransform(HatCS.GetRelativeTransform(BoneCS));
		// The shape's own default material is the grey grid: tint the plain basic-shape material instead.
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Hat->GetMaterial(0));
		if (!Mid)
		{
			static TWeakObjectPtr<UMaterialInterface> Plain;
			if (!Plain.IsValid()) Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			Mid = Plain.IsValid() ? UMaterialInstanceDynamic::Create(Plain.Get(), this) : nullptr;
			if (Mid) Hat->SetMaterial(0, Mid);
		}
		if (Mid) Mid->SetVectorParameterValue(TEXT("Color"), Pc.Color);
		Hat->SetVisibility(true);
	}
	for (int32 i = Pieces.Num(); i < ClassHatPieces.Num(); ++i)
	{
		if (ClassHatPieces[i]) ClassHatPieces[i]->SetVisibility(false);
	}
}

void ARFCharacter::UpdateNameplate(FName ViewerFaction, bool bSquadmate, float DistanceCm, bool bHasLOS)
{
	if (!Nameplate) return;
	const bool bEnemy = !ViewerFaction.IsNone() && ViewerFaction != FactionId;
	bool bShow = false;
	FLinearColor Color = FLinearColor::White;
	bShow = ShouldShowNameplate(bEnemy, bSquadmate, DistanceCm, bHasLOS);
	Color = bEnemy ? FLinearColor(0.9f, 0.15f, 0.15f) : FLinearColor(0.25f, 0.55f, 1.f);
	Nameplate->SetVisibility(bShow);
	Nameplate->SetTintColorAndOpacity(Color);
}

bool ARFCharacter::ShouldShowNameplate(bool bEnemy, bool bSquadmate, float DistanceCm, bool bHasLOS)
{
	if (bEnemy) return DistanceCm <= 1500.f && bHasLOS;
	return DistanceCm <= (bSquadmate ? 2500.f : 1200.f);
}

FText ARFCharacter::NameplateLabel(FName Faction, FName InUnitClass)
{
	if (Faction == TEXT("british") || Faction == TEXT("continental"))
	{
		return FText::FromString(PA250UnitClasses::KitFor(Faction, InUnitClass).Label);
	}
	if (InUnitClass == TEXT("militia")) return FText::FromString(TEXT("Militia"));
	if (Faction == TEXT("british")) return FText::FromString(TEXT("Regular"));
	if (Faction == TEXT("continental")) return FText::FromString(TEXT("Provincial"));
	return FText::FromName(Faction);
}

float ARFCharacter::ViewStress(float HealthFraction, bool bSuppressed, float InHitPulse)
{
	const float Hurt = 1.f - FMath::Clamp(HealthFraction, 0.f, 1.f);
	return FMath::Clamp(FMath::Max(0.75f * Hurt * Hurt, bSuppressed ? 0.35f : 0.f) + 0.6f * FMath::Clamp(InHitPulse, 0.f, 1.f), 0.f, 1.f);
}

void ARFCharacter::TickFootsteps(float DeltaTime)
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bIsDead || MountedVisual || !Move || !Move->IsMovingOnGround())
	{
		return;
	}
	const float Speed = GetVelocity().Size2D();
	if (Speed < 60.f)
	{
		StrideCm = FMath::Min(StrideCm, 40.f); // the next step comes soon after he sets off
		return;
	}
	StrideCm += Speed * DeltaTime;
	const float Stride = Speed > 500.f ? 105.f : 80.f; // a running stride is longer
	if (StrideCm >= Stride)
	{
		StrideCm = 0.f;
		FPA250BattleAudio::PlayFootstep(this, GetActorLocation() - FVector(0.f, 0.f, 80.f), IsPlayerControlled());
	}
}

void ARFCharacter::TickViewFeedback(float DeltaTime)
{
	HitPulse = FMath::Max(0.f, HitPulse - DeltaTime / 0.5f);
	if (!Camera || !IsPlayerControlled())
	{
		return;
	}
	TickSuppression(DeltaTime);
	const float HealthFraction = HealthComponent && HealthComponent->MaxHealth > 0.f ? HealthComponent->CurrentHealth / HealthComponent->MaxHealth : 1.f;
	const float Want = bIsDead ? 0.f : ViewStress(HealthFraction, SuppressionTimer > 0.f, HitPulse);
	// Snaps in on a hit, eases out as the danger passes.
	ViewStressSmoothed = Want > ViewStressSmoothed ? Want : FMath::FInterpTo(ViewStressSmoothed, Want, DeltaTime, 2.5f);
	// Only the stressed view is overridden; at rest the level's own post-process (PP_Dawn) is untouched.
	// Exposure is never set here.
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	const bool bOn = ViewStressSmoothed > 0.01f;
	PP.bOverride_VignetteIntensity = bOn;
	PP.bOverride_ColorSaturation = bOn;
	PP.bOverride_SceneFringeIntensity = bOn;
	// Suppression blurs the view (intensity.suppression.blur_amount at full): a shallow focus that
	// opens as it falls away. Only while suppressed; otherwise the level's own focus stands.
	const float Blur = FMath::Clamp(SuppressionLevel * PA250BattleFeel::Get().SuppressionBlurAmount, 0.f, 1.f);
	const bool bBlur = !bIsDead && Blur > 0.01f;
	PP.bOverride_DepthOfFieldFocalDistance = bBlur;
	PP.bOverride_DepthOfFieldFstop = bBlur;
	if (bBlur)
	{
		PP.DepthOfFieldFocalDistance = 150.f;
		PP.DepthOfFieldFstop = FMath::Lerp(16.f, 1.2f, Blur);
	}
	if (bOn)
	{
		PP.VignetteIntensity = 0.4f + 0.75f * ViewStressSmoothed;
		const float Sat = 1.f - 0.4f * ViewStressSmoothed;
		PP.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
		PP.SceneFringeIntensity = 3.f * FMath::Clamp(HitPulse, 0.f, 1.f);
	}
	Camera->PostProcessBlendWeight = 1.f;
}

void ARFCharacter::RefreshNameplate()
{
	if (!Nameplate) return;
	UWorld* World = GetWorld();
	if (!World || bIsDead || FactionId.IsNone())
	{
		Nameplate->SetVisibility(false);
		return;
	}
	if (!Nameplate->GetSlateWidget().IsValid())
	{
		Nameplate->SetSlateWidget(
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(FLinearColor::White)
			.Text(NameplateLabel(FactionId, UnitClass)));
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	const AActor* View = PC ? PC->GetViewTarget() : nullptr;
	const ARFCharacter* Viewer = Cast<ARFCharacter>(View);
	if (!Viewer || Viewer == this || Viewer->IsDead())
	{
		Nameplate->SetVisibility(false);
		return;
	}
	if (TSharedPtr<SWidget> Plate = Nameplate->GetSlateWidget())
	{
		StaticCastSharedPtr<STextBlock>(Plate)->SetText(NameplateLabel(FactionId, UnitClass));
	}
	const float Dist = FVector::Dist(GetActorLocation(), Viewer->GetActorLocation());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Nameplate), false, Viewer);
	Params.AddIgnoredActor(this);
	if (Viewer->Musket) Params.AddIgnoredActor(Viewer->Musket);
	FHitResult Hit;
	const FVector From = Viewer->GetActorLocation() + FVector(0.f, 0.f, 70.f);
	const FVector To = GetActorLocation() + FVector(0.f, 0.f, 70.f);
	const bool bLOS = !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
	const bool bSquad = SquadId >= 0 && SquadId == Viewer->SquadId && FactionId == Viewer->FactionId;
	UpdateNameplate(Viewer->FactionId, bSquad, Dist, bLOS);
}

void ARFCharacter::ApplyThirdPartySoldierMesh()
{
	static TWeakObjectPtr<USkeletalMesh> British, Continental;
	// PIE teardown / collection may release the weak cache between battles.
	if (!British.IsValid())
		British = PA250ThirdParty::LoadFirstOf<USkeletalMesh>(PA250ThirdParty::SoldierBritish);
	if (!Continental.IsValid())
		Continental = PA250ThirdParty::LoadFirstOf<USkeletalMesh>(PA250ThirdParty::SoldierContinental);
	USkeletalMesh* Wanted = FactionId == TEXT("british") ? British.Get()
	                      : FactionId == TEXT("continental") ? Continental.Get()
	                                                         : nullptr;
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!Wanted || !MeshComp || MeshComp->GetSkeletalMeshAsset() == Wanted)
	{
		return;
	}
	const bool bPackSoldier = Wanted->GetPathName().StartsWith(TEXT("/Game/american_soldier/"));
	const USkeletalMesh* Current = MeshComp->GetSkeletalMeshAsset();
	if (!bPackSoldier && Current && Current->GetSkeleton() != Wanted->GetSkeleton())
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250: %s skipped (not on the UE5 mannequin skeleton)"), *Wanted->GetPathName());
		return;
	}
	// Remove the incompatible AnimBP before SetSkeletalMesh triggers InitAnim.
	if (bPackSoldier) MeshComp->SetAnimInstanceClass(nullptr);
	MeshComp->SetSkeletalMeshAsset(Wanted);
	MeshComp->EmptyOverrideMaterials();
	bUsesPeriodSoldier = bPackSoldier;
	if (bPackSoldier)
	{
		SoldierIdle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/american_soldier/demo/ThirdPersonIdle.ThirdPersonIdle"));
		SoldierWalk = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/american_soldier/demo/ThirdPersonWalk.ThirdPersonWalk"));
		SoldierRun = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/american_soldier/demo/ThirdPersonRun.ThirdPersonRun"));
		// Native body anim: plays the pack clip and solves the left hand onto the musket during
		// pose evaluation (UPA250SoldierAnimInstance), never from an actor tick.
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		MeshComp->SetAnimInstanceClass(UPA250SoldierAnimInstance::StaticClass());
		UpdateSoldierAnimation();
		HatMesh->SetVisibility(false);
		TeamMarker->SetVisibility(false);
		UE_LOG(LogTemp, Display, TEXT("PA250 visuals: %s uses %s with pack animations"), *GetName(), *Wanted->GetPathName());
	}
}

void ARFCharacter::TickAnimProbe(float DeltaTime)
{
	if (!IsPlayerControlled() || bIsDead) return;
	static const bool bReloadProbe = FString(FCommandLine::Get()).Contains(TEXT("-PA250AnimProbe=reload"));
	if (bReloadProbe)
	{
		// Debug run: the player fires, then loads standing still, filmed from his front.
		if (AnimProbeTime < 0.f) AnimProbeTime = 0.f;
		AnimProbeTime += DeltaTime;
		const float T = AnimProbeTime - 10.f;
		if (T < 0.f) return;
		if (SpringArm)
		{
			SpringArm->bUsePawnControlRotation = false;
			SpringArm->SetUsingAbsoluteRotation(true);
			SpringArm->SetWorldRotation(FRotator(-4.f, GetActorRotation().Yaw + 150.f, 0.f));
		}
		if (T >= 1.f && T - DeltaTime < 1.f) OnFirePressed();
		if (T >= 2.5f && T - DeltaTime < 2.5f) OnReloadStarted();
		// -PA250ProbeKneel: he ducks for the load, so the kneeling drill is what shows.
		if (T >= 2.6f && T - DeltaTime < 2.6f && FParse::Param(FCommandLine::Get(), TEXT("PA250ProbeKneel"))) OnDuckPressed();
		AMusketWeapon* Gun = GetEquippedFirearm();
		// Take each stage of the drill as the active reload offers it (a press at the sweet spot).
		if (Gun && Gun->IsReloading() && T > 3.f && Gun->GetSequencer().GetPhaseProgress() >= 0.5f && AnimProbeLogTimer > 0.2f)
		{
			OnReloadStarted();
		}
		const int32 Wanted = FMath::FloorToInt((T - 3.f) / 2.f);
		if (T >= 3.f && Wanted >= AnimProbeStills && AnimProbeStills < 9)
		{
			FString Tag;
			FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), Tag);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
				FString::Printf(TEXT("reload_%s_%d.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag, AnimProbeStills)));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			++AnimProbeStills;
		}
		AnimProbeLogTimer -= DeltaTime;
		if (AnimProbeLogTimer <= 0.f)
		{
			AnimProbeLogTimer = 1.f;
			const UPA250SoldierAnimInstance* Inst = Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance());
			float LeftCm = -1.f;
			if (Musket && Musket->GetAnimatedMesh())
			{
				LeftCm = FVector::Dist(GetMesh()->GetSocketLocation(TEXT("hand_l")), Musket->GetAnimatedMesh()->GetSocketLocation(TEXT("b_gun_LeftHandSpot")));
			}
			const FVector RightHand = GetActorTransform().InverseTransformPosition(GetMesh()->GetSocketLocation(TEXT("hand_r")));
			const FPA250MeleePose Pose = Inst ? Inst->GetMeleePose() : FPA250MeleePose();
			UE_LOG(LogTemp, Display, TEXT("PA250 reload probe: t=%.1f reloading=%d phase=%d progress=%.2f tier=%s reload_w=%.2f carry_w=%.2f kneel=%.2f ducked=%d left_hand_to_spot_cm=%.1f right_hand_local=(%.0f,%.0f,%.0f) musket_pitch=%.0f drill_clip=%s drill_t=%.2f drill_w=%.2f"),
				T, Gun && Gun->IsReloading() ? 1 : 0, Gun ? static_cast<int32>(Gun->GetSequencer().GetPhase()) : -1, Gun ? Gun->GetSequencer().GetPhaseProgress() : 0.f,
				Inst && Inst->GetLocomotionReport().bShowingFull ? TEXT("full") : TEXT("cheap"), Pose.Reload, Pose.CarryWeight(), Pose.Kneel,
				GetCharacterMovement() && GetCharacterMovement()->IsCrouching() ? 1 : 0, LeftCm, RightHand.X, RightHand.Y, RightHand.Z,
				Musket ? Musket->GetActorRotation().Pitch : 0.f, Inst && Inst->GetUpperBodyClip() ? *Inst->GetUpperBodyClip()->GetName() : TEXT("none"),
				Inst ? Inst->GetUpperBodyTime() : 0.f, Inst ? Inst->GetUpperBodyWeight() : 0.f);
		}
		if (T > 22.f) FPlatformMisc::RequestExit(false);
		return;
	}
	static const bool bAimProbe = FString(FCommandLine::Get()).Contains(TEXT("-PA250AnimProbe=aim"));
	if (bAimProbe)
	{
		// Debug run: stand at the carry, aim level, push the look up as the mouse would, then down.
		if (AnimProbeTime < 0.f) AnimProbeTime = 0.f;
		AnimProbeTime += DeltaTime;
		const float T = AnimProbeTime - 10.f;
		if (T < 0.f) return;
		if (T >= 2.f && T - DeltaTime < 2.f) StartAim();
		if (T >= 3.f && T < 4.5f) AddControllerPitchInput(-0.2f);   // the same call the mouse makes; up
		if (T >= 6.f && T < 8.5f) AddControllerPitchInput(0.2f);    // down
		if (T >= 10.f && T - DeltaTime < 10.f) StopAim();
		if (T >= 10.5f && T < 12.f) AddControllerPitchInput(-0.2f);  // up again, not aiming
		struct FStill { float At; const TCHAR* Name; };
		static const FStill Stills[] = {{1.5f, TEXT("carry")}, {2.8f, TEXT("aim_level")}, {5.2f, TEXT("aim_up")}, {9.2f, TEXT("aim_down")}, {12.6f, TEXT("carry_look_up")}};
		if (AnimProbeStills < UE_ARRAY_COUNT(Stills) && T >= Stills[AnimProbeStills].At)
		{
			FString Tag;
			FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), Tag);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
				FString::Printf(TEXT("aim_%s_%s.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag, Stills[AnimProbeStills].Name)));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			++AnimProbeStills;
		}
		AnimProbeLogTimer -= DeltaTime;
		if (AnimProbeLogTimer <= 0.f)
		{
			AnimProbeLogTimer = 0.5f;
			const UPA250SoldierAnimInstance* Inst = Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance());
			float HandCm = -1.f, RightCm = -1.f;
			if (Musket && Musket->GetAnimatedMesh() && GetMesh())
			{
				HandCm = FVector::Dist(GetMesh()->GetSocketLocation(TEXT("hand_l")), Musket->GetAnimatedMesh()->GetSocketLocation(TEXT("b_gun_LeftHandSpot")));
				RightCm = FVector::Dist(GetMesh()->GetSocketLocation(TEXT("hand_r")), Musket->GetActorLocation());
			}
			UE_LOG(LogTemp, Display, TEXT("PA250 aim probe: t=%.1f aiming=%d control_pitch=%.1f musket_pitch=%.1f tier=%s left_hand_to_spot_cm=%.1f right_hand_to_musket_cm=%.1f"),
				T, bAiming ? 1 : 0, FRotator::NormalizeAxis(GetControlRotation().Pitch), Musket ? Musket->GetActorRotation().Pitch : 0.f,
				Inst && Inst->GetLocomotionReport().bShowingFull ? TEXT("full") : TEXT("cheap"), HandCm, RightCm);
		}
		// -PA250ProbeFront: the camera looks back at the man from his front (the look still turns him).
		if (FParse::Param(FCommandLine::Get(), TEXT("PA250ProbeFront")) && SpringArm)
		{
			SpringArm->bUsePawnControlRotation = false;
			SpringArm->SetUsingAbsoluteRotation(true);
			SpringArm->SetWorldRotation(FRotator(-6.f, GetActorRotation().Yaw + 155.f, 0.f));
		}
		if (T > 13.5f) FPlatformMisc::RequestExit(false);
		return;
	}
	static const bool bVaultProbe = FString(FCommandLine::Get()).Contains(TEXT("-PA250AnimProbe=vault"));
	if (bVaultProbe)
	{
		// Debug run: the player is stood 6 m from a wall (the nearest, or the -PA250ProbeWall=N-th
		// nearest), square to it, and sprints at it; filmed from the side.
		static FVector WallFace = FVector::ZeroVector;
		static float NextStill = -1.f;
		if (AnimProbeTime < 0.f) AnimProbeTime = 0.f;
		AnimProbeTime += DeltaTime;
		const float T = AnimProbeTime - 10.f;
		if (T < 0.f) return;
		if (T - DeltaTime < 0.f)
		{
			int32 Skip = 0;
			FParse::Value(FCommandLine::Get(), TEXT("PA250ProbeWall="), Skip);
			TArray<AActor*> Walls;
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(TEXT("PA250Cover"))) Walls.Add(*It);
			}
			const FVector Me = GetActorLocation();
			Walls.Sort([Me](const AActor& A, const AActor& B) { return FVector::DistSquared(A.GetActorLocation(), Me) < FVector::DistSquared(B.GetActorLocation(), Me); });
			AnimProbeForward = GetActorForwardVector().GetSafeNormal2D();
			if (Walls.IsValidIndex(Skip))
			{
				const AActor* Wall = Walls[Skip];
				FVector Origin, Extent;
				Wall->GetActorBounds(true, Origin, Extent);
				FVector Axis = Extent.X >= Extent.Y ? FVector::XAxisVector : FVector::YAxisVector;
				float Depth = 2.f * static_cast<float>(FMath::Min(Extent.X, Extent.Y));
				if (const UStaticMeshComponent* WallMesh = Cast<UStaticMeshComponent>(Wall->GetRootComponent()); WallMesh && WallMesh->GetStaticMesh())
				{
					const FVector Size = WallMesh->GetStaticMesh()->GetBoundingBox().GetSize() * WallMesh->GetComponentScale().GetAbs();
					Axis = Size.X >= Size.Y ? WallMesh->GetComponentQuat().GetAxisX() : WallMesh->GetComponentQuat().GetAxisY();
					Depth = static_cast<float>(FMath::Min(Size.X, Size.Y));
					Origin = WallMesh->Bounds.Origin;
				}
				FVector Normal = FVector(-Axis.Y, Axis.X, 0.f).GetSafeNormal2D();
				if (FVector::DotProduct(Normal, Me - Origin) < 0.f) Normal = -Normal;
				WallFace = Origin + Normal * (0.5f * Depth);
				FVector Start = WallFace + Normal * 600.f;
				FHitResult Ground;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250VaultProbe), false, this);
				if (GetWorld()->LineTraceSingleByChannel(Ground, Start + FVector(0.f, 0.f, 400.f), Start - FVector(0.f, 0.f, 800.f), ECC_Visibility, Params))
				{
					Start.Z = Ground.ImpactPoint.Z + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
				}
				SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
				AnimProbeForward = -Normal;
				// A fixed camera beside the wall, looking along it at the place he crosses.
				const FVector Eye = WallFace + Axis.GetSafeNormal2D() * 520.f + Normal * 120.f + FVector(0.f, 0.f, 170.f);
				if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Eye, (WallFace + FVector(0.f, 0.f, 60.f) - Eye).Rotation()))
				{
					if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->SetViewTarget(Cam);
				}
				UE_LOG(LogTemp, Display, TEXT("PA250 vault probe: wall %s (%d of %d), bounds %.0f x %.0f x %.0f cm, start 600 cm from its face"), *Wall->GetName(), Skip,
					Walls.Num(), 2.f * Extent.X, 2.f * Extent.Y, 2.f * Extent.Z);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("PA250 vault probe: no wall %d (the level has %d)"), Skip, Walls.Num());
			}
		}
		if (AController* C = GetController()) C->SetControlRotation(AnimProbeForward.Rotation());
		const float Off = static_cast<float>(FVector::DotProduct(WallFace - GetActorLocation(), AnimProbeForward));
		// He runs at the wall, and halts 3 m beyond it (he used to run on into whatever stood behind).
		if (T >= 1.f && Off > -300.f)
		{
			if (!bSprinting) StartSprint();
			AddMovementInput(AnimProbeForward, 1.f);
		}
		else if (bSprinting)
		{
			StopSprint();
		}
		const bool bOver = Traversal && Traversal->IsTraversing();
		static const bool bStills = FParse::Param(FCommandLine::Get(), TEXT("PA250ProbeStills"));
		if (bStills && NextStill < 0.f && Off < 320.f)
		{
			// Slow motion from here: a screenshot stalls a frame, and at full speed that frame would carry him through the wall.
			NextStill = T;
			GetWorldSettings()->SetTimeDilation(0.12f);
		}
		if (NextStill >= 0.f && T >= NextStill && AnimProbeStills < 9)
		{
			FString Tag;
			FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), Tag);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
				FString::Printf(TEXT("vault_%s_%d.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag, AnimProbeStills)));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			++AnimProbeStills;
			NextStill = T + 0.17f;
		}
		AnimProbeLogTimer -= DeltaTime;
		if (AnimProbeLogTimer <= 0.f)
		{
			const UPA250SoldierAnimInstance* Inst = Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance());
			AnimProbeLogTimer = bOver || (Inst && Inst->GetTraversalWeight() > 0.f) ? 0.f : 0.1f;
			UE_LOG(LogTemp, Display, TEXT("PA250 vault probe: t=%.2f to_face_cm=%.0f feet_z=%.0f speed=%.0f mode=%d over=%d action=%s last=%s clip_t=%.2f weight=%.2f yaw_off=%.0f"), T, Off,
				GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), GetVelocity().Size2D(), static_cast<int32>(GetCharacterMovement()->MovementMode),
				bOver ? 1 : 0, Traversal ? PA250BattleFeel::TraversalName(Traversal->GetAction()) : TEXT("-"),
				Traversal ? PA250TraversalStartName(Traversal->GetLastResult()) : TEXT("-"), Inst ? Inst->GetTraversalTime() : 0.f, Inst ? Inst->GetTraversalWeight() : 0.f,
				FMath::FindDeltaAngleDegrees(AnimProbeForward.Rotation().Yaw, GetActorRotation().Yaw));
		}
		if (T > (bStills ? 22.f : 12.f))
		{
			int32 LinkCrossings = 0, LinkRefused = 0, LinkHeld = 0, LinkCount = 0;
			if (const UPA250TraversalLinks* TraversalLinks = GetWorld()->GetSubsystem<UPA250TraversalLinks>())
			{
				TraversalLinks->Totals(LinkCrossings, LinkRefused, LinkHeld);
				LinkCount = TraversalLinks->Num();
			}
			UE_LOG(LogTemp, Display, TEXT("PA250 vault probe: done, %d crossings, %d stills; bots: %d links, %d crossings, %d refused, %d held back"),
				Traversal ? Traversal->GetStartedCount() : 0, AnimProbeStills, LinkCount, LinkCrossings, LinkRefused, LinkHeld);
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	static const bool bProbe = FString(FCommandLine::Get()).Contains(TEXT("-PA250AnimProbe=move"));
	if (!bProbe) return;
	if (bProbe && FParse::Param(FCommandLine::Get(), TEXT("PA250ProbeFront")) && SpringArm)
	{
		// Look back at the man and the squad behind him from his front.
		SpringArm->bUsePawnControlRotation = false;
		SpringArm->SetUsingAbsoluteRotation(true);
		SpringArm->SetWorldRotation(FRotator(-6.f, AnimProbeForward.Rotation().Yaw + 150.f, 0.f));
	}
	if (AnimProbeTime < 0.f)
	{
		AnimProbeTime = 0.f;
		AnimProbeForward = GetActorForwardVector().GetSafeNormal2D();
	}
	AnimProbeTime += DeltaTime;
	// Seconds after the script starts (it waits 10 s for the level to settle).
	const float T = AnimProbeTime - 10.f;
	if (T < 0.f) return;
	// Pace: 0 walk (WalkSpeed), 1 run (midway in the run band), 2 sprint. Input scale does not
	// change top speed, so each leg sets MaxWalkSpeed itself.
	struct FLeg { float From, To, Scale; int32 Pace; };
	static const FLeg Legs[] = {
		{0.f, 3.f, 1.f, 0},      // walk forward
		{4.5f, 7.5f, -1.f, 1},   // turn about and run back
		{8.5f, 12.f, 1.f, 2},    // sprint forward, the squad following
	};
	float Scale = 0.f;
	int32 Pace = 0;
	for (const FLeg& L : Legs)
	{
		if (T >= L.From && T < L.To)
		{
			Scale = L.Scale;
			Pace = L.Pace;
		}
	}
	const bool bWantSprint = Pace == 2;
	if (bWantSprint != bSprinting)
	{
		if (bWantSprint) StartSprint();
		else StopSprint();
	}
	if (!bWantSprint && GetCharacterMovement())
	{
		const FPA250BattleFeel& Bands = PA250BattleFeel::Get();
		const float RunPace = (Bands.WalkMaxCmS + Bands.RunMaxCmS) * 0.5f;
		GetCharacterMovement()->MaxWalkSpeed = (Pace == 1 && RunPace > WalkSpeed) ? RunPace : WalkSpeed;
	}
	if (Scale != 0.f) AddMovementInput(AnimProbeForward, Scale);
	// The squad follows from the start, so the walk shows men at the shoulder and the sprint men at the trail.
	if (T >= 0.f && T - DeltaTime < 0.f)
	{
		if (APA250BattleGameMode* GM = GetWorld()->GetAuthGameMode<APA250BattleGameMode>()) GM->GivePlayerOrder(this, 0);
	}
	struct FStill { float At; const TCHAR* Name; };
	static const FStill Stills[] = {{1.5f, TEXT("walk")}, {3.35f, TEXT("stop")}, {4.95f, TEXT("pivot")}, {6.5f, TEXT("run")},
		{10.5f, TEXT("sprint_squad")}, {12.5f, TEXT("halt")}};
	if (AnimProbeStills < UE_ARRAY_COUNT(Stills) && T >= Stills[AnimProbeStills].At)
	{
		FString Tag;
		FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), Tag);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
			FString::Printf(TEXT("anim_%s_%s.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag, Stills[AnimProbeStills].Name)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTemp, Display, TEXT("PA250 anim probe: still %s at %.1f s"), *Path, T);
		++AnimProbeStills;
	}
	AnimProbeLogTimer -= DeltaTime;
	if (AnimProbeLogTimer <= 0.f)
	{
		AnimProbeLogTimer = 0.5f;
		const float Speed = GetVelocity().Size2D();
		const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
		float HandCm = -1.f;
		if (Musket && Musket->GetAnimatedMesh() && GetMesh())
		{
			HandCm = FVector::Dist(GetMesh()->GetSocketLocation(TEXT("hand_l")), Musket->GetAnimatedMesh()->GetSocketLocation(TEXT("b_gun_LeftHandSpot")));
		}
		const UPA250SoldierAnimInstance* Inst = Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance());
		UE_LOG(LogTemp, Display, TEXT("PA250 anim probe: t=%.1f speed=%.0f gait=%s tier=%s clip=%s searches=%d jumps=%d left_hand_to_spot_cm=%.1f"), T, Speed,
			PA250BattleFeel::GaitName(PA250BattleFeel::PickGait(Speed, Feel.IdleMaxCmS, Feel.WalkMaxCmS, Feel.RunMaxCmS)),
			Inst && Inst->GetLocomotionReport().bShowingFull ? TEXT("full") : TEXT("cheap"),
			Inst ? *Inst->GetLocomotionReport().Clip.ToString() : TEXT("-"),
			Inst ? Inst->GetLocomotionReport().Searches : 0, Inst ? Inst->GetLocomotionReport().Jumps : 0, HandCm);
	}
	if (T > 17.f)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 anim probe: done, %d stills"), AnimProbeStills);
		FPlatformMisc::RequestExit(false);
	}
}

void ARFCharacter::UpdateSoldierAnimation()
{
	if (!bUsesPeriodSoldier || bIsDead) return;
	// A lunge is carried by the melee pose layer (feet placed by IK), not by the run cycle.
	const float Speed = MeleeComponent && MeleeComponent->IsPlantingFeet() ? 0.f : GetVelocity().Size2D();
	UAnimSequence* Animation = Speed > 280.f ? SoldierRun.Get() : Speed > 5.f ? SoldierWalk.Get() : SoldierIdle.Get();
	float PlayRate = 1.f;
	if (Speed > 280.f)
	{
		PlayRate = FMath::Clamp(Speed / 380.f, 0.85f, 1.35f);
	}
	else if (Speed > 5.f)
	{
		PlayRate = FMath::Clamp(Speed / 220.f, 0.7f, 1.25f);
	}
	if (UPA250SoldierAnimInstance* Instance = Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		if (Animation && Instance->GetLocomotion() != Animation) Instance->SetLocomotion(Animation);
		Instance->SetPlayRate(PlayRate);
		// Full quality (battle_feel.json animation): motion matching over the databases this gait asks
		// for. A soldier easing to a halt searches the stops. Mounted men and a lunge stay on the cheap path.
		const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
		const bool bFull = bAnimFullQuality && !IsMounted() && !(MeleeComponent && MeleeComponent->IsPlantingFeet()) && PA250AnimClips::LocomotionReady();
		if (bFull)
		{
			EPA250FootGait Gait = PA250BattleFeel::PickGait(Speed, Feel.IdleMaxCmS, Feel.WalkMaxCmS, Feel.RunMaxCmS);
			bool bStopping = Gait != EPA250FootGait::Idle && GetCharacterMovement() && GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero(1.0);
			// He is at a stand before his stop clip is done (braking takes a fifth of a second), so for
			// stop_settle_seconds he keeps the stops of the gait he halted from. After that only the
			// standing clips are searched, and the node leaves any clip that is not one of them.
			const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			if (Gait != EPA250FootGait::Idle)
			{
				LastMovingGait = static_cast<uint8>(Gait);
				LastMovingAt = Now;
			}
			else if (LastMovingGait != 0 && Now - LastMovingAt < Feel.StopSettleSeconds)
			{
				Gait = static_cast<EPA250FootGait>(LastMovingGait);
				bStopping = true;
			}
			Instance->SetFullQuality(true, PA250AnimClips::LocomotionDatabases(Gait, bStopping), IsPlayerControlled() ? 0.f : Feel.BotSearchThrottleSeconds);
		}
		else
		{
			Instance->SetFullQuality(false, TArray<UPoseSearchDatabase*>(), 0.f);
		}
	}
}

void ARFCharacter::ApplyTeamMarker()
{
	ApplyThirdPartySoldierMesh();
	if (!TeamMarker || !TeamMarker->GetStaticMesh())
	{
		return;
	}
	const FLinearColor Color = FactionId == TEXT("british") ? FLinearColor(0.8f, 0.05f, 0.05f)
	                         : FactionId == TEXT("continental") ? FLinearColor(0.1f, 0.2f, 0.85f)
	                                                            : FLinearColor(0.5f, 0.5f, 0.5f);
	if (UMaterialInstanceDynamic* MID = TeamMarker->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void ARFCharacter::BeginPlay()
{
	Super::BeginPlay();
	LoadLookSensitivity();
	// -PA250FirstPerson: start with the camera at his eyes (for stills; Z toggles it in play).
	if (FParse::Param(FCommandLine::Get(), TEXT("PA250FirstPerson"))) bFirstPerson = true;

	CurrentFOV = TargetFOV = DefaultFOV;
	CurrentArmLength = HipArmLength;
	CurrentSocketOffset = HipSocketOffset;
	Camera->SetFieldOfView(DefaultFOV);
	if (SpringArm)
	{
		SpringArm->TargetArmLength = HipArmLength;
		SpringArm->SocketOffset = HipSocketOffset;
	}
	ApplyTeamMarker();
	ApplyFactionVisuals();

	HealthComponent->OnDied.AddDynamic(this, &ARFCharacter::OnDeath);
	HealthComponent->OnWounded.AddUObject(this, &ARFCharacter::OnWounded);

	if (!WeaponTuning)
	{
		WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
	}

	if (HasAuthority())
	{
		SpawnMusket();
	}
	if (ARFGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ARFGameMode>() : nullptr)
	{
		GM->RegisterCharacter(this);
	}
}

void ARFCharacter::Destroyed()
{
	if (Musket)
	{
		Musket->Destroy();
		Musket = nullptr;
	}
	Super::Destroyed();
}

void ARFCharacter::SpawnMusket()
{
	if (!MusketClass || Musket)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Musket = GetWorld()->SpawnActor<AMusketWeapon>(MusketClass, GetActorTransform(), Params);
	if (!Musket)
	{
		return;
	}
	if (WeaponTuning)
	{
		Musket->WeaponTuning = WeaponTuning;
		Musket->ApplyWeaponTuning();
	}
	const FName HandSocket(TEXT("hand_r"));
	if (GetMesh() && GetMesh()->DoesSocketExist(HandSocket))
	{
		Musket->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HandSocket);
		Musket->SetActorRelativeRotation(FRotator(0.f, 0.f, 0.f));
	}
	else
	{
		Musket->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Musket->SetActorRelativeLocation(FVector(40.f, 25.f, 20.f));
	}
}

void ARFCharacter::SetServingGun(bool bOn)
{
	if (bServingGun == bOn) return;
	bServingGun = bOn;
	if (bOn && !Spyglass && GetMesh())
	{
		// SM_Spyglass (tools/anim/make_props.py): a brass draw telescope 60 cm drawn out, eyepiece at
		// -X, its origin 8 cm from the eyepiece where the right hand holds it.
		UStaticMesh* Glass = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/PA250/Props/GunTools/SM_Spyglass.SM_Spyglass"));
		if (!Glass)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 spyglass: /Game/PA250/Props/GunTools/SM_Spyglass is missing; the gunner has no glass"));
		}
		else
		{
			Spyglass = NewObject<UStaticMeshComponent>(this, TEXT("PA250Spyglass"));
			Spyglass->SetStaticMesh(Glass);
			Spyglass->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Spyglass->SetCastShadow(true);
			// On the hand bone, so it moves with the hand in the pose that is drawn; only its lie in the
			// hand is worked out from the last pose (TickSpyglass).
			Spyglass->SetupAttachment(GetMesh(), TEXT("hand_r"));
			Spyglass->RegisterComponent();
		}
	}
	if (Spyglass) Spyglass->SetVisibility(bOn);
	if (!bOn) SpyglassRaise = 0.f;
	UE_LOG(LogTemp, Display, TEXT("PA250 spyglass: %s %s"), *GetName(), bOn ? TEXT("takes the glass at the gun") : TEXT("puts the glass away"));
}

float ARFCharacter::SpyglassFovAt(int32 Step, int32 Steps, float Wide, float Narrow)
{
	if (Wide <= 0.f) return 0.f;
	if (Steps <= 0 || Narrow <= 0.f || Narrow >= Wide) return Wide;
	const float T = float(FMath::Clamp(Step, 0, Steps)) / float(Steps);
	return Wide * FMath::Pow(Narrow / Wide, T);
}

float ARFCharacter::GetSpyglassFov() const
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	// It opens where a man aiming his musket or rifle sees (user, 2026-11-18), and zooms in from there.
	return SpyglassFovAt(SpyglassZoom, FMath::RoundToInt(Feel.SpyglassZoomSteps), AimedFOV, Feel.SpyglassFovNarrowDeg);
}

void ARFCharacter::ZoomSpyglass(int32 Steps)
{
	if (!IsLookingThroughGlass() || Steps == 0) return;
	const int32 Max = FMath::Max(0, FMath::RoundToInt(PA250BattleFeel::Get().SpyglassZoomSteps));
	const int32 Was = SpyglassZoom;
	SpyglassZoom = FMath::Clamp(SpyglassZoom + Steps, 0, Max);
	if (SpyglassZoom != Was) UE_LOG(LogTemp, Display, TEXT("PA250 spyglass: zoom %d of %d (fov %.1f)"), SpyglassZoom, Max, GetSpyglassFov());
}

bool ARFCharacter::IsLookingThroughGlass() const
{
	if (!bServingGun || SpyglassRaise < 0.999f || !bAiming || MountedVisual) return false;
	const APlayerController* PC = Cast<APlayerController>(Controller);
	return PC && PC->GetViewTarget() == this;
}

void ARFCharacter::TickSpyglass(float DeltaTime)
{
	if (!bServingGun) return;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const float Rate = Feel.SpyglassRaiseSeconds > KINDA_SMALL_NUMBER ? DeltaTime / Feel.SpyglassRaiseSeconds : 1.f;
	const bool bWasThrough = IsLookingThroughGlass();
	SpyglassRaise = FMath::Clamp(SpyglassRaise + (bAiming && !IsDead() ? Rate : -Rate), 0.f, 1.f);
	const bool bThrough = IsLookingThroughGlass();
	if (bThrough != bWasThrough)
	{
		SpyglassZoom = 0; // it opens wide every time
		UE_LOG(LogTemp, Display, TEXT("PA250 spyglass: %s (fov %.0f)"), bThrough ? TEXT("looking through the glass") : TEXT("glass lowered from the eye"), GetSpyglassFov());
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (!Spyglass || !Body) return;
	// Seen from his own eye it would fill the lens: the round field stands for it then.
	Spyglass->SetVisibility(!bThrough);
	if (Body->GetBoneIndex(TEXT("hand_r")) == INDEX_NONE || Body->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE) return;
	// Read the hands (last frame's pose); never write bones here.
	const FVector HandR = Body->GetBoneLocation(TEXT("hand_r"), EBoneSpaces::WorldSpace);
	const FVector HandL = Body->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace);
	FVector Grip = HandR;
	if (Body->GetBoneIndex(TEXT("middle_01_r")) != INDEX_NONE)
	{
		const FVector Palm = 0.6f * (Body->GetBoneLocation(TEXT("middle_01_r"), EBoneSpaces::WorldSpace) - HandR);
		if (!Palm.ContainsNaN() && Palm.Size() < 12.f) Grip += Palm;
	}
	// Raised, it lies along the hands (right hand at the eyepiece, left further out), as the clip and
	// the review reels hold it; lowered, it hangs closed from his right hand.
	// How far up his right hand has come (hip ~95 cm, eye ~158 cm above his feet) turns the glass from
	// hanging to lying along his hands, so a hand on its way to the eye never carries it upright.
	const FVector Along = HandL - Grip;
	const float Span = Along.Size();
	const float HandUp = Body->GetComponentTransform().InverseTransformPosition(HandR).Z;
	const float Up = FMath::SmoothStep(115.f, 150.f, HandUp);
	const FVector Hang = (-FVector::UpVector + GetActorForwardVector() * 0.25f).GetSafeNormal();
	// Up but the left hand not yet on it: it points ahead from the right hand, the way it will lie.
	const FVector Raised = Span > 12.f && Span < 45.f ? Along / Span : GetActorForwardVector();
	const FVector Dir = Up > 0.f ? FMath::Lerp(Hang, Raised, Up).GetSafeNormal() : Hang;
	// Lowered, its draws are pushed home (half length). Set in the hand's frame of the same pose.
	const FTransform Want(FRotationMatrix::MakeFromXZ(Dir.IsNearlyZero() ? Hang : Dir, FVector::UpVector).Rotator(), Grip, FVector(FMath::Lerp(0.5f, 1.f, Up), 1.f, 1.f));
	Spyglass->SetRelativeTransform(Want.GetRelativeTransform(Body->GetSocketTransform(TEXT("hand_r"), RTS_World)));
}

void ARFCharacter::ApplyCameraPose(float DeltaTime)
{
	if (!SpringArm || !Camera) return;
	const float Y = ShoulderSign * (bAiming ? AdsSocketOffset.Y : HipSocketOffset.Y);
	// First person (on foot): no boom, the camera a hand in front of his eyes so his own head and hat
	// are behind it and his musket is before it.
	// Through the gunner's glass the view is his eye's, narrowed (battle_feel spyglass).
	const bool bGlass = IsLookingThroughGlass();
	const bool bEyes = (bFirstPerson || bGlass) && !MountedVisual;
	const FVector TargetSocket = bEyes ? FirstPersonOffset() + FVector(bGlass ? PA250BattleFeel::Get().SpyglassEyeForwardCm : 0.f, 0.f, 0.f) : FVector(0.f, Y, MountedVisual ? 175.f : (bAiming ? AdsSocketOffset.Z : HipSocketOffset.Z));
	const float TargetArm = bEyes ? 0.f : (MountedVisual ? (bAiming ? 330.f : 550.f) : (bAiming ? AdsArmLength : HipArmLength));
	const float Alpha = CameraBlendSeconds > KINDA_SMALL_NUMBER
		? FMath::Clamp(DeltaTime / CameraBlendSeconds, 0.f, 1.f)
		: 1.f;
	// The glass is at his eye or not: no slide from the shoulder to the lens.
	CurrentArmLength = bGlass ? TargetArm : FMath::Lerp(CurrentArmLength, TargetArm, Alpha);
	CurrentSocketOffset = bGlass ? TargetSocket : FMath::Lerp(CurrentSocketOffset, TargetSocket, Alpha);
	const float GlassFOV = GetSpyglassFov();
	const float WantedFOV = bGlass && GlassFOV > 0.f ? GlassFOV
		: TargetFOV + (MeleeComponent && MeleeComponent->IsCharging() ? ChargeFOVBoost : 0.f);
	// The glass comes to the eye at once (a lerp from 55 reads as a slow zoom, not a lens); once
	// there, the wheel's steps ease in and out.
	CurrentFOV = bGlass && !bGlassView ? WantedFOV : FMath::Lerp(CurrentFOV, WantedFOV, bGlass ? FMath::Clamp(DeltaTime * 12.f, 0.f, 1.f) : Alpha);
	bGlassView = bGlass;
	SpringArm->TargetArmLength = CurrentArmLength;
	SpringArm->SocketOffset = CurrentSocketOffset;
	Camera->SetFieldOfView(CurrentFOV);
}

void ARFCharacter::UpdateFacingMode()
{
	// Over an obstacle his facing is the crossing's (the warp turns him square to it).
	if (IsOverObstacle()) return;
	// Bots face their focus (enemy) while engaging; otherwise they turn into their movement.
	const AAIController* AI = Cast<AAIController>(Controller);
	const bool bBotFocused = AI && (AI->GetFocusActor() != nullptr || FAISystem::IsValidLocation(AI->GetFocalPoint()));
	const bool bFaceYaw = (MountedVisual && MountedVisual->HasFirearm()) || bAiming || FaceControlYawTimer > 0.f || bBotFocused
		// A guard only covers his front, so guarding squares him to where the player is looking.
		|| (IsPlayerControlled() && MeleeComponent && MeleeComponent->IsGuarding());
	bUseControllerRotationYaw = bFaceYaw;
	GetCharacterMovement()->bOrientRotationToMovement = !bFaceYaw;
}

void ARFCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickMounted(DeltaTime); // gait clip + play rate from ground speed (Mounted/PA250MountedRiding.cpp)
	TickThrown(DeltaTime);  // thrown from a fallen horse: loose body, then back on his feet
	TickAnimProbe(DeltaTime);
	UpdateSoldierAnimation();
	if (SuppressionTimer > 0.f)
	{
		SuppressionTimer = FMath::Max(0.f, SuppressionTimer - DeltaTime);
		if (SuppressionTimer <= 0.f) SuppressionSwayDegrees = 0.f;
	}
	if (FaceControlYawTimer > 0.f)
	{
		FaceControlYawTimer = FMath::Max(0.f, FaceControlYawTimer - DeltaTime);
	}
	TickMovementLock(DeltaTime);
	TickHeroArms(DeltaTime);
	if (!MountedVisual && IsPlayerControlled() != bPlayerFeelApplied) ApplyMovementFeel(IsPlayerControlled());
	TickInputBuffer();
	if (Musket) Musket->SetReloadPaused(IsReloadBlockedByMovement());
	TickFootsteps(DeltaTime);
	TickFireOverStance();
	TickViewFeedback(DeltaTime);
	SensitivityToastTimer = FMath::Max(0.f, SensitivityToastTimer - DeltaTime);
	ReloadFeedbackTimer = FMath::Max(0.f, ReloadFeedbackTimer - DeltaTime);
	TickSpyglass(DeltaTime);
	ApplyCameraPose(DeltaTime);
	UpdateFacingMode();

	if (RecoilPitch != 0.f || RecoilYaw != 0.f)
	{
		const float Recover = RecoilRecovery > KINDA_SMALL_NUMBER ? DeltaTime / RecoilRecovery : 1.f;
		const float PitchStep = FMath::Lerp(RecoilPitch, 0.f, Recover);
		const float YawStep = FMath::Lerp(RecoilYaw, 0.f, Recover);
		// Undo the kick: opposite sign to ApplyCameraKick.
		AddControllerPitchInput(RecoilPitch - PitchStep);
		AddControllerYawInput(-(RecoilYaw - YawStep));
		RecoilPitch = PitchStep;
		RecoilYaw = YawStep;
	}

	if (IsPlayerControlled() && AimComponent && WeaponTuning)
	{
		float Spread = WeaponTuning->Values.CurrentSpreadDegrees(bAiming, GetVelocity().Size2D()) * FormationAccuracyScale
			* OfficerAccuracyScale * (Musket ? Musket->ClassSpreadScale : 1.f);
		if (SuppressionTimer > 0.f) Spread += SuppressionSwayDegrees; // FireMusketAt adds this cone too
		AimComponent->ResolveFromOwner(Spread, WeaponTuning->Values.MaxRangeMeters * 100.f);
	}
	RefreshNameplate();
}

void ARFCharacter::NotifyFired()
{
	FaceControlYawTimer = FaceControlYawHoldSeconds;
	if (bHeroArms && GetWorld())
	{
		// The pistol stays in his hand a moment after the shot, and kicks.
		HeroPistolOutUntil = GetWorld()->GetTimeSeconds() + 1.2f;
		HeroRecoilAlpha = 1.f;
	}
	if (WeaponTuning)
	{
		ApplyCameraKick(WeaponTuning->Values.RecoilPitchDegrees, WeaponTuning->Values.RecoilYawDegrees);
		RecoilRecovery = WeaponTuning->Values.RecoilRecoverySeconds;
	}
	// The shot jolts the view open for an instant; the camera blend eases it back.
	if (IsPlayerControlled()) PunchFOV(bAiming ? 3.5f : 2.5f);
}

void ARFCharacter::ApplyCameraKick(float PitchDeg, float YawDeg)
{
	// Kick and recovery must use the same yaw sample, or each shot leaves a random net drift.
	const float Yaw = FMath::FRandRange(-YawDeg, YawDeg);
	RecoilPitch += PitchDeg;
	RecoilYaw += Yaw;
	AddControllerPitchInput(-PitchDeg);
	AddControllerYawInput(Yaw);
}

void ARFCharacter::BuildDefaultInput()
{
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) {
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	if (IMC_ThirdPerson && IA_Move && IA_Look && IA_Fire)
	{
		if (!IA_Reload) { IA_Reload = MakeAction(TEXT("IA_RF_Reload"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_Reload, EKeys::R); }
		if (!IA_ShoulderSwap) { IA_ShoulderSwap = MakeAction(TEXT("IA_RF_Shoulder"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_ShoulderSwap, EKeys::V); }
		if (!IA_Melee) { IA_Melee = MakeAction(TEXT("IA_RF_Melee"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_Melee, EKeys::E); }
		if (!IA_ButtStroke) { IA_ButtStroke = MakeAction(TEXT("IA_RF_Butt"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_ButtStroke, EKeys::Q); }
		if (!IA_Crouch) { IA_Crouch = MakeAction(TEXT("IA_RF_Crouch"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_Crouch, EKeys::LeftControl); }
		if (!IA_Charge) { IA_Charge = MakeAction(TEXT("IA_RF_Charge"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_Charge, EKeys::G); }
		if (!IA_SensUp) { IA_SensUp = MakeAction(TEXT("IA_RF_SensUp"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_SensUp, EKeys::RightBracket); }
		if (!IA_SensDown) { IA_SensDown = MakeAction(TEXT("IA_RF_SensDown"), EInputActionValueType::Boolean); IMC_ThirdPerson->MapKey(IA_SensDown, EKeys::LeftBracket); }
		return;
	}
	IA_Move = MakeAction(TEXT("IA_RF_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(TEXT("IA_RF_Look"), EInputActionValueType::Axis2D);
	IA_Jump = MakeAction(TEXT("IA_RF_Jump"), EInputActionValueType::Boolean);
	IA_Sprint = MakeAction(TEXT("IA_RF_Sprint"), EInputActionValueType::Boolean);
	IA_Aim = MakeAction(TEXT("IA_RF_Aim"), EInputActionValueType::Boolean);
	IA_Fire = MakeAction(TEXT("IA_RF_Fire"), EInputActionValueType::Boolean);
	IA_Reload = MakeAction(TEXT("IA_RF_Reload"), EInputActionValueType::Boolean);
	IA_ShoulderSwap = MakeAction(TEXT("IA_RF_Shoulder"), EInputActionValueType::Boolean);
	IA_Melee = MakeAction(TEXT("IA_RF_Melee"), EInputActionValueType::Boolean);
	IA_ButtStroke = MakeAction(TEXT("IA_RF_Butt"), EInputActionValueType::Boolean);
	IA_SensUp = MakeAction(TEXT("IA_RF_SensUp"), EInputActionValueType::Boolean);
	IA_Charge = MakeAction(TEXT("IA_RF_Charge"), EInputActionValueType::Boolean);
	IA_Crouch = MakeAction(TEXT("IA_RF_Crouch"), EInputActionValueType::Boolean);
	IA_SensDown = MakeAction(TEXT("IA_RF_SensDown"), EInputActionValueType::Boolean);

	IMC_ThirdPerson = NewObject<UInputMappingContext>(this, TEXT("IMC_RF_Default"));
	auto Swizzle = [this]() {
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this);
		S->Order = EInputAxisSwizzle::YXZ;
		return S;
	};
	auto Negate = [this](bool bX, bool bY) {
		UInputModifierNegate* N = NewObject<UInputModifierNegate>(this);
		N->bX = bX; N->bY = bY; N->bZ = false;
		return N;
	};
	auto Map = [this](UInputAction* A, const FKey& Key) -> FEnhancedActionKeyMapping& { return IMC_ThirdPerson->MapKey(A, Key); };

	Map(IA_Move, EKeys::W).Modifiers.Add(Swizzle());
	{
		FEnhancedActionKeyMapping& S = Map(IA_Move, EKeys::S);
		S.Modifiers.Add(Swizzle());
		// After the swizzle the key's value is in Y, so Y is the axis to negate. (It negated X until
		// 2026-11-17, which did nothing: S pushed him forward like W.)
		S.Modifiers.Add(Negate(false, true));
	}
	Map(IA_Move, EKeys::A).Modifiers.Add(Negate(true, false));
	Map(IA_Move, EKeys::D);
	Map(IA_Look, EKeys::Mouse2D).Modifiers.Add(Negate(false, true));
	Map(IA_Jump, EKeys::SpaceBar);
	Map(IA_Sprint, EKeys::LeftShift);
	Map(IA_Aim, EKeys::RightMouseButton);
	Map(IA_Aim, EKeys::C);
	Map(IA_Aim, EKeys::LeftAlt);
	Map(IA_Fire, EKeys::LeftMouseButton);
	Map(IA_Fire, EKeys::F);
	Map(IA_Reload, EKeys::R);
	Map(IA_ShoulderSwap, EKeys::V);
	Map(IA_Melee, EKeys::E);
	Map(IA_ButtStroke, EKeys::Q);
	Map(IA_SensUp, EKeys::RightBracket);
	Map(IA_Charge, EKeys::G);
	Map(IA_Crouch, EKeys::LeftControl);
	Map(IA_SensDown, EKeys::LeftBracket);
}

void ARFCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			BuildDefaultInput();
			Subsystem->ClearAllMappings();
			Subsystem->AddMappingContext(IMC_ThirdPerson, 0);
		}
	}
	EnsureGameMouseCapture();
}

void ARFCharacter::EnsureGameMouseCapture()
{
	// Mouse/trackpad look only gets deltas while the game viewport holds mouse capture
	// (FSceneViewport::OnMouseMove). PIE starts without it (GameGetsMouseControl=False) and loses
	// it on focus changes; until now only a click in the viewport took it back, so a trackpad
	// player moving with WASD and firing with F could not look at all. The keyboard reaching the
	// pawn means the viewport has focus, so taking the mouse here never steals it from the editor.
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC || !PC->IsLocalController() || PC->bShowMouseCursor || bIsDead) return;
	UGameViewportClient* GVC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	FViewport* VP = GVC ? GVC->Viewport : nullptr;
	if (!VP || !VP->HasFocus()) return;
	// Capture alone is not enough. Look needs the mouse in relative ("high precision") mode: without
	// it the deltas come from the pointer's place on the screen, and the hidden pointer stops at the
	// top edge. The view then turns and looks down but will not look any higher, which is the
	// "can't aim higher" report that used to clear only by restarting. The mode is lost without the
	// capture being lost (a menu shown and closed, the window left and returned to, the campaign
	// map's own mouse settings), so both are checked.
	const bool bCaptured = VP->HasMouseCapture();
	const bool bRelative = !FSlateApplication::IsInitialized() || FSlateApplication::Get().IsUsingHighPrecisionMouseMovment();
	if (bCaptured && bRelative) return;
	// Not every frame: a platform that never reports the mode (a streamed desktop) is asked once a second.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMouseCaptureFix < 1.0) return;
	LastMouseCaptureFix = Now;
	PC->SetInputMode(FInputModeGameOnly());
	if (MouseCaptureFixes++ < 8)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 input: look needed the mouse back (capture=%d relative=%d, capture mode %d, lock mode %d); game-only input set again"),
			bCaptured ? 1 : 0, bRelative ? 1 : 0, static_cast<int32>(GVC->GetMouseCaptureMode()), static_cast<int32>(GVC->GetMouseLockMode()));
	}
}

void ARFCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildDefaultInput();
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ARFCharacter::ToggleMounted);
	PlayerInputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ARFCharacter::ToggleFirstPerson); // first / third person
	// The gunner's glass zooms (only while he looks through it): the wheel, or = and - on a trackpad.
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ARFCharacter::OnSpyglassZoomIn);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ARFCharacter::OnSpyglassZoomOut);
	PlayerInputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &ARFCharacter::OnSpyglassZoomIn);
	PlayerInputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &ARFCharacter::OnSpyglassZoomOut);
	PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &ARFCharacter::OnShovePressed);    // the shove
	// The class verb (order a volley or a charge, rally): T. Passive verbs answer with what they do.
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &ARFCharacter::OnAbilityPressed);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ARFCharacter::OnHeroPressed);        // the general, when earned
	PlayerInputComponent->BindKey(EKeys::U, IE_Pressed, this, &ARFCharacter::OnHeroAbilityPressed); // his own ability, as him
	PlayerInputComponent->BindKey(EKeys::K, IE_Pressed, this, &ARFCharacter::OnFallInPressed);      // fall in with a line / fall out
	PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ARFCharacter::OnKey1);
	PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ARFCharacter::OnKey2);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ARFCharacter::OnKey3);
	PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ARFCharacter::OnKey4);
	PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ARFCharacter::OnKey5);
	PlayerInputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ARFCharacter::OnKey6);
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ARFCharacter::Move);
		EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &ARFCharacter::Look);
		EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &ARFCharacter::OnJumpPressed);
		EIC->BindAction(IA_Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		EIC->BindAction(IA_Sprint, ETriggerEvent::Started, this, &ARFCharacter::StartSprint);
		EIC->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &ARFCharacter::StopSprint);
		EIC->BindAction(IA_Aim, ETriggerEvent::Started, this, &ARFCharacter::StartAim);
		EIC->BindAction(IA_Aim, ETriggerEvent::Completed, this, &ARFCharacter::StopAim);
		EIC->BindAction(IA_Fire, ETriggerEvent::Started, this, &ARFCharacter::OnFirePressed);
		EIC->BindAction(IA_Reload, ETriggerEvent::Started, this, &ARFCharacter::OnReloadStarted);
		EIC->BindAction(IA_Reload, ETriggerEvent::Completed, this, &ARFCharacter::OnReloadCompleted);
		EIC->BindAction(IA_ShoulderSwap, ETriggerEvent::Started, this, &ARFCharacter::OnShoulderSwap);
		EIC->BindAction(IA_Melee, ETriggerEvent::Started, this, &ARFCharacter::OnMeleePressed);
		EIC->BindAction(IA_ButtStroke, ETriggerEvent::Started, this, &ARFCharacter::OnButtStrokePressed);
		EIC->BindAction(IA_SensUp, ETriggerEvent::Started, this, &ARFCharacter::OnSensitivityUp);
		EIC->BindAction(IA_Charge, ETriggerEvent::Started, this, &ARFCharacter::OnChargePressed);
		// Guard is a plain held key (no mapping-context entry needed): X up = musket across the body.
		PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &ARFCharacter::OnGuardPressed);
		PlayerInputComponent->BindKey(EKeys::X, IE_Released, this, &ARFCharacter::OnGuardReleased);
		PlayerInputComponent->BindKey(EKeys::O, IE_Pressed, this, &ARFCharacter::OnCycleGraphics);
		EIC->BindAction(IA_Crouch, ETriggerEvent::Started, this, &ARFCharacter::OnDuckPressed);
		EIC->BindAction(IA_Crouch, ETriggerEvent::Completed, this, &ARFCharacter::OnDuckReleased);
		EIC->BindAction(IA_SensDown, ETriggerEvent::Started, this, &ARFCharacter::OnSensitivityDown);
	}
}

void ARFCharacter::Move(const FInputActionValue& Value)
{
	EnsureGameMouseCapture();
	const FVector2D V = Value.Get<FVector2D>();
	if (Controller && !bIsDead && !bThrownFromHorse)
	{
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		// Guard up: he gives ground or closes at a shuffle, not a walk.
		float Pace = MeleeComponent && MeleeComponent->IsGuarding() ? 0.45f : 1.f;
		// Facing his view (aiming, just fired) a man going backwards steps back; he does not run backwards.
		if (bUseControllerRotationYaw && V.Y < -0.1f && GetCharacterMovement())
		{
			const float Top = GetCharacterMovement()->MaxWalkSpeed;
			const float Back = PA250BattleFeel::Get().BackstepSpeedCm;
			if (Back > 0.f && Top > Back) Pace = FMath::Min(Pace, Back / Top);
		}
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y * Pace);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X * Pace);
	}
}

namespace
{
// Look sensitivity: multiplier on raw mouse / trackpad deltas (1.0 = the old 1:1 feel, which was too fast).
TAutoConsoleVariable<float> CVarLookSensitivity(
	TEXT("pa250.LookSensitivity"), 0.2f,
	TEXT("Mouse/trackpad look sensitivity (0.05-2). [ and ] adjust it in game; saved to GameUserSettings."),
	ECVF_Default);
const TCHAR* SensSection = TEXT("/Script/PlayAmerica250.PA250Input");
const TCHAR* SensKey = TEXT("LookSensitivity");
}

float ARFCharacter::GetLookSensitivity()
{
	return FMath::Clamp(CVarLookSensitivity.GetValueOnGameThread(), 0.05f, 2.f);
}

void ARFCharacter::LoadLookSensitivity()
{
	float Saved = 0.f;
	if (GConfig && GConfig->GetFloat(SensSection, SensKey, Saved, GGameUserSettingsIni) && Saved > 0.f)
	{
		CVarLookSensitivity->Set(FMath::Clamp(Saved, 0.05f, 2.f), ECVF_SetByGameSetting);
	}
}

void ARFCharacter::AdjustLookSensitivity(float Factor)
{
	const float NewValue = FMath::Clamp(GetLookSensitivity() * Factor, 0.05f, 2.f);
	CVarLookSensitivity->Set(NewValue, ECVF_SetByGameSetting);
	if (GConfig)
	{
		GConfig->SetFloat(SensSection, SensKey, NewValue, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	SensitivityToastTimer = 1.5f;
}

void ARFCharacter::Look(const FInputActionValue& Value)
{
	EnsureGameMouseCapture(); // a look that arrives while the mouse is not in relative mode stops at the screen's edge
	const FVector2D V = Value.Get<FVector2D>();
	if (Controller)
	{
		// Slower while aiming down sights: the narrower FOV magnifies every degree.
		// Through the glass the look is the aim's at its widest and slows in step with the zoom.
		const float GlassScale = AdsLookScale * (AimedFOV > 0.f ? CurrentFOV / AimedFOV : 1.f);
		const float Scale = GetLookSensitivity() * (IsLookingThroughGlass() && GlassScale > 0.f ? GlassScale : bAiming ? AdsLookScale : 1.f);
		AddControllerYawInput(V.X * Scale * YawLookScale);
		AddControllerPitchInput(V.Y * Scale);
	}
}

void ARFCharacter::OnSensitivityUp()
{
	// While serving a field gun, [ ] cycle the load instead of look sensitivity.
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		if (GM->PlayerCycleGunShot(this, +1)) return;
	}
	AdjustLookSensitivity(1.15f);
}
void ARFCharacter::OnSensitivityDown()
{
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		if (GM->PlayerCycleGunShot(this, -1)) return;
	}
	AdjustLookSensitivity(1.f / 1.15f);
}

void ARFCharacter::StartSprint() { if (MountedVisual) { MountedGaitUp(); return; } bSprinting = true; GetCharacterMovement()->MaxWalkSpeed = SprintSpeed; }
void ARFCharacter::StopSprint() { bSprinting = false; if (MountedVisual) ApplyMountedGaitSpeed(); else GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; }

void ARFCharacter::StartAim()
{
	if (bThrownFromHorse || (MountedVisual && !MountedVisual->HasFirearm())) return;
	bAiming = true;
	if (MountedVisual) MountedVisual->SetAiming(true);
	TargetFOV = AimedFOV;
	UpdateFacingMode();
}

void ARFCharacter::StopAim()
{
	bAiming = false;
	if (MountedVisual) MountedVisual->SetAiming(false);
	TargetFOV = DefaultFOV;
	UpdateFacingMode();
}

void ARFCharacter::OnShoulderSwap() { ShoulderSign *= -1.f; }
void ARFCharacter::ToggleFirstPerson()
{
	bFirstPerson = !bFirstPerson;
	UE_LOG(LogTemp, Display, TEXT("PA250 view: %s person"), bFirstPerson ? TEXT("first") : TEXT("third"));
}
void ARFCharacter::OnShovePressed()
{
	if (MountedVisual || bIsDead || !MeleeComponent) return;
	MeleeComponent->DoShove();
}
void ARFCharacter::OnGuardPressed() { if (MeleeComponent && !bIsDead) MeleeComponent->SetGuardHeld(true); }
void ARFCharacter::OnCycleGraphics()
{
	// The player's graphics setting: the next tier of battle_look.json, set at once and saved.
	const FString Tier = PA250BattleLook::CyclePlayerTier();
	if (Tier.IsEmpty()) return;
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		GM->ShowNotice(FString::Printf(TEXT("Graphics: %s - %s  (O for the next)"), *Tier.ToUpper(), *PA250BattleLook::TierNote(Tier)), 5.f);
	}
}

void ARFCharacter::OnGuardReleased() { if (MeleeComponent) MeleeComponent->SetGuardHeld(false); }
bool ARFCharacter::IsOverObstacle() const
{
	return Traversal && Traversal->IsTraversing();
}

bool ARFCharacter::TryFireNow()
{
	if (bThrownFromHorse || IsOverObstacle()) return false;
	// At a gun he serves, his trigger fires the gun, not his musket.
	if (!MountedVisual && !bIsDead)
	{
		if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
		{
			if (GM->PlayerFiresGun(this)) return true;
		}
	}
	if (MountedVisual && !MountedVisual->HasFirearm())
	{
		MountedVisual->Slash();
		return true;
	}
	// Fallen in with a line at Make ready or Present: his shot is held and goes with the volley.
	if (!MountedVisual && !bIsDead)
	{
		if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
		{
			if (GM->HoldFallenInShot(this)) return true;
		}
	}
	return FireMusket();
}

void ARFCharacter::OnFallInPressed()
{
	if (bIsDead || MountedVisual) return;
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		GM->TryPlayerFallIn(this);
	}
}

void ARFCharacter::OnFirePressed()
{
	// A press that cannot fire yet (the last stage of the load, in the air, over a wall) is kept for
	// input.buffer_seconds.fire and fires the moment it can.
	if (!TryFireNow() && !bIsDead && !MountedVisual && GetWorld()) InputBuffer.Press(EPA250BufferedAction::Fire, GetWorld()->GetTimeSeconds());
}

void ARFCharacter::OnJumpPressed()
{
	if (bIsDead) return;
	if (MountedVisual || !Traversal)
	{
		Jump();
		return;
	}
	if (IsOverObstacle()) return;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const float Speed = static_cast<float>(GetVelocity().Size2D());
	const FName Gait = Speed > Feel.WalkMaxCmS ? FName(TEXT("run")) : Speed > Feel.IdleMaxCmS ? FName(TEXT("walk")) : FName(TEXT("stand"));
	const FVector Way = Speed > Feel.IdleMaxCmS ? GetVelocity() : GetActorForwardVector();
	const EPA250TraversalStart Why = Traversal->TryStart(Way, Feel.TraversalDetectDistanceCm, Gait);
	if (Why == EPA250TraversalStart::Started) return;
	// Just landed or still in the air: the press is kept for input.buffer_seconds.vault. Otherwise it is a jump.
	if ((Why == EPA250TraversalStart::Busy || Why == EPA250TraversalStart::NotGrounded) && GetWorld())
	{
		InputBuffer.Press(EPA250BufferedAction::Vault, GetWorld()->GetTimeSeconds());
	}
	if (Why != EPA250TraversalStart::Busy) Jump();
}

void ARFCharacter::TickInputBuffer()
{
	if (!IsPlayerControlled() || !GetWorld()) return;
	if (bIsDead || MountedVisual)
	{
		InputBuffer = FPA250InputBuffer();
		return;
	}
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const double Now = GetWorld()->GetTimeSeconds();
	if (InputBuffer.Live(EPA250BufferedAction::Fire, Now, Feel.BufferFireSeconds) && TryFireNow())
	{
		InputBuffer.Spend(EPA250BufferedAction::Fire);
	}
	if (IsOverObstacle()) return; // the rest wait until he is over
	if (InputBuffer.Consume(EPA250BufferedAction::Reload, Now, Feel.BufferReloadSeconds)) OnReloadStarted();
	int32 Number = 0;
	if (InputBuffer.Consume(EPA250BufferedAction::Order, Now, Feel.BufferOrderSeconds, &Number)) OnNumberKey(Number);
	if (GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround() && InputBuffer.Live(EPA250BufferedAction::Vault, Now, Feel.BufferVaultSeconds))
	{
		const FVector Way = GetVelocity().Size2D() > Feel.IdleMaxCmS ? GetVelocity() : GetActorForwardVector();
		const EPA250TraversalStart Why = Traversal ? Traversal->TryStart(Way, Feel.TraversalDetectDistanceCm, TEXT("run")) : EPA250TraversalStart::NoObstacle;
		if (Why != EPA250TraversalStart::Busy) InputBuffer.Spend(EPA250BufferedAction::Vault);
	}
}

void ARFCharacter::ApplyMovementFeel(bool bPlayer)
{
	bPlayerFeelApplied = bPlayer;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const ARFCharacter* Defaults = GetClass()->GetDefaultObject<ARFCharacter>();
	const UCharacterMovementComponent* DefaultMove = Defaults ? Defaults->GetCharacterMovement() : nullptr;
	if (!Move || !DefaultMove) return;
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	const bool bUse = bPlayer && Feel.MoveMaxAcceleration > 0.f;
	Move->MaxAcceleration = bUse ? Feel.MoveMaxAcceleration : DefaultMove->MaxAcceleration;
	Move->BrakingDecelerationWalking = bUse ? Feel.MoveBrakingDeceleration : DefaultMove->BrakingDecelerationWalking;
	Move->RotationRate.Yaw = bUse ? Feel.RotationRateYawDeg : DefaultMove->RotationRate.Yaw;
	if (SpringArm && Defaults->SpringArm) SpringArm->CameraLagSpeed = bUse ? Feel.CameraLagSpeed : Defaults->SpringArm->CameraLagSpeed;
	if (bUse)
	{
		// The engine's own smoothing of raw mouse movement (it adds a frame or two of lag to the look).
		GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = Feel.bMouseSmoothing;
		UE_LOG(LogTemp, Display, TEXT("PA250 input: player movement from data: acceleration %.0f, braking %.0f, turn %.0f deg/s, camera lag %.0f, still after a shot %.2f s, mouse smoothing %s"),
			Move->MaxAcceleration, Move->BrakingDecelerationWalking, Move->RotationRate.Yaw, SpringArm ? SpringArm->CameraLagSpeed : 0.f, Feel.FireLockSeconds,
			Feel.bMouseSmoothing ? TEXT("on") : TEXT("off"));
	}
}
void ARFCharacter::OnMeleePressed()
{
	if (bThrownFromHorse) return;
	if (MountedVisual) { SelectMountedSword(); MountedVisual->Slash(); return; }
	if (bIsDead || !MeleeComponent) return;
	if (bSprinting) MeleeComponent->DoCharge();
	else MeleeComponent->RequestThrust();
}
void ARFCharacter::OnDuckPressed() { if (MountedVisual) { MountedGaitDown(); return; } bManualDuck = true; SetDucking(true); }
void ARFCharacter::OnDuckReleased() { bManualDuck = false; if (!bAutoDucked) SetDucking(false); }

void ARFCharacter::TickFireOverStance()
{
	if (!IsPlayerControlled() || bIsDead || MountedVisual)
	{
		bAutoDucked = false;
		return;
	}
	const AMusketWeapon* Gun = GetEquippedFirearm();
	const bool bLoading = Gun && Gun->IsReloading();
	bool bAtCover = false;
	if (bLoading)
	{
		const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
		TArray<const AActor*> Ignore = {this, Musket.Get()};
		bAtCover = PA250Cover::IsAtCover(GetWorld(), Feet, GetActorForwardVector(), Ignore, pa250::battle_rules().cover_snap_cm);
	}
	const bool bDown = PA250Cover::FireOverDown(bAtCover, bLoading, bAiming, bSprinting);
	if (bDown && !bAutoDucked)
	{
		bAutoDucked = true;
		SetDucking(true);
	}
	else if (!bDown && bAutoDucked)
	{
		bAutoDucked = false;
		if (!bManualDuck) SetDucking(false);
	}
}

void ARFCharacter::SetDucking(bool bDuck)
{
	if (bIsDead || MountedVisual) return;
	if (bDuck && !bIsCrouched) Crouch();
	else if (!bDuck && bIsCrouched) UnCrouch();
}

void ARFCharacter::OnNumberKey(int32 Number)
{
	if (bIsDead) return;
	// Given over a wall, an order is kept (input.buffer_seconds.order) and given when he lands.
	if (IsOverObstacle() && IsPlayerControlled() && GetWorld())
	{
		InputBuffer.Press(EPA250BufferedAction::Order, GetWorld()->GetTimeSeconds(), Number);
		return;
	}
	if (MountedVisual)
	{
		if (Number >= 1 && Number <= 3) SelectMountedWeapon(Number - 1);
		return;
	}
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		GM->GivePlayerOrder(this, Number);
	}
}

void ARFCharacter::OnHeroPressed()
{
	if (bIsDead) return;
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		GM->OpenHeroScreen(Cast<APlayerController>(GetController()));
	}
}

void ARFCharacter::OnHeroAbilityPressed()
{
	if (bIsDead) return;
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		GM->UseHeroAbility(this);
	}
}

void ARFCharacter::SetHeroArms(bool bOn, float SwordDamage, float SwordReachCm, float SwordRecoverSeconds, float PistolDamage, float PistolRangeM, float PistolReloadSeconds)
{
	if (bOn == bHeroArms) return;
	StopAim();
	bHeroArms = bOn;
	if (MeleeComponent) MeleeComponent->SetSword(bOn, SwordDamage, SwordReachCm, SwordRecoverSeconds);
	if (!bOn)
	{
		for (UStaticMeshComponent* Part : HeroSword)
		{
			if (Part) Part->SetVisibility(false);
		}
		if (HeroPistol)
		{
			HeroPistol->SetReloadHeld(false);
			HeroPistol->SetActorHiddenInGame(true);
		}
		if (Musket && !bIsDead) Musket->SetActorHiddenInGame(false);
		if (UPA250SoldierAnimInstance* Inst = GetMesh() ? Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
		{
			Inst->SetMountedAim(FPA250MountedAim());
		}
		return;
	}
	if (Musket)
	{
		Musket->SetReloadHeld(false);
		Musket->SetActorHiddenInGame(true);
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (HeroSword.IsEmpty() && GetMesh() && Cube)
	{
		// The sword: the mounted rider's prototype (grip, guard, blade on the right hand) until a period sword is supplied.
		struct FPart { const TCHAR* Name; FVector At; FVector Scale; };
		const FPart Parts[] = {
			{TEXT("HeroSwordGrip"), FVector::ZeroVector, FVector(.04f, .04f, .14f)},
			{TEXT("HeroSwordGuard"), FVector(0.f, 0.f, 7.f), FVector(.18f, .035f, .025f)},
			{TEXT("HeroSwordBlade"), FVector(0.f, 0.f, 50.f), FVector(.05f, .015f, .85f)},
		};
		for (const FPart& P : Parts)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, P.Name);
			Part->SetStaticMesh(Cube);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetupAttachment(GetMesh(), TEXT("hand_r"));
			Part->SetRelativeLocation(P.At);
			Part->SetRelativeScale3D(P.Scale);
			AddInstanceComponent(Part);
			Part->RegisterComponent();
			HeroSword.Add(Part);
		}
	}
	if (!HeroPistol && GetWorld())
	{
		// The pistol: the rider's prototype flintlock (a short barrel and a grip) until a period pistol is supplied.
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.Instigator = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HeroPistol = GetWorld()->SpawnActor<AMusketWeapon>(AMusketWeapon::StaticClass(), GetActorTransform(), Params);
		if (HeroPistol)
		{
			if (HeroPistol->GetAnimatedMesh()) HeroPistol->GetAnimatedMesh()->SetHiddenInGame(true);
			if (UStaticMeshComponent* Barrel = HeroPistol->GetMusketMesh())
			{
				Barrel->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
				Barrel->SetRelativeLocation(FVector(14.f, 0.f, 0.f));
				Barrel->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
				Barrel->SetRelativeScale3D(FVector(.035f, .035f, .32f));
				Barrel->SetVisibility(true);
				Barrel->SetHiddenInGame(false);
			}
			if (Cube)
			{
				UStaticMeshComponent* Grip = NewObject<UStaticMeshComponent>(HeroPistol, TEXT("PistolGrip"));
				Grip->SetupAttachment(HeroPistol->GetRootComponent());
				Grip->SetStaticMesh(Cube);
				Grip->SetRelativeLocation(FVector(0.f, 0.f, -6.f));
				Grip->SetRelativeScale3D(FVector(.05f, .04f, .12f));
				Grip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				HeroPistol->AddInstanceComponent(Grip);
				Grip->RegisterComponent();
			}
			HeroPistol->GetMuzzleLocation()->SetRelativeLocation(FVector(30.f, 0.f, 0.f));
			HeroPistol->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_r"));
			if (USceneComponent* PistolRoot = HeroPistol->GetRootComponent()) PistolRoot->SetUsingAbsoluteRotation(true);
		}
	}
	if (HeroPistol)
	{
		// Its numbers are the persons data's: one damage out to its range, and its own time to load.
		HeroPistol->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
		FPA250WeaponTuningValues& V = HeroPistol->WeaponTuning->Values;
		V.TorsoDamageNear = PistolDamage;
		V.TorsoDamageFar = PistolDamage;
		V.FalloffStartMeters = PistolRangeM;
		V.FalloffEndMeters = PistolRangeM + 1.f;
		V.MaxRangeMeters = PistolRangeM;
		V.ProjectileSpeed = 20000.f; // as the rider's pistol
		for (FPA250ReloadPresetValues* Preset : {&V.Authentic, &V.Arcade})
		{
			const float Share = Preset->BaseSeconds > 0.f ? PistolReloadSeconds / Preset->BaseSeconds : 1.f;
			Preset->BaseSeconds = PistolReloadSeconds;
			Preset->PerfectSeconds *= Share;
			Preset->FailedSeconds *= Share;
		}
		HeroPistol->ApplyWeaponTuning();
		HeroPistol->SetActorHiddenInGame(true); // drawn while he aims
	}
	for (UStaticMeshComponent* Part : HeroSword)
	{
		if (Part) Part->SetVisibility(true);
	}
	HeroPistolOutUntil = -1.f;
	HeroAimAlpha = 0.f;
	HeroRecoilAlpha = 0.f;
}

void ARFCharacter::TickHeroArms(float DeltaTime)
{
	if (!bHeroArms || bIsDead || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bLoading = HeroPistol && HeroPistol->IsReloading();
	// Sword in hand; the pistol is out while he aims, loads, or has just fired.
	const bool bPistolOut = HeroPistol && (bAiming || bLoading || Now < HeroPistolOutUntil);
	for (UStaticMeshComponent* Part : HeroSword)
	{
		if (Part) Part->SetVisibility(!bPistolOut);
	}
	HeroAimAlpha = FMath::FInterpTo(HeroAimAlpha, bAiming ? 1.f : 0.f, DeltaTime, 9.f);
	HeroRecoilAlpha = FMath::FInterpTo(HeroRecoilAlpha, 0.f, DeltaTime, 6.f);
	FRotator View = GetActorRotation();
	if (const AController* C = GetController()) View = C->GetControlRotation();
	if (HeroPistol)
	{
		HeroPistol->SetActorHiddenInGame(!bPistolOut);
		HeroPistol->SetActorRotation(FRotator(View.Pitch + HeroRecoilAlpha * 7.f - (bLoading ? 22.f : 0.f), View.Yaw, 0.f));
	}
	// The one-handed hold the rider uses, on foot: the right arm carries the pistol along his aim.
	if (UPA250SoldierAnimInstance* Inst = GetMesh() ? Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
	{
		FPA250MountedAim Hold;
		Hold.bEnabled = bPistolOut;
		Hold.bPistol = true;
		Hold.AimAlpha = HeroAimAlpha;
		Hold.ReloadAlpha = bLoading ? 1.f : 0.f;
		Hold.RecoilAlpha = HeroRecoilAlpha;
		Hold.DirectionCS = GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(View.Vector());
		Inst->SetMountedAim(Hold);
	}
}

void ARFCharacter::OnAbilityPressed()
{
	if (bIsDead) return;
	if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
	{
		GM->UseClassAbility(this);
	}
}

void ARFCharacter::OnChargePressed()
{
	if (MountedVisual) return;
	if (bIsDead || !MeleeComponent) return;
	MeleeComponent->DoCharge();
}
void ARFCharacter::OnButtStrokePressed()
{
	if (MountedVisual) { SelectMountedSword(); MountedVisual->Slash(); return; }
	if (bIsDead || !MeleeComponent) return;
	MeleeComponent->DoButtStroke();
}
void ARFCharacter::OnReloadCompleted()
{
	if (AMusketWeapon* Equipped = GetEquippedFirearm()) Equipped->SetReloadHeld(false);
}

void ARFCharacter::ApplySuppression(float Seconds, float SwayDegrees)
{
	SuppressionTimer = FMath::Max(SuppressionTimer, Seconds);
	SuppressionSwayDegrees = FMath::Max(SuppressionSwayDegrees, SwayDegrees);
}

void ARFCharacter::AddSuppression(float Amount)
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	SuppressionLevel = PA250Intensity::SuppressionStep(SuppressionLevel, Amount, Feel.SuppressionMax, 0.f, 0.f);
}

void ARFCharacter::TickSuppression(float DeltaTime)
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	SuppressionLevel = PA250Intensity::SuppressionStep(SuppressionLevel, 0.f, Feel.SuppressionMax, Feel.SuppressionDecayPerSecond, DeltaTime);
}

float ARFCharacter::SuppressionSwayNow() const
{
	return SuppressionLevel * PA250BattleFeel::Get().SuppressionAimSwayDeg;
}

bool ARFCharacter::FireMusket()
{
    AMusketWeapon* Equipped = GetEquippedFirearm();
	if (bIsDead || !Equipped || !Equipped->CanFire() || !Controller)
	{
		return false;
	}
	const float MaxRange = WeaponTuning ? WeaponTuning->Values.MaxRangeMeters * 100.f : 20000.f;

	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	if (MountedVisual) MountedVisual->PrepareShot();
	const FVector Muzzle = Equipped->GetMuzzleLocation()->GetComponentLocation();
	// Spread is applied once, by AMusketWeapon::Fire; the trace only finds the point under the reticle.
	const FPA250AimResult Aim = UPA250AimComponent::ResolveAim(
		GetWorld(), ViewLoc, ViewRot.Vector(), Muzzle, 0.f, MaxRange, this, Equipped, FactionId);
	if (AimComponent)
	{
		const float Spread = AimComponent->LastResult.SpreadDegrees;
		AimComponent->LastResult = Aim;
		AimComponent->LastResult.SpreadDegrees = Spread;
	}

	// Blocked: the ball goes where the muzzle actually points (into the cover).
	// Nothing under the reticle (sky/horizon): fire flat along the line, no drop correction for a
	// made-up max-range distance - that is what sent shots metres over the reticle.
	if (Aim.bBlocked) return FireMusketAt(Aim.MuzzleHitPoint, true);
	return FireMusketAt(Aim.CameraAimPoint, Aim.bCameraHit);
}

bool ARFCharacter::FireMusketAt(FVector TargetLocation, bool bCompensateDrop)
{
    AMusketWeapon* Equipped = GetEquippedFirearm();
	if (bIsDead || bThrownFromHorse || bEmbarked || !Equipped || !Equipped->CanFire())
	{
		return false;
	}
	// Muskets are fired standing: no shots in mid-air; a ducked man rises to fire.
	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		return false;
	}
	if (bIsCrouched)
	{
		UnCrouch();
		GetCharacterMovement()->UnCrouch(false);
	}
	if (MountedVisual) MountedVisual->PrepareShot();
	const FVector Muzzle = Equipped->GetMuzzleLocation()->GetComponentLocation();
	FVector Dir = (TargetLocation - Muzzle).GetSafeNormal();
	if (bCompensateDrop)
	{
		AMusketWeapon::SolveLaunchDirection(Muzzle, TargetLocation, Equipped->GetProjectileSpeed(), Equipped->GetProjectileGravityZ(), Dir);
	}
	{
		// Shaken aim: the timed sway (volleys, blasts) or the player's suppression, whichever is worse.
		const float Sway = FMath::Max(SuppressionTimer > 0.f ? SuppressionSwayDegrees : 0.f, SuppressionSwayNow());
		if (Sway > 0.f) Dir = PA250BattleRng::Get().VRandCone(Dir, FMath::DegreesToRadians(Sway));
	}
	// A moving saddle throws the aim off: nothing at a stand, up to 4 degrees at a gallop.
	if (MountedVisual)
	{
		if (const float Sway = MountedFireSwayDegrees(GetVelocity().Size2D()); Sway > 0.01f)
		{
			Dir = PA250BattleRng::Get().VRandCone(Dir, FMath::DegreesToRadians(Sway));
		}
	}
	// Flash in the pan or misfire (battle_feel.json musket; PA250MusketFeel): bots and the player alike,
	// rain from PA250BattleWeather. His Nth pull has its own roll from the battle seed.
	{
		const PA250MusketFeel::FChances Chances = PA250MusketFeel::ChancesFor(PA250BattleFeel::Get(), MisfireChance, MisfireStress, PA250BattleWeather::Get(GetWorld()));
		APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr;
		const uint64 BattleSeed = GM ? GM->GetBattleSeed() : static_cast<uint64>(PA250BattleRng::FallbackSeed);
		const float Roll = PA250MusketFeel::RollFor(BattleSeed, GetTypeHash(GetFName()), static_cast<uint32>(TriggerPulls++));
		const EPA250TriggerResult Pull = PA250MusketFeel::Resolve(Roll, Chances);
		if (Pull != EPA250TriggerResult::Fires)
		{
			if (Equipped->Misfire(Pull == EPA250TriggerResult::FlashInPan))
			{
				++Misfires;
				LockMovement(0.4f);
				if (GM) GM->RecordMisfire(FactionId);
			}
			return false;
		}
	}
	// Spread uses the speed at the trigger pull, so firing on the run is still inaccurate.
	const float OwnerSpeed = GetVelocity().Size2D();
	// The firelock's own reach: past its accurate range the cone widens (class kit, unit_classes.json).
	const float RangeScale = Equipped->RangeSpreadScale(FVector::Dist(GetActorLocation(), TargetLocation));
	const bool bShot = Equipped->Fire(Dir, bAiming, OwnerSpeed, FormationAccuracyScale * OfficerAccuracyScale * RangeScale);
	if (bShot)
	{
		// The player's stand after his shot is battle_feel.json movement.fire_lock_seconds; a bot's is his weapon's.
		const float PlayerStill = PA250BattleFeel::Get().FireLockSeconds;
		if (!MountedVisual) LockMovement(IsPlayerControlled() && PlayerStill > 0.f ? PlayerStill : WeaponTuning ? WeaponTuning->Values.FireStillSeconds : 0.6f);
		NotifyFired();
		if (MountedVisual) MountedVisual->NotifyFirearmShot();
		if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
		{
			GM->NotifyShot(this, Dir, true);
		}
	}
	return bShot;
}

void ARFCharacter::LockMovement(float Seconds)
{
	if (bIsDead || Seconds <= 0.f) return;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move) return;
	MovementLockTimer = FMath::Max(MovementLockTimer, Seconds);
	if (!bMovementLocked && Move->IsMovingOnGround())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
		bMovementLocked = true;
	}
}

void ARFCharacter::TickMovementLock(float DeltaTime)
{
	if (MovementLockTimer <= 0.f) return;
	MovementLockTimer = FMath::Max(0.f, MovementLockTimer - DeltaTime);
	if (MovementLockTimer <= 0.f && bMovementLocked)
	{
		bMovementLocked = false;
		if (!bIsDead && GetCharacterMovement())
		{
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
}

bool ARFCharacter::IsReloadBlockedByMovement() const
{
	return !bIsDead && ReloadBlockedByMovement(IsPlayerControlled(), IsMounted(), GetVelocity().Size2D(), WalkSpeed);
}

void ARFCharacter::OnReloadStarted()
{
	// Pressed over a wall, the load is kept (input.buffer_seconds.reload) and begun when he lands.
	if (IsOverObstacle() && IsPlayerControlled() && GetWorld())
	{
		InputBuffer.Press(EPA250BufferedAction::Reload, GetWorld()->GetTimeSeconds());
		return;
	}
	// At a gun he serves, the key works the gun's drill.
	if (!bIsDead && !MountedVisual)
	{
		if (APA250BattleGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<APA250BattleGameMode>() : nullptr)
		{
			EActiveReloadHit GunHit = EActiveReloadHit::None;
			if (GM->PlayerWorksGun(this, GunHit))
			{
				if (GunHit != EActiveReloadHit::None)
				{
					LastReloadHit = GunHit;
					ReloadFeedbackTimer = 0.9f;
				}
				return;
			}
		}
	}
	AMusketWeapon* Equipped = GetEquippedFirearm();
	if (!Equipped || bIsDead) return;
	Equipped->SetReloadHeld(true);
	const EActiveReloadHit Hit = Equipped->CommitReload();
	if (Hit != EActiveReloadHit::None)
	{
		LastReloadHit = Hit;
		ReloadFeedbackTimer = 0.9f;
	}
}

// The fallen: battle_feel.json intensity.corpses; a console variable set to 0 or more overrides the file.
static TAutoConsoleVariable<float> CVarCorpseSeconds(TEXT("pa250.CorpseSeconds"), -1.f,
	TEXT("Seconds a fallen soldier stays on the field (-1: battle_feel.json intensity.corpses.seconds)."));
static TAutoConsoleVariable<int32> CVarMaxCorpses(TEXT("pa250.MaxCorpses"), -1,
	TEXT("Most fallen soldiers left on the field at once; the oldest is cleared first (-1: battle_feel.json intensity.corpses.max)."));

float ARFCharacter::GetCorpseSeconds()
{
	const float Override = CVarCorpseSeconds.GetValueOnGameThread();
	return FMath::Max(3.f, Override >= 0.f ? Override : PA250BattleFeel::Get().CorpseSeconds);
}
int32 ARFCharacter::GetMaxCorpses()
{
	const int32 Override = CVarMaxCorpses.GetValueOnGameThread();
	return FMath::Max(0, Override >= 0 ? Override : PA250BattleFeel::Get().CorpsesMax);
}

void ARFCharacter::SetLookBudget(bool bCastShadow, bool bUpdateRateOptimisation)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body) return;
	if (Body->bEnableUpdateRateOptimizations != bUpdateRateOptimisation) Body->bEnableUpdateRateOptimizations = bUpdateRateOptimisation;
	const int8 Want = bCastShadow ? 1 : 0;
	if (LookShadow == Want) return;
	LookShadow = Want;
	Body->SetCastShadow(bCastShadow);
	for (USkeletalMeshComponent* Part : BodyParts)
	{
		if (Part) Part->SetCastShadow(bCastShadow);
	}
	for (UStaticMeshComponent* Piece : ClassHatPieces)
	{
		if (Piece) Piece->SetCastShadow(bCastShadow);
	}
	if (HatMesh) HatMesh->SetCastShadow(bCastShadow);
}

void ARFCharacter::FreezeCorpse()
{
	// He lies as he fell: the body stops simulating and stops being animated, so the fallen cost a draw and nothing more.
	USkeletalMeshComponent* Body = GetMesh();
	if (!bIsDead || !Body || bCorpseFrozen) return;
	bCorpseFrozen = true;
	Body->bNoSkeletonUpdate = true;
	Body->PutAllRigidBodiesToSleep();
	Body->SetAllBodiesSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetComponentTickEnabled(false);
	SetActorTickEnabled(false);
}

void ARFCharacter::OnWounded(float Damage, const FVector& From)
{
	if (bIsDead) return;
	// Hit but standing: he staggers, loses his aim and shoots worse for a few seconds.
	bFlinched = true;
	HitPulse = 1.f;
	LockMovement(0.45f);
	ApplySuppression(4.f, 3.f);
	if (IsPlayerControlled())
	{
		PunchFOV(5.f);
		ApplyCameraKick(-1.2f, 1.5f);
	}
}

void ARFCharacter::OnDeath(AActor* Victim)
{
	if (bIsDead)
	{
		return;
	}
	Dismount();
	bIsDead = true;
	if (Traversal) Traversal->Cancel();
	if (HeroPistol) HeroPistol->SetActorHiddenInGame(true);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}
	// Drop the capsule before the mesh simulates, or the ragdoll collides with it and falls through.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		FHitResult Hit;
		const FVector Start = GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector End = Start - FVector(0.f, 0.f, 400.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250RagdollSettle), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params) && Hit.bBlockingHit)
		{
			// Standing height is about 90 cm above the feet. Lift only when already below the floor.
			if (GetActorLocation().Z < Hit.ImpactPoint.Z + 40.f)
			{
				FVector Loc = GetActorLocation();
				Loc.Z = Hit.ImpactPoint.Z + 90.f;
				SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		MeshComp->SetSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();
		MeshComp->SetEnableGravity(true);
		if (HealthComponent && HealthComponent->bHasLastDamageLocation)
		{
			const FVector ImpulseDir =
				(GetActorLocation() - HealthComponent->LastDamageLocation).GetSafeNormal();
			MeshComp->AddImpulseAtLocation(ImpulseDir * 420.f + FVector(0.f, 0.f, 180.f),
										   HealthComponent->LastDamageLocation);
		}
	}
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();
	// The musket falls with him instead of staying levelled at the enemy.
	if (Musket)
	{
		Musket->SetActorTickEnabled(false);
		if (USceneComponent* Root = Musket->GetRootComponent())
		{
			Root->SetUsingAbsoluteRotation(true);
			Root->SetWorldRotation(FRotator(0.f, GetActorRotation().Yaw + FMath::FRandRange(-70.f, 70.f), 90.f));
		}
	}
	if (TeamMarker) TeamMarker->SetVisibility(false);
	// The fallen stay on the field (they are what the fight looked like), up to a cap: oldest cleared first.
	SetLifeSpan(GetCorpseSeconds());
	if (const float FreezeAfter = PA250BattleFeel::Get().CorpseFreezeAfterSeconds; FreezeAfter > 0.f && !IsPlayerControlled())
	{
		FTimerHandle FreezeTimer;
		GetWorldTimerManager().SetTimer(FreezeTimer, this, &ARFCharacter::FreezeCorpse, FreezeAfter, false);
	}
	TArray<ARFCharacter*> Fallen;
	for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDead() && !It->IsPlayerControlled() && *It != this) Fallen.Add(*It);
	}
	const int32 Over = Fallen.Num() + 1 - GetMaxCorpses();
	if (Over > 0)
	{
		Fallen.Sort([](const ARFCharacter& A, const ARFCharacter& B) { return A.GetLifeSpan() < B.GetLifeSpan(); });
		for (int32 i = 0; i < FMath::Min(Over, Fallen.Num()); ++i) Fallen[i]->SetLifeSpan(0.5f);
	}
}

void ARFCharacter::ToggleMounted()
{
    if (!IsPlayerControlled() || bIsDead || bThrownFromHorse) return;
    if (MountedVisual) { Dismount(); return; }
    // A dragoon has his own horse: he mounts it where he left it, or not at all.
    if (KeepsOwnHorse())
    {
        FString Why;
        if (!OwnHorse || OwnHorse->IsHorseDead()) Why = TEXT("Your horse is down");
        else if (FVector::Dist2D(OwnHorse->GetActorLocation(), GetActorLocation()) > RemountReachCm)
            Why = FString::Printf(TEXT("Your horse is %.0f m away"), FVector::Dist2D(OwnHorse->GetActorLocation(), GetActorLocation()) / 100.f);
        if (!Why.IsEmpty())
        {
            if (GEngine) GEngine->AddOnScreenDebugMessage(25073, 2.f, FColor::White, Why);
            return;
        }
        MountHorse(OwnHorse, false);
        return;
    }
    // GAME INVENTION: after a horse is shot from under him, a fresh one takes a while to come up.
    if (GetWorld()->GetTimeSeconds() < RemountBlockedUntil)
    {
        if (GEngine) GEngine->AddOnScreenDebugMessage(25073, 2.f, FColor::White,
            FString::Printf(TEXT("No horse yet (%.0f s)"), RemountBlockedUntil - GetWorld()->GetTimeSeconds()));
        return;
    }
    MountHorse(nullptr, false);
}

void ARFCharacter::Dismount()
{
    // A dragoon's horse stays where he got down; the prototype horse goes away with him.
    if (APA250MountedSoldier* Horse = LeaveSaddle())
    {
        if (KeepsOwnHorse() && Horse == OwnHorse) Horse->Picket();
        else Horse->Destroy();
    }
}

void ARFCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Dismount();
    if (OwnHorse && !OwnHorse->IsHorseDead()) OwnHorse->Destroy(); // its man is gone from the field
    OwnHorse = nullptr;
    if (MountedPistol) { MountedPistol->Destroy(); MountedPistol = nullptr; }
    if (HeroPistol) { HeroPistol->Destroy(); HeroPistol = nullptr; }
    Super::EndPlay(EndPlayReason);
}

AMusketWeapon* ARFCharacter::GetEquippedFirearm() const
{
    if (!MountedVisual) return bHeroArms && HeroPistol ? HeroPistol.Get() : Musket.Get();
    return MountedWeaponIndex == 1 ? Musket.Get() : MountedWeaponIndex == 2 ? MountedPistol.Get() : nullptr;
}

void ARFCharacter::SelectMountedSword() { SelectMountedWeapon(0); }
void ARFCharacter::SelectMountedRifle() { SelectMountedWeapon(1); }
void ARFCharacter::SelectMountedPistol() { SelectMountedWeapon(2); }

void ARFCharacter::SelectMountedWeapon(int32 Index)
{
    if (!MountedVisual || bIsDead) return;
    if (MountedWeaponIndex == Index) return;
    StopAim();
    if (Index == 2 && !MountedPistol)
    {
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.Instigator = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        MountedPistol = GetWorld()->SpawnActor<AMusketWeapon>(AMusketWeapon::StaticClass(), GetActorTransform(), Params);
        if (!MountedPistol) return;
        // Prototype flintlock: short barrel and grip until a period pistol mesh is supplied.
        if (MountedPistol->GetAnimatedMesh()) MountedPistol->GetAnimatedMesh()->SetHiddenInGame(true);
        auto* PistolMesh = MountedPistol->GetMusketMesh();
        PistolMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        PistolMesh->SetRelativeLocation(FVector(14.f, 0.f, 0.f));
        PistolMesh->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
        PistolMesh->SetRelativeScale3D(FVector(.035f, .035f, .32f));
        PistolMesh->SetVisibility(true);
        PistolMesh->SetHiddenInGame(false);
        auto* Grip = NewObject<UStaticMeshComponent>(MountedPistol, TEXT("PistolGrip"));
        Grip->SetupAttachment(MountedPistol->GetRootComponent());
        Grip->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Grip->SetRelativeLocation(FVector(0.f, 0.f, -6.f));
        Grip->SetRelativeScale3D(FVector(.05f, .04f, .12f));
        Grip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MountedPistol->AddInstanceComponent(Grip);
        Grip->RegisterComponent();
        MountedPistol->GetMuzzleLocation()->SetRelativeLocation(FVector(30.f, 0.f, 0.f));
        MountedPistol->WeaponTuning = UPA250WeaponTuning::MakeRuntimeDefaults();
        MountedPistol->WeaponTuning->Values.TorsoDamageNear = 55.f;
        MountedPistol->WeaponTuning->Values.TorsoDamageFar = 25.f;
        MountedPistol->WeaponTuning->Values.ProjectileSpeed = 20000.f;
        MountedPistol->ApplyWeaponTuning();
    }
    if (Index == 1 && !Musket) return;
    if (AMusketWeapon* Old = GetEquippedFirearm()) { Old->SetReloadHeld(false); Old->SetActorHiddenInGame(true); }
    MountedWeaponIndex = Index;
    MountedVisual->SetFirearm(GetEquippedFirearm(), Index == 2);
    if (GEngine) GEngine->AddOnScreenDebugMessage(25071, 4.f, FColor::White,
        FString::Printf(TEXT("Mounted: %s | 1 Sword  2 Musket  3 Pistol | LMB Fire/Cut | RMB Aim | R Reload | Shift faster  Ctrl slower | H Dismount"), Index == 0 ? TEXT("Sword") : Index == 1 ? TEXT("Musket") : TEXT("Pistol")));
}
