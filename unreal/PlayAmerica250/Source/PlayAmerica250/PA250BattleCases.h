// PA250BattleCases.h — PA250.Battle.* assertions shared by the automation tests and the
// PA250Battle commandlet (Automation can SIGBUS on this Mac; the commandlet is the fallback).
// Every case drives shipped code (sequencer presets, ledger, slot ring, aim traces), not copies.
#pragma once

#include "CoreMinimal.h"

namespace PA250BattleCases
{
struct FLog
{
	int32 Passed = 0;
	TArray<FString> Failures;
	void Check(bool bCond, const FString& What);
};

void RunAim(FLog& Log);       // dual-trace in a real world: clear shot near reticle, occluded muzzle -> bBlocked
void RunWeapon(FLog& Log);    // falloff curve, Arcade/Authentic reload durations, jam blocks fire until cleared
void RunFaction(FLog& Log);   // faction looks and lookup
void RunMelee(FLog& Log);     // backstab / reloading multiplier, butt-stroke keeps reload progress
void RunConquest(FLog& Log);  // tickets -1/death, 5 s bleed, losses in units, capture rate
void RunSpacing(FLog& Log);   // ring slots, sectors, one bot per slot, engagement buckets, FTEST criteria
void RunTraining(FLog& Log);  // player loading untouched; AI reload by training: baseline, equality, stress never faster
void RunMorale(FLog& Log);    // hysteresis, resolve by training, rally delay, withdrawal vs rout, player never ordered
void RunAudio(FLog& Log);     // every sound bank / voice line loads; shots, reload steps, jams, melee, charges make sound
void RunBattleRoad(FLog& Log); // L_Lexington walls tagged as cover + muster points; cover-line picks; militia muster math
void RunTactics(FLog& Log);   // cover query vs a fence, line of sight, repositioning costs, role/training independence
}
