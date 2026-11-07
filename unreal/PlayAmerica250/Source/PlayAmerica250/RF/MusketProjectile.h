// MusketProjectile.h — a ballistic musket ball (projectile, not hitscan).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusketProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;

UCLASS()
class PLAYAMERICA250_API AMusketProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMusketProjectile();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float Damage = 40.f;

	/** Same-faction hits are ignored unless this is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bFriendlyFire = false;

	USphereComponent* GetCollisionComponent() const { return CollisionComponent; }
	UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleDefaultsOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	           const FHitResult& Hit);
};
