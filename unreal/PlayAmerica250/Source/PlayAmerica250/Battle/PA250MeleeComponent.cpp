#include "PA250MeleeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Kismet/GameplayStatics.h"
#include "MusketWeapon.h"
#include "PA250BattleGameMode.h"
#include "PA250BattleAudio.h"
#include "PA250Cover.h"
#include "PA250SoldierAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"

UPA250MeleeComponent::UPA250MeleeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPA250MeleeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!Tuning)
	{
		Tuning = UPA250MeleeTuning::MakeRuntimeDefaults();
	}
}

const FPA250MeleeTuningValues& UPA250MeleeComponent::GetValues() const
{
	static FPA250MeleeTuningValues Fallback;
	return Tuning ? Tuning->Values : Fallback;
}

ARFCharacter* UPA250MeleeComponent::OwnerChar() const
{
	return Cast<ARFCharacter>(GetOwner());
}

void UPA250MeleeComponent::SetFeedback(const FString& Text, bool bHit)
{
	FeedbackText = Text;
	bFeedbackHit = bHit;
	FeedbackTimer = 1.2f;
}

void UPA250MeleeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickHitStop();
	BusyTimer = FMath::Max(0.f, BusyTimer - DeltaTime);
	ChargeCooldown = FMath::Max(0.f, ChargeCooldown - DeltaTime);
	FeedbackTimer = FMath::Max(0.f, FeedbackTimer - DeltaTime);

	ARFCharacter* Me = OwnerChar();
	if (Me && Me->IsDead())
	{
		PendingThrustTimer = -1.f;
		if (IsCharging()) EndCharge(false);
		SetVisual(EPA250MeleeVisual::None);
		FlinchTime = -1.f;
		RearFootLag = 0.f;
		PublishPose();
		return;
	}

	const FPA250MeleeTuningValues& V = GetValues();
	TickVisual(DeltaTime);

	if (PendingThrustTimer >= 0.f)
	{
		// The lunge follows its man through the wind-up, then steps in behind the point.
		if (const ARFCharacter* Target = ThrustTarget.Get(); Target && !Target->IsDead()) FaceTarget(Target, false);
		if (!bStepStarted && VisualTime >= CoilFraction * V.ThrustWindupSeconds) StartThrustStep();

		PendingThrustTimer -= DeltaTime;
		if (PendingThrustTimer <= 0.f)
		{
			PendingThrustTimer = -1.f;
			const ARFCharacter* Victim = SweepAttack(V.ThrustReachCm, V.ThrustRadiusCm, V.ThrustDamage, true, EPA250MeleeAction::Thrust);
			// Full extension on the frame the point lands; a hit holds it there (longer in a dead man).
			VisualTime = V.ThrustWindupSeconds;
			VisualHold = !Victim ? 0.08f : bLastBlowKilled ? 0.25f : 0.16f;
			bStepping = false;
			ApplyStep(StepDir, 0.f, VisualHold + 0.15f);
			BusyTimer = FMath::Max(BusyTimer, VisualHold + 0.2f);
			SetFeedback(Victim ? TEXT("Bayonet hit") : TEXT("Thrust missed"), Victim != nullptr);
		}
	}

	if (IsCharging())
	{
		ChargeTimer = FMath::Max(0.f, ChargeTimer - DeltaTime);
		if (SweepAttack(V.ChargeHitReachCm, V.ChargeHitRadiusCm, V.ChargeDamage, true, EPA250MeleeAction::Charge))
		{
			EndCharge(true);
		}
		else if (ChargeTimer <= 0.f)
		{
			EndCharge(false);
		}
	}

	PublishPose();
}

void UPA250MeleeComponent::SetVisual(EPA250MeleeVisual InVisual, float Hold)
{
	Visual = InVisual;
	VisualTime = 0.f;
	VisualHold = Hold;
	bStepStarted = false;
	bStepping = false;
}

void UPA250MeleeComponent::TickVisual(float DeltaTime)
{
	const FPA250MeleeTuningValues& V = GetValues();
	if (Visual != EPA250MeleeVisual::None) VisualTime += DeltaTime;

	// The rear foot stays where it was while the step carries the body on, then is drawn up.
	const ARFCharacter* Me = OwnerChar();
	if (bStepping && Me)
	{
		const FVector Now = Me->GetActorLocation();
		RearFootLag = FMath::Min(45.f, RearFootLag + FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Now - LastStepLocation, StepDir))));
		LastStepLocation = Now;
	}
	else if (RearFootLag > 0.f && (Visual != EPA250MeleeVisual::Thrust || VisualTime > V.ThrustWindupSeconds + VisualHold))
	{
		RearFootLag = FMath::FInterpTo(RearFootLag, 0.f, DeltaTime, 9.f);
	}

	switch (Visual)
	{
	case EPA250MeleeVisual::Thrust:
		if (PendingThrustTimer < 0.f && VisualTime >= V.ThrustWindupSeconds + VisualHold + ThrustRecoverSeconds) SetVisual(EPA250MeleeVisual::None);
		break;
	case EPA250MeleeVisual::ButtStroke:
		if (VisualTime >= ButtSeconds) SetVisual(EPA250MeleeVisual::None);
		break;
	case EPA250MeleeVisual::Charge:
		if (!IsCharging()) SetVisual(EPA250MeleeVisual::None);
		break;
	case EPA250MeleeVisual::ChargeStrike:
		if (VisualTime >= StrikeDriveSeconds + VisualHold + StrikeRecoverSeconds) SetVisual(EPA250MeleeVisual::None);
		break;
	case EPA250MeleeVisual::Whiff:
		if (VisualTime >= V.ChargeWhiffRecoverySeconds) SetVisual(EPA250MeleeVisual::None);
		break;
	default:
		break;
	}

	if (FlinchTime >= 0.f)
	{
		FlinchTime += DeltaTime;
		if (FlinchTime >= V.FlinchSeconds) FlinchTime = -1.f;
	}
}

bool UPA250MeleeComponent::IsPlantingFeet() const
{
	switch (Visual)
	{
	case EPA250MeleeVisual::Thrust:       return bStepStarted;
	case EPA250MeleeVisual::ChargeStrike:
	case EPA250MeleeVisual::Whiff:        return true;
	default:                              return false;
	}
}

namespace
{
float Smooth01(float X)
{
	X = FMath::Clamp(X, 0.f, 1.f);
	return X * X * (3.f - 2.f * X);
}
}

FPA250MeleePose UPA250MeleeComponent::ThrustPose(float T, float Windup, float Hold)
{
	FPA250MeleePose P;
	const float W = FMath::Max(0.05f, Windup);
	const float CoilEnd = CoilFraction * W;
	if (T < 0.f) return P;
	if (T < CoilEnd)
	{
		const float X = 1.f - T / CoilEnd;
		P.Coil = 1.f - X * X; // quick draw-back that settles
	}
	else if (T < W)
	{
		const float X = (T - CoilEnd) / (W - CoilEnd);
		P.Coil = 1.f - X;
		P.Lunge = X * X; // accelerates into the target and stops dead on it
	}
	else if (T < W + Hold)
	{
		P.Lunge = 1.f;
	}
	else
	{
		P.Lunge = 1.f - Smooth01((T - W - Hold) / ThrustRecoverSeconds);
	}
	return P;
}

FPA250MeleePose UPA250MeleeComponent::ButtPose(float T)
{
	FPA250MeleePose P;
	if (T < 0.f) return P;
	P.Butt = T < 0.08f ? T / 0.08f : T < 0.15f ? 1.f : 1.f - Smooth01((T - 0.15f) / (ButtSeconds - 0.15f));
	return P;
}

FPA250MeleePose UPA250MeleeComponent::ChargePose(float T)
{
	FPA250MeleePose P;
	P.Level = Smooth01(T / 0.15f);
	return P;
}

FPA250MeleePose UPA250MeleeComponent::StrikePose(float T, float Hold)
{
	FPA250MeleePose P;
	if (T < 0.f) return P;
	const float Out = 1.f - Smooth01((T - StrikeDriveSeconds - Hold) / StrikeRecoverSeconds);
	P.Lunge = FMath::Min(1.f, T / StrikeDriveSeconds) * Out;
	P.Level = (1.f - 0.7f * FMath::Min(1.f, T / StrikeDriveSeconds)) * Out;
	return P;
}

FPA250MeleePose UPA250MeleeComponent::WhiffPose(float T, float Recovery)
{
	// Nothing to stop him: he pitches on over a long catching step and straightens up.
	FPA250MeleePose P;
	const float X = FMath::Clamp(T / FMath::Max(0.05f, Recovery), 0.f, 1.f);
	P.Level = 1.f - Smooth01(X);
	P.Lunge = 0.55f * FMath::Sin(UE_PI * X);
	return P;
}

float UPA250MeleeComponent::FlinchWeight(float T, float Duration)
{
	if (T < 0.f || T >= Duration) return 0.f;
	if (T < 0.05f) return T / 0.05f;
	const float X = 1.f - (T - 0.05f) / FMath::Max(0.05f, Duration - 0.05f);
	return X * X;
}

FPA250MeleePose UPA250MeleeComponent::GetBodyPose() const
{
	const FPA250MeleeTuningValues& V = GetValues();
	FPA250MeleePose P;
	switch (Visual)
	{
	case EPA250MeleeVisual::Thrust:       P = ThrustPose(VisualTime, V.ThrustWindupSeconds, VisualHold); break;
	case EPA250MeleeVisual::ButtStroke:   P = ButtPose(VisualTime); break;
	case EPA250MeleeVisual::Charge:       P = ChargePose(VisualTime); break;
	case EPA250MeleeVisual::ChargeStrike: P = StrikePose(VisualTime, VisualHold); break;
	case EPA250MeleeVisual::Whiff:        P = WhiffPose(VisualTime, V.ChargeWhiffRecoverySeconds); break;
	default: break;
	}
	P.RearFootLagCm = RearFootLag;
	return P;
}

void UPA250MeleeComponent::PublishPose()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me) return;
	const bool bDead = Me->IsDead();
	FPA250MeleePose Pose = bDead ? FPA250MeleePose() : GetBodyPose();
	if (Me->Musket)
	{
		// Butt-stroke: muzzle whipped up and back so the butt leads. Coil: the point dips a touch.
		Me->Musket->MeleePitchOffset = 115.f * Pose.Butt - 6.f * Pose.Coil;
	}
	USkeletalMeshComponent* Body = Me->GetMesh();
	UPA250SoldierAnimInstance* Anim = Body ? Cast<UPA250SoldierAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (!Anim)
	{
		SlideMusket(55.f * Pose.Lunge - 12.f * Pose.Coil + 30.f * Pose.Butt + 40.f * Pose.Level);
		return;
	}
	const FVector FlinchWorld = !bDead && FlinchTime >= 0.f
		? FlinchDirWorld * (FlinchStrength * FlinchWeight(FlinchTime, GetValues().FlinchSeconds)) : FVector::ZeroVector;
	if (Pose.IsActive() || !FlinchWorld.IsNearlyZero(1.e-3))
	{
		const FTransform& ToWorld = Body->GetComponentTransform();
		Pose.ForwardCS = ToWorld.InverseTransformVectorNoScale(Me->GetActorForwardVector());
		Pose.RightCS = ToWorld.InverseTransformVectorNoScale(Me->GetActorRightVector());
		Pose.FlinchCS = ToWorld.InverseTransformVectorNoScale(FlinchWorld);
	}
	Anim->SetMeleePose(Pose);
}

void UPA250MeleeComponent::StartHitStop(float Seconds)
{
	AActor* Owner = GetOwner();
	if (!Owner || Seconds <= 0.f) return;
	Owner->CustomTimeDilation = 0.05f;
	HitStopEndSeconds = FPlatformTime::Seconds() + Seconds; // real time: the owner's own clock is nearly stopped
}

void UPA250MeleeComponent::TickHitStop()
{
	if (HitStopEndSeconds > 0.0 && FPlatformTime::Seconds() >= HitStopEndSeconds)
	{
		HitStopEndSeconds = 0.0;
		if (AActor* Owner = GetOwner()) Owner->CustomTimeDilation = 1.f;
	}
}

void UPA250MeleeComponent::ReceiveHit(const FVector& WorldPushDir, float Strength, float StaggerSeconds)
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead()) return;
	FlinchDirWorld = WorldPushDir.GetSafeNormal2D();
	FlinchStrength = FMath::Clamp(Strength, 0.f, 1.f);
	FlinchTime = FlinchDirWorld.IsNearlyZero() ? -1.f : 0.f;
	if (PendingThrustTimer >= 0.f)
	{
		// A blow breaks a wind-up: the first point home wins the exchange.
		PendingThrustTimer = -1.f;
		SetVisual(EPA250MeleeVisual::None);
		BusyTimer = FMath::Max(BusyTimer, 0.3f);
		if (UCharacterMovementComponent* Move = Me->GetCharacterMovement()) Move->RemoveRootMotionSource(TEXT("PA250MeleeStep"));
	}
	Me->LockMovement(StaggerSeconds);
}

void UPA250MeleeComponent::FaceTarget(const ARFCharacter* Target, bool bTurnView)
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || !Target) return;
	const FVector Dir = (Target->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D();
	if (Dir.IsNearlyZero()) return;
	Me->SetActorRotation(Dir.Rotation());
	// When the view steers the body (aiming, just fired) it has to come round too, or the body snaps back.
	if (AController* C = Me->GetController(); C && (bTurnView || Me->bUseControllerRotationYaw))
	{
		FRotator Ctl = C->GetControlRotation();
		Ctl.Yaw = Dir.Rotation().Yaw;
		C->SetControlRotation(Ctl);
	}
}

void UPA250MeleeComponent::ApplyStep(const FVector& Dir, float DistanceCm, float Seconds)
{
	ARFCharacter* Me = OwnerChar();
	UCharacterMovementComponent* Move = Me ? Me->GetCharacterMovement() : nullptr;
	if (!Move || !Move->IsMovingOnGround() || Seconds <= 0.f) return;
	static const FName StepName(TEXT("PA250MeleeStep"));
	Move->RemoveRootMotionSource(StepName);
	TSharedPtr<FRootMotionSource_ConstantForce> Step = MakeShared<FRootMotionSource_ConstantForce>();
	Step->InstanceName = StepName;
	Step->AccumulateMode = ERootMotionAccumulateMode::Override;
	Step->Priority = 9;
	Step->Force = Dir.GetSafeNormal2D() * (DistanceCm / Seconds);
	Step->Duration = Seconds;
	Step->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate); // keep gravity / slopes
	Step->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Step->FinishVelocityParams.ClampVelocity = 0.f;
	Move->ApplyRootMotionSource(Step);
}

void UPA250MeleeComponent::StartThrustStep()
{
	bStepStarted = true;
	const ARFCharacter* Me = OwnerChar();
	if (!Me) return;
	const FPA250MeleeTuningValues& V = GetValues();
	StepDir = Me->GetActorForwardVector().GetSafeNormal2D();
	float Distance = 0.5f * V.ThrustStepCm; // nobody in front: a shorter step behind the point
	if (const ARFCharacter* Target = ThrustTarget.Get(); Target && !Target->IsDead())
	{
		const FVector To = Target->GetActorLocation() - Me->GetActorLocation();
		if (!To.GetSafeNormal2D().IsNearlyZero()) StepDir = To.GetSafeNormal2D();
		Distance = FMath::Clamp(static_cast<float>(To.Size2D()) - V.ThrustStandoffCm, 0.f, V.ThrustStepCm);
	}
	if (Distance > 5.f)
	{
		ApplyStep(StepDir, Distance, FMath::Max(0.05f, V.ThrustWindupSeconds * (1.f - CoilFraction)));
		bStepping = true;
		LastStepLocation = Me->GetActorLocation();
	}
}

bool UPA250MeleeComponent::DoThrust()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead() || IsBusy()) return false;
	const FPA250MeleeTuningValues& V = GetValues();
	BusyTimer = V.ThrustWindupSeconds + 0.3f;
	PendingThrustTimer = V.ThrustWindupSeconds;
	SetVisual(EPA250MeleeVisual::Thrust, 0.08f);
	RearFootLag = 0.f;
	ThrustTarget = FindThrustTarget();
	if (const ARFCharacter* Target = ThrustTarget.Get()) FaceTarget(Target, false);
	FPA250BattleAudio::PlayAt(Me, TEXT("melee_swing"), Me->GetActorLocation(), EPA250SoundRange::Near, 0.8f, 0.1f);
	return true;
}

bool UPA250MeleeComponent::DoButtStroke()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead() || IsBusy()) return false;
	const FPA250MeleeTuningValues& V = GetValues();
	BusyTimer = ButtSeconds;
	SetVisual(EPA250MeleeVisual::ButtStroke);
	FPA250BattleAudio::PlayAt(Me, TEXT("melee_swing"), Me->GetActorLocation(), EPA250SoundRange::Near, 0.7f, 0.15f);
	// Never touches the musket's reload: a butt-stroke mid-reload keeps the reload where it was.
	const ARFCharacter* Victim = SweepAttack(V.ButtStrokeReachCm, 35.f, V.ButtStrokeDamage, false, EPA250MeleeAction::ButtStroke);
	SetFeedback(Victim ? TEXT("Butt-stroke - staggered") : TEXT("Butt-stroke missed"), Victim != nullptr);
	return true;
}

bool UPA250MeleeComponent::DoCharge()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead() || IsBusy() || ChargeCooldown > 0.f || Me->IsMovementLocked()) return false;
	UCharacterMovementComponent* Move = Me->GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround()) return false;
	const FPA250MeleeTuningValues& V = GetValues();

	const float Duration = FMath::Max(0.1f, V.ChargeDurationSeconds);
	// Charge assist: if an enemy is in reach roughly in front, turn to him and dash at him.
	FVector Dir = Me->GetActorForwardVector().GetSafeNormal2D();
	if (const ARFCharacter* Target = FindChargeTarget())
	{
		Dir = (Target->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D();
		Me->SetActorRotation(Dir.Rotation());
		if (AController* C = Me->GetController())
		{
			FRotator Ctl = C->GetControlRotation();
			Ctl.Yaw = Dir.Rotation().Yaw;
			C->SetControlRotation(Ctl);
		}
	}
	TSharedPtr<FRootMotionSource_ConstantForce> Dash = MakeShared<FRootMotionSource_ConstantForce>();
	Dash->InstanceName = TEXT("PA250BayonetCharge");
	Dash->AccumulateMode = ERootMotionAccumulateMode::Override;
	Dash->Priority = 10;
	Dash->Force = Dir * (V.ChargeLungeCm / Duration);
	Dash->Duration = Duration;
	Dash->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate); // keep gravity / slopes
	Dash->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Dash->FinishVelocityParams.ClampVelocity = 200.f;
	ChargeRootMotionId = Move->ApplyRootMotionSource(Dash);

	ChargeTimer = Duration;
	ChargeCooldown = V.ChargeCooldownSeconds;
	SetVisual(EPA250MeleeVisual::Charge);
	SetFeedback(TEXT("CHARGE!"), false);
	FPA250BattleAudio::PlayVoice(Me, Me->FactionId, FMath::RandBool() ? TEXT("charge") : TEXT("charge_b"), Me->GetActorLocation() + FVector(0.f, 0.f, 70.f));
	return true;
}

ARFCharacter* UPA250MeleeComponent::FindChargeTarget() const
{
	const FPA250MeleeTuningValues& V = GetValues();
	return FindAssistTarget(V.ChargeAssistRangeCm, V.ChargeAssistConeDegrees);
}

ARFCharacter* UPA250MeleeComponent::FindThrustTarget() const
{
	const FPA250MeleeTuningValues& V = GetValues();
	return FindAssistTarget(V.ThrustAssistRangeCm, V.ThrustAssistConeDegrees);
}

ARFCharacter* UPA250MeleeComponent::FindAssistTarget(float RangeCm, float ConeDegrees) const
{
	const ARFCharacter* Me = OwnerChar();
	UWorld* World = GetWorld();
	if (!Me || !World) return nullptr;
	const FVector Fwd = Me->GetActorForwardVector().GetSafeNormal2D();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(ConeDegrees));
	ARFCharacter* Best = nullptr;
	float BestD = RangeCm;
	for (TActorIterator<ARFCharacter> It(World); It; ++It)
	{
		ARFCharacter* O = *It;
		if (O == Me || O->IsDead() || O->FactionId.IsNone() || O->FactionId == Me->FactionId) continue;
		const FVector To = O->GetActorLocation() - Me->GetActorLocation();
		const float D = To.Size2D();
		if (D >= BestD || FVector::DotProduct(To.GetSafeNormal2D(), Fwd) < MinDot) continue;
		const TArray<const AActor*> Ignore = {Me, O, Me->Musket.Get(), O->Musket.Get()};
		if (!PA250Cover::HasLineOfSight(World, Me->GetActorLocation() + FVector(0, 0, 60), O->GetActorLocation() + FVector(0, 0, 40), Ignore)) continue;
		BestD = D;
		Best = O;
	}
	return Best;
}

void UPA250MeleeComponent::EndCharge(bool bHit)
{
	ChargeTimer = 0.f;
	ARFCharacter* Me = OwnerChar();
	UCharacterMovementComponent* Move = Me ? Me->GetCharacterMovement() : nullptr;
	if (Move && ChargeRootMotionId != 0)
	{
		Move->RemoveRootMotionSourceByID(ChargeRootMotionId);
		Move->Velocity *= 0.25f;
	}
	ChargeRootMotionId = 0;
	if (!Me || Me->IsDead())
	{
		SetVisual(EPA250MeleeVisual::None);
		return;
	}
	if (bHit)
	{
		// Drive the point home, feet planted, then draw it out.
		SetVisual(EPA250MeleeVisual::ChargeStrike, bLastBlowKilled ? 0.28f : 0.2f);
		ApplyStep(Me->GetActorForwardVector(), 0.f, StrikeDriveSeconds + VisualHold + 0.1f);
		BusyTimer = StrikeDriveSeconds + VisualHold + 0.1f;
		SetFeedback(TEXT("Charge hit!"), true);
	}
	else
	{
		// Whiffed charges are punished so the charge is a commitment, not a free dash.
		SetVisual(EPA250MeleeVisual::Whiff);
		Me->LockMovement(GetValues().ChargeWhiffRecoverySeconds);
		BusyTimer = GetValues().ChargeWhiffRecoverySeconds;
		SetFeedback(TEXT("Charge missed - off balance"), false);
	}
}

void UPA250MeleeComponent::SlideMusket(float OffsetCm)
{
	if (!bHaveMusketRest && FMath::IsNearlyZero(OffsetCm)) return;
	ARFCharacter* Me = OwnerChar();
	USceneComponent* Root = Me && Me->Musket ? Me->Musket->GetRootComponent() : nullptr;
	USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
	if (!Root || !Parent) return;
	if (!bHaveMusketRest)
	{
		MusketRestRelative = Root->GetRelativeLocation();
		bHaveMusketRest = true;
	}
	const FTransform ParentT = Parent->GetSocketTransform(Root->GetAttachSocketName());
	const FVector LocalOffset = ParentT.InverseTransformVectorNoScale(Me->GetActorForwardVector() * OffsetCm);
	Root->SetRelativeLocation(MusketRestRelative + LocalOffset);
}

float UPA250MeleeComponent::ComputeHitDamage(float BaseDamage, bool bAllowBonus, const FVector& VictimForward,
	const FVector& VictimToAttacker, bool bVictimReloading, const FPA250MeleeTuningValues& V)
{
	if (!bAllowBonus) return BaseDamage;
	const float Dot = FVector::DotProduct(VictimForward.GetSafeNormal2D(), VictimToAttacker.GetSafeNormal2D());
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
	const bool bBehind = Angle > V.BackstabAngleDegrees;
	return (bBehind || bVictimReloading) ? BaseDamage * V.BackstabOrReloadMultiplier : BaseDamage;
}

ARFCharacter* UPA250MeleeComponent::SweepAttack(float ReachCm, float RadiusCm, float Damage, bool bAllowBackstabBonus, EPA250MeleeAction Action)
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || !GetWorld() || Me->IsDead()) return nullptr;
	if (!Me->HasAuthority()) return nullptr;

	const FVector Start = Me->GetActorLocation() + FVector(0, 0, 40.f);
	const FVector End = Start + Me->GetActorForwardVector() * ReachCm;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250Melee), false, Me);
	if (Me->Musket) Params.AddIgnoredActor(Me->Musket);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(RadiusCm);
	GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, Shape, Params);

	const FPA250MeleeTuningValues& V = GetValues();
	for (const FHitResult& Hit : Hits)
	{
		ARFCharacter* Victim = Cast<ARFCharacter>(Hit.GetActor());
		if (!Victim || Victim->IsDead() || Victim->FactionId == Me->FactionId) continue;

		const bool bReloading = Victim->Musket && Victim->Musket->IsReloading();
		const float Applied = ComputeHitDamage(Damage, bAllowBackstabBonus, Victim->GetActorForwardVector(),
			Me->GetActorLocation() - Victim->GetActorLocation(), bReloading, V);
		UGameplayStatics::ApplyPointDamage(Victim, Applied, Me->GetActorForwardVector(), Hit,
			Me->GetController(), Me, nullptr);
		FPA250BattleAudio::PlayAt(Me, TEXT("impact_body"), Victim->GetActorLocation(), EPA250SoundRange::Near, 1.f, 0.1f);
		if (APA250BattleGameMode* GM = GetWorld()->GetAuthGameMode<APA250BattleGameMode>())
		{
			GM->NotifyProjectileHit(Me, Victim, Applied); // hitmarker for the local attacker
		}

		const bool bKilled = Victim->IsDead();
		bLastBlowKilled = bKilled;
		const FVector Push = Me->GetActorForwardVector().GetSafeNormal2D();
		if (!bKilled)
		{
			// What the blow does to a man who survives it: rocked along the thrust, feet stopped.
			float Stagger = V.ThrustHitStaggerSeconds, Strength = 0.8f;
			if (Action == EPA250MeleeAction::ButtStroke)
			{
				Stagger = V.ButtStrokeStaggerSeconds;
				Strength = 1.f;
			}
			else if (Action == EPA250MeleeAction::Charge)
			{
				Stagger = 0.f; // thrown back instead
				Strength = 1.f;
				Victim->LaunchCharacter(Push * V.ChargeKnockback + FVector(0.f, 0.f, 150.f), true, true);
			}
			if (Victim->MeleeComponent) Victim->MeleeComponent->ReceiveHit(Push, Strength, Stagger);
			else Victim->LockMovement(Stagger);
		}

		// Weight: both men freeze for an instant and the camera takes the jolt (player's fights only).
		const bool bMine = Me->IsPlayerControlled(), bTheirs = Victim->IsPlayerControlled();
		if ((bMine || bTheirs) && V.HitStopSeconds > 0.f)
		{
			const float Stop = V.HitStopSeconds * (Action == EPA250MeleeAction::Charge ? 1.5f : 1.f);
			StartHitStop(Stop);
			if (!bKilled && Victim->MeleeComponent) Victim->MeleeComponent->StartHitStop(Stop);
		}
		if (bMine)
		{
			Me->ApplyCameraKick(0.6f, 0.4f);
			Me->PunchFOV(Action == EPA250MeleeAction::Charge ? -5.f : -3.f);
		}
		if (bTheirs && !bKilled)
		{
			Victim->ApplyCameraKick(1.8f, 1.4f);
			Victim->PunchFOV(4.f);
		}
		return Victim; // one victim per swing
	}
	return nullptr;
}
