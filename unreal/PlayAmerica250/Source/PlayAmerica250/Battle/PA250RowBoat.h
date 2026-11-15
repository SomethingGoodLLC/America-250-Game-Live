// PA250RowBoat.h — a ship's boat that rows the first wave of a landing ashore (BattleSetup.mode
// "landing"). Men aboard neither move nor fire; when the keel touches they are set down on the
// beach and are soldiers again. GAMEPLAY SETTINGS: battle_rules.json modes.landing.
// PLACEHOLDER ART: the hull is engine cubes in the battlefield's dark wood; no oars, no oarsmen, and
// the men stand in it (a boat mesh and a seated pose are listed in PROGRESS.md as art still needed).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250RowBoat.generated.h"

class ARFCharacter;
class UStaticMeshComponent;

UENUM()
enum class EPA250BoatState : uint8
{
	Waiting,   // lying off, loaded
	Rowing,
	Beached,   // keel aground, the men getting out
	Empty
};

/** Pure boat rules (checked without a world). */
namespace PA250Boats
{
/** Seat Index of a boat: two files, bow to stern, in the boat's own axes (X forward, cm). */
inline FVector SeatOffset(int32 Index, int32 Seats)
{
	const int32 Rows = FMath::Max(1, (Seats + 1) / 2);
	const int32 Row = Index / 2;
	const float Along = Rows > 1 ? FMath::Lerp(190.f, -230.f, float(Row) / float(Rows - 1)) : 0.f;
	return FVector(Along, (Index % 2 == 0) ? -52.f : 52.f, 0.f);
}
/** Where man Index stands when he is set down: in front of the bow, two files, clear of the water. */
inline FVector AshoreOffset(int32 Index)
{
	return FVector(420.f + 130.f * (Index / 2), (Index % 2 == 0) ? -90.f : 90.f, 0.f);
}
/** Seconds to row a course. */
inline float RowSeconds(float CourseCm, float SpeedCm) { return SpeedCm > 1.f ? CourseCm / SpeedCm : 0.f; }
/** Boats the first wave needs: enough for the men, never more than the most allowed. */
inline int32 BoatsFor(int32 Men, int32 MenPerBoat, int32 MostBoats)
{
	if (Men <= 0) return 0;
	return FMath::Clamp((Men + FMath::Max(1, MenPerBoat) - 1) / FMath::Max(1, MenPerBoat), 1, FMath::Max(1, MostBoats));
}
}

UCLASS()
class PLAYAMERICA250_API APA250RowBoat : public AActor
{
	GENERATED_BODY()

public:
	APA250RowBoat();
	virtual void Tick(float DeltaSeconds) override;

	/** Lay the boat off at Start, bow toward Beach. Inland is the unit direction the men go ashore in. */
	void Launch(FName InFaction, const FVector& Start, const FVector& InBeach, const FVector& InInland, int32 InSeats);
	/** Put a man in the next free seat. False when the boat is full or under way to the beach no longer. */
	bool Embark(ARFCharacter* Man);
	void StartRowing();
	EPA250BoatState GetState() const { return State; }
	bool HasRoom() const;
	int32 GetAboard() const;
	int32 GetSetAshore() const { return SetAshore; }
	FName GetFaction() const { return Faction; }

private:
	void SetMenAshore();
	void Release(ARFCharacter* Man, const FVector& At, const FRotator& Facing);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Bow;
	TArray<TWeakObjectPtr<ARFCharacter>> Seats;
	FName Faction;
	FVector Beach = FVector::ZeroVector;
	FVector Inland = FVector::ForwardVector;
	EPA250BoatState State = EPA250BoatState::Waiting;
	float BeachedSeconds = 0.f;
	float BobTime = 0.f;
	float WaterZ = 0.f;
	int32 SetAshore = 0;
};
