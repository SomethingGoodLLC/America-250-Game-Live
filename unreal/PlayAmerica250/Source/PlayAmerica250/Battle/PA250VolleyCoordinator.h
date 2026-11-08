// PA250VolleyCoordinator.h — squad volleys, callouts, suppression.
#pragma once

#include "CoreMinimal.h"
#include "FormationQuery.h"
#include "UObject/Object.h"
#include "PA250VolleyCoordinator.generated.h"

class ARFCharacter;
class APA250BattleGameMode;
class UMusketTuning;

UENUM()
enum class EVolleyCall : uint8
{
	None,
	MakeReady,
	Present,
	Fire
};

UCLASS()
class PLAYAMERICA250_API UPA250VolleyCoordinator : public UObject
{
	GENERATED_BODY()

public:
	void Init(APA250BattleGameMode* InGM);
	void Tick(float DeltaSeconds);
	void NotifyShot(ARFCharacter* Shooter, const FVector& Dir, bool bHit);

	FString GetCallout() const;
	int32 GetVolleyCount() const { return VolleyCount; }
	float GetAverageHits() const { return VolleyCount > 0 ? (float)TotalHits / VolleyCount : 0.f; }
	int32 GetLineEngagements() const { return LineEngagements; }
	int32 GetEngagements() const { return Engagements; }

private:
	void RefreshFormations();
	void TickSquads(float DeltaSeconds);
	void FireVolley(const TArray<ARFCharacter*>& Squad);

	TWeakObjectPtr<APA250BattleGameMode> GameMode;
	TMap<FName, TArray<FVolleyShot>> RecentShots;
	float ShotClock = 0.f;
	FString Callout;
	float CalloutTimer = 0.f;
	int32 VolleyCount = 0;
	int32 TotalHits = 0;
	int32 LineEngagements = 0;
	int32 Engagements = 0;

	struct FSquad
	{
		FName Faction;
		TArray<TWeakObjectPtr<ARFCharacter>> Members;
		EVolleyCall Stage = EVolleyCall::None;
		float StageTimer = 0.f;
	};
	TArray<FSquad> Squads;
};
