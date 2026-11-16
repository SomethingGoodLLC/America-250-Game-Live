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
void RunConquest(FLog& Log);  // tickets -1/death, Resolve bleed clock, recorded casualties
void RunTickets(FLog& Log);   // Artillery pass: invariant tickets==casualties; bleed never spends men; Resolve breaks
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
void RunPlaytest(FLog& Log);     // playtest 2026-11-15: the file at the leader's shoulders, the mission card, why a fort is not taken
void RunFeel(FLog& Log);         // smoke that hides men, knockdown and dirt, sound late with distance, drum calls, first person, the shove
void RunSupport(FLog& Log);      // ensign and the colours, musician relay and cadence, surgeon; field caps; the result carries them
void RunMarch(FLog& Log);        // campaign pass M1 (PA250CampaignPassCases.cpp): dashes along the road, the faded part, arrowhead, arrival label, the halt order
void RunPeople(FLog& Log);       // campaign pass M2 to M4: the people panel, the floor, the decision, the commander and the estimate on the card
void RunStory(FLog& Log);        // campaign pass M5 and M6: a letter's orders on the card; the battle story for every town, its situation line, the pan
void RunEconomy(FLog& Log);      // Phase 4 as the game's own sim build reads it: ground rules, level costs, powder, the opening accounts
void RunSpawn(FLog& Log);        // Artillery M2: scored spawns, formation spacing, AICommander helpers
void RunCannonCrew(FLog& Log);   // Artillery M3: tools-only NPC, holster rules, role sockets, fallback/POW
void RunMusketReload(FLog& Log); // Artillery M4: musket 3-phase regression snapshot before gun drill
void RunCannonReloadHUD(FLog& Log); // Artillery M4: 6-step gun drill timings + musket wrapper identity
void RunRoundShot(FLog& Log);    // Artillery M5: bounce/graze thresholds, speed bands for KIA/WIA/stun
void RunCannonAim(FLog& Log);    // Artillery M6: quoin/heat/recoil helpers; no wind-drift/wet-powder asserts
void RunCanister(FLog& Log);     // Artillery M7: grape enum → CANISTER label; pellet/range/cone
void RunBlast(FLog& Log);        // Artillery M9: blast rings, cover demote, ragdoll cap, chest seed, invariant
void RunVolleySync(FLog& Log);   // Artillery M9: seeded 0–150 ms stage jitter (replaces VolleyShotDelay jitter)
}
