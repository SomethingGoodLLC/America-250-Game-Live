#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MBTraversalComponent.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimMontage;
class UMBGeneratedClip;
class UPrimitiveComponent;

/** Upright, unit-scale course frame at the ground directly below the wall center. +X crosses the wall. */
USTRUCT(BlueprintType)
struct FMBVaultTarget {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Traversal") FTransform CourseFrame = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Traversal") TObjectPtr<UPrimitiveComponent> Wall;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Traversal") TObjectPtr<UPrimitiveComponent> LandingSurface;
};

UENUM(BlueprintType)
enum class EMBVaultStart : uint8 {
    Started,
    Busy,
    InvalidSetup,
    IncompatibleMesh,
    WrongObstacle,
    NotGrounded,
    InvalidApproach,
    BlockedLanding,
    BlockedPath
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMBVaultFinished, bool, bCompleted);

/** Input-independent playback and collision for authored vaults on an ACharacter. */
UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class MOTIONBRICKSTRAVERSAL_API UMBTraversalComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UMBTraversalComponent();

    UFUNCTION(BlueprintCallable, Category="Traversal")
    EMBVaultStart TryStart(UMBGeneratedClip* Clip, const FMBVaultTarget& Target);

    UFUNCTION(BlueprintCallable, Category="Traversal") void CancelVault();
    UFUNCTION(BlueprintPure, Category="Traversal") bool IsVaulting() const { return ActiveClip != nullptr; }
    UFUNCTION(BlueprintPure, Category="Traversal") float GetElapsed() const { return Elapsed; }
    bool IsPoseSynchronized() const;

    UPROPERTY(BlueprintAssignable, Category="Traversal") FMBVaultFinished OnVaultFinished;
    UPROPERTY(EditAnywhere, Category="Traversal", meta=(ClampMin="1")) float VaultHalfHeight = 36.f;
    UPROPERTY(EditAnywhere, Category="Traversal") FName MontageSlot = TEXT("DefaultSlot");

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
    bool HasCapsuleRoom(const ACharacter& Pawn, FVector At, float HalfHeight) const;
    bool FindLanding(const ACharacter& Pawn, const FMBVaultTarget& Target, FVector& At, float HalfHeight) const;
    void Finish(bool bCompleted);

    UPROPERTY(Transient) TObjectPtr<UMBGeneratedClip> ActiveClip;
    UPROPERTY(Transient) FMBVaultTarget ActiveTarget;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> Montage;
    UPROPERTY(Transient) TSubclassOf<UAnimInstance> PriorAnimClass;
    FVector Origin = FVector::ZeroVector;
    float Elapsed = 0.f;
    float StandingHalfHeight = 0.f;
    bool bMovementTickWasEnabled = true;
    bool bRecoverStandingCapsule = false;
};
