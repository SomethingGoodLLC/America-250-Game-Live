// PA250StoryArt.h — the full-screen pictures of the campaign pass (decisions, battle stories).
// Public-domain paintings are wanted for these; none is shipped yet. A key resolves to the imported
// texture /Game/PA250/Stories/T_Story_<key> when it exists, else to a generated placeholder wash
// (a sepia sky over dark ground, varied by key), and the log says which. Display only.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace PA250StoryArt
{
/** Content path a key's painting is looked for at. */
PLAYAMERICA250_API FString AssetPathFor(const FString& Key);
/** The picture for a key. bOutPlaceholder: no painting is imported for it yet. */
PLAYAMERICA250_API UTexture2D* TextureFor(UObject* Outer, const FString& Key, bool& bOutPlaceholder);
/** Pure: the colour of the placeholder at a point (U, V in 0..1), for one key. */
PLAYAMERICA250_API FColor PlaceholderPixel(const FString& Key, float U, float V);
}
