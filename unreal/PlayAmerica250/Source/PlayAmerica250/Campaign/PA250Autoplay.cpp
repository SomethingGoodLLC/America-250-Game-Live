#include "PA250Autoplay.h"
#include "Campaign/PA250Subsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"

namespace PA250Autoplay
{
static TArray<FString> GFailures;
static TArray<FString> GNotes;

bool IsAutoplay() { return FParse::Param(FCommandLine::Get(), TEXT("PA250Autoplay")); }

FString SelftestName()
{
	FString Name;
	FParse::Value(FCommandLine::Get(), TEXT("PA250Selftest="), Name);
	return Name;
}

void Check(UPA250Subsystem* Sub, bool bCondition, const FString& What)
{
	GNotes.Add((bCondition ? TEXT("PASS ") : TEXT("FAIL ")) + What);
	UE_LOG(LogTemp, Display, TEXT("PA250 %s: %s"), bCondition ? TEXT("PASS") : TEXT("FAIL"), *What);
	if (!bCondition)
	{
		GFailures.Add(What);
	}
	if (Sub)
	{
		Sub->GetControllerMutable().record(bCondition ? "check_pass" : "check_fail", TCHAR_TO_UTF8(*What));
	}
}

const TArray<FString>& Failures() { return GFailures; }
const TArray<FString>& Notes() { return GNotes; }

void Finish(UPA250Subsystem* Sub, const FString& Name)
{
	const bool bPassed = GFailures.Num() == 0;
	FString Json = TEXT("{\n  \"name\": \"") + Name + TEXT("\",\n  \"passed\": ") + (bPassed ? TEXT("true") : TEXT("false")) + TEXT(",\n  \"failures\": [");
	for (int32 i = 0; i < GFailures.Num(); ++i)
	{
		Json += FString::Printf(TEXT("%s\"%s\""), i ? TEXT(", ") : TEXT(""), *GFailures[i].ReplaceCharWithEscapedChar());
	}
	Json += TEXT("],\n  \"notes\": [");
	for (int32 i = 0; i < GNotes.Num(); ++i)
	{
		Json += FString::Printf(TEXT("%s\"%s\""), i ? TEXT(", ") : TEXT(""), *GNotes[i].ReplaceCharWithEscapedChar());
	}
	Json += TEXT("]\n}\n");
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), Name + TEXT("_result.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (Sub)
	{
		Sub->WriteTrace();
	}
	if (Sub && Name == TEXT("autoplay"))
	{
		// Single file the run script asserts on: verdict + every flow transition with its state hash.
		FString Trace = Json;
		Trace.TrimEndInline();
		Trace.RemoveFromEnd(TEXT("}"));
		Trace += TEXT(",\n  \"final_hash\": \"") + Sub->GetStateHash() + TEXT("\",\n  \"events\": ") + FString(UTF8_TO_TCHAR(Sub->GetController().trace_json().c_str())) + TEXT("\n}\n");
		FFileHelper::SaveStringToFile(Trace, *Sub->TracePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 %s %s (%d failure(s))"), *Name, bPassed ? TEXT("PASSED") : TEXT("FAILED"), GFailures.Num());
	GLog->Flush();
	FPlatformMisc::RequestExitWithStatus(true, bPassed ? 0 : 1);
}
}
