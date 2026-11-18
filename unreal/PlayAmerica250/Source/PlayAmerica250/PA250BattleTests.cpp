// Automation tests: PA250.Battle.Aim / Weapon / Faction / Melee / Conquest / Spacing.
// Bodies live in PA250BattleCases.cpp so the PA250Battle commandlet runs the same assertions.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PA250BattleCases.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
bool Report(FAutomationTestBase& Test, const PA250BattleCases::FLog& Log)
{
	for (const FString& F : Log.Failures)
	{
		Test.AddError(F);
	}
	return Log.Failures.Num() == 0;
}
}

#define PA250_BATTLE_TEST(Class, Name, Fn) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, Name, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter) \
	bool Class::RunTest(const FString& Parameters) \
	{ \
		PA250BattleCases::FLog Log; \
		PA250BattleCases::Fn(Log); \
		return Report(*this, Log); \
	}

PA250_BATTLE_TEST(FPA250BattleAim, "PA250.Battle.Aim", RunAim)
PA250_BATTLE_TEST(FPA250BattleWeapon, "PA250.Battle.Weapon", RunWeapon)
PA250_BATTLE_TEST(FPA250BattleFaction, "PA250.Battle.Faction", RunFaction)
PA250_BATTLE_TEST(FPA250BattleMelee, "PA250.Battle.Melee", RunMelee)
PA250_BATTLE_TEST(FPA250BattleConquest, "PA250.Battle.Conquest", RunConquest)
PA250_BATTLE_TEST(FPA250BattleTickets, "PA250.Battle.Tickets", RunTickets)
PA250_BATTLE_TEST(FPA250BattleSpacing, "PA250.Battle.Spacing", RunSpacing)
PA250_BATTLE_TEST(FPA250BattleTraining, "PA250.Battle.Training", RunTraining)
PA250_BATTLE_TEST(FPA250BattleMorale, "PA250.Battle.Morale", RunMorale)
PA250_BATTLE_TEST(FPA250BattleTactics, "PA250.Battle.Tactics", RunTactics)
PA250_BATTLE_TEST(FPA250BattleAudio, "PA250.Battle.Audio", RunAudio)
PA250_BATTLE_TEST(FPA250BattleBattleRoad, "PA250.Battle.BattleRoad", RunBattleRoad)
PA250_BATTLE_TEST(FPA250BattleScenario, "PA250.Battle.Scenario", RunScenario)
PA250_BATTLE_TEST(FPA250BattleSpawn, "PA250.Battle.Spawn", RunSpawn)
PA250_BATTLE_TEST(FPA250BattleCannonCrew, "PA250.Battle.CannonCrew", RunCannonCrew)
PA250_BATTLE_TEST(FPA250BattleMusketReload, "PA250.Battle.MusketReload", RunMusketReload)
PA250_BATTLE_TEST(FPA250BattleCannonReloadHUD, "PA250.Battle.CannonReloadHUD", RunCannonReloadHUD)
PA250_BATTLE_TEST(FPA250BattleRoundShot, "PA250.Battle.RoundShot", RunRoundShot)
PA250_BATTLE_TEST(FPA250BattleCannonAim, "PA250.Battle.CannonAim", RunCannonAim)
PA250_BATTLE_TEST(FPA250BattleCanister, "PA250.Battle.Canister", RunCanister)
PA250_BATTLE_TEST(FPA250BattleBlast, "PA250.Battle.Blast", RunBlast)
PA250_BATTLE_TEST(FPA250BattleVolleySync, "PA250.Battle.VolleySync", RunVolleySync)
PA250_BATTLE_TEST(FPA250BattleClipActions, "PA250.Battle.ClipActions", RunClipActions)
PA250_BATTLE_TEST(FPA250BattleVolley, "PA250.Battle.Volley", RunVolley)
PA250_BATTLE_TEST(FPA250BattleOfficerLoss, "PA250.Battle.OfficerLoss", RunOfficerLoss)
PA250_BATTLE_TEST(FPA250BattleFallIn, "PA250.Battle.FallIn", RunFallIn)
PA250_BATTLE_TEST(FPA250BattleSuppression, "PA250.Battle.Suppression", RunSuppression)
PA250_BATTLE_TEST(FPA250BattleMusketFeel, "PA250.Battle.MusketFeel", RunMusketFeel)
PA250_BATTLE_TEST(FPA250BattleFXEvents, "PA250.Battle.FXEvents", RunFXEvents)
PA250_BATTLE_TEST(FPA250BattleShells, "PA250.Battle.Shells", RunShells)
PA250_BATTLE_TEST(FPA250BattleGunCrewFeel, "PA250.Battle.GunCrew", RunGunCrew)

#undef PA250_BATTLE_TEST

#endif
