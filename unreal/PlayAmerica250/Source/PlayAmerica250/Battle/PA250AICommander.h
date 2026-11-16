// PA250AICommander.h — battle-level objective assignment for newly spawned squads.
// Artillery pass M2: every squad gets an order; idle pawns are reassigned.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PA250AICommander.generated.h"

class APA250BattleGameMode;
class APA250ControlPoint;
class APA250FieldGun;
class ARFCharacter;
class UPA250VolleyCoordinator;

UCLASS()
class PLAYAMERICA250_API UPA250AICommander : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Call after a bot is spawned: assign an objective within 2 s. */
	void OnSquadMemberSpawned(ARFCharacter* Soldier);
	/** Seeded spawn counter for PickSpawnTransform / formation slots. */
	uint32 NextSpawnSalt();

	/** Pure score for a candidate spawn (higher is better). */
	static float ScoreSpawn(const FVector& At, const FVector& Front, const TArray<FVector>& RecentDeaths,
		const TArray<FVector>& EnemyGunMuzzles, const TArray<FVector>& EnemyLocs, UWorld* World);

	/** Two-rank formation offset for slot Index (≥1.5 m apart). */
	static FVector FormationOffset(int32 SlotIndex, const FVector& Forward, float SpacingCm = 150.f);

private:
	void AssignObjective(ARFCharacter* Soldier);
	void TickIdleWatch(float DeltaTime);

	uint32 SpawnSalt = 0;
	TMap<TWeakObjectPtr<ARFCharacter>, float> IdleSeconds;
	TMap<TWeakObjectPtr<ARFCharacter>, FVector> LastLoc;
	TArray<TPair<FVector, float>> RecentDeaths; // location, world time
	TSet<int32> AssignedSquads;
};
