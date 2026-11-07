// Runs the canonical PA250Sim cases (unreal/PA250Sim/tests/sim_cases.hpp) as Unreal automation tests.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "tests/sim_cases.hpp"

namespace
{
struct FUnrealReporter : pa250_tests::Reporter
{
	FAutomationTestBase* Test;
	explicit FUnrealReporter(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual void failure(const char* File, int Line, const char* Expr) override
	{
		Test->AddError(FString::Printf(TEXT("%s:%d CHECK FAILED: %s"), UTF8_TO_TCHAR(File), Line, UTF8_TO_TCHAR(Expr)));
	}
	virtual void message(const std::string& Text) override { Test->AddInfo(UTF8_TO_TCHAR(Text.c_str())); }
	virtual void test_done(const char* Name, bool bOk) override
	{
		Test->AddInfo(FString::Printf(TEXT("%s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), UTF8_TO_TCHAR(Name)));
	}
};

FString RepoPath(const TCHAR* Relative)
{
	// Project dir is <repo>/unreal/PlayAmerica250
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../.."), Relative));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPA250SimCoreTest, "PA250.Sim.Core",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPA250SimCoreTest::RunTest(const FString& Parameters)
{
	FUnrealReporter Reporter(this);
	const FString DataDir = RepoPath(TEXT("unreal/PA250Sim/data"));
	const FString VectorDir = RepoPath(TEXT("protocol/test_vectors/clauses"));
	const int Failed = pa250_tests::run_all(Reporter, TCHAR_TO_UTF8(*DataDir), TCHAR_TO_UTF8(*VectorDir), false);
	AddInfo(FString::Printf(TEXT("%d checks, %d failed"), pa250_tests::g_checks(), Failed));
	return Failed == 0;
}

#endif
