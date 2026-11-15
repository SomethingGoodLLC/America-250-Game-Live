#pragma once

#include "Animation/AnimInstance.h"
#include "MBPreviewSoldierAnim.generated.h"

class UAnimSequence;

UCLASS(Transient, NotBlueprintable)
class UMBPreviewSoldierAnim : public UAnimInstance {
    GENERATED_BODY()
public:
    void SetLocomotion(UAnimSequence* InSequence) { Sequence = InSequence; }
    UAnimSequence* GetLocomotion() const { return Sequence; }

protected:
    FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;

private:
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Sequence;
};
