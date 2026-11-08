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
	BusyTimer = FMath::Max(0.f, BusyTimer - DeltaTime);
	ChargeCooldown = FMath::Max(0.f, ChargeCooldown - DeltaTime);
	FeedbackTimer = FMath::Max(0.f, FeedbackTimer - DeltaTime);

	const ARFCharacter* Me = OwnerChar();
	if (Me && Me->IsDead())
	{
		PendingThrustTimer = -1.f;
		if (IsCharging()) EndCharge(false);
	}

	const FPA250MeleeTuningValues& V = GetValues();
	if (PendingThrustTimer >= 0.f)
	{
		PendingThrustTimer -= DeltaTime;
		if (PendingThrustTimer <= 0.f)
		{
			PendingThrustTimer = -1.f;
			const ARFCharacter* Victim = SweepAttack(V.ThrustReachCm, V.ThrustRadiusCm, V.ThrustDamage, true, EPA250MeleeAction::Thrust);
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

	if (JabTimer > 0.f || IsCharging())
	{
		JabTimer = FMath::Max(0.f, JabTimer - DeltaTime);
		ApplyJabPose();
	}
}

bool UPA250MeleeComponent::DoThrust()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead() || IsBusy()) return false;
	const FPA250MeleeTuningValues& V = GetValues();
	BusyTimer = V.ThrustWindupSeconds + 0.2f;
	PendingThrustTimer = V.ThrustWindupSeconds;
	// Bayonet pokes out over the wind-up and peaks as the hit lands.
	StartJab(55.f, V.ThrustWindupSeconds / 0.7f);
	FPA250BattleAudio::PlayAt(Me, TEXT("melee_swing"), Me->GetActorLocation(), EPA250SoundRange::Near, 0.8f, 0.1f);
	return true;
}

bool UPA250MeleeComponent::DoButtStroke()
{
	ARFCharacter* Me = OwnerChar();
	if (!Me || Me->IsDead() || IsBusy()) return false;
	const FPA250MeleeTuningValues& V = GetValues();
	BusyTimer = 0.4f;
	StartJab(30.f, 0.3f);
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
	StartJab(45.f, Duration + 0.2f);
	SetFeedback(TEXT("CHARGE!"), false);
	FPA250BattleAudio::PlayVoice(Me, Me->FactionId, FMath::RandBool() ? TEXT("charge") : TEXT("charge_b"), Me->GetActorLocation() + FVector(0.f, 0.f, 70.f));
	return true;
}

ARFCharacter* UPA250MeleeComponent::FindChargeTarget() const
{
	const ARFCharacter* Me = OwnerChar();
	UWorld* World = GetWorld();
	if (!Me || !World) return nullptr;
	const FPA250MeleeTuningValues& V = GetValues();
	const FVector Fwd = Me->GetActorForwardVector().GetSafeNormal2D();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(V.ChargeAssistConeDegrees));
	ARFCharacter* Best = nullptr;
	float BestD = V.ChargeAssistRangeCm;
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
	JabTimer = FMath::Min(JabTimer, 0.15f);
	if (!Me || Me->IsDead()) return;
	if (bHit)
	{
		BusyTimer = 0.25f;
		SetFeedback(TEXT("Charge hit!"), true);
	}
	else
	{
		// Whiffed charges are punished so the charge is a commitment, not a free dash.
		Me->LockMovement(GetValues().ChargeWhiffRecoverySeconds);
		BusyTimer = GetValues().ChargeWhiffRecoverySeconds;
		SetFeedback(TEXT("Charge missed - off balance"), false);
	}
}

void UPA250MeleeComponent::StartJab(float DistanceCm, float Seconds)
{
	JabDistance = DistanceCm;
	JabDuration = FMath::Max(0.05f, Seconds);
	JabTimer = JabDuration;
}

void UPA250MeleeComponent::ApplyJabPose()
{
	// Placeholder animation (no montages in Content): slide the musket along the soldier's facing.
	ARFCharacter* Me = OwnerChar();
	USceneComponent* Root = Me && Me->Musket ? Me->Musket->GetRootComponent() : nullptr;
	USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
	if (!Root || !Parent) return;
	if (!bHaveMusketRest)
	{
		MusketRestRelative = Root->GetRelativeLocation();
		bHaveMusketRest = true;
	}
	float Offset = 0.f;
	if (IsCharging())
	{
		Offset = JabDistance; // levelled for the whole dash
	}
	else if (JabTimer > 0.f)
	{
		const float T = 1.f - JabTimer / JabDuration; // 0..1
		Offset = T < 0.7f ? JabDistance * FMath::Square(T / 0.7f) : JabDistance * (1.f - (T - 0.7f) / 0.3f);
	}
	const FTransform ParentT = Parent->GetSocketTransform(Root->GetAttachSocketName());
	const FVector LocalOffset = ParentT.InverseTransformVectorNoScale(Me->GetActorForwardVector() * Offset);
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

		if (!Victim->IsDead())
		{
			if (Action == EPA250MeleeAction::ButtStroke)
			{
				Victim->LockMovement(V.ButtStrokeStaggerSeconds);
			}
			else if (Action == EPA250MeleeAction::Charge)
			{
				Victim->LaunchCharacter(Me->GetActorForwardVector().GetSafeNormal2D() * V.ChargeKnockback + FVector(0.f, 0.f, 150.f), true, true);
			}
		}
		return Victim; // one victim per swing
	}
	return nullptr;
}
