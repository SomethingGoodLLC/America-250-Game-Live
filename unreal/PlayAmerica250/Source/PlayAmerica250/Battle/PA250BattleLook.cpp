#include "PA250BattleLook.h"

#include "Battle/PA250BattleFeel.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FLoaded
{
	bool bTried = false;
	FPA250BattleLookData Data;
	FString Applied;
};

FLoaded& Loaded()
{
	static FLoaded L;
	return L;
}

const TCHAR* const BandKeys[3] = {TEXT("dawn"), TEXT("day"), TEXT("dusk")};

bool ReadTriple(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, FVector& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!J->TryGetArrayField(Key, A) || A->Num() != 3) return false;
	Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	return true;
}

bool ReadLook(const TSharedPtr<FJsonObject>& J, FPA250LookSettings& Out)
{
	FVector Rot, Color, Gain;
	double Lux = 0, Sky = 0, Fog = 0, Falloff = 0, Ev = 0, Temp = 0, Bloom = 0, Vignette = 0;
	bool bVolumetric = false;
	if (!J.IsValid() || !ReadTriple(J, TEXT("sun_rot"), Rot) || !ReadTriple(J, TEXT("sun_color"), Color) || !ReadTriple(J, TEXT("gain"), Gain) ||
		!J->TryGetNumberField(TEXT("sun_lux"), Lux) || !J->TryGetNumberField(TEXT("sky_intensity"), Sky) || !J->TryGetNumberField(TEXT("fog_density"), Fog) ||
		!J->TryGetNumberField(TEXT("fog_height_falloff"), Falloff) || !J->TryGetBoolField(TEXT("volumetric_fog"), bVolumetric) ||
		!J->TryGetNumberField(TEXT("exposure_ev"), Ev) || !J->TryGetNumberField(TEXT("white_temp"), Temp) || !J->TryGetNumberField(TEXT("bloom"), Bloom) ||
		!J->TryGetNumberField(TEXT("vignette"), Vignette))
	{
		return false;
	}
	Out.SunRot = FRotator(Rot.X, Rot.Y, Rot.Z);
	Out.SunLux = static_cast<float>(Lux);
	Out.SunColor = FColor(static_cast<uint8>(FMath::Clamp(Color.X, 0.0, 255.0)), static_cast<uint8>(FMath::Clamp(Color.Y, 0.0, 255.0)),
		static_cast<uint8>(FMath::Clamp(Color.Z, 0.0, 255.0)), 255);
	Out.SkyIntensity = static_cast<float>(Sky);
	Out.FogDensity = static_cast<float>(Fog);
	Out.FogHeightFalloff = static_cast<float>(Falloff);
	Out.bVolumetricFog = bVolumetric;
	Out.ExposureEv = static_cast<float>(Ev);
	Out.WhiteTemp = static_cast<float>(Temp);
	Out.Gain = Gain;
	Out.Bloom = static_cast<float>(Bloom);
	Out.Vignette = static_cast<float>(Vignette);
	return true;
}

FString LevelKey(const UWorld* World)
{
	const FString Map = World ? World->GetMapName() : FString();
	if (Map.Contains(TEXT("L_Concord"))) return TEXT("L_Concord");
	if (Map.Contains(TEXT("L_Harbor"))) return TEXT("L_Harbor");
	if (Map.Contains(TEXT("L_Lexington"))) return TEXT("L_Lexington");
	return FString();
}
}

FString PA250BattleLook::DataPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/battle_look.json")));
}

bool PA250BattleLook::LoadFromFile(const FString& Path, TArray<FString>* OutErrors)
{
	FLoaded& L = Loaded();
	L.bTried = true;
	L.Data = FPA250BattleLookData();
	TArray<FString> Errors;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		Errors.Add(FString::Printf(TEXT("cannot read %s"), *Path));
	}
	else if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Errors.Add(FString::Printf(TEXT("%s is not valid JSON"), *Path));
	}
	else
	{
		FPA250BattleLookData D;
		double Bar = 0;
		if (!Root->TryGetNumberField(TEXT("frame_rate_bar"), Bar)) Errors.Add(TEXT("no number \"frame_rate_bar\""));
		D.FrameRateBar = static_cast<float>(Bar);
		const TSharedPtr<FJsonObject>* Bands = nullptr;
		if (!Root->TryGetObjectField(TEXT("time_bands"), Bands)) Errors.Add(TEXT("no \"time_bands\" object"));
		for (int32 b = 0; b < 3 && Bands; ++b)
		{
			const TArray<TSharedPtr<FJsonValue>>* Ranges = nullptr;
			if (!(*Bands)->TryGetArrayField(BandKeys[b], Ranges) || Ranges->IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("no hours for band \"%s\""), BandKeys[b]));
				continue;
			}
			for (const TSharedPtr<FJsonValue>& V : *Ranges)
			{
				const TArray<TSharedPtr<FJsonValue>>& Pair = V->AsArray();
				if (Pair.Num() == 2) D.Bands[b].Add(FIntPoint(static_cast<int32>(Pair[0]->AsNumber()), static_cast<int32>(Pair[1]->AsNumber())));
			}
		}
		FString Default;
		Root->TryGetStringField(TEXT("default_band"), Default);
		D.DefaultBand = INDEX_NONE;
		for (int32 b = 0; b < 3; ++b)
		{
			if (Default == BandKeys[b]) D.DefaultBand = b;
		}
		if (D.DefaultBand == INDEX_NONE)
		{
			Errors.Add(TEXT("no \"default_band\" of dawn, day or dusk"));
			D.DefaultBand = 1;
		}
		const TSharedPtr<FJsonObject>* Levels = nullptr;
		if (!Root->TryGetObjectField(TEXT("levels"), Levels)) Errors.Add(TEXT("no \"levels\" object"));
		else
		{
			for (const auto& Pair : (*Levels)->Values)
			{
				const TSharedPtr<FJsonObject>* Level = nullptr;
				TArray<FPA250LookSettings> Looks;
				Looks.SetNum(3);
				bool bWhole = Pair.Value->TryGetObject(Level);
				for (int32 b = 0; b < 3 && bWhole; ++b)
				{
					const TSharedPtr<FJsonObject>* Look = nullptr;
					bWhole = (*Level)->TryGetObjectField(BandKeys[b], Look) && ReadLook(*Look, Looks[b]);
				}
				if (bWhole) D.Levels.Add(FString(Pair.Key), MoveTemp(Looks));
				else Errors.Add(FString::Printf(TEXT("level \"%s\" lacks a whole look for dawn, day and dusk"), *Pair.Key));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Tiers = nullptr;
		if (!Root->TryGetArrayField(TEXT("tiers"), Tiers) || Tiers->IsEmpty()) Errors.Add(TEXT("no \"tiers\" list"));
		else
		{
			for (const TSharedPtr<FJsonValue>& V : *Tiers)
			{
				const TSharedPtr<FJsonObject> J = V->AsObject();
				const TSharedPtr<FJsonObject>* Cvars = nullptr;
				FPA250LookTier Tier;
				if (!J.IsValid() || !J->TryGetStringField(TEXT("name"), Tier.Name) || !J->TryGetObjectField(TEXT("cvars"), Cvars))
				{
					Errors.Add(TEXT("a tier lacks its name or its cvars"));
					continue;
				}
				for (const auto& C : (*Cvars)->Values) Tier.Cvars.Add(FString(C.Key), static_cast<float>(C.Value->AsNumber()));
				D.Tiers.Add(MoveTemp(Tier));
			}
		}
		const TSharedPtr<FJsonObject>* Chosen = nullptr;
		if (!Root->TryGetObjectField(TEXT("chosen"), Chosen)) Errors.Add(TEXT("no \"chosen\" object"));
		else
		{
			for (const auto& Pair : (*Chosen)->Values)
			{
				const TSharedPtr<FJsonObject>* J = nullptr;
				FString Tier;
				if (Pair.Value->TryGetObject(J) && (*J)->TryGetStringField(TEXT("tier"), Tier)) D.Chosen.Add(FName(*Pair.Key), Tier);
			}
		}
		const TSharedPtr<FJsonObject>* Lod = nullptr;
		double Shadow = 0;
		if (!Root->TryGetObjectField(TEXT("soldier_lod"), Lod) || !(*Lod)->TryGetNumberField(TEXT("shadow_cast_m"), Shadow) ||
			!(*Lod)->TryGetBoolField(TEXT("update_rate_optimisation"), D.bUpdateRateOptimisation))
		{
			Errors.Add(TEXT("no \"soldier_lod\" with shadow_cast_m and update_rate_optimisation"));
		}
		D.ShadowCastM = static_cast<float>(Shadow);
		if (Errors.IsEmpty())
		{
			D.bLoaded = true;
			L.Data = MoveTemp(D);
		}
	}
	for (const FString& E : Errors) UE_LOG(LogTemp, Error, TEXT("PA250 battle look: %s"), *E);
	if (!Errors.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 battle look: FALLBACK, every level keeps the light it was built with and the project's own rendering settings. Fix %s."), *Path);
	}
	if (OutErrors) *OutErrors = Errors;
	return Errors.IsEmpty();
}

const FPA250BattleLookData& PA250BattleLook::Get()
{
	FLoaded& L = Loaded();
	if (!L.bTried) LoadFromFile(DataPath());
	return L.Data;
}

int32 PA250BattleLook::BandForHour(int32 Hour, const TArray<FIntPoint> Bands[3], int32 DefaultBand)
{
	for (int32 b = 0; b < 3; ++b)
	{
		for (const FIntPoint& R : Bands[b])
		{
			if (Hour >= R.X && Hour <= R.Y) return b;
		}
	}
	return FMath::Clamp(DefaultBand, 0, 2);
}

const TCHAR* PA250BattleLook::BandName(int32 Band)
{
	return BandKeys[FMath::Clamp(Band, 0, 2)];
}

FString PA250BattleLook::TierFor(FName Platform, const FPA250BattleLookData& Data, bool& bOutFallback)
{
	bOutFallback = false;
	if (Data.Tiers.IsEmpty()) return FString();
	if (const FString* Chosen = Data.Chosen.Find(Platform))
	{
		if (Data.Tiers.ContainsByPredicate([Chosen](const FPA250LookTier& T) { return T.Name == *Chosen; })) return *Chosen;
	}
	bOutFallback = true;
	return Data.Tiers[0].Name;
}

FString PA250BattleLook::Applied()
{
	return Loaded().Applied;
}

bool PA250BattleLook::Apply(UWorld* World, int32 HourOfDay, bool bRelight)
{
	const FPA250BattleLookData& Data = Get();
	Loaded().Applied.Reset();
	if (!World || !Data.bLoaded) return false;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250Hour="), HourOfDay)) bRelight = true;
	const int32 Band = BandForHour(HourOfDay, Data.Bands, Data.DefaultBand);
	const FString Level = LevelKey(World);
	const TArray<FPA250LookSettings>* Looks = Data.Levels.Find(Level);
	if (!bRelight)
	{
		// Left as the level was built.
	}
	else if (Looks)
	{
		const FPA250LookSettings& S = (*Looks)[Band];
		if (TActorIterator<ADirectionalLight> It(World); It) // the first one only
		{
			It->SetActorRotation(S.SunRot);
			if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			{
				Sun->SetIntensity(S.SunLux);
				Sun->SetLightColor(S.SunColor);
			}
		}
		if (TActorIterator<ASkyLight> It(World); It) // the first one only
		{
			if (USkyLightComponent* Sky = It->GetLightComponent()) Sky->SetIntensity(S.SkyIntensity);
		}
		if (TActorIterator<AExponentialHeightFog> It(World); It) // the first one only
		{
			if (UExponentialHeightFogComponent* Fog = It->GetComponent())
			{
				Fog->SetFogDensity(S.FogDensity);
				Fog->SetFogHeightFalloff(S.FogHeightFalloff);
				Fog->SetVolumetricFog(S.bVolumetricFog);
			}
		}
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (!It->bUnbound) continue;
			FPostProcessSettings& P = It->Settings;
			P.bOverride_AutoExposureBias = true;
			P.AutoExposureBias = S.ExposureEv;
			P.bOverride_WhiteTemp = true;
			P.WhiteTemp = S.WhiteTemp;
			P.bOverride_ColorGain = true;
			P.ColorGain = FVector4(S.Gain.X, S.Gain.Y, S.Gain.Z, 1.0);
			P.bOverride_BloomIntensity = true;
			P.BloomIntensity = S.Bloom;
			P.bOverride_VignetteIntensity = true;
			P.VignetteIntensity = S.Vignette;
			break;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 battle look: no look for level \"%s\": it keeps the light it was built with"), *World->GetMapName());
	}
	// The rendering tier: this platform's, or the one asked for on the command line.
	bool bFallback = false;
	const FName Platform = PA250BattleFeel::PlatformKey();
	FString TierName = TierFor(Platform, Data, bFallback);
	if (bFallback)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 battle look: no tier chosen for platform \"%s\" (chosen): the lowest, %s"), *Platform.ToString(), *TierName);
	}
	FString Asked;
	if (FParse::Value(FCommandLine::Get(), TEXT("PA250Look="), Asked))
	{
		if (Data.Tiers.ContainsByPredicate([&Asked](const FPA250LookTier& T) { return T.Name == Asked; })) TierName = Asked;
		else UE_LOG(LogTemp, Error, TEXT("PA250 battle look: -PA250Look=%s is not a tier in the file"), *Asked);
	}
	int32 Set = 0;
	if (const FPA250LookTier* Tier = Data.Tiers.FindByPredicate([&TierName](const FPA250LookTier& T) { return T.Name == TierName; }))
	{
		for (const auto& C : Tier->Cvars)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*C.Key))
			{
				Var->Set(C.Value, ECVF_SetByConsole);
				++Set;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("PA250 battle look: tier %s names an unknown console variable %s"), *TierName, *C.Key);
			}
		}
	}
	Loaded().Applied = FString::Printf(TEXT("%s/%s/%s"), Level.IsEmpty() ? TEXT("?") : *Level, bRelight ? BandName(Band) : TEXT("as_built"), *TierName);
	UE_LOG(LogTemp, Display, TEXT("PA250 look: %s, band %s (hour %d), tier %s for %s (%d variables set)%s"), Level.IsEmpty() ? *World->GetMapName() : *Level, BandName(Band),
		HourOfDay, *TierName, *Platform.ToString(), Set, (Looks && bRelight) ? TEXT("") : TEXT(", lights left as built"));
	return true;
}
