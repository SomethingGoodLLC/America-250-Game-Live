// PA250Intensity.h — how hard the battle presses on the player (M6, GAME INVENTION): the start at a
// near miss, the shake and the thrown earth of round shot, the layered battle music. Every number is
// battle_feel.json intensity. The pure rules are here (shared vectors music_layers_*); the world's
// part is in PA250Intensity.cpp. Display and sound only: nothing here changes the fight.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Components/AudioComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "PA250Intensity.generated.h"

class ARFCharacter;
class UAudioComponent;

namespace PA250Music
{
/** Pure: 0 (the day goes well) to 1 (his squads broken and his reserve spent). SteadyShare: the share
 *  of the player's squads that stand steady; ReserveShare: the share of his reserve still unspent. */
inline float Intensity(float SteadyShare, float ReserveShare, float MoraleWeight, float TicketWeight)
{
	const float Steady = FMath::Clamp(SteadyShare, 0.f, 1.f);
	const float Reserve = FMath::Clamp(ReserveShare, 0.f, 1.f);
	return FMath::Clamp(MoraleWeight * (1.f - Steady) + TicketWeight * (1.f - Reserve), 0.f, 1.f);
}

/** Pure: a layer's loudness, 0 to 1: silent below From, full at Full, rising evenly between. A layer
 *  whose Full is not above its From is a step: on from From upward (so 0, 0 always plays). */
inline float LayerGain(float Intensity, float From, float Full)
{
	if (Full <= From) return Intensity >= From ? 1.f : 0.f;
	return FMath::Clamp((Intensity - From) / (Full - From), 0.f, 1.f);
}
}

namespace PA250Intensity
{
/** Pure: the camera shake's scale DistM from where a ball struck: Scale within InnerM, nothing beyond OuterM. */
inline float ShakeScaleAt(float DistM, float InnerM, float OuterM, float Scale)
{
	if (DistM <= InnerM) return Scale;
	if (DistM >= OuterM || OuterM <= InnerM) return 0.f;
	return Scale * (1.f - (DistM - InnerM) / (OuterM - InnerM));
}

/** Pure: may the same shout be given again? */
inline bool MayShout(float Now, float LastAt, float CooldownSeconds)
{
	return Now - LastAt >= CooldownSeconds;
}

/** A ball has passed the player's ear (AMusketProjectile): the start of the view, a flinch of the
 *  body away from its path, a short shake, and its crack. True when it counted as a near miss. */
PLAYAMERICA250_API bool NearMiss(ARFCharacter* Player, const FVector& ShotFrom, const FVector& Closest, const FVector& Listener);
/** Round shot has struck at At: the local player's camera shakes by his distance, and close to it earth is thrown over his view. */
PLAYAMERICA250_API void RoundShotStrike(UWorld* World, const FVector& At);
/** The one shake, at a scale. */
PLAYAMERICA250_API void Shake(UWorld* World, float Scale);
/** How often each has happened (for the trace and the cases). */
PLAYAMERICA250_API int32 NearMissCount();
PLAYAMERICA250_API int32 ShakeCount();
PLAYAMERICA250_API int32 DirtCount();
PLAYAMERICA250_API void ResetCounts();
}

/** The battle's one camera shake: noise in place and in turn, its size and time from battle_feel.json intensity.shake. */
UCLASS()
class PLAYAMERICA250_API UPA250FeelShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UPA250FeelShake(const FObjectInitializer& ObjectInitializer);
};

/**
 * The battle's music (M6): the layers of one piece, started together so they stay in step, each as
 * loud as the day asks (PA250Music). A layer whose sound is not on disk is named once in the log
 * (MISSING) and left out; with none on disk there is no battle music.
 */
struct PLAYAMERICA250_API FPA250BattleMusic
{
	/** Starts every layer that is on disk, silent but the first. Returns how many started. */
	int32 Start(const UObject* Ctx);
	/** Each layer toward the loudness the day asks. */
	void Update(float SteadyShare, float ReserveShare);
	void Stop();
	int32 NumPlaying() const { return Layers.Num(); }
	float GetIntensity() const { return LastIntensity; }
	const TArray<FString>& GetMissing() const { return Missing; }

private:
	struct FLayer
	{
		TStrongObjectPtr<UAudioComponent> Sound;
		float From = 0.f;
		float Full = 0.f;
		float Gain = -1.f;
	};
	TArray<FLayer> Layers;
	TArray<FString> Missing;
	float LastIntensity = 0.f;
};
