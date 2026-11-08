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
PA250_BATTLE_TEST(FPA250BattleSpacing, "PA250.Battle.Spacing", RunSpacing)
PA250_BATTLE_TEST(FPA250BattleTraining, "PA250.Battle.Training", RunTraining)
PA250_BATTLE_TEST(FPA250BattleMorale, "PA250.Battle.Morale", RunMorale)
PA250_BATTLE_TEST(FPA250BattleTactics, "PA250.Battle.Tactics", RunTactics)
PA250_BATTLE_TEST(FPA250BattleAudio, "PA250.Battle.Audio", RunAudio)
PA250_BATTLE_TEST(FPA250BattleBattleRoad, "PA250.Battle.BattleRoad", RunBattleRoad)

#undef PA250_BATTLE_TEST

#endif
