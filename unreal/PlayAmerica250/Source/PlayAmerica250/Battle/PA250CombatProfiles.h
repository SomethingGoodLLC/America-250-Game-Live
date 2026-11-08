// PA250CombatProfiles.h — GAMEPLAY SETTINGS (provisional, not historical claims).
// Three independent knobs, joined only by the Assignments table:
//   * Training (Recruit / Trained / Veteran): AI reload pace & consistency, cohesion, morale resolve.
//   * Tactical role (Line / Light / Irregular): cover preference, order width, withdrawal and fire style.
//   * Assignment: (faction, unit class) -> (training, role). The ONLY place faction appears.
// The player's own reload never reads any of this.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PA250AIReload.h"
#include "PA250CombatProfiles.generated.h"

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250TrainingProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	FName Name = TEXT("Trained");

	// ---- AI reload (gameplay settings). Baseline = the weapon preset's ideal drill path. ----
	/** Chance a reload step goes cleanly (no fumble) when calm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload", meta = (ClampMin = "0", ClampMax = "1"))
	float ReloadSkill = 0.85f;

	/** Multiplier on the ideal step time when calm (1 = drill-perfect, never below 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload", meta = (ClampMin = "1"))
	float ReloadPace = 1.12f;

	/** Random extra per step, as a fraction of the ideal (consistency; 0 = metronome). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload", meta = (ClampMin = "0"))
	float ReloadJitter = 0.12f;

	/** Extra fraction of step time at full stress (also raises the fumble chance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload", meta = (ClampMin = "0"))
	float StressReloadPenalty = 0.6f;

	/** Extra fraction of step time while walking or running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reload", meta = (ClampMin = "0"))
	float MovingReloadPenalty = 0.4f;

	// ---- Marksmanship ----
	/** Bot aiming error cone, degrees (on top of the weapon's own spread). Same for volleys and single shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksmanship", meta = (ClampMin = "0"))
	float AimErrorDegrees = 2.5f;

	// ---- Cohesion ----
	/** Scales FormationArriveCm: <1 = men stand closer to their exact place in the ranks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cohesion", meta = (ClampMin = "0.2"))
	float FormationToleranceScale = 1.f;

	/** Scales FormationMaxLagCm: <1 = the line waits for stragglers sooner (keeps ranks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cohesion", meta = (ClampMin = "0.2"))
	float StragglerToleranceScale = 1.f;

	/** Seconds to re-form ranks after a rally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cohesion", meta = (ClampMin = "0"))
	float ReformSeconds = 3.5f;

	// ---- Morale ----
	/** Divides incoming pressure: higher = harder to shake or rout (never immune). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morale", meta = (ClampMin = "0.1"))
	float MoraleResolve = 1.15f;

	/** Seconds a routed unit must stay below the calm threshold before it starts to rally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morale", meta = (ClampMin = "0"))
	float RallySeconds = 8.f;

	FPA250AIReloadParams ToReloadParams() const
	{
		FPA250AIReloadParams P;
		P.Skill = ReloadSkill;
		P.Pace = FMath::Max(1.f, ReloadPace);
		P.Jitter = FMath::Max(0.f, ReloadJitter);
		P.StressPenalty = FMath::Max(0.f, StressReloadPenalty);
		P.MovingPenalty = FMath::Max(0.f, MovingReloadPenalty);
		return P;
	}
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250TacticalRole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	FName Name = TEXT("Line");

	/** Men look for a covered firing position (fence, wall) near their place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	bool bPrefersCover = false;

	/** Open order: one rank, wider files. Off = close-order ranks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	bool bOpenOrder = false;

	/** Multiplier on FileSpacingCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role", meta = (ClampMin = "0.5"))
	float FileSpacingScale = 1.f;

	/** True = fire at will (skirmish). False = coordinated squad volley mode (editable; not faction-locked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	bool bIndividualFire = false;

	/** Pull back deliberately when the enemy closes while the squad is out of cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	bool bWithdrawWhenExposed = false;

	/** "Exposed" = enemy closer than this with most men out of cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	float ExposedRangeCm = 3500.f;

	/** How far a deliberate withdrawal falls back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	float RepositionCm = 2500.f;

	/** Minimum time between withdrawals (repositioning is not free). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	float RepositionCooldownSeconds = 10.f;

	/** Only charge a wavering enemy, or one caught reloading while outnumbered (skirmishers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	bool bCautiousCharge = false;

	/** How far from his place a man will look for cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Role")
	float CoverSearchCm = 600.f;
};

USTRUCT(BlueprintType)
struct PLAYAMERICA250_API FPA250CombatAssignment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assignment")
	FName Faction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assignment")
	FName UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assignment")
	FName Training;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assignment")
	FName Role;
};

UCLASS(BlueprintType)
class PLAYAMERICA250_API UPA250CombatProfiles : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Defaults live here: DA_CombatProfiles is created from the class defaults. */
	UPA250CombatProfiles();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FPA250TrainingProfile> Trainings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FPA250TacticalRole> Roles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TArray<FPA250CombatAssignment> Assignments;

	/** Fallbacks when nothing matches: Trained / Line. */
	const FPA250TrainingProfile& TrainingFor(FName Faction, FName UnitClass) const;
	const FPA250TacticalRole& RoleFor(FName Faction, FName UnitClass) const;
	const FPA250TrainingProfile& TrainingNamed(FName Name) const;
	const FPA250TacticalRole& RoleNamed(FName Name) const;

	static UPA250CombatProfiles* MakeRuntimeDefaults();

private:
	const FPA250CombatAssignment* FindAssignment(FName Faction, FName UnitClass) const;
};
