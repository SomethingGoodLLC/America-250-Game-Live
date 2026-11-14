// PA250Support.h — the support roles: the ensign's colours, the musician's relay and cadence, the
// surgeon's hands. Pure rules here (checked without a world) and the colours themselves as an actor.
// GAMEPLAY SETTINGS: every value is unit_classes.json verbs.ensign / musician / surgeon.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250Support.generated.h"

class ARFCharacter;
class UStaticMeshComponent;

namespace PA250Support
{
/** A routing squad needs less calm to rally with its side's colours flying near it. */
inline float RallySecondsNearColours(float RallySeconds, bool bColoursNear, float Scale)
{
	return bColoursNear ? RallySeconds * FMath::Clamp(Scale, 0.05f, 1.f) : RallySeconds;
}

/** Does an order given at Giver reach a line standing at Line? An officer's word carries OwnRadiusCm.
 *  Any giver's word (the player's too) carries on through a musician: a line within RelayCm of a
 *  musician who is himself within OwnRadiusCm of the giver hears it. bOutRelayed: it took a musician. */
inline bool OrderReaches(const FVector& Giver, const FVector& Line, bool bOfficer, float OwnRadiusCm, const TArray<FVector>& Musicians,
	float RelayCm, bool* bOutRelayed = nullptr)
{
	if (bOutRelayed) *bOutRelayed = false;
	if (bOfficer && FVector::Dist2D(Giver, Line) <= OwnRadiusCm) return true;
	for (const FVector& M : Musicians)
	{
		if (FVector::Dist2D(M, Giver) <= OwnRadiusCm && FVector::Dist2D(M, Line) <= RelayCm)
		{
			if (bOutRelayed) *bOutRelayed = true;
			return true;
		}
	}
	return false;
}

/** Loading in cadence: men who fire by volley load a little faster within earshot of a drum or fife. */
inline float CadenceReloadScale(bool bVolleyRole, bool bMusicianNear, float Scale)
{
	return (bVolleyRole && bMusicianNear) ? FMath::Clamp(Scale, 0.5f, 1.f) : 1.f;
}

/** A fallen man can be saved while the surgeon's window from his fall is open. */
inline bool CanStillBeSaved(float FellAt, float Now, float WindowSeconds)
{
	return Now >= FellAt && Now - FellAt <= WindowSeconds;
}

/** May another man of this class take the field? MaxOnField 0 = as many as the army has. */
inline bool ClassHasFieldRoom(int32 AliveOfClass, int32 MaxOnField)
{
	return MaxOnField <= 0 || AliveOfClass < MaxOnField;
}
}

UENUM()
enum class EPA250ColoursState : uint8
{
	Carried,
	Fallen,
	Taken
};

/** A side's colours: carried by its ensign, dropped where he falls, taken up again by a friend who
 *  reaches them or carried off by an enemy who does. */
UCLASS()
class PLAYAMERICA250_API APA250Colours : public AActor
{
	GENERATED_BODY()

public:
	APA250Colours();

	void Raise(FName InFaction, ARFCharacter* InBearer);
	FName GetFaction() const { return Faction; }
	EPA250ColoursState GetState() const { return State; }
	ARFCharacter* GetBearer() const { return Bearer.Get(); }
	/** The bearer is down: the colours lie where he fell. */
	void Drop();
	/** A friend takes them up. */
	void TakeUp(ARFCharacter* NewBearer);
	/** An enemy carries them off: they are out of the fight. */
	void CarryOff();
	/** Seconds an enemy has stood over the fallen colours (the battle counts it up to the capture time). */
	float EnemyOverSeconds = 0.f;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Pole;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Cloth;
	FName Faction;
	EPA250ColoursState State = EPA250ColoursState::Carried;
	TWeakObjectPtr<ARFCharacter> Bearer;
};
