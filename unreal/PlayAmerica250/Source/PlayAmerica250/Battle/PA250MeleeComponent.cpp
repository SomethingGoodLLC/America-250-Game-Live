#include "PA250MeleeComponent.h"
#include "PA250UnitClasses.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Kismet/GameplayStatics.h"
#include "MusketWeapon.h"
#include "PA250BattleGameMode.h"
#include "PA250BattleAudio.h"
#include "PA250Smoke.h"
#include "PA250Cover.h"
#include "PA250SoldierAnimInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
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
	// Order within a frame: movement, then this (hit sweeps and the pose see where he really is),
	// then the body's animation (it evaluates the pose published this frame, not last frame's).
	if (ARFCharacter* Me = OwnerChar())
	{
		if (Me->GetCharacterMovement()) AddTickPrerequisiteComponent(Me->GetCharacterMovement());
		if (Me->GetMesh()) Me->GetMesh()->AddTickPrerequisiteComponent(this);
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
		bGuarding = false;
		GuardWeight = 0.f;
		PublishPose();
		return;
	}

	const FPA250MeleeTuningValues& V = GetValues();
	TickVisual(DeltaTime);
	TickGuard(DeltaTime);

	if (PendingThrustTimer >= 0.f)
	{
		// The lunge follows its man through the wind-up, then steps in behind the point.
		if (const ARFCharacter* Target = ThrustTarget.Get(); Target && !Target->IsDead()) FaceTarget(Target, false);
		const bool bStepWasRunning = bStepStarted;
		if (!bStepStarted && VisualTime >= CoilFraction * ActiveWindup) StartThrustStep();

		// The point lands when the wind-up is over AND the step has carried him in. On a slow frame both
		// come due at once; resolving the hit then would sweep from where he stood before stepping (and miss).
		PendingThrustTimer = FMath::Max(0.f, PendingThrustTimer - DeltaTime);
		if (bStepWasRunning) StepRemaining = FMath::Max(0.f, StepRemaining - DeltaTime);
		if (PendingThrustTimer <= 0.f && StepRemaining <= 0.f)
		{
			PendingThrustTimer = -1.f;
			const ARFCharacter* Victim = bClubSwing
				? SweepAttack(V.ClubReachCm, V.ClubRadiusCm, V.ClubDamage, true, EPA250MeleeAction::Club)
				: SweepAttack(V.ThrustReachCm, V.ThrustRadiusCm, V.ThrustDamage, true, EPA250MeleeAction::Thrust);
			bStepping = false;
			if (LastBlow == EPA250Blow::Parried)
			{
				// Turned aside: no hold, no recovery - he is thrown off (ReceiveHit already stopped him).
				SetVisual(EPA250MeleeVisual::None);
				SetFeedback(TEXT("Parried - off balance"), false);
			}
			else
			{
				// Full extension on the frame the point lands; a hit holds it there (longer in a dead man).
				VisualTime = ActiveWindup;
				VisualHold = !Victim ? 0.08f : bLastBlowKilled ? 0.25f : 0.16f;
				ApplyStep(StepDir, 0.f, VisualHold + 0.15f);
				BusyTimer = FMath::Max(BusyTimer, VisualHold + 0.2f);
				SetFeedback(!Victim ? (bClubSwing ? TEXT("Swing missed") : TEXT("Thrust missed"))
					: LastBlow == EPA250Blow::Blocked ? TEXT("Blocked - butt-stroke (Q) breaks a guard")
					: bLastBlowKilled ? (bClubSwing ? TEXT("Struck him down") : TEXT("Run through"))
					: bRiposteThrust ? TEXT("Riposte!") : bClubSwing ? TEXT("Clubbed him - staggered") : TEXT("Bayonet hit"),
					Victim != nullptr && LastBlow != EPA250Blow::Blocked);
			}
			bRiposteThrust = false;
		}
	}

	if (IsCharging())
	{
		ChargeTimer = FMath::Max(0.f, ChargeTimer - DeltaTime);
		// Swept from where he was last frame, so a slow frame cannot carry the point through a man unnoticed.
		const FVector From = LastChargeLocation;
		LastChargeLocation = Me ? Me->GetActorLocation() : From;
		if (ARFCharacter* Struck = SweepAttack(V.ChargeHitReachCm, V.ChargeHitRadiusCm, bClubCharge ? V.ClubChargeDamage : V.ChargeDamage, true, EPA250MeleeAction::Charge, &From))
		{
			ChargeVictim = Struck;
			const bool bParried = LastBlow == EPA250Blow::Parried;
			EndCharge(!bParried);
			if (bParried) SetFeedback(TEXT("Charge parried - off balance"), false);
			else if (LastBlow == EPA250Blow::Blocked) SetFeedback(TEXT("Charge blocked"), false);
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
	StepRemaining = 0.f;
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
	else if (RearFootLag > 0.f && (Visual != EPA250MeleeVisual::Thrust || VisualTime > ActiveWindup + VisualHold))
	{
		RearFootLag = FMath::FInterpTo(RearFootLag, 0.f, DeltaTime, 9.f);
	}

	switch (Visual)
	{
	case EPA250MeleeVisual::Thrust:
		if (PendingThrustTimer < 0.f && VisualTime >= ActiveWindup + VisualHold + ThrustRecoverSeconds) SetVisual(EPA250MeleeVisual::None);
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

void UPA250MeleeComponent::TickReloadPose(float DeltaTime)
{
	ARFCharacter* Me = OwnerChar();
	AMusketWeapon* Gun = Me ? Me->Musket.Get() : nullptr;
	float Target = 0.f;
	bool bGrounded = false;
	if (Gun && Gun->IsReloading() && !Me->IsDead() && !Me->IsMounted())
	{
		const EActiveReloadPhase Phase = Gun->GetSequencer().GetPhase();
		if (Phase == EActiveReloadPhase::PrimePan || Phase == EActiveReloadPhase::RamCharge || Phase == EActiveReloadPhase::ShoulderAim)
		{
			ReloadPhase = static_cast<uint8>(Phase);
			ReloadProgress = FMath::Clamp(Gun->GetSequencer().GetPhaseProgress(), 0.f, 1.f);
		}
		// Any swing, the guard, a blow, or the shot's follow-through interrupts the hands (the reload itself runs on).
		const bool bFree = Visual == EPA250MeleeVisual::None && !bGuarding && FlinchTime < 0.f && PresentTimer <= 0.f && !IsCharging();
		if (bFree && ReloadPhase != 0)
		{
			// Recovering arms: the hold eases back to the carry over the last drill phase.
			Target = ReloadPhase == static_cast<uint8>(EActiveReloadPhase::ShoulderAim) ? 1.f - Smooth01((ReloadProgress - 0.35f) / 0.6f) : 1.f;
		}
		// The butt goes to the ground to ram only if he is standing still; on the move he keeps it at the waist.
		bGrounded = ReloadPhase >= static_cast<uint8>(EActiveReloadPhase::RamCharge) && Me->GetVelocity().Size2D() < 60.f;
	}
	else
	{
		ReloadPhase = 0;
	}
	ReloadWeight = FMath::FInterpTo(ReloadWeight, Target, DeltaTime, Target > ReloadWeight ? 7.f : 9.f);
	RamBlend = FMath::FInterpTo(RamBlend, bGrounded ? 1.f : 0.f, DeltaTime, 4.5f);
}

FVector UPA250MeleeComponent::ReloadHandLocal(uint8 Phase, float Progress, const FVector& Root, const FVector& GunForward, float MuzzleCm, float RamrodCm)
{
	// Soldier space: X forward, Y right, Z up from the feet.
	const FVector Pan = Root + GunForward * 6.f + FVector(0.f, 0.f, 7.f);
	const FVector Box(-6.f, 24.f, 96.f);    // cartridge box on the right hip
	const FVector Mouth(13.f, 3.f, 156.f);  // bite the cartridge
	const FVector Muzzle = Root + GunForward * MuzzleCm;
	const FVector Top = Muzzle + GunForward * 7.f;
	const FVector High = Muzzle + GunForward * 36.f;
	auto Seg = [Progress](float A, float B) { return Smooth01((Progress - A) / FMath::Max(0.001f, B - A)); };
	if (Phase == static_cast<uint8>(EActiveReloadPhase::PrimePan))
	{
		// Handle cartridge, bite it, prime the pan, shut it.
		if (Progress < 0.3f) return FMath::Lerp(Pan, Box, Seg(0.f, 0.2f));
		if (Progress < 0.6f) return FMath::Lerp(Box, Mouth, Seg(0.3f, 0.5f));
		return FMath::Lerp(Mouth, Pan, Seg(0.6f, 0.85f));
	}
	if (Phase == static_cast<uint8>(EActiveReloadPhase::RamCharge))
	{
		// Charge the muzzle, draw the rammer, three strokes, return it.
		if (Progress < 0.12f) return FMath::Lerp(Pan, Top, Seg(0.f, 0.11f));
		if (RamrodCm >= 0.f)
		{
			// Follow the musket's own ramrod. The bone is the rod's leading end: down the barrel while he rams
			// (his hand is a rod's length above it), out past the muzzle while he draws and returns it.
			const float Along = RamrodCm < 100.f ? RamrodCm + 95.f : RamrodCm - 20.f;
			return Root + GunForward * FMath::Clamp(Along, MuzzleCm + 4.f, MuzzleCm + 45.f);
		}
		if (Progress < 0.2f) return Top;
		if (Progress < 0.3f) return FMath::Lerp(Top, High, Seg(0.2f, 0.3f));
		if (Progress < 0.8f)
		{
			const float Stroke = 0.5f + 0.5f * FMath::Cos(2.f * UE_PI * 3.f * (Progress - 0.3f) / 0.5f); // 1 = up, 0 = home
			return FMath::Lerp(Top, High, Stroke);
		}
		return FMath::Lerp(High, Top, Seg(0.8f, 0.92f));
	}
	// Recover: the hand goes back to the wrist of the stock.
	return FMath::Lerp(Top, Pan, Seg(0.f, 0.35f));
}

bool UPA250MeleeComponent::IssuedBayonet(FName Faction, FName UnitClass, uint32 Seed)
{
	switch (PA250UnitClasses::KitFor(Faction, UnitClass).Bayonet)
	{
	case EPA250Bayonet::None: return false;
	case EPA250Bayonet::Some: return Seed % 4 == 0;
	default: return true;
	}
}

bool UPA250MeleeComponent::HasBayonet() const
{
	if (BayonetOverride >= 0) return BayonetOverride != 0;
	const ARFCharacter* Me = OwnerChar();
	return !Me || IssuedBayonet(Me->FactionId, Me->UnitClass, Me->GetClothingSeed());
}

FPA250MeleePose UPA250MeleeComponent::ClubPose(float T, float Windup, float Hold)
{
	// Up over the shoulder, then down: the blow is fastest as it lands.
	FPA250MeleePose P;
	const float W = FMath::Max(0.05f, Windup);
	const float RaiseEnd = CoilFraction * W;
	if (T < 0.f) return P;
	if (T < RaiseEnd)
	{
		const float X = 1.f - T / RaiseEnd;
		P.Raise = 1.f - X * X;
	}
	else if (T < W)
	{
		const float X = (T - RaiseEnd) / (W - RaiseEnd);
		P.Raise = 1.f - X;
		P.Chop = X * X;
	}
	else if (T < W + Hold)
	{
		P.Chop = 1.f;
	}
	else
	{
		P.Chop = 1.f - Smooth01((T - W - Hold) / ThrustRecoverSeconds);
	}
	return P;
}

FPA250MeleePose UPA250MeleeComponent::GetBodyPose() const
{
	const FPA250MeleeTuningValues& V = GetValues();
	FPA250MeleePose P;
	switch (Visual)
	{
	// Until the hit is resolved the lunge only goes as far as full extension (never on into the recovery).
	case EPA250MeleeVisual::Thrust:
	{
		const float T = PendingThrustTimer >= 0.f ? FMath::Min(VisualTime, ActiveWindup) : VisualTime;
		P = bClubSwing ? ClubPose(T, ActiveWindup, VisualHold) : ThrustPose(T, ActiveWindup, VisualHold);
		break;
	}
	case EPA250MeleeVisual::ButtStroke:   P = ButtPose(VisualTime); break;
	case EPA250MeleeVisual::Charge:       P = ChargePose(VisualTime); break;
	case EPA250MeleeVisual::ChargeStrike: P = StrikePose(VisualTime, VisualHold); break;
	case EPA250MeleeVisual::Whiff:        P = WhiffPose(VisualTime, V.ChargeWhiffRecoverySeconds); break;
	default: break;
	}
	if (bClubCharge && (Visual == EPA250MeleeVisual::Charge || Visual == EPA250MeleeVisual::ChargeStrike))
	{
		// The rush: musket carried up over the shoulder, brought down on contact instead of driven in.
		// On contact the raise hands over to the blow (dropping it at once left the musket level for a frame).
		P.Chop = P.Lunge;
		P.Raise = Visual == EPA250MeleeVisual::Charge ? P.Level : VisualTime < StrikeDriveSeconds ? 1.f - P.Chop : 0.f;
		P.Lunge = 0.f;
		P.Level *= 0.5f;
	}
	P.RearFootLagCm = RearFootLag;
	P.Guard = GuardWeight;
	P.Shoulder = ShoulderWeight;
	P.Reload = ReloadWeight;
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
		// Guard: carried high across the body, muzzle up and to the left.
		// Club: the musket turns end for end (butt up over the shoulder at -70, butt out in front at -190).
		Me->Musket->MeleePitchOffset = 115.f * Pose.Butt - 6.f * Pose.Coil + 48.f * Pose.Guard - 70.f * Pose.Raise - 190.f * Pose.Chop;
		Me->Musket->MeleeYawOffset = -24.f * Pose.Guard;
		// A man with a bayonet shows one; a man without does not.
		if (const bool bFixed = HasBayonet(); bFixed != Me->Musket->IsBayonetFixed()) Me->Musket->SetBayonetFixed(bFixed);
	}
	USkeletalMeshComponent* Body = Me->GetMesh();
	UPA250SoldierAnimInstance* Anim = Body ? Cast<UPA250SoldierAnimInstance>(Body->GetAnimInstance()) : nullptr;
	// Clubbed, the hands hold the barrel: the musket slides back through them so the butt is the far end.
	const float GripShift = 70.f * FMath::Max(Pose.Raise, Pose.Chop);
	const FVector ClubOffset = Me->Musket ? -Me->Musket->GetActorForwardVector() * GripShift : FVector::ZeroVector;
	if (!Anim)
	{
		// Mannequin fallback (no native body anim): the musket alone carries the motion.
		OffsetMusket(ClubOffset + Me->GetActorForwardVector() * (55.f * Pose.Lunge - 12.f * Pose.Coil + 30.f * Pose.Butt + 40.f * Pose.Level + 40.f * Pose.Chop));
		return;
	}
	// The musket hangs off the wrist bone; seat it in the palm (most of the way to the knuckles).
	FVector Palm = FVector::ZeroVector;
	if (Me->IsUsingPeriodSoldier() && Body->GetBoneIndex(TEXT("middle_01_r")) != INDEX_NONE && Body->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE)
	{
		Palm = 0.9f * (Body->GetBoneLocation(TEXT("middle_01_r"), EBoneSpaces::WorldSpace) - Body->GetBoneLocation(TEXT("hand_r"), EBoneSpaces::WorldSpace));
		if (Palm.ContainsNaN() || Palm.Size() > 12.f) Palm = FVector::ZeroVector;
	}
	// Reload drill: the musket is parked on the body (waist to prime, butt grounded to ram) and both hands follow it.
	const FTransform& BodyToWorld = Body->GetComponentTransform();
	const FVector FwdCS = BodyToWorld.InverseTransformVectorNoScale(Me->GetActorForwardVector());
	const FVector RightCS = BodyToWorld.InverseTransformVectorNoScale(Me->GetActorRightVector());
	auto LocalToWorld = [&](const FVector& L) { return BodyToWorld.TransformPosition(FwdCS * L.X + RightCS * L.Y + FVector::UpVector * L.Z); };
	FVector ReloadShift = FVector::ZeroVector, Parked = FVector::ZeroVector;
	bool bParked = false;
	if (Me->Musket) Me->Musket->BodyPoseAlpha = bDead ? 0.f : ReloadWeight;
	if (!bDead && ReloadWeight > 0.005f && Me->Musket && Me->IsUsingPeriodSoldier())
	{
		const FQuat PrimeRot = FRotator(10.f, -35.f, 0.f).Quaternion(), RamRot = FRotator(100.f, -15.f, 0.f).Quaternion();
		const FQuat Rel = FQuat::Slerp(PrimeRot, RamRot, RamBlend).GetNormalized();
		const FVector GunFwd = Rel.RotateVector(FVector::ForwardVector);
		const FVector PrimeRoot(18.f, 8.f, 106.f);
		const FVector RamRoot = FVector(40.f, -14.f, 3.f) + RamRot.RotateVector(FVector::ForwardVector) * 35.f; // butt on the ground by the left foot
		const FVector Root = FMath::Lerp(PrimeRoot, RamRoot, RamBlend);
		const float MuzzleCm = Me->Musket->GetMuzzleLocation() ? Me->Musket->GetMuzzleLocation()->GetRelativeLocation().X : 115.f;
		Me->Musket->BodyPoseRotation = FRotator(0.f, Me->GetActorRotation().Yaw, 0.f).Quaternion() * Rel;
		float RamrodCm = -1.f;
		if (const USkeletalMeshComponent* GunMesh = Me->Musket->GetAnimatedMesh(); GunMesh && GunMesh->GetBoneIndex(TEXT("b_gun_Ramrod")) != INDEX_NONE)
		{
			RamrodCm = Me->Musket->GetActorTransform().InverseTransformPosition(GunMesh->GetSocketLocation(TEXT("b_gun_Ramrod"))).X;
		}
		const FVector Hand = ReloadHandLocal(ReloadPhase, ReloadProgress, Root, GunFwd, MuzzleCm, RamrodCm);
		Pose.ReloadRightHandCS = FwdCS * Hand.X + RightCS * Hand.Y + FVector::UpVector * Hand.Z;
		// From wherever the right hand is carrying it to the parked spot.
		if (USceneComponent* GunRoot = Me->Musket->GetRootComponent(); GunRoot && GunRoot->GetAttachParent())
		{
			const FVector Carried = GunRoot->GetAttachParent()->GetSocketLocation(GunRoot->GetAttachSocketName());
			ReloadShift = ReloadWeight * (LocalToWorld(Root) - Carried - Palm);
		}
		Parked = LocalToWorld(Root);
		bParked = ReloadWeight > 0.98f;
	}
	else
	{
		Pose.Reload = 0.f;
	}
	OffsetMusket(ClubOffset + Palm + ReloadShift, bParked ? &Parked : nullptr);
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
	if (SlowMoEndSeconds > 0.0 && FPlatformTime::Seconds() >= SlowMoEndSeconds)
	{
		SlowMoEndSeconds = 0.0;
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
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
	TSharedPtr<FRootMotionSource> Step;
	if (DistanceCm > 0.f)
	{
		// Move-to, not a constant force: a force runs for whole frames, so one slow frame carried
		// the lunge twice its length (measured 228 cm for a 110 cm step at ~15 fps).
		TSharedPtr<FRootMotionSource_MoveToForce> To = MakeShared<FRootMotionSource_MoveToForce>();
		To->StartLocation = Me->GetActorLocation();
		To->TargetLocation = To->StartLocation + Dir.GetSafeNormal2D() * DistanceCm;
		To->bRestrictSpeedToExpected = false;
		Step = To;
	}
	else
	{
		TSharedPtr<FRootMotionSource_ConstantForce> Hold = MakeShared<FRootMotionSource_ConstantForce>();
		Hold->Force = FVector::ZeroVector;
		Step = Hold;
	}
	Step->InstanceName = StepName;
	Step->AccumulateMode = ERootMotionAccumulateMode::Override;
	Step->Priority = 9;
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
		Distance = FMath::Clamp(static_cast<float>(To.Size2D()) - (bClubSwing ? V.ClubStandoffCm : V.ThrustStandoffCm), 0.f, V.ThrustStepCm);
	}
	const UCharacterMovementComponent* Move = Me->GetCharacterMovement();
	if (Distance > 5.f && Move && Move->IsMovingOnGround())
	{
		StepRemaining = FMath::Max(0.05f, ActiveWindup * (1.f - CoilFraction));
		ApplyStep(StepDir, Distance, StepRemaining);
		bStepping = true;
		LastStepLocation = Me->GetActorLocation();
	}
}

bool UPA250MeleeComponent::DoThrust()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead() || IsBusy()) return false;
	const FPA250MeleeTuningValues& V = GetValues();
	ThrustTarget = FindThrustTarget();
	ARFCharacter* Target = ThrustTarget.Get();
	// Bots know the answer to a raised guard: close enough, most of them go in with the butt instead.
	if (Target && !Me->IsPlayerControlled() && Target->MeleeComponent && Target->MeleeComponent->IsGuarding()
		&& FVector::Dist2D(Target->GetActorLocation(), Me->GetActorLocation()) <= V.ButtStrokeReachCm + 35.f && FMath::FRand() < 0.6f)
	{
		FaceTarget(Target, false);
		return DoButtStroke();
	}
	// Riposte: straight off a parry, half the wind-up. Against the player a bot's thrust is drawn out so it can be read.
	bRiposteThrust = RiposteTimer > 0.f;
	RiposteTimer = 0.f;
	bClubSwing = !HasBayonet();
	ActiveWindup = bClubSwing ? V.ClubWindupSeconds : V.ThrustWindupSeconds;
	if (bRiposteThrust) ActiveWindup *= 0.5f;
	else if (Target && Target->IsPlayerControlled() && !Me->IsPlayerControlled())
	{
		ActiveWindup = FMath::Max(ActiveWindup, V.ThrustWindupVsPlayerSeconds + (bClubSwing ? 0.1f : 0.f));
	}
	BusyTimer = ActiveWindup + 0.3f;
	PendingThrustTimer = ActiveWindup;
	SetVisual(EPA250MeleeVisual::Thrust, 0.08f);
	RearFootLag = 0.f;
	if (Target)
	{
		FaceTarget(Target, false);
		if (Target->MeleeComponent) Target->MeleeComponent->NotifyIncoming(ActiveWindup);
	}
	FPA250BattleAudio::PlayAt(Me, TEXT("melee_swing"), Me->GetActorLocation(), EPA250SoundRange::Near, 0.8f, 0.1f);
	return true;
}

void UPA250MeleeComponent::RequestThrust()
{
	if (!DoThrust()) BufferedThrustTimer = 0.3f;
}

void UPA250MeleeComponent::NotifyIncoming(float Seconds)
{
	const ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead()) return;
	ThreatTimer = FMath::Max(ThreatTimer, Seconds + 0.15f);
	// Bots: some get the musket up in time (a block, never a parry). A man still reeling cannot.
	if (!Me->IsPlayerControlled() && Visual == EPA250MeleeVisual::None && !bGuarding && FlinchTime < 0.f
		&& FMath::FRand() < GetValues().BotGuardChance)
	{
		AutoGuardTimer = Seconds + 0.35f;
	}
}

EPA250Guard UPA250MeleeComponent::ResolveGuard(const FVector& AttackerLocation) const
{
	const ARFCharacter* Me = OwnerChar();
	if (!bGuarding || !Me || Me->IsDead()) return EPA250Guard::None;
	const FPA250MeleeTuningValues& V = GetValues();
	const FVector To = (AttackerLocation - Me->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(Me->GetActorForwardVector().GetSafeNormal2D(), To) < FMath::Cos(FMath::DegreesToRadians(V.GuardArcDegrees))) return EPA250Guard::None;
	return bGuardHeld && GuardAge <= V.ParryWindowSeconds ? EPA250Guard::Parry : EPA250Guard::Block;
}

void UPA250MeleeComponent::TickGuard(float DeltaTime)
{
	AutoGuardTimer = FMath::Max(0.f, AutoGuardTimer - DeltaTime);
	ThreatTimer = FMath::Max(0.f, ThreatTimer - DeltaTime);
	RiposteTimer = FMath::Max(0.f, RiposteTimer - DeltaTime);
	GuardLockout = FMath::Max(0.f, GuardLockout - DeltaTime);
	BufferedThrustTimer = FMath::Max(0.f, BufferedThrustTimer - DeltaTime);

	// The guard is a stance, not an attack: it drops for any swing, and a man who is reeling cannot hold it.
	const bool bWant = (bGuardHeld || AutoGuardTimer > 0.f) && Visual == EPA250MeleeVisual::None && !IsCharging()
		&& FlinchTime < 0.f && GuardLockout <= 0.f;
	if (bWant != bGuarding)
	{
		bGuarding = bWant;
		GuardAge = 0.f;
		if (!bWant && Visual == EPA250MeleeVisual::None) GuardLockout = 0.4f; // no flicking it up and down to parry everything
	}
	if (bGuarding) GuardAge += DeltaTime;
	GuardWeight = FMath::FInterpTo(GuardWeight, bGuarding ? 1.f : 0.f, DeltaTime, 16.f);

	// Musket to the shoulder: while the player aims, while a bot presents, and for a beat after any shot.
	if (const ARFCharacter* Me = OwnerChar())
	{
		const bool bLoaded = Me->Musket && Me->Musket->CanFire();
		if (bMusketWasLoaded && !bLoaded && Me->Musket && Me->Musket->IsReloading()) Present(0.55f);
		bMusketWasLoaded = bLoaded;
		if (Me->IsAiming()) Present(0.12f);
	}
	PresentTimer = FMath::Max(0.f, PresentTimer - DeltaTime);
	const bool bShouldered = PresentTimer > 0.f && Visual == EPA250MeleeVisual::None && !bGuarding && FlinchTime < 0.f;
	ShoulderWeight = FMath::FInterpTo(ShoulderWeight, bShouldered ? 1.f : 0.f, DeltaTime, 12.f);

	TickReloadPose(DeltaTime);

	if (BufferedThrustTimer > 0.f && !IsBusy())
	{
		BufferedThrustTimer = 0.f;
		DoThrust();
	}
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
	LastChargeLocation = Me->GetActorLocation();
	bClubCharge = !HasBayonet();
	if (ARFCharacter* Target = FindChargeTarget())
	{
		if (Target->MeleeComponent) Target->MeleeComponent->NotifyIncoming(Duration);
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
		// A slow frame can carry him right onto his man before the hit is seen: stand him off at striking distance.
		if (const ARFCharacter* Struck = ChargeVictim.Get())
		{
			const FVector Away = (Me->GetActorLocation() - Struck->GetActorLocation()).GetSafeNormal2D();
			const float Gap = FVector::Dist2D(Me->GetActorLocation(), Struck->GetActorLocation());
			const float Standoff = bClubCharge ? GetValues().ClubStandoffCm : 150.f;
			if (!Away.IsNearlyZero() && Gap < Standoff)
			{
				Me->SetActorLocation(Me->GetActorLocation() + Away * (Standoff - Gap), true);
			}
		}
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

void UPA250MeleeComponent::OffsetMusket(const FVector& WorldOffset, const FVector* AbsoluteWorld)
{
	ARFCharacter* Me = OwnerChar();
	USceneComponent* Root = Me && Me->Musket ? Me->Musket->GetRootComponent() : nullptr;
	USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
	if (!Root || !Parent) return;
	if (AbsoluteWorld)
	{
		// Parked on the body (reload): an offset from the hand would shake with every stroke of that hand,
		// because the hand's position here is a frame old.
		if (!bHaveMusketRest)
		{
			MusketRestRelative = Root->GetRelativeLocation();
			bHaveMusketRest = true;
		}
		if (!Root->IsUsingAbsoluteLocation()) Root->SetUsingAbsoluteLocation(true);
		Root->SetWorldLocation(*AbsoluteWorld);
		return;
	}
	if (Root->IsUsingAbsoluteLocation()) Root->SetUsingAbsoluteLocation(false);
	else if (!bHaveMusketRest && WorldOffset.IsNearlyZero()) return;
	if (!bHaveMusketRest)
	{
		MusketRestRelative = Root->GetRelativeLocation();
		bHaveMusketRest = true;
	}
	const FTransform ParentT = Parent->GetSocketTransform(Root->GetAttachSocketName());
	Root->SetRelativeLocation(MusketRestRelative + ParentT.InverseTransformVectorNoScale(WorldOffset));
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

bool UPA250MeleeComponent::IsBehind(const FVector& VictimForward, const FVector& VictimToAttacker, const FPA250MeleeTuningValues& V)
{
	const float Dot = FVector::DotProduct(VictimForward.GetSafeNormal2D(), VictimToAttacker.GetSafeNormal2D());
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f))) > V.BackstabAngleDegrees;
}

ARFCharacter* UPA250MeleeComponent::SweepAttack(float ReachCm, float RadiusCm, float Damage, bool bAllowBackstabBonus, EPA250MeleeAction Action, const FVector* SweepFrom)
{
	LastBlow = EPA250Blow::Miss;
	ARFCharacter* Me = OwnerChar();
	if (!Me || !GetWorld() || Me->IsDead()) return nullptr;
	if (!Me->HasAuthority()) return nullptr;

	const FVector Here = Me->GetActorLocation() + FVector(0, 0, 40.f);
	const FVector Start = SweepFrom ? *SweepFrom + FVector(0, 0, 40.f) : Here;
	const FVector End = Here + Me->GetActorForwardVector() * ReachCm;
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

		const FVector Push = Me->GetActorForwardVector().GetSafeNormal2D();
		const FVector VictimToMe = Me->GetActorLocation() - Victim->GetActorLocation();
		const bool bMine = Me->IsPlayerControlled(), bTheirs = Victim->IsPlayerControlled();
		const bool bRiposte = (Action == EPA250MeleeAction::Thrust || Action == EPA250MeleeAction::Club) && bRiposteThrust;
		// A butt-stroke goes under or round a guard: it is the guard-breaker.
		const EPA250Guard Guard = Action != EPA250MeleeAction::ButtStroke && Victim->MeleeComponent
			? Victim->MeleeComponent->ResolveGuard(Me->GetActorLocation()) : EPA250Guard::None;

		if (Guard == EPA250Guard::Parry)
		{
			// Turned aside at the last moment: nothing lands, the attacker is thrown off and open to a riposte.
			LastBlow = EPA250Blow::Parried;
			bLastBlowKilled = false;
			FPA250BattleAudio::PlayAtOr(Me, TEXT("bayonet_clash"), TEXT("flint_click"), Victim->GetActorLocation(), EPA250SoundRange::Near, 1.f, 0.05f);
			UPA250MeleeComponent* Theirs = Victim->MeleeComponent;
			Theirs->RiposteTimer = V.RiposteWindowSeconds;
			Theirs->ThreatTimer = 0.f;
			Theirs->SetFeedback(TEXT("Parried! Strike back - E"), true);
			if ((bMine || bTheirs) && V.HitStopSeconds > 0.f)
			{
				StartHitStop(V.HitStopSeconds);
				Theirs->StartHitStop(V.HitStopSeconds);
			}
			ReceiveHit(-Push, 1.f, V.ParryStaggerSeconds);
			BusyTimer = FMath::Max(BusyTimer, V.ParryStaggerSeconds);
			return Victim;
		}

		const bool bReloading = Victim->Musket && Victim->Musket->IsReloading();
		float Applied = ComputeHitDamage(Damage, bAllowBackstabBonus, Victim->GetActorForwardVector(), VictimToMe, bReloading, V);
		Applied *= FMath::Max(0.f, DamageScale);
		if (bRiposte) Applied *= V.RiposteDamageMultiplier;
		const bool bBlocked = Guard == EPA250Guard::Block;
		if (bBlocked) Applied *= Action == EPA250MeleeAction::Charge ? V.ChargeBlockDamageScale : V.BlockDamageScale;

		// No one-blow kills from the front. The reload and vulnerability multipliers stacked to 3x, so a
		// charge always killed a man who had just fired; now the first frontal blow leaves him standing
		// (and warned). From behind, or as a riposte, the bayonet still kills outright.
		URFHealthComponent* Health = Victim->HealthComponent;
		const bool bMercy = Action != EPA250MeleeAction::ButtStroke && !bRiposte && V.FrontalBlowLeavesHealth > 0.f && Health
			&& !IsBehind(Victim->GetActorForwardVector(), VictimToMe, V)
			&& Health->CurrentHealth >= V.FrontalMercyHealthFraction * Health->MaxHealth;
		const float HealthBefore = Health ? Health->CurrentHealth : 0.f;
		if (bMercy) Health->MinHealthAfterNextDamage = V.FrontalBlowLeavesHealth;
		UGameplayStatics::ApplyPointDamage(Victim, Applied, Me->GetActorForwardVector(), Hit,
			Me->GetController(), Me, nullptr);
		if (Health) Health->MinHealthAfterNextDamage = 0.f;
		// Steel on the musket for a block, the stock's knock for a butt-stroke, the point going home otherwise.
		if (bBlocked) FPA250BattleAudio::PlayAtOr(Me, TEXT("bayonet_clash"), TEXT("flint_click"), Victim->GetActorLocation(), EPA250SoundRange::Near, 0.85f, 0.1f);
		else FPA250BattleAudio::PlayAtOr(Me, Action == EPA250MeleeAction::ButtStroke ? TEXT("butt_thud") : TEXT("bayonet_stab"), TEXT("impact_body"),
			Victim->GetActorLocation(), EPA250SoundRange::Near, 1.f, 0.1f);
		if (!bBlocked && Action != EPA250MeleeAction::ButtStroke)
		{
			if (UPA250SmokeSubsystem* Bank = GetWorld()->GetSubsystem<UPA250SmokeSubsystem>())
			{
				Bank->AddImpact(Victim->GetActorLocation() + FVector(0.f, 0.f, 35.f) - Push * 25.f, -Push, true);
			}
		}
		if (APA250BattleGameMode* GM = GetWorld()->GetAuthGameMode<APA250BattleGameMode>())
		{
			GM->NotifyProjectileHit(Me, Victim, Health ? HealthBefore - Health->CurrentHealth : Applied); // hitmarker for the local attacker
		}

		const bool bKilled = Victim->IsDead();
		bLastBlowKilled = bKilled;
		if (!bBlocked)
		{
			// Something to see where the blow lands (the only hit effect in Content is the combat template's).
			static TWeakObjectPtr<UNiagaraSystem> HitFx;
			static bool bHitFxTried = false;
			if (!bHitFxTried)
			{
				bHitFxTried = true;
				HitFx = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			if (UNiagaraSystem* Fx = HitFx.Get())
			{
				const FVector At = Victim->GetActorLocation() + FVector(0.f, 0.f, 35.f) - Push * 25.f;
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Fx, At, (-Push).Rotation());
			}
		}
		// The player's kill gets a beat of slow motion (not while the battle is already time-scaled, e.g. autoplay).
		if (bKilled && bMine && V.KillSlowMoSeconds > 0.f && FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(this), 1.f))
		{
			UGameplayStatics::SetGlobalTimeDilation(this, FMath::Clamp(V.KillSlowMoScale, 0.05f, 1.f));
			SlowMoEndSeconds = FPlatformTime::Seconds() + V.KillSlowMoSeconds;
			Me->PunchFOV(-4.f);
		}
		LastBlow = bKilled ? EPA250Blow::Kill : bBlocked ? EPA250Blow::Blocked : EPA250Blow::Hit;
		if (!bKilled && bBlocked)
		{
			// Caught on the musket: jarred, not stopped. A charge still shoves him back.
			if (Action == EPA250MeleeAction::Charge) Victim->LaunchCharacter(Push * (0.5f * V.ChargeKnockback) + FVector(0.f, 0.f, 80.f), true, true);
			Victim->MeleeComponent->ThreatTimer = 0.f;
			Victim->MeleeComponent->SetFeedback(TEXT("Blocked"), true);
		}
		else if (!bKilled)
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
				Victim->LaunchCharacter(Push * (V.ChargeKnockback * (bClubCharge ? V.ClubChargeKnockbackScale : 1.f)) + FVector(0.f, 0.f, 150.f), true, true);
			}
			else if (Action == EPA250MeleeAction::Club)
			{
				Stagger = V.ClubStaggerSeconds;
				Strength = 1.f;
			}
			if (Victim->MeleeComponent)
			{
				Victim->MeleeComponent->ReceiveHit(Push, Strength, Stagger);
				Victim->MeleeComponent->ThreatTimer = 0.f;
				if (bTheirs) Victim->MeleeComponent->SetFeedback(TEXT("Wounded! Hold X to guard"), false);
			}
			else Victim->LockMovement(Stagger);
		}

		// Weight: both men freeze for an instant and the camera takes the jolt (player's fights only).
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
