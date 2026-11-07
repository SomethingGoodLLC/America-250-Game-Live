#include "RFHealthComponent.h"
#include "GameFramework/Actor.h"

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
	CurrentHealth = FMath::Clamp(CurrentHealth - Damage, 0.f, MaxHealth);
	if (CurrentHealth <= 0.f)
	{
		LastInstigator = InstigatedBy;
		OnDied.Broadcast(GetOwner());
	}
}
