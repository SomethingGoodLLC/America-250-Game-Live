#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "PA250MountedTest.generated.h"

class UChildActorComponent;
class USpringArmComponent;
class UCameraComponent;

/** Standalone test pawn for the mounted animation prototype. */
UCLASS()
class PLAYAMERICA250_API APA250MountedTestPawn : public ACharacter
{
    GENERATED_BODY()
public:
    APA250MountedTestPawn();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
private:
    void Slash();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UChildActorComponent> MountedVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
};

/** Use via ?game=/Script/PlayAmerica250.PA250MountedTestGameMode on a test map. */
UCLASS()
class PLAYAMERICA250_API APA250MountedTestGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    APA250MountedTestGameMode();
};
