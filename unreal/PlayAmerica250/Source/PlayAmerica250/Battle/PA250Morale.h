// PA250Morale.h — squad morale as plain C++ (no UObject), driven by the volley coordinator.
// GAMEPLAY SETTINGS, provisional. States:
//   Steady -> Shaken -> Routing -> Rallying -> Steady   (panic path, needs a rally)
//   Steady/Shaken -> Withdrawing -> Steady              (deliberate, ordered, stays formed)
// Thresholds are separated (enter > exit) and states have a minimum dwell time, so a squad
// hovering near a threshold does not flip every frame. Training scales resolve; nobody is immune.
#pragma once

#include "CoreMinimal.h"

enum class EPA250Morale : uint8
{
	Steady,
	Shaken,
	Withdrawing,
	Routing,
	Rallying
};

inline const TCHAR* MoraleName(EPA250Morale M)
{
	switch (M)
	{
	case EPA250Morale::Steady: return TEXT("Steady");
	case EPA250Morale::Shaken: return TEXT("Shaken");
	case EPA250Morale::Withdrawing: return TEXT("Withdrawing");
	case EPA250Morale::Routing: return TEXT("Routing");
	case EPA250Morale::Rallying: return TEXT("Rallying");
	}
	return TEXT("?");
}

/** What a squad in each state may do. The coordinator and bots read this; tests check it. */
struct FPA250MoraleOrders
{
	bool bKeepFormation = true;  // men keep their places in the ranks
	bool bMayVolley = true;      // squad volleys / individual fire allowed (when halted)
	bool bMayCharge = true;      // bayonet charge allowed
	bool bMayAdvance = true;     // march / advance-by-volley allowed
	bool bFallBack = false;      // ordered move away from the enemy, facing it
	bool bFleeToRear = false;    // run for the rear, formation abandoned
	bool bNeedsRally = false;    // only a rally (time calm) ends this state
	float MarchScale = 1.f;
	float ReadyFractionAdd = 0.f;  // a shaken line waits for more muskets before it fires
	float ReloadStress = 0.f;      // added to the AI reload stress

	static FPA250MoraleOrders For(EPA250Morale M)
	{
		FPA250MoraleOrders O;
		switch (M)
		{
		case EPA250Morale::Steady:
			break;
		case EPA250Morale::Shaken:
			O.bMayCharge = false;
			O.MarchScale = 0.6f;
			O.ReadyFractionAdd = 0.2f;
			O.ReloadStress = 0.2f;
			break;
		case EPA250Morale::Withdrawing:
			O.bMayVolley = false; // moving back: no shots until halted (Steady again)
			O.bMayCharge = false;
			O.bMayAdvance = false;
			O.bFallBack = true;
			O.MarchScale = 0.6f;
			O.ReloadStress = 0.15f;
			break;
		case EPA250Morale::Routing:
			O.bKeepFormation = false;
			O.bMayVolley = false;
			O.bMayCharge = false;
			O.bMayAdvance = false;
			O.bFleeToRear = true;
			O.bNeedsRally = true;
			O.ReloadStress = 0.5f;
			break;
		case EPA250Morale::Rallying:
			O.bMayVolley = false; // re-forming first
			O.bMayCharge = false;
			O.bMayAdvance = false;
			O.MarchScale = 0.f;
			O.ReloadStress = 0.2f;
			break;
		}
		return O;
	}
};

struct FPA250MoraleTuning
{
	float ShakenEnter = 0.45f;
	float ShakenExit = 0.30f;   // < ShakenEnter: hysteresis
	float RoutEnter = 0.85f;
	float MinStateSeconds = 3.f;
};

/** Pure: pressure on a squad this moment (0 = none; ~1+ = overwhelming). */
inline float PA250MoralePressure(float RecentLossFraction, float SuppressedFraction, float Strength, bool bEnemyClose, bool bFlanked,
	bool bBeingCharged = false)
{
	return (bBeingCharged ? 0.3f : 0.f) // cold steel coming at you shakes any line
		+ FMath::Max(0.f, RecentLossFraction) * 1.5f
		+ FMath::Clamp(SuppressedFraction, 0.f, 1.f) * 0.35f
		+ (1.f - FMath::Clamp(Strength, 0.f, 1.f)) * 0.35f
		+ (bEnemyClose ? 0.15f : 0.f)
		+ (bFlanked ? 0.25f : 0.f);
}

struct FPA250MoraleState
{
	EPA250Morale State = EPA250Morale::Steady;
	float TimeInState = 1000.f;
	float CalmTime = 0.f;
	int32 Transitions = 0;

	/** Returns true when the state changed this tick. */
	bool Tick(float Dt, float Pressure, float Resolve, float RallySeconds, float ReformSeconds,
		bool bWithdrawOrder, bool bWithdrawDone, const FPA250MoraleTuning& M = FPA250MoraleTuning())
	{
		TimeInState += Dt;
		const float E = Pressure / FMath::Max(0.1f, Resolve);
		const bool bDwelt = TimeInState >= M.MinStateSeconds;
		switch (State)
		{
		case EPA250Morale::Steady:
			if (bDwelt && E >= M.RoutEnter) return Set(EPA250Morale::Routing);
			if (bDwelt && E >= M.ShakenEnter) return Set(EPA250Morale::Shaken);
			if (bWithdrawOrder) return Set(EPA250Morale::Withdrawing);
			break;
		case EPA250Morale::Shaken:
			if (bDwelt && E >= M.RoutEnter) return Set(EPA250Morale::Routing);
			if (bDwelt && E <= M.ShakenExit) return Set(EPA250Morale::Steady);
			if (bWithdrawOrder) return Set(EPA250Morale::Withdrawing);
			break;
		case EPA250Morale::Withdrawing:
			if (E >= M.RoutEnter) return Set(EPA250Morale::Routing); // a withdrawal can still break
			if (bWithdrawDone) return Set(E >= M.ShakenEnter ? EPA250Morale::Shaken : EPA250Morale::Steady);
			break;
		case EPA250Morale::Routing:
			CalmTime = E <= M.ShakenExit ? CalmTime + Dt : 0.f;
			if (CalmTime >= RallySeconds) return Set(EPA250Morale::Rallying);
			break;
		case EPA250Morale::Rallying:
			if (E >= M.RoutEnter) return Set(EPA250Morale::Routing);
			if (TimeInState >= ReformSeconds) return Set(EPA250Morale::Steady);
			break;
		}
		return false;
	}

	bool Set(EPA250Morale S)
	{
		State = S;
		TimeInState = 0.f;
		CalmTime = 0.f;
		++Transitions;
		return true;
	}
};

/** Pure: should a squad order a bayonet charge now? No faction input - the same rules for every side.
 *  Line troops charge when they outnumber the enemy nearby, or catch him reloading while their own
 *  muskets are mostly empty (cold steel beats a 10 s reload). Cautious roles (light / irregular) only
 *  charge a wavering enemy, or one caught reloading when they also outnumber him. */
inline bool PA250ShouldOrderCharge(EPA250Morale Morale, bool bCautiousRole, float ContactDistCm, float RangeCm,
	bool bEnemyWavering, float EnemyReloadingFraction, float OurLoadedFraction, int32 OurCount, int32 TheirCount)
{
	if (Morale != EPA250Morale::Steady || OurCount <= 0 || ContactDistCm > RangeCm) return false;
	if (bEnemyWavering) return true;
	const float Theirs = float(FMath::Max(1, TheirCount));
	const bool bCaughtReloading = EnemyReloadingFraction >= 0.5f;
	if (bCautiousRole) return bCaughtReloading && OurCount >= 1.2f * Theirs;
	return OurCount >= 1.25f * Theirs || (bCaughtReloading && OurLoadedFraction < 0.5f);
}
