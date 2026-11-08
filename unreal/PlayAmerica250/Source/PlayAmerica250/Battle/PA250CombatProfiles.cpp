#include "PA250CombatProfiles.h"

namespace
{
FPA250TrainingProfile MakeTraining(FName Name, float Skill, float Pace, float Jitter, float Stress, float Moving,
	float Tolerance, float Straggler, float Reform, float Resolve, float Rally)
{
	FPA250TrainingProfile T;
	T.Name = Name;
	T.ReloadSkill = Skill;
	T.ReloadPace = Pace;
	T.ReloadJitter = Jitter;
	T.StressReloadPenalty = Stress;
	T.MovingReloadPenalty = Moving;
	T.FormationToleranceScale = Tolerance;
	T.StragglerToleranceScale = Straggler;
	T.ReformSeconds = Reform;
	T.MoraleResolve = Resolve;
	T.RallySeconds = Rally;
	return T;
}

FPA250TacticalRole MakeRole(FName Name, bool bCover, bool bOpen, float FileScale, bool bIndividual, bool bWithdraw,
	float ExposedCm, float RepositionCm, float Cooldown)
{
	FPA250TacticalRole R;
	R.Name = Name;
	R.bPrefersCover = bCover;
	R.bOpenOrder = bOpen;
	R.FileSpacingScale = FileScale;
	R.bIndividualFire = bIndividual;
	R.bWithdrawWhenExposed = bWithdraw;
	R.ExposedRangeCm = ExposedCm;
	R.RepositionCm = RepositionCm;
	R.RepositionCooldownSeconds = Cooldown;
	return R;
}

FPA250CombatAssignment Assign(FName Faction, FName UnitClass, FName Training, FName Role)
{
	FPA250CombatAssignment A;
	A.Faction = Faction;
	A.UnitClass = UnitClass;
	A.Training = Training;
	A.Role = Role;
	return A;
}
}

UPA250CombatProfiles::UPA250CombatProfiles()
{
	// GAMEPLAY SETTINGS — provisional balance values, not historical rates of fire.
	//                         skill  pace  jitter stress moving | tol  strag reform | resolve rally
	Trainings.Add(MakeTraining(TEXT("Veteran"), 0.97f, 1.00f, 0.06f, 0.45f, 0.30f, 0.75f, 0.6f, 2.0f, 1.6f, 6.f));
	Trainings.Add(MakeTraining(TEXT("Trained"), 0.88f, 1.10f, 0.12f, 0.60f, 0.40f, 1.00f, 1.0f, 3.5f, 1.2f, 9.f));
	Trainings.Add(MakeTraining(TEXT("Recruit"), 0.70f, 1.25f, 0.25f, 0.85f, 0.55f, 1.40f, 1.6f, 6.0f, 0.8f, 14.f));
	// Marksmanship (bot aim cone): matches the old class-based 2 deg line / 3 deg militia at the ends.
	Trainings[0].AimErrorDegrees = 2.0f;
	Trainings[1].AimErrorDegrees = 2.5f;
	Trainings[2].AimErrorDegrees = 3.0f;

	//                     cover  open  files  indiv  withdraw  exposed  reposition  cooldown
	Roles.Add(MakeRole(TEXT("Line"), false, false, 1.0f, false, false, 0.f, 2000.f, 15.f));
	Roles.Add(MakeRole(TEXT("Light"), true, true, 2.0f, true, true, 3500.f, 2500.f, 9.f));
	Roles.Add(MakeRole(TEXT("Irregular"), true, true, 1.6f, true, true, 4000.f, 3000.f, 12.f));
	Roles[1].bCautiousCharge = true; // Light
	Roles[2].bCautiousCharge = true; // Irregular

	// Lexington 1775 starting point (editable). Set continental/line_infantry to Veteran for an exact match.
	Assignments.Add(Assign(TEXT("british"), TEXT("line_infantry"), TEXT("Veteran"), TEXT("Line")));
	Assignments.Add(Assign(TEXT("continental"), TEXT("line_infantry"), TEXT("Trained"), TEXT("Line")));
	// Minute companies drilled regularly and included French & Indian War veterans: Trained, not raw.
	Assignments.Add(Assign(TEXT("continental"), TEXT("militia"), TEXT("Trained"), TEXT("Irregular")));
}

UPA250CombatProfiles* UPA250CombatProfiles::MakeRuntimeDefaults()
{
	return NewObject<UPA250CombatProfiles>();
}

const FPA250CombatAssignment* UPA250CombatProfiles::FindAssignment(FName Faction, FName UnitClass) const
{
	for (const FPA250CombatAssignment& A : Assignments)
	{
		if (A.Faction == Faction && A.UnitClass == UnitClass) return &A;
	}
	for (const FPA250CombatAssignment& A : Assignments)
	{
		if (A.Faction.IsNone() && A.UnitClass == UnitClass) return &A; // class-only rule
	}
	return nullptr;
}

const FPA250TrainingProfile& UPA250CombatProfiles::TrainingNamed(FName Name) const
{
	static const FPA250TrainingProfile Fallback;
	for (const FPA250TrainingProfile& T : Trainings)
	{
		if (T.Name == Name) return T;
	}
	for (const FPA250TrainingProfile& T : Trainings)
	{
		if (T.Name == TEXT("Trained")) return T;
	}
	return Fallback;
}

const FPA250TacticalRole& UPA250CombatProfiles::RoleNamed(FName Name) const
{
	static const FPA250TacticalRole Fallback;
	for (const FPA250TacticalRole& R : Roles)
	{
		if (R.Name == Name) return R;
	}
	for (const FPA250TacticalRole& R : Roles)
	{
		if (R.Name == TEXT("Line")) return R;
	}
	return Fallback;
}

const FPA250TrainingProfile& UPA250CombatProfiles::TrainingFor(FName Faction, FName UnitClass) const
{
	const FPA250CombatAssignment* A = FindAssignment(Faction, UnitClass);
	return TrainingNamed(A ? A->Training : FName(TEXT("Trained")));
}

const FPA250TacticalRole& UPA250CombatProfiles::RoleFor(FName Faction, FName UnitClass) const
{
	const FPA250CombatAssignment* A = FindAssignment(Faction, UnitClass);
	return RoleNamed(A ? A->Role : FName(TEXT("Line")));
}
