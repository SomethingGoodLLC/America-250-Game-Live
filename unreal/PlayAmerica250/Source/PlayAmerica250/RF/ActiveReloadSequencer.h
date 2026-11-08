// ActiveReloadSequencer.h — plain C++ timed reload phases (no UObject). Unit-testable.
#pragma once

#include "CoreMinimal.h"

enum class EActiveReloadPhase : uint8
{
	Idle = 0,
	PrimePan,
	RamCharge,
	ShoulderAim,
	MissDelay,
	Jammed,
	Complete
};

enum class EActiveReloadHit : uint8
{
	None = 0,
	Miss,
	Sweet,
	Perfect
};

struct FActiveReloadTuningPlain
{
	float PhasePrimePan = 2.f;
	float PhaseRamCharge = 3.f;
	float PhaseShoulderAim = 2.f;
	float SweetZoneFraction = 0.35f;
	float PerfectZoneFraction = 0.12f;
	float PerfectReloadScale = 0.75f;
	float MissDelaySeconds = 1.5f;
	float JamChanceOnMiss = 0.12f;
	float JamClearHoldSeconds = 1.25f;
	float PerfectDamageBonus = 0.15f;
};

struct FActiveReloadStats
{
	int32 Perfect = 0;
	int32 Sweet = 0;
	int32 Miss = 0;
	int32 Jam = 0;
};

/**
 * Three-phase active reload driven by Tick(dt).
 * Call TryCommit() when the player/bot presses reload; TryClearJam while holding.
 */
class FActiveReloadSequencer
{
public:
	void Configure(const FActiveReloadTuningPlain& InTuning) { Tuning = InTuning; }
	void Reset();
	void BeginReload();
	void Tick(float DeltaSeconds);
	/** Commit the current phase window. Returns the hit quality. */
	EActiveReloadHit TryCommit();
	/** While jammed, hold reload; returns true when cleared. */
	bool TryClearJam(bool bHolding, float DeltaSeconds);
	/** Bot helper: roll Perfect/Sweet/Miss from skill in [0,1], then apply. */
	EActiveReloadHit BotCommit(float Skill01, FRandomStream& Rng);

	bool IsReloading() const { return Phase != EActiveReloadPhase::Idle && Phase != EActiveReloadPhase::Complete; }
	bool IsComplete() const { return Phase == EActiveReloadPhase::Complete; }
	bool IsJammed() const { return Phase == EActiveReloadPhase::Jammed; }
	bool IsLoadedReady() const { return Phase == EActiveReloadPhase::Idle || Phase == EActiveReloadPhase::Complete; }
	EActiveReloadPhase GetPhase() const { return Phase; }
	float GetPhaseProgress() const;
	float GetSweetStart() const;
	float GetSweetEnd() const;
	float GetPerfectStart() const;
	float GetPerfectEnd() const;
	float ConsumeDamageBonus();
	float PeekDamageBonus() const { return PendingDamageBonus; }
	const FActiveReloadStats& GetStats() const { return Stats; }
	FString PhaseLabel() const;

private:
	float PhaseDuration(EActiveReloadPhase P) const;
	void AdvanceAfterHit(EActiveReloadHit Hit, FRandomStream* JamRng);
	void EnterPhase(EActiveReloadPhase P);
	void GoToNextCombatPhase();
	void FinishReload();
	EActiveReloadHit Classify(float Progress01) const;

	FActiveReloadTuningPlain Tuning;
	EActiveReloadPhase Phase = EActiveReloadPhase::Idle;
	EActiveReloadPhase ResumePhase = EActiveReloadPhase::PrimePan;
	float PhaseTime = 0.f;
	float PhaseLen = 0.f;
	float SpeedScale = 1.f;
	float PendingDamageBonus = 0.f;
	float JamHold = 0.f;
	int32 PerfectHits = 0;
	int32 PhasesCompleted = 0;
	FActiveReloadStats Stats;
};
