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
void RunScenario(FLog& Log);  // Concord locks every post but the crossing; Battle Road wins by men preserved
void RunTactics(FLog& Log);   // cover query vs a fence, line of sight, repositioning costs, role/training independence
void RunCampaignRead(FLog& Log); // field strength from the sim, army_broke / posts_swept guards, cartridge boxes and the side's pool
void RunCampaignArc(FLog& Log);  // the rules and mission files load; mission tracker and end screen text
void RunClasses(FLog& Log);      // unit_classes.json covers every class (no fallback); bands, accuracy over range, the class verbs
void RunSquad(FLog& Log);        // the player's file: who is in it, the six orders, the volley his shot fires, the HUD line, his aim
void RunWorks(FLog& Log);        // the fire-over stance, field works by fortification level, where they stand, who mans them, the sapper
void RunGunnery(FLog& Log);      // field guns: the data, which shot, the drill by crew, overrun and push, the asset's sockets, guns in the setup and the result
void RunModes(FLog& Log);        // conquest, assault, landing: the order of posts, what is closed, the clock, the boats' course, the level, the card line
void RunFeel(FLog& Log);         // smoke that hides men, knockdown and dirt, sound late with distance, drum calls, first person, the shove
void RunSupport(FLog& Log);      // ensign and the colours, musician relay and cadence, surgeon; field caps; the result carries them
void RunEconomy(FLog& Log);      // Phase 4 as the game's own sim build reads it: ground rules, level costs, powder, the opening accounts
}
