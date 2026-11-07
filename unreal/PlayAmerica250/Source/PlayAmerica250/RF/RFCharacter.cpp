#include "RFCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
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
#include "MusketWeapon.h" // IWYU pragma: keep
#include "RFGameMode.h"
#include "RFHealthComponent.h"
#include "UObject/ConstructorHelpers.h"

ARFCharacter::ARFCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 400.f;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bDoCollisionTest = false;
	SpringArm->SocketOffset = FVector(0.f, 50.f, 70.f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;

	HealthComponent = CreateDefaultSubobject<URFHealthComponent>(TEXT("HealthComponent"));

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

	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->JumpZVelocity = 450.f;

	// Template mannequin (shipped with the project's Characters content pack). Optional.
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
}

void ARFCharacter::SetTeam(FName InFaction, FName InUnitClass)
{
	FactionId = InFaction;
	UnitClass = InUnitClass;
	ApplyTeamMarker();
}

void ARFCharacter::ApplyTeamMarker()
{
	if (!TeamMarker || !TeamMarker->GetStaticMesh())
	{
		return;
	}
	// British redcoats vs Continental blue.
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

	CurrentFOV = TargetFOV = DefaultFOV;
	Camera->SetFieldOfView(DefaultFOV);
	ApplyTeamMarker();

	HealthComponent->OnDied.AddDynamic(this, &ARFCharacter::OnDeath);

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

void ARFCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!FMath::IsNearlyEqual(CurrentFOV, TargetFOV, 0.1f))
	{
		CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, FOVInterpSpeed);
		Camera->SetFieldOfView(CurrentFOV);
	}
}

// ---------------------------------------------------------------- input

void ARFCharacter::BuildDefaultInput()
{
	if (IMC_ThirdPerson && IA_Move && IA_Look && IA_Fire)
	{
		return;
	}
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) {
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	IA_Move = MakeAction(TEXT("IA_RF_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(TEXT("IA_RF_Look"), EInputActionValueType::Axis2D);
	IA_Jump = MakeAction(TEXT("IA_RF_Jump"), EInputActionValueType::Boolean);
	IA_Sprint = MakeAction(TEXT("IA_RF_Sprint"), EInputActionValueType::Boolean);
	IA_Aim = MakeAction(TEXT("IA_RF_Aim"), EInputActionValueType::Boolean);
	IA_Fire = MakeAction(TEXT("IA_RF_Fire"), EInputActionValueType::Boolean);

	IMC_ThirdPerson = NewObject<UInputMappingContext>(this, TEXT("IMC_RF_Default"));
	auto Swizzle = [this]() {
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this);
		S->Order = EInputAxisSwizzle::YXZ;
		return S;
	};
	auto Negate = [this](bool bX, bool bY) {
		UInputModifierNegate* N = NewObject<UInputModifierNegate>(this);
		N->bX = bX;
		N->bY = bY;
		N->bZ = false;
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
	Map(IA_Aim, EKeys::C);              // trackpad: hold C, the pad has no comfortable right-click
	Map(IA_Aim, EKeys::LeftAlt);
	Map(IA_Fire, EKeys::LeftMouseButton);
	Map(IA_Fire, EKeys::F);             // trackpad: tap-to-look would otherwise fire
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
	}
}

void ARFCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	if (Controller && !bIsDead)
	{
		AddMovementInput(GetActorForwardVector(), V.Y);
		AddMovementInput(GetActorRightVector(), V.X);
	}
}

void ARFCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	if (Controller)
	{
		AddControllerYawInput(V.X);
		AddControllerPitchInput(V.Y);
	}
}

void ARFCharacter::StartSprint() { GetCharacterMovement()->MaxWalkSpeed = SprintSpeed; }
void ARFCharacter::StopSprint() { GetCharacterMovement()->MaxWalkSpeed = WalkSpeed; }

void ARFCharacter::StartAim()
{
	bAiming = true;
	TargetFOV = AimedFOV;
	SpringArm->SocketOffset = FVector(0.f, 80.f, 70.f);
}

void ARFCharacter::StopAim()
{
	bAiming = false;
	TargetFOV = DefaultFOV;
	SpringArm->SocketOffset = FVector(0.f, 50.f, 70.f);
}

void ARFCharacter::OnFirePressed() { FireMusket(); }

// ---------------------------------------------------------------- combat

bool ARFCharacter::FireMusket()
{
	if (bIsDead || !Musket || !Musket->CanFire() || !Controller)
	{
		return false;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector TraceEnd = ViewLoc + ViewRot.Vector() * 20000.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MusketAim), true, this);
	Params.AddIgnoredActor(Musket);
	const FVector AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, TraceEnd, ECC_Visibility, Params) ? Hit.ImpactPoint : TraceEnd;
	return FireMusketAt(AimPoint);
}

bool ARFCharacter::FireMusketAt(FVector TargetLocation)
{
	if (bIsDead || !Musket || !Musket->CanFire())
	{
		return false;
	}
	const FVector Muzzle = Musket->GetMuzzleLocation()->GetComponentLocation();
	// Lift the aim point to cancel the projectile's gravity drop over the flight time.
	const float Dist = FVector::Dist(Muzzle, TargetLocation);
	const float Speed = FMath::Max(Musket->GetTuning().ProjectileSpeed, 1.f);
	const float T = Dist / Speed;
	const float GravityScale = 0.3f;  // keep in sync with AMusketProjectile
	TargetLocation.Z += 0.5f * 980.f * GravityScale * T * T;
	return Musket->Fire((TargetLocation - Muzzle).GetSafeNormal());
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
