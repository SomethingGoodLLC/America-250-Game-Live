#pragma once

#include "CoreMinimal.h"

/**
 * Battle feel (unreal/PA250Sim/data/battle_feel.json, contract protocol/schemas/battle_feel.yaml):
 * how the battle moves, answers the hand and looks alive. Display and handling only; nothing here
 * reaches the campaign. Every number is the data file's. The values below are not tuning: they are
 * the neutral state a missing or broken file leaves (everyone on the cheap path, nothing blended),
 * and any use of them logs an Error ("PA250 battle feel: FALLBACK").
 */
struct FPA250AnimBudget
{
	/** Soldiers at full quality at once, the player among them. 1 = the player only. */
	int32 FullQualityMax = 1;
	float SwitchDistanceM = 0.f;
	/** False: this platform has not been measured and holds the lowest setting. */
	bool bMeasured = false;
};

struct FPA250CarryPose
{
	float PitchDeg = 0.f;
	float YawDeg = 0.f;
	float HandForwardCm = 0.f;
	float HandRightCm = 0.f;
	float HandUpCm = 0.f;
};

struct FPA250BattleFeel
{
	bool bLoaded = false;

	// animation
	bool bMotionMatching = false;
	/** This platform's budget (animation.platforms.<win64|mac>). */
	FPA250AnimBudget Budget;
	float SwitchHysteresisM = 0.f;
	float TierBlendSeconds = 0.f;
	float CheapCrossfadeSeconds = 0.f;
	float MMBlendSeconds = 0.f;
	float BotSearchThrottleSeconds = 0.f;
	float PlayRateMin = 1.f;
	float PlayRateMax = 1.f;
	float UpperBodySway = 0.f;
	float IdleMaxCmS = 0.f;
	float WalkMaxCmS = 0.f;
	float RunMaxCmS = 0.f;

	// carry
	float CarryBlendSeconds = 0.f;
	/** carry.shoulder_at_walk: a walking bot with no enemy in view shoulders his piece. */
	bool bShoulderAtWalk = false;
	/** carry.clear: a trailed musket that would be in a friend is raised. */
	float ClearDistanceCm = 0.f;
	float ClearHalfWidthCm = 0.f;
	float ClearPitchDeg = 0.f;
	float ClearYawDeg = 0.f;
	float ClearCheckSeconds = 0.f;
	FPA250CarryPose Trail;
	FPA250CarryPose Shoulder;
	FPA250CarryPose Charge;

	// kneel (a ducked man)
	float KneelDropCm = 0.f;
	float KneelRearFootBackCm = 0.f;
	float KneelLeanDeg = 0.f;
	float KneelBlendSeconds = 0.f;

	// reload (the left hand while ramming)
	float RamLeftHandHeightCm = 0.f;
	float RamLeftHandMaxSlideCm = 0.f;

	// verify
	float FrameTimeMarginPct = 0.f;
};

/** The clip set a speed asks for. */
enum class EPA250FootGait : uint8
{
	Idle,
	Walk,
	Run,
	Sprint
};

/** How the musket is carried on the move (battle_feel.json carry). */
enum class EPA250Carry : uint8
{
	Trail,
	Shoulder,
	Charge
};

namespace PA250BattleFeel
{
PLAYAMERICA250_API FString DataPath();
/** "win64", "mac", or the platform's own name in lower case (which has no entry: lowest setting). */
PLAYAMERICA250_API FName PlatformKey();
/** Reads the file for one platform. False, with the reasons, when it cannot be read or lacks a
 *  key; the neutral state is then in force. A platform with no entry is an error. */
PLAYAMERICA250_API bool LoadFromFile(const FString& Path, FName Platform, TArray<FString>* OutErrors = nullptr);
/** The loaded values; loads DataPath() for PlatformKey() on first use. */
PLAYAMERICA250_API const FPA250BattleFeel& Get();
/** How often the neutral state was handed out because the file was missing or broken. */
PLAYAMERICA250_API int32 FallbackUses();
PLAYAMERICA250_API const TCHAR* GaitName(EPA250FootGait Gait);

/** One soldier for the tier rule. */
struct FTierInput
{
	/** Settles equal distances, so the choice is the same from frame to frame. */
	FString Key;
	float DistanceM = 0.f;
	bool bPlayer = false;
	/** He was at full quality at the last pick (he keeps it out to distance + hysteresis). */
	bool bWasFull = false;
};

/**
 * Pure. Indices into Soldiers of those at full quality: the player, then the nearest inside the
 * switch distance, until FullQualityMax are (the player counts as one). Empty when motion matching
 * is off. The player comes first; the rest in order of distance, then key.
 */
PLAYAMERICA250_API TArray<int32> PickFullQuality(bool bMotionMatching, const FPA250AnimBudget& Budget, float HysteresisM,
	const TArray<FTierInput>& Soldiers);

/** Pure. A band's upper edge belongs to it. */
PLAYAMERICA250_API EPA250FootGait PickGait(float SpeedCmS, float IdleMaxCmS, float WalkMaxCmS, float RunMaxCmS);
}
