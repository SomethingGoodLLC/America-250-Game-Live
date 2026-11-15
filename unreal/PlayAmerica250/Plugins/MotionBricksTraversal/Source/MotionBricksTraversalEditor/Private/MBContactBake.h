#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;
class UMBGeneratedClip;

/** Correct the three authored contacts for Quinn on the fixed OneHandedJump0 course. */
bool BakeVaultContacts(const FReferenceSkeleton& Skeleton, const TArray<FTransform>& Reference,
    TArray<TArray<FTransform>>& LocalFrames, UMBGeneratedClip& Clip);
