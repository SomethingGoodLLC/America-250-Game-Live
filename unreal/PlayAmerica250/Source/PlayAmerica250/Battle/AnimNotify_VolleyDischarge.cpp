#include "AnimNotify_VolleyDischarge.h"
#include "PA250BattleGameMode.h"
#include "PA250VolleyCoordinator.h"
#include "RFCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_VolleyDischarge::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp) return;
	ARFCharacter* Man = Cast<ARFCharacter>(MeshComp->GetOwner());
	if (!Man || Man->IsDead()) return;
	UWorld* World = Man->GetWorld();
	APA250BattleGameMode* GM = World ? World->GetAuthGameMode<APA250BattleGameMode>() : nullptr;
	if (!GM || !GM->GetVolleys()) return;
	// Fires his pending volley shot now and removes it, so its timer cannot fire it again.
	GM->GetVolleys()->NotifyVolleyDischarge(Man);
}
