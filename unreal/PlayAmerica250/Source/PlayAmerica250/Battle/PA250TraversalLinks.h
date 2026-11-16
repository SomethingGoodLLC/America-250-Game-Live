#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavAreas/NavArea.h"
#include "Subsystems/WorldSubsystem.h"
#include "PA250TraversalLinks.generated.h"

class UNavLinkCustomComponent;

/** What a bot's path pays to go over an obstacle: battle_feel.json traversal.bot_link_cost times open ground. */
UCLASS()
class PLAYAMERICA250_API UPA250NavArea_Traversal : public UNavArea
{
	GENERATED_BODY()

public:
	UPA250NavArea_Traversal();
};

/**
 * One place where bots may cross a wall, fence or field work (M2): a smart navigation link from
 * the ground before it to the ground beyond, both ways. A bot whose path takes it is sent over by
 * his traversal component when he reaches its near end. Men start over the same link no closer
 * together than traversal.bot_stagger_seconds.
 */
UCLASS(NotPlaceable)
class PLAYAMERICA250_API APA250TraversalLink : public AActor
{
	GENERATED_BODY()

public:
	APA250TraversalLink();
	/** World ends, each on the ground on one side of the obstacle. */
	void SetEnds(const FVector& A, const FVector& B);

	int32 Crossings = 0;
	int32 Refused = 0;
	int32 Held = 0;

private:
	void OnReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint);

	UPROPERTY(VisibleAnywhere, Category = "Traversal")
	TObjectPtr<UNavLinkCustomComponent> Link;
	float LastStart = -1000.f;
};

namespace PA250Traversal
{
/** Pure: the places along a wall block where links go: one every SpacingCm of its length (at least
 *  one), as pairs of ends StandOffCm clear of each face. Centre and AxisLong (level) place it. */
PLAYAMERICA250_API void LinkEnds(const FVector& Centre, const FVector& AxisLong, float LengthCm, float DepthCm, float SpacingCm, float StandOffCm,
	TArray<TPair<FVector, FVector>>& Out);
}

/**
 * Places the traversal links of a battle level once its navmesh stands, and again when asked
 * (a work raised or battered down). Links go only where the obstacle can be crossed by the data's
 * limits from both sides and both ends are on the navmesh; anything else stays cover.
 */
UCLASS()
class PLAYAMERICA250_API UPA250TraversalLinks : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPA250TraversalLinks, STATGROUP_Tickables); }
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	void RequestRebuild() { bDirty = true; }
	/** Removes every link and places them again from the level as it stands. Returns how many. */
	int32 Rebuild();
	int32 Num() const { return Links.Num(); }
	/** Crossings begun through the links, and starts refused or held back, since the level began. */
	void Totals(int32& OutCrossings, int32& OutRefused, int32& OutHeld) const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<APA250TraversalLink>> Links;
	bool bDirty = true;
	float Waited = 0.f;
	/** Places that were off the navmesh at the last placing, and how often it has been tried again for them. */
	int32 LastOffNav = 0;
	int32 Retries = 0;
	double RetryAt = 0.0;
	int32 PastCrossings = 0;
	int32 PastRefused = 0;
	int32 PastHeld = 0;
};
