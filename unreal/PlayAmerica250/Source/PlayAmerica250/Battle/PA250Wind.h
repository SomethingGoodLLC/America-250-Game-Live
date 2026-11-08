// PA250Wind.h — the one breeze over the battlefield, shared by everything that shows it
// (smoke drift, the way the flags fly). Visual only; it never affects a ball or the battle.
#pragma once

#include "CoreMinimal.h"

namespace PA250Wind
{
/** Light dawn breeze, cm/s (~0.8 m/s). */
inline FVector Velocity() { return FVector(70.f, 30.f, 0.f); }
/** Downwind direction as a yaw: flags fly this way. */
inline float Yaw() { return Velocity().Rotation().Yaw; }
} // namespace PA250Wind
