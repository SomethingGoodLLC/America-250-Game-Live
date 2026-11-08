// PA250AIReload.h — AI reload timing, separate from the player's manual loading.
// The AI presses "reload" on the SAME FActiveReloadSequencer through TryCommit, so it obeys the
// player's rules exactly: same steps, windows, fumble pause, jams, damage bonus; nothing skipped.
// Training only changes WHEN the AI presses. Plain C++ (no UObject) so it is unit-testable.
#pragma once

#include "CoreMinimal.h"
#include "ActiveReloadSequencer.h"

/** Gameplay settings taken from a training profile (see PA250CombatProfiles.h). */
struct FPA250AIReloadParams
{
	float Skill = 0.85f;         // chance a step goes cleanly when calm
	float Pace = 1.1f;           // >= 1: multiplier on the ideal (window-centre) commit point
	float Jitter = 0.12f;        // random extra, fraction of ideal
	float StressPenalty = 0.6f;  // extra fraction at full stress
	float MovingPenalty = 0.4f;  // extra fraction while moving
};

struct FPA250AIReloadConditions
{
	float Stress01 = 0.f;  // suppression, casualties, morale (0 calm .. 1 overwhelmed)
	bool bMoving = false;
};

struct FPA250AIReloadPlanner
{
	/** Pure: progress (0..1) of the step at which to press. Never earlier than the ideal centre;
	 *  every factor is >= 1, so stress or movement can only push the press later. 1 = let the
	 *  step run out (the unhurried base reload). */
	static float PlanTarget(float Center, const FPA250AIReloadParams& P, const FPA250AIReloadConditions& C, float U01)
	{
		const float Stress = FMath::Clamp(C.Stress01, 0.f, 1.f);
		const float Mul = FMath::Max(1.f, P.Pace) * (1.f + FMath::Max(0.f, P.Jitter) * FMath::Clamp(U01, 0.f, 1.f))
			* (1.f + Stress * FMath::Max(0.f, P.StressPenalty)) * (C.bMoving ? 1.f + FMath::Max(0.f, P.MovingPenalty) : 1.f);
		return FMath::Clamp(Center * Mul, Center, 1.f);
	}

	/** Pure: chance this step starts with a fumbled (early) press. Rises with stress and movement. */
	static float FumbleChance(const FPA250AIReloadParams& P, const FPA250AIReloadConditions& C)
	{
		const float Stress = FMath::Clamp(C.Stress01, 0.f, 1.f);
		// Calm: (1 - skill). Under fire a green soldier fumbles far more (up to 3x); on the move, too.
		const float Base = (1.f - FMath::Clamp(P.Skill, 0.f, 1.f)) * (1.f + 2.f * Stress);
		return FMath::Clamp(Base + (C.bMoving ? 0.6f * P.MovingPenalty : 0.f), 0.f, 0.95f);
	}

	/** Call every frame while the musket reloads (before the sequencer's own Tick).
	 *  Returns the hit if it pressed this frame, else None. */
	EActiveReloadHit Tick(FActiveReloadSequencer& S, const FPA250AIReloadParams& P, const FPA250AIReloadConditions& C, FRandomStream& Rng)
	{
		const EActiveReloadPhase Ph = S.GetPhase();
		const bool bStep = Ph == EActiveReloadPhase::PrimePan || Ph == EActiveReloadPhase::RamCharge || Ph == EActiveReloadPhase::ShoulderAim;
		if (Ph == EActiveReloadPhase::Idle || Ph == EActiveReloadPhase::Complete)
		{
			PlannedPhase = EActiveReloadPhase::Idle;
			return EActiveReloadHit::None;
		}
		if (!bStep) return EActiveReloadHit::None; // fumble pause / jam: wait (jam is cleared by holding)

		const float Center = 0.5f * (S.GetSweetStart() + S.GetSweetEnd());
		if (Ph != PlannedPhase)
		{
			// Always three draws per step, whatever the conditions, so a calmer and a more stressed
			// run with the same seed see the same random numbers (per-sample comparisons are fair).
			PlannedPhase = Ph;
			U = Rng.FRand();
			const float FumbleRoll = Rng.FRand();
			FumbleAt = FMath::Lerp(0.02f, FMath::Max(0.03f, S.GetSweetStart() - 0.02f), Rng.FRand());
			bFumblePending = FumbleRoll < FumbleChance(P, C);
			Target = PlanTarget(Center, P, C, U);
		}
		// Conditions can worsen mid-step; they never pull the press earlier.
		Target = FMath::Max(Target, PlanTarget(Center, P, C, U));
		// Never press after the zone has passed (that is a fumble with a pause): a man too slow for
		// the zone just lets the step run out - the normal, unhurried reload. Keeps "slower" monotone.
		if (Target > S.GetSweetEnd() - 0.01f) Target = 1.f;

		const float Progress = S.GetPhaseProgress();
		if (bFumblePending && Progress >= FumbleAt)
		{
			bFumblePending = false;
			return S.TryCommit(); // a real early press: the player's fumble rules apply
		}
		if (Target < 0.999f && Progress >= Target)
		{
			return S.TryCommit();
		}
		return EActiveReloadHit::None;
	}

	EActiveReloadPhase PlannedPhase = EActiveReloadPhase::Idle;
	float U = 0.f;
	float Target = 1.f;
	float FumbleAt = 0.f;
	bool bFumblePending = false;
};
