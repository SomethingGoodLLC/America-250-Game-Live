// PA250FlagArt.h — procedural flag artwork and cloth motion for capture posts (visual only).
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace PA250FlagArt
{
/** Flag texture size (2:1, the cloth's aspect). */
constexpr int32 TexW = 256;
constexpr int32 TexH = 128;

/** Pure: colour at (U, V) in [0,1], V = 0 at the top edge.
 *  british     - the 1707 Union flag (St George over St Andrew; St Patrick's saltire only came in 1801).
 *  continental - the New England "Pine Tree" flag flown by colonial forces in 1775.
 *  Anything else is plain white. */
FLinearColor Pixel(FName Faction, float U, float V);

/** Builds (and caches per faction) a transient texture from Pixel(), 2x2 supersampled. */
UTexture2D* GetTexture(FName Faction);

/** Pure cloth motion: sideways ripple and droop (cm) for a point X01 along the fly (0 = at the pole)
 *  and Z01 up the hoist, at time T. Zero at the pole so the flag stays tied on. */
FVector ClothOffset(float X01, float Z01, float T, float LengthCm);
} // namespace PA250FlagArt
