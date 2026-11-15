// PA250Hero.h — the playable general (M5, GAME INVENTION). Pure rules: the score that earns him, the
// meter that limits him, and the words of the takeover screen. Every number is the persons data's
// (pa250::hero_rules(), pa250::commander_rules(), the battle setup's side). The battle's part
// (possession, kit, abilities) is APA250BattleGameMode's, in Battle/PA250BattleHero.cpp.
// The campaign learns of it only through BattleResult: commander_events says what became of him;
// the hero block is a report.
#pragma once

#include "CoreMinimal.h"
#include "pa250/sim.hpp"

enum class EPA250HeroEvent : uint8
{
	Kill,     // an enemy by the player's own hand
	Capture,  // a post taken with the player on it
	Rally     // a routing squad brought back by the player
};

struct FPA250HeroState
{
	int32 Score = 0;               // earned in the ranks, toward the threshold
	bool bPlaying = false;         // the player is the general now
	float Meter = 0.f;             // seconds left of it
	int32 Takeovers = 0;
	float SecondsPlayed = 0.f;
	bool bFellWhilePlayed = false;
};

namespace PA250Hero
{
inline int32 ScoreFor(EPA250HeroEvent Event, const pa250::HeroRules& R)
{
	switch (Event)
	{
	case EPA250HeroEvent::Kill: return R.score_kill;
	case EPA250HeroEvent::Capture: return R.score_capture;
	default: return R.score_rally;
	}
}

inline float RefillFor(EPA250HeroEvent Event, const pa250::HeroRules& R)
{
	switch (Event)
	{
	case EPA250HeroEvent::Kill: return R.refill_kill_seconds;
	case EPA250HeroEvent::Capture: return R.refill_capture_seconds;
	default: return R.refill_rally_seconds;
	}
}

/** What the player has just done. In the ranks it adds to the score; as the general it gives back
 *  time on the meter, never past a full one. Nothing without the rules. */
inline void Note(FPA250HeroState& S, EPA250HeroEvent Event, const pa250::HeroRules& R)
{
	if (!R.loaded) return;
	if (S.bPlaying) S.Meter = FMath::Min(R.meter_seconds, S.Meter + RefillFor(Event, R));
	else S.Score += ScoreFor(Event, R);
}

/** May the general be taken now: the rules are loaded, the player's side has a general with a hero
 *  kit (no general, no hero; the enemy's general has none), he is alive, he is not being played
 *  already, and the score has reached the threshold. */
inline bool CanOffer(const FPA250HeroState& S, const pa250::HeroRules& R, bool bSideHasHero, bool bGeneralAlive)
{
	return R.loaded && bSideHasHero && bGeneralAlive && !S.bPlaying && S.Score >= R.score_threshold;
}

/** Taking him: the score is spent and the meter is full. */
inline void Begin(FPA250HeroState& S, const pa250::HeroRules& R)
{
	S.bPlaying = true;
	S.Score = 0;
	S.Meter = R.meter_seconds;
	++S.Takeovers;
}

/** A tick as the general: the meter runs down. True when it has run out. */
inline bool Step(FPA250HeroState& S, float DeltaSeconds)
{
	if (!S.bPlaying) return false;
	S.SecondsPlayed += DeltaSeconds;
	S.Meter = FMath::Max(0.f, S.Meter - DeltaSeconds);
	return S.Meter <= 0.f;
}

/** He is given back to himself (the meter ran out) or he fell. */
inline void End(FPA250HeroState& S, bool bFell)
{
	S.bPlaying = false;
	S.Meter = 0.f;
	if (bFell) S.bFellWhilePlayed = true;
}

/** What his fall costs, in the words of the takeover screen. All from the persons data. */
inline FString FallCostText(const pa250::CommanderRules& C)
{
	const int32 Total = FMath::Max(1, C.fate_wounded + C.fate_killed + C.fate_captured);
	auto Pct = [Total](int32 N) { return FMath::RoundToInt(100.f * N / Total); };
	FString Text = FString::Printf(TEXT("If he falls: wounded %d in 100 (away %d days), killed %d in 100, taken %d in 100."), Pct(C.fate_wounded), C.wounded_days,
		Pct(C.fate_killed), Pct(C.fate_captured));
	Text += FString::Printf(TEXT("\nHis army is then a detachment (it takes in at most %d units and cannot besiege) until it has a general again."), C.detachment_unit_cap);
	if (C.army_morale_on_fall != 0 || C.army_morale_on_fall_victory != 0)
	{
		Text += FString::Printf(TEXT("\nHis army's morale: %+d if the day is lost, %+d if it is won. His men on this field take a morale check the moment he falls."),
			C.army_morale_on_fall, C.army_morale_on_fall_victory);
	}
	return Text;
}

/** The kit he fights with, in the words of the takeover screen. */
inline FString KitText(const pa250::SideSetup& Side, const pa250::HeroRules& R)
{
	FString Text = FString::Printf(TEXT("For %.0f seconds (kills, posts and rallies give time back):"), R.meter_seconds);
	Text += FString::Printf(TEXT("\n  Health x %.1f.  His presence steadies men within %.0f m."), Side.hero_health_scale, Side.hero_aura_radius_m);
	Text += FString::Printf(TEXT("\n  Sword: %.0f damage, %.1f m reach.  Pistol: %.0f damage to %.0f m, %.0f s to load (hold aim to draw it)."), R.sword_damage,
		R.sword_reach_cm / 100.f, R.pistol_damage, R.pistol_range_m, R.pistol_reload_seconds);
	Text += FString::Printf(TEXT("\n  T  Rally: every routing squad within %.0f m comes back to him (%.0f s between)."), Side.hero_rally_radius_m, Side.hero_rally_cooldown_seconds);
	if (!Side.hero_ability.kind.empty())
	{
		Text += FString::Printf(TEXT("\n  U  %s: %s (%.0f s between)."), UTF8_TO_TCHAR(Side.hero_ability.label.c_str()), UTF8_TO_TCHAR(Side.hero_ability.text.c_str()),
			Side.hero_ability.cooldown_seconds);
	}
	return Text;
}
}
