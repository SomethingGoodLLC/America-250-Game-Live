#pragma once

#include "CoreMinimal.h"

/**
 * PA250Combat cases for battle feel (battle_feel.json): movement, handling, intensity. Pure checks
 * that need no world. The shared vectors are protocol/test_vectors/anim (also checked by pytest).
 */
namespace PA250FeelCases
{
using FCheck = TFunctionRef<void(bool, const TCHAR*)>;

/** M2: how an obstacle is crossed (shared vectors traversal_classify_*). */
void RunTraversal(FCheck Check);

/** M7: the battle look (shared vectors time_band_*): bands, levels, tiers, the platform's choice. */
void RunLook(FCheck Check);

/** M6: the music layers (shared vectors music_layers_*), the shake by distance, the shouts' cooldown, the file's values. */
void RunIntensity(FCheck Check);

/** M5: the playable general's pure rules (score, meter, offer, the screen's words), by the persons data. */
void RunHero(FCheck Check);

/** M4: the clip registry, its placeholders and the loading drill's names (shared vector drill_clip_names). */
void RunClips(FCheck Check);

/** M3: the input buffer (shared vectors input_buffer_*) and the movement values. */
void RunInput(FCheck Check);

/** Cover: places along a wall, who takes which, the fire-over stance. */
void RunCover(FCheck Check);

/** M1: the data file, who is animated at full quality, which gait a speed asks for. */
void RunAnimation(FCheck Check);
}
