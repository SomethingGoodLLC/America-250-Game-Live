#include "RFHealthComponent.h"
#include "GameFramework/Actor.h"
#include "MusketWeapon.h"
#include "RFCharacter.h"

URFHealthComponent::URFHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URFHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = MaxHealth;
	if (AActor* Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.AddDynamic(this, &URFHealthComponent::OnTakeAnyDamage);
	}
}

void URFHealthComponent::OnTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
                                         AController* InstigatedBy, AActor* DamageCauser)
{
	if (Damage <= 0.f || CurrentHealth <= 0.f)
	{
		return;
	}
	float Applied = Damage;
	if (const ARFCharacter* C = Cast<ARFCharacter>(GetOwner()))
	{
		if (C->Musket && C->Musket->IsReloading())
		{
			const float Vuln = C->Musket->GetTuning().ActiveReload.ReloadVulnerability;
			Applied *= FMath::Max(1.f, Vuln);
		}
	}
	if (DamageCauser)
	{
		LastDamageLocation = DamageCauser->GetActorLocation();
		bHasLastDamageLocation = true;
	}
	else if (InstigatedBy && InstigatedBy->GetPawn())
	{
		LastDamageLocation = InstigatedBy->GetPawn()->GetActorLocation();
		bHasLastDamageLocation = true;
	}
	const float Floor = FMath::Min(MinHealthAfterNextDamage, CurrentHealth);
	MinHealthAfterNextDamage = 0.f;
	CurrentHealth = FMath::Clamp(CurrentHealth - Applied, FMath::Max(0.f, Floor), MaxHealth);
	if (CurrentHealth <= 0.f)
	{
		LastInstigator = InstigatedBy;
		OnDied.Broadcast(GetOwner());
	}
	else
	{
		OnWounded.Broadcast(Applied, bHasLastDamageLocation ? LastDamageLocation : GetOwner()->GetActorLocation());
	}
}
