// PA250ControlPoint.h — capturable conquest post (Green / Meetinghouse / Buckman).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250ControlPoint.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250PostSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	int32 Sector = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	bool bClaimed = false;

	TWeakObjectPtr<AActor> Claimant;
};

UCLASS()
class PLAYAMERICA250_API APA250ControlPoint : public AActor
{
	GENERATED_BODY()

public:
	APA250ControlPoint();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName PostId = TEXT("green");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName InitialOwner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float Radius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CaptureSeconds = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CapturePerAttackerScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CapturePerDefenderScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	float CaptureRateScale = 1.f;

	UFUNCTION(BlueprintPure, Category = "Objective")
	FName GetHolder() const { return Holder; }

	UFUNCTION(BlueprintPure, Category = "Objective")
	FName GetCapturer() const { return Capturer; }

	UFUNCTION(BlueprintPure, Category = "Objective")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsContested() const { return bContested; }

	int32 GetPresence(FName Faction) const { const int32* N = Presence.Find(Faction); return N ? *N : 0; }

	bool ConsumeFlipFlag()
	{
		const bool B = bFlippedSinceLastCheck;
		bFlippedSinceLastCheck = false;
		return B;
	}

	void ApplyTuning(float InRadius, float InCaptureSeconds, float InPerAttackerScale);
	void BuildSlotRing(int32 SlotCount, float MinRadiusCm, float MaxRadiusCm);
	const TArray<FPA250PostSlot>& GetSlots() const { return Slots; }
	bool ClaimSlot(int32 Index, AActor* Claimant);
	void ReleaseSlot(AActor* Claimant);
	int32 FindFreeSlot(int32 PreferredSector) const;
	/** Angular octants around the post; squads prefer one sector each. */
	static constexpr int32 NumSectors = 8;
	/** Wrap-around distance between two sectors (0..4). */
	static int32 SectorDistance(int32 A, int32 B);
	FVector GetSlotLocation(int32 Index) const;

	/** Pure: which flag flies and how high (0 = foot of the pole, 1 = top). A held post's flag lowers
	 *  while the enemy neutralises it; on a neutral post the capturer's flag rises. Bare pole = NAME_None. */
	static void FlagTarget(FName InHolder, FName InCapturer, float Progress, FName& OutFaction, float& OutHeight01);
	/** Displayed (animated) flag: follows FlagTarget, lowering fully before it changes colour. */
	FName GetFlagFaction() const { return FlagFaction; }
	float GetFlagHeight() const { return FlagHeight; }
	/** Advance the flag animation (called from Tick; public for tests). */
	void UpdateFlag(float DeltaSeconds);
	/** Player-facing name for a post id ("green" -> "Lexington Green"). */
	static FString DisplayName(FName PostId);
	/** HUD marker anchor: just above the flagpole. */
	FVector GetMarkerLocation() const;
	static constexpr float PoleHeight = 900.f;
	/** Preview/test only: hand the post to a faction immediately (the flag then animates up). */
	void DebugSetHolder(FName NewHolder) { Capturer = NAME_None; CaptureProgress = 0.f; SetHolder(NewHolder); }
	/** Concord's magazine and foothold are not the crossing: they cannot change hands. */
	void SetContestLocked(bool bLocked) { bContestLocked = bLocked; }
	bool IsContestLocked() const { return bContestLocked; }
	/** Scenario setup already chose the holder; BeginPlay must not restore the level default. */
	void MarkHoldersAssigned() { bHoldersAssigned = true; }
	/** Empty means DisplayName(PostId). Concord calls the green "North Bridge". */
	void SetObjectiveTitle(FString Title) { ObjectiveTitle = std::move(Title); }
	FString GetObjectiveTitle() const { return ObjectiveTitle.IsEmpty() ? DisplayName(PostId) : ObjectiveTitle; }
	/** One capture step from explicit counts. A locked post does not move. */
	bool ApplyCapturePressure(FName Sole, int32 Attackers, int32 Defenders, float DeltaSeconds);
	FVector2D GetPoleOffset() const { return PoleOffset; }
	/** Test hook: the cloth's current vertex count (0 before it is built). */
	int32 GetClothVertexCount() const;

	/** Pure capture-rate helper for tests: rate from attacker/defender counts. */
	static float CaptureRate(int32 Attackers, int32 Defenders, float PerAttackerScale, float PerDefenderScale);

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Zone;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Disc;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FlagPole;

	/** Waving cloth (procedural grid) carrying the faction's flag texture. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> FlagCloth;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Finial;

private:
	void UpdateColor();
	void TickCapture(float DeltaSeconds);
	void PlaceFlag();
	void UpdateCloth(bool bCreate);
	/** Moves the pole (not the capture zone) to the nearest spot where pole + flag clear buildings. */
	void FindClearPoleSpot();
	void SetHolder(FName NewHolder);

	FName Holder = NAME_None;
	FName Capturer = NAME_None;
	bool bContestLocked = false;
	bool bHoldersAssigned = false;
	FString ObjectiveTitle;
	float CaptureProgress = 0.f;
	bool bContested = false;
	bool bFlippedSinceLastCheck = false;
	TMap<FName, int32> Presence;
	TArray<FPA250PostSlot> Slots;
	float LogicAccum = 0.f;
	float PoleBaseZ = -140.f;
	FVector2D PoleOffset = FVector2D::ZeroVector;
	FName FlagFaction = NAME_None;
	float FlagHeight = 0.f;
	FName FlagColorFaction = TEXT("unset");
	float ClothTime = 0.f;
	static constexpr int32 ClothSegX = 16;
	static constexpr int32 ClothSegZ = 8;
	static constexpr float ClothLength = 180.f;
	static constexpr float ClothHeight = 90.f;
};
