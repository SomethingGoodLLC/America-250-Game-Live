#include "PA250BattleFeel.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformProperties.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FLoadedFeel
{
	FPA250BattleFeel Feel;
	bool bTried = false;
	int32 FallbackUses = 0;
};

FLoadedFeel& Loaded()
{
	static FLoadedFeel L;
	return L;
}

/** Reads required fields and remembers the ones that are missing. */
struct FReader
{
	TArray<FString>& Errors;

	const TSharedPtr<FJsonObject>* Object(const TSharedPtr<FJsonObject>& In, const TCHAR* Key, const FString& Where) const
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		if (!In.IsValid() || !In->TryGetObjectField(Key, Out) || !Out || !Out->IsValid())
		{
			Errors.Add(FString::Printf(TEXT("no \"%s%s\" object"), *Where, Key));
			return nullptr;
		}
		return Out;
	}
	void Number(const TSharedPtr<FJsonObject>& In, const TCHAR* Key, const FString& Where, float& Out) const
	{
		double Value = 0.0;
		if (!In.IsValid() || !In->TryGetNumberField(Key, Value))
		{
			Errors.Add(FString::Printf(TEXT("no number \"%s%s\""), *Where, Key));
			return;
		}
		Out = static_cast<float>(Value);
	}
	void Bool(const TSharedPtr<FJsonObject>& In, const TCHAR* Key, const FString& Where, bool& Out) const
	{
		if (!In.IsValid() || !In->TryGetBoolField(Key, Out))
		{
			Errors.Add(FString::Printf(TEXT("no true/false \"%s%s\""), *Where, Key));
		}
	}
	void Carry(const TSharedPtr<FJsonObject>& In, const TCHAR* Key, FPA250CarryPose& Out) const
	{
		const FString Where = FString::Printf(TEXT("carry.%s."), Key);
		if (const TSharedPtr<FJsonObject>* J = Object(In, Key, TEXT("carry.")))
		{
			Number(*J, TEXT("pitch_deg"), Where, Out.PitchDeg);
			Number(*J, TEXT("yaw_deg"), Where, Out.YawDeg);
			Number(*J, TEXT("hand_forward_cm"), Where, Out.HandForwardCm);
			Number(*J, TEXT("hand_right_cm"), Where, Out.HandRightCm);
			Number(*J, TEXT("hand_up_cm"), Where, Out.HandUpCm);
		}
	}
};
}

FString PA250BattleFeel::DataPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/battle_feel.json")));
}

FName PA250BattleFeel::PlatformKey()
{
	const FString Name = FString(FPlatformProperties::IniPlatformName()).ToLower();
	if (Name == TEXT("windows")) return TEXT("win64");
	return FName(*Name);
}

const TCHAR* PA250BattleFeel::GaitName(EPA250FootGait Gait)
{
	switch (Gait)
	{
	case EPA250FootGait::Idle: return TEXT("idle");
	case EPA250FootGait::Walk: return TEXT("walk");
	case EPA250FootGait::Run: return TEXT("run");
	default: return TEXT("sprint");
	}
}

bool PA250BattleFeel::LoadFromFile(const FString& Path, FName Platform, TArray<FString>* OutErrors)
{
	FLoadedFeel& L = Loaded();
	L.bTried = true;
	TArray<FString> Errors;
	FPA250BattleFeel Feel;
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
	if (Root.IsValid())
	{
		const FReader R{Errors};
		if (const TSharedPtr<FJsonObject>* Anim = R.Object(Root, TEXT("animation"), TEXT("")))
		{
			const FString W = TEXT("animation.");
			R.Bool(*Anim, TEXT("motion_matching"), W, Feel.bMotionMatching);
			R.Number(*Anim, TEXT("switch_hysteresis_m"), W, Feel.SwitchHysteresisM);
			R.Number(*Anim, TEXT("tier_blend_seconds"), W, Feel.TierBlendSeconds);
			R.Number(*Anim, TEXT("cheap_crossfade_seconds"), W, Feel.CheapCrossfadeSeconds);
			R.Number(*Anim, TEXT("mm_blend_seconds"), W, Feel.MMBlendSeconds);
			R.Number(*Anim, TEXT("bot_search_throttle_seconds"), W, Feel.BotSearchThrottleSeconds);
			R.Number(*Anim, TEXT("play_rate_min"), W, Feel.PlayRateMin);
			R.Number(*Anim, TEXT("play_rate_max"), W, Feel.PlayRateMax);
			R.Number(*Anim, TEXT("upper_body_sway"), W, Feel.UpperBodySway);
			if (const TSharedPtr<FJsonObject>* Bands = R.Object(*Anim, TEXT("gait_speed_cm_s"), W))
			{
				const FString BW = TEXT("animation.gait_speed_cm_s.");
				R.Number(*Bands, TEXT("idle_max"), BW, Feel.IdleMaxCmS);
				R.Number(*Bands, TEXT("walk_max"), BW, Feel.WalkMaxCmS);
				R.Number(*Bands, TEXT("run_max"), BW, Feel.RunMaxCmS);
			}
			if (const TSharedPtr<FJsonObject>* Platforms = R.Object(*Anim, TEXT("platforms"), W))
			{
				const FString PW = FString::Printf(TEXT("animation.platforms.%s."), *Platform.ToString());
				const TSharedPtr<FJsonObject>* Mine = nullptr;
				if ((*Platforms)->TryGetObjectField(Platform.ToString(), Mine) && Mine && Mine->IsValid())
				{
					float Max = 1.f;
					R.Number(*Mine, TEXT("full_quality_max"), PW, Max);
					Feel.Budget.FullQualityMax = FMath::Max(1, FMath::RoundToInt(Max));
					R.Number(*Mine, TEXT("switch_distance_m"), PW, Feel.Budget.SwitchDistanceM);
					FString Measured;
					Feel.Budget.bMeasured = (*Mine)->TryGetStringField(TEXT("measured"), Measured) && !Measured.IsEmpty();
				}
				else
				{
					// A platform nobody has listed gets the lowest setting, and says so.
					Errors.Add(FString::Printf(TEXT("no budget for platform \"%s\" (animation.platforms): the player alone is at full quality"),
						*Platform.ToString()));
				}
			}
		}
		if (const TSharedPtr<FJsonObject>* Carry = R.Object(Root, TEXT("carry"), TEXT("")))
		{
			R.Number(*Carry, TEXT("blend_seconds"), TEXT("carry."), Feel.CarryBlendSeconds);
			R.Bool(*Carry, TEXT("shoulder_at_walk"), TEXT("carry."), Feel.bShoulderAtWalk);
			if (const TSharedPtr<FJsonObject>* Clear = R.Object(*Carry, TEXT("clear"), TEXT("carry.")))
			{
				R.Number(*Clear, TEXT("distance_cm"), TEXT("carry.clear."), Feel.ClearDistanceCm);
				R.Number(*Clear, TEXT("half_width_cm"), TEXT("carry.clear."), Feel.ClearHalfWidthCm);
				R.Number(*Clear, TEXT("pitch_deg"), TEXT("carry.clear."), Feel.ClearPitchDeg);
				R.Number(*Clear, TEXT("yaw_deg"), TEXT("carry.clear."), Feel.ClearYawDeg);
				R.Number(*Clear, TEXT("check_seconds"), TEXT("carry.clear."), Feel.ClearCheckSeconds);
			}
			R.Carry(*Carry, TEXT("trail"), Feel.Trail);
			R.Carry(*Carry, TEXT("shoulder"), Feel.Shoulder);
			R.Carry(*Carry, TEXT("charge"), Feel.Charge);
		}
		if (const TSharedPtr<FJsonObject>* Kneel = R.Object(Root, TEXT("kneel"), TEXT("")))
		{
			R.Number(*Kneel, TEXT("drop_cm"), TEXT("kneel."), Feel.KneelDropCm);
			R.Number(*Kneel, TEXT("rear_foot_back_cm"), TEXT("kneel."), Feel.KneelRearFootBackCm);
			R.Number(*Kneel, TEXT("lean_deg"), TEXT("kneel."), Feel.KneelLeanDeg);
			R.Number(*Kneel, TEXT("blend_seconds"), TEXT("kneel."), Feel.KneelBlendSeconds);
		}
		if (const TSharedPtr<FJsonObject>* Reload = R.Object(Root, TEXT("reload"), TEXT("")))
		{
			R.Number(*Reload, TEXT("ram_left_hand_height_cm"), TEXT("reload."), Feel.RamLeftHandHeightCm);
			R.Number(*Reload, TEXT("ram_left_hand_max_slide_cm"), TEXT("reload."), Feel.RamLeftHandMaxSlideCm);
		}
		if (const TSharedPtr<FJsonObject>* Verify = R.Object(Root, TEXT("verify"), TEXT("")))
		{
			R.Number(*Verify, TEXT("frame_time_margin_pct"), TEXT("verify."), Feel.FrameTimeMarginPct);
		}
	}
	if (OutErrors) *OutErrors = Errors;
	if (!Errors.IsEmpty())
	{
		// A platform without a budget is the one error that still leaves a usable file.
		const bool bOnlyPlatform = Errors.Num() == 1 && Errors[0].Contains(TEXT("no budget for platform"));
		for (const FString& E : Errors)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 battle feel: %s"), *E);
		}
		if (!bOnlyPlatform)
		{
			L.Feel = FPA250BattleFeel();
			return false;
		}
	}
	Feel.bLoaded = true;
	L.Feel = Feel;
	UE_LOG(LogTemp, Display, TEXT("PA250 battle feel: loaded for %s (motion matching %s, full quality %d within %.0f m, %s)"),
		*Platform.ToString(), Feel.bMotionMatching ? TEXT("on") : TEXT("off"), Feel.Budget.FullQualityMax, Feel.Budget.SwitchDistanceM,
		Feel.Budget.bMeasured ? TEXT("measured") : TEXT("NOT measured on this platform"));
	return Errors.IsEmpty();
}

const FPA250BattleFeel& PA250BattleFeel::Get()
{
	FLoadedFeel& L = Loaded();
	if (!L.bTried)
	{
		LoadFromFile(DataPath(), PlatformKey());
	}
	if (!L.Feel.bLoaded)
	{
		if (L.FallbackUses++ == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("PA250 battle feel: FALLBACK to the neutral state (cheap animation path, no blends). Fix %s."), *DataPath());
		}
	}
	return L.Feel;
}

int32 PA250BattleFeel::FallbackUses()
{
	return Loaded().FallbackUses;
}

TArray<int32> PA250BattleFeel::PickFullQuality(bool bMotionMatching, const FPA250AnimBudget& Budget, float HysteresisM,
	const TArray<FTierInput>& Soldiers)
{
	TArray<int32> Full;
	if (!bMotionMatching) return Full;
	TArray<int32> Others;
	for (int32 i = 0; i < Soldiers.Num(); ++i)
	{
		if (Soldiers[i].bPlayer) Full.Add(i);
		else Others.Add(i);
	}
	Others.Sort([&Soldiers](int32 A, int32 B)
	{
		if (Soldiers[A].DistanceM != Soldiers[B].DistanceM) return Soldiers[A].DistanceM < Soldiers[B].DistanceM;
		return Soldiers[A].Key < Soldiers[B].Key;
	});
	for (int32 i : Others)
	{
		if (Full.Num() >= Budget.FullQualityMax) break;
		const float Reach = Budget.SwitchDistanceM + (Soldiers[i].bWasFull ? HysteresisM : 0.f);
		if (Soldiers[i].DistanceM <= Reach) Full.Add(i);
	}
	return Full;
}

EPA250FootGait PA250BattleFeel::PickGait(float SpeedCmS, float IdleMaxCmS, float WalkMaxCmS, float RunMaxCmS)
{
	if (SpeedCmS <= IdleMaxCmS) return EPA250FootGait::Idle;
	if (SpeedCmS <= WalkMaxCmS) return EPA250FootGait::Walk;
	if (SpeedCmS <= RunMaxCmS) return EPA250FootGait::Run;
	return EPA250FootGait::Sprint;
}
