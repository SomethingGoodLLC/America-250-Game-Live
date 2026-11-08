#include "RFCharacter.h"
#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "PA250SoldierAnimInstance.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MusketWeapon.h"
#include "PA250AimComponent.h"
#include "PA250BattleGameMode.h"
#include "PA250FactionVisuals.h"
#include "PA250MeleeComponent.h"
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
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 10.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;
	Camera->FieldOfView = DefaultFOV;

	HealthComponent = CreateDefaultSubobject<URFHealthComponent>(TEXT("HealthComponent"));
	AimComponent = CreateDefaultSubobject<UPA250AimComponent>(TEXT("AimComponent"));
	MeleeComponent = CreateDefaultSubobject<UPA250MeleeComponent>(TEXT("MeleeComponent"));

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
		}
		if (Coat)
		{
			// Slot 1 is the pack's outer clothing; leave skin, eyes, hair and kit untouched.
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Coat, this);
			// The pack's coat albedo is dark once desaturated, so the tint needs a strong boost
			// for wool colours to read (homespun more than the regulars' red).
			MID->SetVectorParameterValue(TEXT("CoatColor"), CoatColor * (bHomespun ? 3.5f : 3.f));
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

void ARFCharacter::UpdateNameplate(FName ViewerFaction, bool bSquadmate, float DistanceCm, bool bHasLOS)
{
	if (!Nameplate) return;
	const bool bEnemy = !ViewerFaction.IsNone() && ViewerFaction != FactionId;
	bool bShow = false;
	FLinearColor Color = FLinearColor::White;
	if (bSquadmate)
	{
		bShow = true;
		Color = FLinearColor(0.25f, 0.55f, 1.f);
	}
	else if (bEnemy && DistanceCm <= 5000.f && bHasLOS)
	{
		bShow = true;
		Color = FLinearColor(0.9f, 0.15f, 0.15f);
	}
	else if (!bEnemy && DistanceCm <= 3000.f)
	{
		bShow = true;
		Color = FLinearColor(0.25f, 0.55f, 1.f);
	}
	Nameplate->SetVisibility(bShow);
	Nameplate->SetTintColorAndOpacity(Color);
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
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(FLinearColor::White)
			.Text(FText::FromName(FactionId)));
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
		StaticCastSharedPtr<STextBlock>(Plate)->SetText(FText::FromName(FactionId));
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

void ARFCharacter::UpdateSoldierAnimation()
{
	if (!bUsesPeriodSoldier || bIsDead) return;
	const float Speed = GetVelocity().Size2D();
	UAnimSequence* Animation = Speed > 300.f ? SoldierRun.Get() : Speed > 5.f ? SoldierWalk.Get() : SoldierIdle.Get();
	if (UPA250SoldierAnimInstance* Instance = Cast<UPA250SoldierAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		if (Animation && Instance->GetLocomotion() != Animation) Instance->SetLocomotion(Animation);
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

void ARFCharacter::ApplyCameraPose(float DeltaTime)
{
	if (!SpringArm || !Camera) return;
	const float Y = ShoulderSign * (bAiming ? AdsSocketOffset.Y : HipSocketOffset.Y);
	const FVector TargetSocket(0.f, Y, bAiming ? AdsSocketOffset.Z : HipSocketOffset.Z);
	const float TargetArm = bAiming ? AdsArmLength : HipArmLength;
	const float Alpha = CameraBlendSeconds > KINDA_SMALL_NUMBER
		? FMath::Clamp(DeltaTime / CameraBlendSeconds, 0.f, 1.f)
		: 1.f;
	CurrentArmLength = FMath::Lerp(CurrentArmLength, TargetArm, Alpha);
	CurrentSocketOffset = FMath::Lerp(CurrentSocketOffset, TargetSocket, Alpha);
	CurrentFOV = FMath::Lerp(CurrentFOV, TargetFOV, Alpha);
	SpringArm->TargetArmLength = CurrentArmLength;
	SpringArm->SocketOffset = CurrentSocketOffset;
	Camera->SetFieldOfView(CurrentFOV);
}

void ARFCharacter::UpdateFacingMode()
{
	// Bots face their focus (enemy) while engaging; otherwise they turn into their movement.
	const AAIController* AI = Cast<AAIController>(Controller);
	const bool bBotFocused = AI && (AI->GetFocusActor() != nullptr || FAISystem::IsValidLocation(AI->GetFocalPoint()));
	const bool bFaceYaw = bAiming || FaceControlYawTimer > 0.f || bBotFocused;
	bUseControllerRotationYaw = bFaceYaw;
	GetCharacterMovement()->bOrientRotationToMovement = !bFaceYaw;
}

void ARFCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
	SensitivityToastTimer = FMath::Max(0.f, SensitivityToastTimer - DeltaTime);
	ReloadFeedbackTimer = FMath::Max(0.f, ReloadFeedbackTimer - DeltaTime);
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
		float Spread = WeaponTuning->Values.CurrentSpreadDegrees(bAiming, GetVelocity().Size2D()) * FormationAccuracyScale;
		if (SuppressionTimer > 0.f) Spread += SuppressionSwayDegrees; // FireMusketAt adds this cone too
		AimComponent->ResolveFromOwner(Spread, WeaponTuning->Values.MaxRangeMeters * 100.f);
	}
	RefreshNameplate();
}

void ARFCharacter::NotifyFired()
{
	FaceControlYawTimer = FaceControlYawHoldSeconds;
	if (WeaponTuning)
	{
		ApplyCameraKick(WeaponTuning->Values.RecoilPitchDegrees, WeaponTuning->Values.RecoilYawDegrees);
		RecoilRecovery = WeaponTuning->Values.RecoilRecoverySeconds;
	}
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
		S.Modifiers.Add(Negate(true, false));
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
	if (!VP || VP->HasMouseCapture() || !VP->HasFocus()) return;
	PC->SetInputMode(FInputModeGameOnly());
	UE_LOG(LogTemp, Display, TEXT("PA250 input: game viewport had no mouse capture; took it for mouse/trackpad look"));
}

void ARFCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildDefaultInput();
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ARFCharacter::Move);
		EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &ARFCharacter::Look);
		EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
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
		EIC->BindAction(IA_Crouch, ETriggerEvent::Started, this, &ARFCharacter::OnDuckPressed);
		EIC->BindAction(IA_Crouch, ETriggerEvent::Completed, this, &ARFCharacter::OnDuckReleased);
		EIC->BindAction(IA_SensDown, ETriggerEvent::Started, this, &ARFCharacter::OnSensitivityDown);
	}
}

void ARFCharacter::Move(const FInputActionValue& Value)
{
	EnsureGameMouseCapture();
	const FVector2D V = Value.Get<FVector2D>();
	if (Controller && !bIsDead)
	{
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
		AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
	}
}

namespace
{
// Look sensitivity: multiplier on raw mouse / trackpad deltas (1.0 = the old 1:1 feel, which was too fast).
TAutoConsoleVariable<float> CVarLookSensitivity(
	TEXT("pa250.LookSensitivity"), 0.35f,
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
	const FVector2D V = Value.Get<FVector2D>();
	if (Controller)
	{
		// Slower while aiming down sights: the narrower FOV magnifies every degree.
		const float Scale = GetLookSensitivity() * (bAiming ? AdsLookScale : 1.f);
		AddControllerYawInput(V.X * Scale * YawLookScale);
		AddControllerPitchInput(V.Y * Scale);
	}
}

void ARFCharacter::OnSensitivityUp() { AdjustLookSensitivity(1.15f); }
void ARFCharacter::OnSensitivityDown() { AdjustLookSensitivity(1.f / 1.15f); }

void ARFCharacter::StartSprint() { bSprinting = true; GetCharacterMovement()->MaxWalkSpeed = SprintSpeed; }
void ARFCharacter::StopSprint() { bSprinting = false; GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; }

void ARFCharacter::StartAim()
{
	bAiming = true;
	TargetFOV = AimedFOV;
	UpdateFacingMode();
}

void ARFCharacter::StopAim()
{
	bAiming = false;
	TargetFOV = DefaultFOV;
	UpdateFacingMode();
}

void ARFCharacter::OnShoulderSwap() { ShoulderSign *= -1.f; }
void ARFCharacter::OnFirePressed() { FireMusket(); }
void ARFCharacter::OnMeleePressed()
{
	if (bIsDead || !MeleeComponent) return;
	if (bSprinting) MeleeComponent->DoCharge();
	else MeleeComponent->DoThrust();
}
void ARFCharacter::OnDuckPressed() { SetDucking(true); }
void ARFCharacter::OnDuckReleased() { SetDucking(false); }

void ARFCharacter::SetDucking(bool bDuck)
{
	if (bIsDead) return;
	if (bDuck && !bIsCrouched) Crouch();
	else if (!bDuck && bIsCrouched) UnCrouch();
}

void ARFCharacter::OnChargePressed()
{
	if (bIsDead || !MeleeComponent) return;
	MeleeComponent->DoCharge();
}
void ARFCharacter::OnButtStrokePressed()
{
	if (bIsDead || !MeleeComponent) return;
	MeleeComponent->DoButtStroke();
}
void ARFCharacter::OnReloadCompleted()
{
	if (Musket) Musket->SetReloadHeld(false);
}

void ARFCharacter::ApplySuppression(float Seconds, float SwayDegrees)
{
	SuppressionTimer = FMath::Max(SuppressionTimer, Seconds);
	SuppressionSwayDegrees = FMath::Max(SuppressionSwayDegrees, SwayDegrees);
}

bool ARFCharacter::FireMusket()
{
	if (bIsDead || !Musket || !Musket->CanFire() || !Controller)
	{
		return false;
	}
	const float MaxRange = WeaponTuning ? WeaponTuning->Values.MaxRangeMeters * 100.f : 20000.f;

	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Muzzle = Musket->GetMuzzleLocation()->GetComponentLocation();
	// Spread is applied once, by AMusketWeapon::Fire; the trace only finds the point under the reticle.
	const FPA250AimResult Aim = UPA250AimComponent::ResolveAim(
		GetWorld(), ViewLoc, ViewRot.Vector(), Muzzle, 0.f, MaxRange, this, Musket, FactionId);
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
	if (bIsDead || !Musket || !Musket->CanFire())
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
	const FVector Muzzle = Musket->GetMuzzleLocation()->GetComponentLocation();
	FVector Dir = (TargetLocation - Muzzle).GetSafeNormal();
	if (bCompensateDrop)
	{
		AMusketWeapon::SolveLaunchDirection(Muzzle, TargetLocation, Musket->GetProjectileSpeed(), Musket->GetProjectileGravityZ(), Dir);
	}
	if (SuppressionTimer > 0.f && SuppressionSwayDegrees > 0.f)
	{
		Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(SuppressionSwayDegrees));
	}
	// Spread uses the speed at the trigger pull, so firing on the run is still inaccurate.
	const float OwnerSpeed = GetVelocity().Size2D();
	const bool bShot = Musket->Fire(Dir, bAiming, OwnerSpeed, FormationAccuracyScale);
	if (bShot)
	{
		LockMovement(WeaponTuning ? WeaponTuning->Values.FireStillSeconds : 0.6f);
		NotifyFired();
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

void ARFCharacter::OnReloadStarted()
{
	if (!Musket || bIsDead) return;
	Musket->SetReloadHeld(true);
	const EActiveReloadHit Hit = Musket->CommitReload();
	if (Hit != EActiveReloadHit::None)
	{
		LastReloadHit = Hit;
		ReloadFeedbackTimer = 0.9f;
	}
}

void ARFCharacter::OnDeath(AActor* Victim)
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComp->SetSimulatePhysics(true);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();
	SetLifeSpan(3.f);
}
