#include "ActiveReloadSequencer.h"

void FActiveReloadSequencer::Reset()
{
	Phase = EActiveReloadPhase::Idle;
	PhaseTime = 0.f;
	PhaseLen = 0.f;
	SpeedScale = 1.f;
	PendingDamageBonus = 0.f;
	JamHold = 0.f;
	PerfectHits = 0;
	PhasesCompleted = 0;
	ResumePhase = EActiveReloadPhase::PrimePan;
}

void FActiveReloadSequencer::BeginReload()
{
	PerfectHits = 0;
	PhasesCompleted = 0;
	SpeedScale = 1.f;
	PendingDamageBonus = 0.f;
	JamHold = 0.f;
	ResumeTime = 0.f;
	bMissedInPhase = false;
	EnterPhase(EActiveReloadPhase::PrimePan);
}

float FActiveReloadSequencer::PhaseDuration(EActiveReloadPhase P) const
{
	switch (P)
	{
	case EActiveReloadPhase::PrimePan: return Tuning.PhasePrimePan;
	case EActiveReloadPhase::RamCharge: return Tuning.PhaseRamCharge;
	case EActiveReloadPhase::ShoulderAim: return Tuning.PhaseShoulderAim;
	case EActiveReloadPhase::MissDelay: return Tuning.MissDelaySeconds;
	default: return 0.f;
	}
}

void FActiveReloadSequencer::EnterPhase(EActiveReloadPhase P)
{
	Phase = P;
	PhaseTime = 0.f;
	if (P == EActiveReloadPhase::PrimePan || P == EActiveReloadPhase::RamCharge || P == EActiveReloadPhase::ShoulderAim)
	{
		ResumePhase = P;
		PhaseLen = PhaseDuration(P) * SpeedScale;
	}
	else
	{
		PhaseLen = PhaseDuration(P);
	}
	if (PhaseLen < 0.05f && P != EActiveReloadPhase::Jammed && P != EActiveReloadPhase::Complete && P != EActiveReloadPhase::Idle)
	{
		PhaseLen = 0.05f;
	}
}

EActiveReloadHit FActiveReloadSequencer::Classify(float Progress01) const
{
	const float Ps = GetPerfectStart();
	const float Pe = GetPerfectEnd();
	const float Ss = GetSweetStart();
	const float Se = GetSweetEnd();
	if (Progress01 >= Ps && Progress01 <= Pe) return EActiveReloadHit::Perfect;
	if (Progress01 >= Ss && Progress01 <= Se) return EActiveReloadHit::Sweet;
	return EActiveReloadHit::Miss;
}

float FActiveReloadSequencer::GetPhaseProgress() const
{
	if (PhaseLen <= 0.f) return 1.f;
	return FMath::Clamp(PhaseTime / PhaseLen, 0.f, 1.f);
}

float FActiveReloadSequencer::GetSweetStart() const
{
	return FMath::Clamp(Tuning.WindowCenter - Tuning.SweetZoneFraction * 0.5f, 0.f, 1.f);
}

float FActiveReloadSequencer::GetSweetEnd() const
{
	return FMath::Clamp(Tuning.WindowCenter + Tuning.SweetZoneFraction * 0.5f, 0.f, 1.f);
}

float FActiveReloadSequencer::GetPerfectStart() const
{
	return FMath::Clamp(Tuning.WindowCenter - Tuning.PerfectZoneFraction * 0.5f, 0.f, 1.f);
}

float FActiveReloadSequencer::GetPerfectEnd() const
{
	return FMath::Clamp(Tuning.WindowCenter + Tuning.PerfectZoneFraction * 0.5f, 0.f, 1.f);
}

void FActiveReloadSequencer::GoToNextCombatPhase()
{
	bMissedInPhase = false;
	ResumeTime = 0.f;
	if (PhasesCompleted <= 0)
	{
		EnterPhase(EActiveReloadPhase::PrimePan);
	}
	else if (PhasesCompleted == 1)
	{
		EnterPhase(EActiveReloadPhase::RamCharge);
	}
	else if (PhasesCompleted == 2)
	{
		EnterPhase(EActiveReloadPhase::ShoulderAim);
	}
	else
	{
		FinishReload();
	}
}

void FActiveReloadSequencer::FinishReload()
{
	if (PerfectHits >= 3)
	{
		// Perfect path already applied SpeedScale during phases; grant bonus.
		PendingDamageBonus = Tuning.PerfectDamageBonus;
	}
	EnterPhase(EActiveReloadPhase::Complete);
}

void FActiveReloadSequencer::AdvanceAfterHit(EActiveReloadHit Hit, FRandomStream* JamRng)
{
	switch (Hit)
	{
	case EActiveReloadHit::Perfect:
		++Stats.Perfect;
		++PerfectHits;
		++PhasesCompleted;
		SpeedScale = FMath::Lerp(1.f, Tuning.PerfectReloadScale, PerfectHits / 3.f);
		break;
	case EActiveReloadHit::Sweet:
		++Stats.Sweet;
		++PhasesCompleted;
		break;
	case EActiveReloadHit::Miss:
		++Stats.Miss;
		bMissedInPhase = true;
		ResumeTime = Tuning.bMissResumesProgress ? PhaseTime : 0.f;
		{
			const float Roll = JamRng ? JamRng->FRand() : FMath::FRand();
			if (Roll < Tuning.JamChanceOnMiss)
			{
				++Stats.Jam;
				EnterPhase(EActiveReloadPhase::Jammed);
				return;
			}
			EnterPhase(EActiveReloadPhase::MissDelay);
			return;
		}
	default:
		return;
	}

	if (PhasesCompleted >= 3)
	{
		if (PerfectHits >= 3)
		{
			SpeedScale = Tuning.PerfectReloadScale;
			PendingDamageBonus = Tuning.PerfectDamageBonus;
		}
		EnterPhase(EActiveReloadPhase::Complete);
	}
	else
	{
		GoToNextCombatPhase();
	}
}

void FActiveReloadSequencer::Tick(float DeltaSeconds)
{
	if (Phase == EActiveReloadPhase::Idle || Phase == EActiveReloadPhase::Complete || Phase == EActiveReloadPhase::Jammed)
	{
		return;
	}
	PhaseTime += DeltaSeconds;
	if (Phase == EActiveReloadPhase::MissDelay)
	{
		if (PhaseTime >= PhaseLen)
		{
			EnterPhase(ResumePhase);
			PhaseTime = ResumeTime;
		}
		return;
	}
	if (PhaseTime >= PhaseLen && (Phase == EActiveReloadPhase::PrimePan || Phase == EActiveReloadPhase::RamCharge || Phase == EActiveReloadPhase::ShoulderAim))
	{
		if (Tuning.bTimeoutCompletesPhase)
		{
			// Base path: no press is a normal, unhurried reload (no stat, no bonus, no jam roll).
			++PhasesCompleted;
			if (PhasesCompleted >= 3) FinishReload();
			else GoToNextCombatPhase();
			return;
		}
		AdvanceAfterHit(EActiveReloadHit::Miss, nullptr);
	}
}

EActiveReloadHit FActiveReloadSequencer::TryCommit()
{
	if (Phase != EActiveReloadPhase::PrimePan && Phase != EActiveReloadPhase::RamCharge && Phase != EActiveReloadPhase::ShoulderAim)
	{
		return EActiveReloadHit::None;
	}
	const EActiveReloadHit Hit = Classify(GetPhaseProgress());
	if (Hit == EActiveReloadHit::Miss && Tuning.bIgnoreRepeatMiss && bMissedInPhase)
	{
		return EActiveReloadHit::None; // already fumbled this step; mashing costs nothing more
	}
	AdvanceAfterHit(Hit, nullptr);
	return Hit;
}

EActiveReloadHit FActiveReloadSequencer::BotCommit(float Skill01, FRandomStream& Rng)
{
	if (Phase != EActiveReloadPhase::PrimePan && Phase != EActiveReloadPhase::RamCharge && Phase != EActiveReloadPhase::ShoulderAim)
	{
		return EActiveReloadHit::None;
	}
	const float S = FMath::Clamp(Skill01, 0.f, 1.f);
	const float Roll = Rng.FRand();
	EActiveReloadHit Hit = EActiveReloadHit::Miss;
	if (Roll < S * 0.35f)
	{
		Hit = EActiveReloadHit::Perfect;
	}
	else if (Roll < S)
	{
		Hit = EActiveReloadHit::Sweet;
	}
	AdvanceAfterHit(Hit, &Rng);
	return Hit;
}

bool FActiveReloadSequencer::TryClearJam(bool bHolding, float DeltaSeconds)
{
	if (Phase != EActiveReloadPhase::Jammed)
	{
		JamHold = 0.f;
		return false;
	}
	if (!bHolding)
	{
		JamHold = 0.f;
		return false;
	}
	JamHold += DeltaSeconds;
	if (JamHold >= Tuning.JamClearHoldSeconds)
	{
		JamHold = 0.f;
		EnterPhase(ResumePhase);
		PhaseTime = ResumeTime;
		return true;
	}
	return false;
}

float FActiveReloadSequencer::ConsumeDamageBonus()
{
	const float B = PendingDamageBonus;
	PendingDamageBonus = 0.f;
	return B;
}

FString FActiveReloadSequencer::PhaseLabel() const
{
	switch (Phase)
	{
	case EActiveReloadPhase::PrimePan: return TEXT("1/3  Prime the pan");
	case EActiveReloadPhase::RamCharge: return TEXT("2/3  Ram the ball");
	case EActiveReloadPhase::ShoulderAim: return TEXT("3/3  Shoulder arms");
	case EActiveReloadPhase::MissDelay: return TEXT("Fumbled - redoing step");
	case EActiveReloadPhase::Jammed: return TEXT("JAMMED");
	case EActiveReloadPhase::Complete: return TEXT("Loaded");
	default: return TEXT("");
	}
}
