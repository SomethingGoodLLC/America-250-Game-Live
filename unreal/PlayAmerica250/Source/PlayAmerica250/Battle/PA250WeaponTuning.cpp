#include "PA250WeaponTuning.h"

UPA250WeaponTuning::UPA250WeaponTuning()
{
	// GAMEPLAY SETTINGS (user-chosen). Arcade (default): ~10 s reload, forgiving: wide press zone,
	// a fumble only pauses, no jams. 7 s with perfect presses, 10 s if you just wait, 14 s fumbling.
	Values.Arcade.BaseSeconds = 10.f;
	Values.Arcade.PerfectSeconds = 7.f;
	Values.Arcade.FailedSeconds = 14.f;
	Values.Arcade.PerfectDamageBonus = 0.15f;
	Values.Arcade.JamChanceOnMiss = 0.f;
	Values.Arcade.JamClearSeconds = 1.5f;
	// Authentic: the 15-45 s range of a real muzzle-loader. 15 s drill-perfect, 30 s unhurried,
	// 45 s fumbling every step; misses can jam. AI training/stress spreads soldiers across it.
	Values.Authentic.BaseSeconds = 30.f;
	Values.Authentic.PerfectSeconds = 15.f;
	Values.Authentic.FailedSeconds = 45.f;
	Values.Authentic.PerfectDamageBonus = 0.15f;
	Values.Authentic.JamChanceOnMiss = 0.10f;
	Values.Authentic.JamClearSeconds = 2.5f;
}

UPA250WeaponTuning* UPA250WeaponTuning::MakeRuntimeDefaults()
{
	return NewObject<UPA250WeaponTuning>();
}
