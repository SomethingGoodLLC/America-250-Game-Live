#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250MountedSoldier.generated.h"

class AMusketWeapon;
class UAnimSequence;
class UBoxComponent;
class UCapsuleComponent;
class UPrimitiveComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/** Horse gaits, slowest first. Ground speeds come from SourceArt/warhorse/WarHorse_clips.json. */
enum class EPA250Gait : uint8
{
    Walk,
    Trot,
    Canter,
    Gallop
};

/** A colonial rider on a war horse: gait clips, sword, firearms, a horse that can be shot and fall. */
UCLASS(Blueprintable)
class PLAYAMERICA250_API APA250MountedSoldier : public AActor
{
    GENERATED_BODY()
public:
    APA250MountedSoldier();

    /** Ground speed (cm/s) each gait clip was authored at; matching it keeps the hooves from sliding. */
    static float GaitClipSpeed(EPA250Gait Gait);
    /** Clip for a measured ground speed, with hysteresis so the gait does not flicker at a boundary. */
    static EPA250Gait GaitForSpeed(float Speed, EPA250Gait Current);
    /** Clip play rate for a speed in a gait (clamped so a gait never plays absurdly fast or slow). */
    static float GaitPlayRate(float Speed, EPA250Gait Gait);
    /** GAME INVENTION: about three close musket balls bring a horse down. */
    static constexpr float HorseMaxHealth = 250.f;

    /** Play a single slash, then return to the mounted idle. The blade's path is swept for hits. */
    UFUNCTION(BlueprintCallable, Category="Mounted combat")
    void Slash();
    bool IsSlashing() const;
    /** Legacy two-state driver (test pawn): idle or walk. */
    void SetMoving(bool bMoving);
    /** Picks idle / walk / trot / canter / gallop from the ground speed and matches the play rate to it. */
    void SetGroundSpeed(float Speed);
    float GetHorsePlayRate() const;
    UAnimSequence* GetHorseClip() const { return CurrentHorseClip; }
    void SetFirearm(AMusketWeapon* Weapon, bool bPistol);
    void SetAiming(bool bAiming) { bWantsAim = bAiming; }
    void PrepareShot();
    void NotifyFirearmShot();
    bool HasFirearm() const { return ActiveFirearm != nullptr; }
    USkeletalMeshComponent* GetRiderMesh() const { return Rider; }
    USkeletalMeshComponent* GetHorseMesh() const { return Horse; }

    float GetHorseHealth() const { return HorseHealth; }
    bool IsHorseDead() const { return bHorseDead; }
    /** True for the rider's hit capsule (damage there belongs to the man, not the horse). */
    bool IsRiderComponent(const UPrimitiveComponent* Comp) const;
    /**
     * The horse goes down: detached from the rider's capsule, it plays its death clip, carries its
     * momentum forward for a moment and stays on the field. Its rider is hidden here (he is thrown).
     */
    void BeginHorseDeath(const FVector& Velocity);
    /** Rider got down but keeps his horse (dragoons): it stands where he left it, riderless, still a target. */
    void Picket();
    /** Its rider is back in the saddle (the soldier attaches it to his capsule). */
    void Remount();
    bool IsRidden() const { return bRidden; }
    /** The seated rider wears the soldier's own coat (same period soldier mesh, his per-slot materials). */
    void DressRiderLike(const USkeletalMeshComponent* Soldier);

    virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator,
                             AActor* DamageCauser) override;
    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<USkeletalMeshComponent> Horse;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<USkeletalMeshComponent> Rider;
    /** Hit volume for the horse's barrel, neck and head (query only; balls stop here and wound the horse). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UBoxComponent> HorseBody;
    /** Hit volume for the seated man; damage is passed on to the owning soldier. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UCapsuleComponent> RiderBody;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UStaticMeshComponent> SwordBlade;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UStaticMeshComponent> SwordGuard;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mounted")
    TObjectPtr<UStaticMeshComponent> SwordGrip;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseIdle;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseWalk;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseTrot;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseCanter;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseGallop;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseHitReact;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> HorseDeath;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> RiderIdle;
    UPROPERTY(EditDefaultsOnly, Category="Mounted|Animation")
    TObjectPtr<UAnimSequence> RiderSlash;

private:
    void ResumeIdle();
    void PlayHorseClip(UAnimSequence* Clip, bool bLoop, float Rate);
    void EndHitReact();
    void SweepSword();
    FVector SwordTip() const;
    void TickDeathSlide(float DeltaSeconds);
    FTimerHandle SlashTimer;
    FTimerHandle HitReactTimer;
    bool bIsMoving = false;
    bool bPistolAim = false;
    bool bWantsAim = false;
    float AimAlpha = 0.f;
    float ReloadAlpha = 0.f;
    float RecoilAlpha = 0.f;
    void UpdateFirearmPose(float DeltaSeconds);
    UPROPERTY(Transient) TObjectPtr<AMusketWeapon> ActiveFirearm;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentHorseClip;
    EPA250Gait CurrentGait = EPA250Gait::Walk;
    float GroundSpeed = 0.f;
    bool bHitReacting = false;

    float HorseHealth = HorseMaxHealth;
    bool bHorseDead = false;
    bool bRidden = true;
    FVector DeathVelocity = FVector::ZeroVector;
    float DeathSlideTime = 0.f;

    float SlashElapsed = 0.f;
    FVector LastSwordTip = FVector::ZeroVector;
    UPROPERTY(Transient) TSet<TObjectPtr<AActor>> SlashVictims;
};
