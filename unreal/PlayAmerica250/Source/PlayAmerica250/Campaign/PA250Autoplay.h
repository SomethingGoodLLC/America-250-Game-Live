// PA250Autoplay.h — headless self-test / autoplay helpers (flags: -PA250Autoplay, -PA250Selftest=<name>).
#pragma once

#include "CoreMinimal.h"

class UPA250Subsystem;

namespace PA250Autoplay
{
/** -PA250Autoplay: drive the whole vertical-slice loop with no human input. */
PLAYAMERICA250_API bool IsAutoplay();
/** -PA250Selftest=<name>: run one scripted check in the current level, write a result file, and exit. */
PLAYAMERICA250_API FString SelftestName();

/** Records a named check. Failures are collected and written to the result file. */
PLAYAMERICA250_API void Check(UPA250Subsystem* Sub, bool bCondition, const FString& What);

/** Writes Saved/PA250/<Name>_result.json (+ the transition trace) and exits the process with 0 or 1. */
PLAYAMERICA250_API void Finish(UPA250Subsystem* Sub, const FString& Name);

/** Failed-check list accumulated so far. */
PLAYAMERICA250_API const TArray<FString>& Failures();
PLAYAMERICA250_API const TArray<FString>& Notes();
}
