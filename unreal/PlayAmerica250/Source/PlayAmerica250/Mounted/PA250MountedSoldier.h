#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250MountedSoldier.generated.h"

class UAnimSequence;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/** Placeable visual prototype: a colonial rider, horse and sword. */
UCLASS(Blueprintable)
class PLAYAMERICA250_API APA250MountedSoldier : public AActor
{
    GENERATED_BODY()
public:
    APA250MountedSoldier();
    /** Play a single slash, then return to the mounted idle. */
    UFUNCTION(BlueprintCallable, Category="Mounted combat")
    void Slash();
    /** Switch between idle and the in-place walk clip. */
    void SetMoving(bool bMoving);

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<USkeletalMeshComponent> Horse;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<USkeletalMeshComponent> Rider;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UStaticMeshComponent> SwordBlade;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UStaticMeshComponent> SwordGuard;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UStaticMeshComponent> SwordGrip;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseIdle;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseWalk;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> RiderIdle;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> RiderSlash;

private:
    void ResumeIdle();
    FTimerHandle SlashTimer;
    bool bIsMoving = false;
};
