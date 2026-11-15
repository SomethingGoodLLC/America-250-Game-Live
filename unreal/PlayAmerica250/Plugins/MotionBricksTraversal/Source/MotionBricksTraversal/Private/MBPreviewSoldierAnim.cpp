#include "MBPreviewSoldierAnim.h"

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"

namespace {
const FName PreviewVaultSlot(TEXT("DefaultSlot"));

class FMBPreviewSoldierProxy : public FAnimInstanceProxy {
public:
    using FAnimInstanceProxy::FAnimInstanceProxy;

    void Initialize(UAnimInstance* Instance) override {
        FAnimInstanceProxy::Initialize(Instance);
        RegisterSlotNodeWithAnimInstance(PreviewVaultSlot);
    }

    void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override {
        FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
        const UAnimSequence* Wanted = CastChecked<UMBPreviewSoldierAnim>(Instance)->GetLocomotion();
        if (Wanted != Sequence) {
            Sequence = Wanted;
            Time = 0.0;
        }
    }

    void Update(float DeltaSeconds) override {
        if (Sequence && Sequence->GetPlayLength() > UE_KINDA_SMALL_NUMBER) {
            Time = FMath::Fmod(Time+DeltaSeconds,Sequence->GetPlayLength());
        }
        GetSlotWeight(PreviewVaultSlot,SlotWeight,SourceWeight,TotalWeight);
        UpdateSlotNodeWeight(PreviewVaultSlot,SlotWeight,1.f);
    }

    bool Evaluate(FPoseContext& Output) override {
        FPoseContext Locomotion(Output);
        Locomotion.ResetToRefPose();
        FAnimationPoseData SourceData(Locomotion);
        if (Sequence) Sequence->GetAnimationPose(SourceData,FAnimExtractContext(Time,false,{},true));
        FAnimationPoseData OutputData(Output);
        SlotEvaluatePose(PreviewVaultSlot,SourceData,SourceWeight,OutputData,SlotWeight,TotalWeight);
        return true;
    }

private:
    const UAnimSequence* Sequence = nullptr;
    double Time = 0.0;
    float SlotWeight = 0.f;
    float SourceWeight = 1.f;
    float TotalWeight = 0.f;
};
}

FAnimInstanceProxy* UMBPreviewSoldierAnim::CreateAnimInstanceProxy() {
    return new FMBPreviewSoldierProxy(this);
}

void UMBPreviewSoldierAnim::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) {
    delete static_cast<FMBPreviewSoldierProxy*>(Proxy);
}
