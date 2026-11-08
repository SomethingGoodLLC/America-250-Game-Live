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
	CurrentHealth = FMath::Clamp(CurrentHealth - Applied, 0.f, MaxHealth);
	if (CurrentHealth <= 0.f)
	{
		LastInstigator = InstigatedBy;
		OnDied.Broadcast(GetOwner());
	}
}
