// MusketProjectile.h — a ballistic musket ball (projectile, not hitscan).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusketProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UPA250WeaponTuning;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TObjectPtr<UPA250WeaponTuning> WeaponTuning;

	FVector SpawnLocation = FVector::ZeroVector;
	FVector PrevLocation = FVector::ZeroVector;
	bool bHavePrev = false;
	bool bWhizzed = false;

	USphereComponent* GetCollisionComponent() const { return CollisionComponent; }
	UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }
	UStaticMeshComponent* GetTracer() const { return TracerMesh; }
	/** Stretch the tracer from the ball back toward the muzzle (capped at TracerLengthCm). */
	void UpdateTracer();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleDefaultsOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	/** Glowing streak trailing the ball so a 300 m/s shot is visible. Grows from the muzzle. */
	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> TracerMesh;

	UPROPERTY(EditAnywhere, Category = "Visual")
	float TracerLengthCm = 450.f;

	UPROPERTY(EditAnywhere, Category = "Visual")
	float TracerThicknessCm = 4.f;

	UPROPERTY(EditAnywhere, Category = "Visual")
	FLinearColor TracerColor = FLinearColor(8.f, 5.5f, 2.2f);

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	           const FHitResult& Hit);
};
