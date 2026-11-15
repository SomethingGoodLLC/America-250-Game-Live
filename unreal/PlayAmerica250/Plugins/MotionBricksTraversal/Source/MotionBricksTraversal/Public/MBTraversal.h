#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "MBTraversal.generated.h"

class UAnimSequence;
class UMBTraversalComponent;
class USkeletalMesh;
class UStaticMeshComponent;
struct FMBVaultTarget;

USTRUCT()
struct FMBCourseContact {
    GENERATED_BODY()

    UPROPERTY() FName Bone;
    UPROPERTY() FVector BoneLocalPoint = FVector::ZeroVector;
    UPROPERTY() FVector CourseAnchor = FVector::ZeroVector;
    UPROPERTY() int32 FirstFrame = 0;
    UPROPERTY() int32 LastFrame = 0;
};

UCLASS(BlueprintType)
class MOTIONBRICKSTRAVERSAL_API UMBGeneratedClip : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") TObjectPtr<UAnimSequence> Animation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") TArray<FVector> RootOffsets;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") float FPS = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") float WallDistance = 167.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") float WallHeight = 68.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal") float WallDepth = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal") float WallWidth = 70.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal") bool bMatchWallDimensions = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Traversal") TSoftObjectPtr<USkeletalMesh> TargetMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") FString Provenance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") bool bEvaluationOnly = true;
    UPROPERTY() TArray<FMBCourseContact> Contacts;
    float Duration() const { return RootOffsets.Num() > 1 && FMath::IsFinite(FPS) && FPS > 0.f ? (RootOffsets.Num()-1)/FPS : 0.f; }
    FVector Sample(float Time) const;
};

UCLASS()
class MOTIONBRICKSTRAVERSAL_API AMBTestCourse : public AActor {
    GENERATED_BODY()
public:
    AMBTestCourse();
    UPROPERTY(VisibleAnywhere, Category="MotionBricks Evaluation") TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY(VisibleAnywhere, Category="MotionBricks Evaluation") TObjectPtr<UStaticMeshComponent> Wall;
    UPROPERTY(VisibleAnywhere, Category="MotionBricks Evaluation") TObjectPtr<UStaticMeshComponent> LandingBlocker;
    void ToggleLanding();
    FMBVaultTarget GetVaultTarget() const;
};

UCLASS()
class MOTIONBRICKSTRAVERSAL_API AMBTestCharacter : public ACharacter {
    GENERATED_BODY()
public:
    AMBTestCharacter();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MotionBricks Evaluation") TObjectPtr<UMBGeneratedClip> Clip;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Traversal") TObjectPtr<UMBTraversalComponent> Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MotionBricks Evaluation") FString LastResult = TEXT("Ready");
    UFUNCTION(BlueprintCallable, Category="MotionBricks Evaluation") bool TryTraversal();
    UFUNCTION(BlueprintCallable, Category="MotionBricks Evaluation") void ResetAttempt();
    UFUNCTION(BlueprintCallable, Category="MotionBricks Evaluation") void ToggleBlockedLanding();
private:
    void Trigger();
    void SelectManny();
    void SelectQuinn();
    void ConfigureSoldier(bool bManny);
    void BuildStandInGear(bool bManny);
    void SampleMusketClearance();
    UFUNCTION() void HandleVaultFinished(bool bCompleted);
    void SetResult(const FString& Message);
    void TickVerification(float DeltaSeconds);
    void SampleVerificationContacts(UWorld* World, ELevelTick TickType, float DeltaSeconds);
    TWeakObjectPtr<AMBTestCourse> ActiveCourse;
    float NormalHalfHeight = 88.f;
    UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Gear;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> Musket;
    FString SoldierName;
    int32 MusketChecks = 0;
    int32 MusketWallHits = 0;
    double MusketMinFloorClearance = TNumericLimits<double>::Max();
    bool bDebug = true;
    bool bVerify = false;
    bool bVerificationPassed = true;
    int32 VerificationPhase = 0;
    float VerificationTime = 0.f;
    TArray<float> TraversalFrameTimes;
    FVector VerificationStart;
    FVector VerificationLanding;
    int32 VerificationCapture = 0;
    struct FObservedContact {
        int32 Samples = 0;
        double HeightError = 0.0;
        double PlanarError = 0.0;
    };
    TArray<FObservedContact> ObservedContacts;
    FDelegateHandle ContactVerificationTick;
};

UCLASS()
class MOTIONBRICKSTRAVERSAL_API AMBTestGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    AMBTestGameMode();
};
