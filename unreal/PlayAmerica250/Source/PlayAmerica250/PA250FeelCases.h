#pragma once

#include "CoreMinimal.h"

/**
 * PA250Combat cases for battle feel (battle_feel.json): movement, handling, intensity. Pure checks
 * that need no world. The shared vectors are protocol/test_vectors/anim (also checked by pytest).
 */
namespace PA250FeelCases
{
using FCheck = TFunctionRef<void(bool, const TCHAR*)>;

/** Cover: places along a wall, who takes which, the fire-over stance. */
void RunCover(FCheck Check);

/** M1: the data file, who is animated at full quality, which gait a speed asks for. */
void RunAnimation(FCheck Check);
}
