#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_VolleyDischarge.generated.h"

/** Notify_Volley_Discharge: fire the man's pending volley shot on the recoil frame. */
UCLASS()
class PLAYAMERICA250_API UAnimNotify_VolleyDischarge : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Notify_Volley_Discharge"); }
};
