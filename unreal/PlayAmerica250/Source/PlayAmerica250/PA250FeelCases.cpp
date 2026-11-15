#include "PA250FeelCases.h"

#include "Battle/PA250BattleFeel.h"
#include "RF/PA250SoldierAnimInstance.h"
#include "Battle/PA250MeleeComponent.h"
#include "Battle/PA250Cover.h"
#include "RF/MusketWeapon.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString VectorDir()
{
	// Project dir is <repo>/unreal/PlayAmerica250
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../protocol/test_vectors/anim")));
}

/** Every vector whose file name starts with Prefix, in name order. */
TArray<TSharedPtr<FJsonObject>> Vectors(const TCHAR* Prefix)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(VectorDir(), FString(Prefix) + TEXT("*.json")), true, false);
	Files.Sort();
	TArray<TSharedPtr<FJsonObject>> Out;
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(VectorDir(), File)) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
		{
			Out.Add(Root);
		}
	}
	return Out;
}

EPA250FootGait GaitFromName(const FString& Name)
{
	if (Name == TEXT("idle")) return EPA250FootGait::Idle;
	if (Name == TEXT("walk")) return EPA250FootGait::Walk;
	if (Name == TEXT("run")) return EPA250FootGait::Run;
	return EPA250FootGait::Sprint;
}
}

void PA250FeelCases::RunCover(FCheck Check)
{
	// A wall 400 cm long and 80 cm deep along X at the origin: places every 100 cm, 60 cm back, on both faces.
	TArray<PA250Cover::FWallSlot> Slots;
	PA250Cover::AddWallSlots(FVector::ZeroVector, FVector::XAxisVector, 400.f, 80.f, 100.f, 60.f, Slots);
	bool bPlaces = Slots.Num() == 8;
	for (const PA250Cover::FWallSlot& S : Slots)
	{
		bPlaces = bPlaces && FMath::IsNearlyEqual(FMath::Abs(S.Pos.Y), 100.f, 0.01f) && FMath::Abs(S.Pos.X) <= 150.01f &&
			FMath::IsNearlyEqual(S.Out.Y, FMath::Sign(S.Pos.Y)) && FMath::IsNearlyZero(S.Out.X);
	}
	Check(bPlaces, TEXT("cover: a wall gives places along both faces, a stand-off back from it"));
	// The enemy is to the north (+Y): a man is given a place on the south face, the nearest to where he stands.
	const FVector Threat(0.f, 5000.f, 0.f);
	TSet<int32> Taken;
	const int32 A = PA250Cover::PickWallSlot(Slots, FVector(140.f, -300.f, 0.f), Threat, 1200.f, Taken);
	Check(A != INDEX_NONE && Slots[A].Pos.Y < 0.f && FMath::IsNearlyEqual(Slots[A].Pos.X, 150.f, 0.01f),
		TEXT("cover: a man takes the nearest place with the wall between him and the enemy"));
	Taken.Add(A);
	const int32 B = PA250Cover::PickWallSlot(Slots, FVector(140.f, -300.f, 0.f), Threat, 1200.f, Taken);
	Check(B != INDEX_NONE && B != A && Slots[B].Pos.Y < 0.f && FMath::IsNearlyEqual(Slots[B].Pos.X, 50.f, 0.01f),
		TEXT("cover: the next man takes the next place, not the same one"));
	Check(PA250Cover::PickWallSlot(Slots, FVector(140.f, -3000.f, 0.f), Threat, 1200.f, TSet<int32>()) == INDEX_NONE,
		TEXT("cover: a wall too far from his place in the squad is not his"));
	const int32 South = PA250Cover::PickWallSlot(Slots, FVector(0.f, 0.f, 0.f), FVector(0.f, -5000.f, 0.f), 1200.f, TSet<int32>());
	Check(South != INDEX_NONE && Slots[South].Pos.Y > 0.f, TEXT("cover: with the enemy on the other side the places are on the other face"));
	// A wall that runs toward the enemy is not cover: standing beside its end he is in the open.
	const FVector EndOn(6000.f, -400.f, 0.f);
	Check(PA250Cover::PickWallSlot(Slots, FVector(150.f, -100.f, 0.f), EndOn, 1200.f, TSet<int32>(), 50.f) == INDEX_NONE,
		TEXT("cover: a wall end-on to the enemy gives no place"));
	Check(PA250Cover::PickWallSlot(Slots, FVector(150.f, -100.f, 0.f), FVector(1500.f, 3000.f, 0.f), 1200.f, TSet<int32>(), 50.f) != INDEX_NONE,
		TEXT("cover: with the enemy within the angle of the wall's square it does"));
	{
		using M = UPA250MeleeComponent;
		float W = 0.f;
		for (int32 i = 0; i < 5; ++i) W = M::StepKneel(W, true, 0.05f, 0.25f);
		const bool bDown = FMath::IsNearlyEqual(W, 1.f, 1.e-3f);
		for (int32 i = 0; i < 5; ++i) W = M::StepKneel(W, false, 0.05f, 0.25f);
		Check(bDown && FMath::IsNearlyZero(W, 1.e-3f) && M::StepKneel(0.f, true, 0.01f, 0.f) == 1.f, TEXT("cover: a ducked man goes down to the kneel and comes up again over the file's time"));
		const FPA250BattleFeel& F = PA250BattleFeel::Get();
		Check(F.KneelDropCm >= 30.f && F.KneelBlendSeconds > 0.f, TEXT("cover: the kneel lowers him enough to be hidden behind a 1.1 m wall (the file's drop)"));
	}
	// The loading drill's left hand while ramming: up the barrel to where a standing man can hold it.
	{
		const FPA250BattleFeel& F = PA250BattleFeel::Get();
		const float Slide = AMusketWeapon::RamHandSlideCm(45.f, 0.98f, F.RamLeftHandHeightCm, F.RamLeftHandMaxSlideCm);
		Check(Slide > 0.f && Slide <= F.RamLeftHandMaxSlideCm && FMath::IsNearlyEqual(45.f + Slide * 0.98f, F.RamLeftHandHeightCm, 0.5f),
			TEXT("reload: ramming, the left hand holds the barrel at the file's height, not at the knee"));
		Check(AMusketWeapon::RamHandSlideCm(45.f, 0.2f, F.RamLeftHandHeightCm, F.RamLeftHandMaxSlideCm) == 0.f &&
			AMusketWeapon::RamHandSlideCm(130.f, 0.98f, F.RamLeftHandHeightCm, F.RamLeftHandMaxSlideCm) == 0.f,
			TEXT("reload: a musket carried level, or a spot already high enough, is left alone"));
		Check(AMusketWeapon::RamHandSlideCm(-200.f, 0.98f, F.RamLeftHandHeightCm, F.RamLeftHandMaxSlideCm) == F.RamLeftHandMaxSlideCm,
			TEXT("reload: the hand never goes past the file's reach along the barrel"));
	}
	TArray<PA250Cover::FWallSlot> None;
	PA250Cover::AddWallSlots(FVector::ZeroVector, FVector::XAxisVector, 400.f, 80.f, 0.f, 60.f, None);
	Check(None.Num() == 0, TEXT("cover: with no spacing in the data there are no wall places (the old search only)"));
	Check(PA250Cover::FireOverDown(true, true, false, false) && !PA250Cover::FireOverDown(true, false, true, false) &&
		!PA250Cover::FireOverDown(false, true, false, false), TEXT("cover: behind a wall a man is down while he loads and up to fire"));
}

void PA250FeelCases::RunAnimation(FCheck Check)
{
	// The file itself, for each platform it names and for one it does not.
	{
		TArray<FString> Errors;
		const bool bWin = PA250BattleFeel::LoadFromFile(PA250BattleFeel::DataPath(), TEXT("win64"), &Errors);
		Check(bWin && Errors.IsEmpty(), TEXT("anim: battle_feel.json loads for win64 with every key"));
		const FPA250BattleFeel Win = PA250BattleFeel::Get();
		Check(Win.bLoaded && Win.Budget.FullQualityMax >= 1 && Win.PlayRateMin <= 1.f && Win.PlayRateMax >= 1.f,
			TEXT("anim: the win64 budget and play rates are read"));
		Check(Win.IdleMaxCmS < Win.WalkMaxCmS && Win.WalkMaxCmS < Win.RunMaxCmS, TEXT("anim: the gait bands rise"));

		const bool bMac = PA250BattleFeel::LoadFromFile(PA250BattleFeel::DataPath(), TEXT("mac"), &Errors);
		const FPA250BattleFeel Mac = PA250BattleFeel::Get();
		Check(bMac && Errors.IsEmpty(), TEXT("anim: battle_feel.json loads for mac"));
		Check(Mac.Budget.bMeasured || (Mac.Budget.FullQualityMax == 1 && Mac.Budget.SwitchDistanceM == 0.f),
			TEXT("anim: a platform not yet measured holds the lowest setting (the player alone)"));

		const bool bOther = PA250BattleFeel::LoadFromFile(PA250BattleFeel::DataPath(), TEXT("linux"), &Errors);
		const FPA250BattleFeel Other = PA250BattleFeel::Get();
		Check(!bOther && Errors.Num() == 1 && Other.bLoaded && Other.Budget.FullQualityMax == 1 && Other.Budget.SwitchDistanceM == 0.f,
			TEXT("anim: a platform with no entry gets the lowest setting and an error"));

		const int32 Before = PA250BattleFeel::FallbackUses();
		const bool bMissing = PA250BattleFeel::LoadFromFile(PA250BattleFeel::DataPath() + TEXT(".absent"), TEXT("win64"), &Errors);
		const FPA250BattleFeel Missing = PA250BattleFeel::Get();
		Check(!bMissing && !Missing.bLoaded && !Missing.bMotionMatching && Missing.Budget.FullQualityMax == 1 &&
			PA250BattleFeel::FallbackUses() == Before + 1,
			TEXT("anim: a missing file leaves the neutral state (cheap path) and counts a fallback"));

		// Leave this platform's values loaded for whatever runs next.
		PA250BattleFeel::LoadFromFile(PA250BattleFeel::DataPath(), PA250BattleFeel::PlatformKey(), &Errors);
	}

	// Who is at full quality (shared vectors).
	{
		const TArray<TSharedPtr<FJsonObject>> Tier = Vectors(TEXT("tier_pick_"));
		Check(Tier.Num() >= 6, TEXT("anim: the tier vectors are found"));
		bool bAll = true;
		for (const TSharedPtr<FJsonObject>& V : Tier)
		{
			FPA250AnimBudget Budget;
			const TSharedPtr<FJsonObject> B = V->GetObjectField(TEXT("budget"));
			Budget.FullQualityMax = static_cast<int32>(B->GetNumberField(TEXT("full_quality_max")));
			Budget.SwitchDistanceM = static_cast<float>(B->GetNumberField(TEXT("switch_distance_m")));
			TArray<PA250BattleFeel::FTierInput> Soldiers;
			for (const TSharedPtr<FJsonValue>& S : V->GetArrayField(TEXT("soldiers")))
			{
				const TSharedPtr<FJsonObject> J = S->AsObject();
				PA250BattleFeel::FTierInput In;
				In.Key = J->GetStringField(TEXT("id"));
				In.DistanceM = static_cast<float>(J->GetNumberField(TEXT("distance_m")));
				In.bPlayer = J->GetBoolField(TEXT("player"));
				In.bWasFull = J->GetBoolField(TEXT("was_full"));
				Soldiers.Add(In);
			}
			const TArray<int32> Full = PA250BattleFeel::PickFullQuality(V->GetBoolField(TEXT("motion_matching")), Budget,
				static_cast<float>(V->GetNumberField(TEXT("hysteresis_m"))), Soldiers);
			TArray<FString> Got;
			for (int32 i : Full) Got.Add(Soldiers[i].Key);
			TArray<FString> Want;
			for (const TSharedPtr<FJsonValue>& W : V->GetArrayField(TEXT("expect_full"))) Want.Add(W->AsString());
			if (Got != Want)
			{
				bAll = false;
				UE_LOG(LogTemp, Display, TEXT("PA250.Combat tier vector %s: got [%s], want [%s]"), *V->GetStringField(TEXT("name")),
					*FString::Join(Got, TEXT(",")), *FString::Join(Want, TEXT(",")));
			}
		}
		Check(bAll, TEXT("anim: tier choice follows the distance and count in data"));
	}

	// The cheap path cross-fades (it used to cut to frame 0 of the new clip).
	{
		using P = FPA250SoldierAnimProxy;
		bool bRises = true;
		float Last = -1.f;
		for (int32 i = 0; i <= 20; ++i)
		{
			const float A = P::CrossfadeAlpha(0.2f * i / 20.f, 0.2f);
			bRises = bRises && A >= Last && A >= 0.f && A <= 1.f;
			Last = A;
		}
		const bool bEnds = FMath::IsNearlyZero(P::CrossfadeAlpha(0.f, 0.2f)) && FMath::IsNearlyEqual(P::CrossfadeAlpha(0.2f, 0.2f), 1.f) &&
			FMath::IsNearlyEqual(P::CrossfadeAlpha(5.f, 0.2f), 1.f) && FMath::IsNearlyEqual(P::CrossfadeAlpha(0.1f, 0.2f), 0.5f);
		const bool bNoFade = FMath::IsNearlyEqual(P::CrossfadeAlpha(0.f, 0.f), 1.f);
		const FTransform Old(FQuat(FVector::UpVector, 0.0), FVector(0.0, 0.0, 0.0));
		const FTransform New(FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI), FVector(10.0, 0.0, 4.0));
		const FTransform Half = P::BlendBone(Old, New, 0.5f);
		const bool bHalf = Half.GetTranslation().Equals(FVector(5.0, 0.0, 2.0), 1.e-3) && Half.GetRotation().IsNormalized() &&
			FMath::IsNearlyEqual(Half.GetRotation().GetAngle(), UE_DOUBLE_HALF_PI * 0.5, 0.02);
		// The same rotation written with the opposite sign must not send the bone the long way round.
		const FQuat Q = New.GetRotation();
		const FTransform Flipped(FQuat(-Q.X, -Q.Y, -Q.Z, -Q.W), New.GetTranslation());
		const bool bShort = P::BlendBone(Old, Flipped, 0.5f).GetRotation().AngularDistance(Half.GetRotation()) < 0.02;
		const bool bEdges = P::BlendBone(Old, New, 0.f).Equals(Old, 1.e-4) && P::BlendBone(Old, New, 1.f).Equals(New, 1.e-4);
		Check(bRises && bEnds && bNoFade && bHalf && bShort && bEdges, TEXT("anim: the cheap path cross-fades"));
		Check(PA250BattleFeel::Get().CheapCrossfadeSeconds > 0.f, TEXT("anim: the cross-fade time is the file's and is not zero"));
	}

	// Coming up to full quality: the cheap path shows until the motion-matched player has a clip.
	{
		using P = FPA250SoldierAnimProxy;
		float A = 0.f;
		bool bRises = true;
		for (int32 i = 0; i < 10; ++i)
		{
			const float Next = P::StepTierAlpha(A, true, 0.05f, 0.25f);
			bRises = bRises && Next >= A;
			A = Next;
		}
		Check(bRises && FMath::IsNearlyEqual(A, 1.f) && FMath::IsNearlyEqual(P::StepTierAlpha(0.f, true, 0.05f, 0.25f), 0.2f),
			TEXT("anim: full quality blends in over the file's time"));
		Check(P::StepTierAlpha(0.7f, false, 0.05f, 0.25f) == 0.f && P::StepTierAlpha(0.f, false, 0.05f, 0.25f) == 0.f,
			TEXT("anim: the index not ready falls back to the cheap path"));
		Check(P::StepTierAlpha(0.f, true, 0.05f, 0.f) == 1.f, TEXT("anim: with no blend time full quality shows at once"));
	}

	// The three carries: which one, the blend between them, and the carry never outweighing a hold.
	{
		using M = UPA250MeleeComponent;
		const bool bChoice = M::ChooseCarry(true, true, false, true) == EPA250Carry::Charge && M::ChooseCarry(true, false, true, false) == EPA250Carry::Charge &&
			M::ChooseCarry(false, true, false, true) == EPA250Carry::Shoulder && M::ChooseCarry(false, true, true, true) == EPA250Carry::Trail &&
			M::ChooseCarry(false, true, false, false) == EPA250Carry::Trail && M::ChooseCarry(false, false, false, true) == EPA250Carry::Trail;
		Check(bChoice, TEXT("anim: charging is charge bayonet, a bot walking with no enemy is at the shoulder, otherwise the trail"));
		const FPA250BattleFeel& F = PA250BattleFeel::Get();
		const FPA250CarryPose Half = M::BlendCarry(F.Trail, F.Shoulder, 0.5f);
		const bool bBlend = FMath::IsNearlyEqual(Half.PitchDeg, 0.5f * (F.Trail.PitchDeg + F.Shoulder.PitchDeg)) &&
			FMath::IsNearlyEqual(M::BlendCarry(F.Trail, F.Charge, 0.f).HandRightCm, F.Trail.HandRightCm) &&
			FMath::IsNearlyEqual(M::BlendCarry(F.Trail, F.Charge, 1.f).HandRightCm, F.Charge.HandRightCm) &&
			FMath::IsNearlyEqual(M::BlendCarry(F.Trail, F.Charge, 7.f).YawDeg, F.Charge.YawDeg);
		Check(bBlend, TEXT("anim: one carry blends into another and stops at it"));
		Check(F.Shoulder.PitchDeg > F.Trail.PitchDeg && F.Trail.PitchDeg > F.Charge.PitchDeg && F.CarryBlendSeconds > 0.f,
			TEXT("anim: the file's carries differ (shouldered steepest, charge bayonet levelled)"));
		// A trailed musket is raised when a friend stands where its muzzle would be.
		const FVector Fwd = FVector::XAxisVector;
		Check(M::MuzzleInFriend(FVector::ZeroVector, Fwd, FVector(80.f, 20.f, 0.f), 190.f, 60.f) &&
			!M::MuzzleInFriend(FVector::ZeroVector, Fwd, FVector(80.f, 120.f, 0.f), 190.f, 60.f) &&
			!M::MuzzleInFriend(FVector::ZeroVector, Fwd, FVector(-80.f, 0.f, 0.f), 190.f, 60.f) &&
			!M::MuzzleInFriend(FVector::ZeroVector, Fwd, FVector(400.f, 0.f, 0.f), 190.f, 60.f),
			TEXT("anim: a friend in front of the trailed muzzle counts; one beside, behind or far ahead does not"));
		Check(F.ClearPitchDeg > F.Trail.PitchDeg + 20.f && F.ClearDistanceCm > 100.f, TEXT("anim: the raised carry lifts the muzzle well above the trail (the file's angle)"));
		bool bWeights = true;
		for (int32 i = 0; i < 64; ++i)
		{
			FPA250MeleePose P;
			P.Carry = 1.f;
			P.Coil = (i & 1) ? 0.7f : 0.f;
			P.Level = (i & 2) ? 0.6f : 0.f;
			P.Shoulder = (i & 4) ? 1.f : 0.f;
			P.Reload = (i & 8) ? 0.5f : 0.f;
			P.Guard = (i & 16) ? 0.9f : 0.f;
			P.Chop = (i & 32) ? 1.f : 0.f;
			const float W = P.CarryWeight();
			const float Others = FMath::Clamp(P.Coil + P.Level + P.Shoulder + P.Reload + P.Guard + P.Chop, 0.f, 1.f);
			bWeights = bWeights && W >= 0.f && W <= 1.f && W + Others <= 1.f + KINDA_SMALL_NUMBER;
		}
		Check(bWeights, TEXT("anim: carry weights never exceed one"));
	}

	// Which gait a speed asks for (shared vectors, then the file's own bands).
	{
		const TArray<TSharedPtr<FJsonObject>> Gait = Vectors(TEXT("gait_pick_"));
		Check(Gait.Num() >= 2, TEXT("anim: the gait vectors are found"));
		bool bAll = true;
		for (const TSharedPtr<FJsonObject>& V : Gait)
		{
			const TSharedPtr<FJsonObject> Bands = V->GetObjectField(TEXT("bands"));
			for (const TSharedPtr<FJsonValue>& C : V->GetArrayField(TEXT("cases")))
			{
				const TSharedPtr<FJsonObject> J = C->AsObject();
				const EPA250FootGait Got = PA250BattleFeel::PickGait(static_cast<float>(J->GetNumberField(TEXT("speed_cm_s"))),
					static_cast<float>(Bands->GetNumberField(TEXT("idle_max"))), static_cast<float>(Bands->GetNumberField(TEXT("walk_max"))),
					static_cast<float>(Bands->GetNumberField(TEXT("run_max"))));
				if (Got != GaitFromName(J->GetStringField(TEXT("gait"))))
				{
					bAll = false;
					UE_LOG(LogTemp, Display, TEXT("PA250.Combat gait vector %s: %.1f cm/s gave %s, want %s"), *V->GetStringField(TEXT("name")),
						J->GetNumberField(TEXT("speed_cm_s")), PA250BattleFeel::GaitName(Got), *J->GetStringField(TEXT("gait")));
				}
			}
		}
		Check(bAll, TEXT("anim: gait picks the database set"));
		const FPA250BattleFeel& F = PA250BattleFeel::Get();
		Check(PA250BattleFeel::PickGait(0.f, F.IdleMaxCmS, F.WalkMaxCmS, F.RunMaxCmS) == EPA250FootGait::Idle &&
			PA250BattleFeel::PickGait(F.RunMaxCmS + 1.f, F.IdleMaxCmS, F.WalkMaxCmS, F.RunMaxCmS) == EPA250FootGait::Sprint,
			TEXT("anim: standing still is idle and above the run band is a sprint, by the file's bands"));
	}
}
