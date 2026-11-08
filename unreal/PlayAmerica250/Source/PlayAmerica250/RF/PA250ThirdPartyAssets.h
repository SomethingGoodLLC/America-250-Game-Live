#pragma once

// Visual-only lookups preserve the original Fab package paths.
// Nothing here changes gameplay: a missing asset returns nullptr and callers
// keep the template placeholder (mannequin, no smoke).

#include "CoreMinimal.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace PA250ThirdParty
{
// Object paths (Package.Asset). Asset names follow docs in PROGRESS.md ("Visual pass").
inline const TCHAR* const SoldierBritish[] = {
	TEXT("/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER.SK_AMERICAN_SOLDIER"),
	TEXT("/Game/ThirdParty/Characters/SKM_British_Regular.SKM_British_Regular"),
	TEXT("/Game/ThirdParty/Characters/SKM_Soldier_1775.SKM_Soldier_1775"),
};
inline const TCHAR* const SoldierContinental[] = {
	TEXT("/Game/american_soldier/base_mesh/SK_AMERICAN_SOLDIER.SK_AMERICAN_SOLDIER"),
	TEXT("/Game/ThirdParty/Characters/SKM_Continental_Militia.SKM_Continental_Militia"),
	TEXT("/Game/ThirdParty/Characters/SKM_Soldier_1775.SKM_Soldier_1775"),
};
inline const TCHAR* const Musket[] = {
	TEXT("/Game/HS_WeaponPack_01/Meshes/MusketRifle/Meshes/SM_HS_WP01_MusketRifle_X_0.SM_HS_WP01_MusketRifle_X_0"),
};
// Skeletal variant: the pack's Firing/Reloading animations are authored on the Y_0 skeleton.
inline const TCHAR* const MusketAnimated[] = {
	TEXT("/Game/HS_WeaponPack_01/Meshes/MusketRifle/Meshes/SK_HS_WP01_MusketRifle_Y_0.SK_HS_WP01_MusketRifle_Y_0"),
};
inline const TCHAR* const MusketFireAnim[] = {
	TEXT("/Game/HS_WeaponPack_01/Meshes/MusketRifle/Animations/AS_HS_WP01_MusketRifle_Firing.AS_HS_WP01_MusketRifle_Firing"),
};
inline const TCHAR* const MusketReloadAnim[] = {
	TEXT("/Game/HS_WeaponPack_01/Meshes/MusketRifle/Animations/AS_HS_WP01_MusketRifle_Reloading.AS_HS_WP01_MusketRifle_Reloading"),
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
