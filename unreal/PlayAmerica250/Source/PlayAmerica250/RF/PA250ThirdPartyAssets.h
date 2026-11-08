#pragma once

// Visual-only lookups for optional Fab assets dropped into Content/ThirdParty.
// Nothing here changes gameplay: a missing asset returns nullptr and callers
// keep the template placeholder (mannequin, no smoke).

#include "CoreMinimal.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace PA250ThirdParty
{
// Object paths (Package.Asset). Asset names follow docs in PROGRESS.md ("Visual pass").
inline const TCHAR* const SoldierBritish[] = {
	TEXT("/Game/ThirdParty/Characters/SKM_British_Regular.SKM_British_Regular"),
	TEXT("/Game/ThirdParty/Characters/SKM_Soldier_1775.SKM_Soldier_1775"),
};
inline const TCHAR* const SoldierContinental[] = {
	TEXT("/Game/ThirdParty/Characters/SKM_Continental_Militia.SKM_Continental_Militia"),
	TEXT("/Game/ThirdParty/Characters/SKM_Soldier_1775.SKM_Soldier_1775"),
};
inline const TCHAR* const MuzzleSmoke[] = {
	TEXT("/Game/ThirdParty/FX/NS_MuzzleSmoke.NS_MuzzleSmoke"),
	TEXT("/Game/ThirdParty/FX/NS_MusketSmoke.NS_MusketSmoke"),
};

// First candidate whose package exists on disk. Callers cache the result.
template <typename T, size_t N>
T* LoadFirstOf(const TCHAR* const (&Candidates)[N])
{
	for (const TCHAR* Path : Candidates)
	{
		if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(Path))))
		{
			continue;
		}
		if (T* Obj = Cast<T>(StaticLoadObject(T::StaticClass(), nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet)))
		{
			return Obj;
		}
	}
	return nullptr;
}
} // namespace PA250ThirdParty
