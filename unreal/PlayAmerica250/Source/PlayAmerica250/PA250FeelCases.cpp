#include "PA250FeelCases.h"
#include "Battle/PA250Traversal.h"
#include "Battle/PA250Hero.h"
#include "Battle/PA250Intensity.h"
#include "Battle/PA250BattleLook.h"
#include "RF/PA250InputBuffer.h"
#include "RF/RFCharacter.h"
#include "Battle/PA250TraversalLinks.h"
#include "Battle/PA250AnimClips.h"

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

void PA250FeelCases::RunClips(FCheck Check)
{
	auto PhaseOf = [](const FString& Name)
	{
		if (Name == TEXT("PrimePan")) return EActiveReloadPhase::PrimePan;
		if (Name == TEXT("RamCharge")) return EActiveReloadPhase::RamCharge;
		if (Name == TEXT("ShoulderAim")) return EActiveReloadPhase::ShoulderAim;
		if (Name == TEXT("MissDelay")) return EActiveReloadPhase::MissDelay;
		if (Name == TEXT("Jammed")) return EActiveReloadPhase::Jammed;
		if (Name == TEXT("Complete")) return EActiveReloadPhase::Complete;
		return EActiveReloadPhase::Idle;
	};
	const TArray<TSharedPtr<FJsonObject>> Docs = Vectors(TEXT("drill_clip_names"));
	Check(Docs.Num() == 1, TEXT("reload: the drill-name vector is found"));
	TArray<FString> Errors;
	const bool bRead = PA250AnimClips::LoadRegistry(PA250AnimClips::DataPath(), &Errors);
	Check(bRead && PA250AnimClips::Registry().Num() >= 19, TEXT("anim: the clip registry is read"));
	bool bNames = true, bEntries = true;
	int32 DrillNames = 0;
	for (const TSharedPtr<FJsonObject>& V : Docs)
	{
		for (const TSharedPtr<FJsonValue>& C : V->GetArrayField(TEXT("cases")))
		{
			const TSharedPtr<FJsonObject> J = C->AsObject();
			const bool bKneel = J->GetBoolField(TEXT("kneel"));
			const FString Want = J->GetStringField(TEXT("name"));
			const FName Got = PA250AnimClips::DrillClipName(PhaseOf(J->GetStringField(TEXT("phase"))), bKneel);
			if (Got != (Want.IsEmpty() ? NAME_None : FName(*Want)))
			{
				bNames = false;
				UE_LOG(LogTemp, Display, TEXT("PA250.Combat drill name: %s gave %s, want %s"), *J->GetStringField(TEXT("phase")), *Got.ToString(), *Want);
			}
			if (Want.IsEmpty()) continue;
			++DrillNames;
			const FPA250ClipEntry* Entry = PA250AnimClips::FindClip(Got);
			bEntries &= Entry && Entry->Group == FName(TEXT("loading_drill")) && Entry->bWired && Entry->Stance == FName(bKneel ? TEXT("kneel") : TEXT("stand")) &&
				Want.EndsWith(Entry->Stage.ToString()) && !Entry->Shows.IsEmpty();
		}
	}
	Check(bNames && DrillNames == 10, TEXT("reload: each stage maps to a clip name, standing and kneeling"));
	Check(bEntries, TEXT("reload: every drill name is in the registry with its stage, stance and placeholder"));
	// Every name either gives a clip or falls to its placeholder (and says so once); a name the registry lacks gives nothing.
	int32 Resolved = 0;
	for (const FPA250ClipEntry& E : PA250AnimClips::Registry())
	{
		Resolved += PA250AnimClips::Clip(E.Name) ? 1 : 0;
		PA250AnimClips::Clip(E.Name); // asked again: still one line
	}
	Check(Resolved + PA250AnimClips::PlaceholdersLogged() == PA250AnimClips::Registry().Num(), TEXT("anim: every registry name resolves or warns"));
	Check(!PA250AnimClips::Clip(TEXT("no_such_clip")) && !PA250AnimClips::Clip(TEXT("no_such_clip")) && PA250AnimClips::UnknownLogged() == 1,
		TEXT("anim: a name the registry does not hold gives no clip and is reported once"));
	Check(!PA250AnimClips::LoadRegistry(PA250AnimClips::DataPath() + TEXT(".absent"), &Errors) && PA250AnimClips::Registry().Num() == 0 &&
		!PA250AnimClips::Clip(TEXT("reload_stand_prime_pan")),
		TEXT("anim: without the registry file every name falls to its placeholder"));
	PA250AnimClips::LoadRegistry(PA250AnimClips::DataPath());
}

void PA250FeelCases::RunLook(FCheck Check)
{
	TArray<FString> Errors;
	Check(PA250BattleLook::LoadFromFile(PA250BattleLook::DataPath(), &Errors), TEXT("look: the file loads"));
	const FPA250BattleLookData& D = PA250BattleLook::Get();
	const TArray<TSharedPtr<FJsonObject>> Docs = Vectors(TEXT("time_band_"));
	Check(Docs.Num() >= 2, TEXT("look: the time-band vectors are found"));
	bool bAll = true;
	for (const TSharedPtr<FJsonObject>& V : Docs)
	{
		TArray<FIntPoint> Bands[3];
		const TSharedPtr<FJsonObject> B = V->GetObjectField(TEXT("time_bands"));
		for (int32 b = 0; b < 3; ++b)
		{
			for (const TSharedPtr<FJsonValue>& R : B->GetArrayField(PA250BattleLook::BandName(b)))
			{
				Bands[b].Add(FIntPoint(static_cast<int32>(R->AsArray()[0]->AsNumber()), static_cast<int32>(R->AsArray()[1]->AsNumber())));
			}
		}
		int32 Default = 1;
		for (int32 b = 0; b < 3; ++b)
		{
			if (V->GetStringField(TEXT("default_band")) == PA250BattleLook::BandName(b)) Default = b;
		}
		for (const TSharedPtr<FJsonValue>& C : V->GetArrayField(TEXT("cases")))
		{
			const TSharedPtr<FJsonObject> J = C->AsObject();
			const int32 Hour = static_cast<int32>(J->GetNumberField(TEXT("hour")));
			if (J->GetStringField(TEXT("band")) != PA250BattleLook::BandName(PA250BattleLook::BandForHour(Hour, Bands, Default)))
			{
				bAll = false;
				UE_LOG(LogTemp, Display, TEXT("PA250.Combat time band vector %s: hour %d"), *V->GetStringField(TEXT("name")), Hour);
			}
		}
	}
	Check(bAll, TEXT("look: the band follows the hour by the file's bands"));
	int32 Covered = 0;
	bool bOnce = true;
	for (int32 Hour = 0; Hour < 24; ++Hour)
	{
		int32 In = 0;
		for (int32 b = 0; b < 3; ++b)
		{
			for (const FIntPoint& R : D.Bands[b]) In += (Hour >= R.X && Hour <= R.Y) ? 1 : 0;
		}
		Covered += In > 0 ? 1 : 0;
		bOnce &= In == 1;
	}
	Check(Covered == 24 && bOnce, TEXT("look: every hour falls in one band"));
	bool bLevels = D.Levels.Num() == 3;
	for (const TCHAR* Level : {TEXT("L_Lexington"), TEXT("L_Concord"), TEXT("L_Harbor")})
	{
		const TArray<FPA250LookSettings>* Looks = D.Levels.Find(Level);
		bLevels &= Looks && Looks->Num() == 3;
		for (int32 b = 0; bLevels && b < 3; ++b) bLevels &= (*Looks)[b].SunLux > 0.f && (*Looks)[b].SunRot.Pitch < 0.f;
	}
	Check(bLevels, TEXT("look: every battle level has settings for every band"));
	bool bFallback = true;
	const FString Mine = PA250BattleLook::TierFor(PA250BattleFeel::PlatformKey(), D, bFallback);
	Check(!Mine.IsEmpty() && !bFallback && D.Tiers.ContainsByPredicate([&Mine](const FPA250LookTier& T) { return T.Name == Mine; }),
		TEXT("look: the chosen tier exists for this platform"));
	bool bOther = false;
	const FString Unknown = PA250BattleLook::TierFor(TEXT("amiga"), D, bOther);
	bool bMac = true;
	const FString Mac = PA250BattleLook::TierFor(TEXT("mac"), D, bMac);
	Check(bOther && D.Tiers.Num() > 0 && Unknown == D.Tiers[0].Name && !bMac && !Mac.IsEmpty(), TEXT("look: an unmeasured platform gets the lowest tier"));
	// The player's own choice (O in a battle) goes round the file's tiers.
	Check(PA250BattleLook::NextTier({TEXT("a"), TEXT("b"), TEXT("c")}, TEXT("a")) == TEXT("b") && PA250BattleLook::NextTier({TEXT("a"), TEXT("b"), TEXT("c")}, TEXT("c")) == TEXT("a")
		&& PA250BattleLook::NextTier({TEXT("a"), TEXT("b")}, TEXT("zzz")) == TEXT("a") && PA250BattleLook::NextTier({}, TEXT("a")).IsEmpty() && D.Tiers.Num() >= 2
		&& !PA250BattleLook::TierNote(D.Tiers[0].Name).IsEmpty(),
		TEXT("look: the player's key steps through the tiers and round again, and every tier says what it is"));
	Check(PA250BattleFeel::Get().BackstepSpeedCm > 0.f && PA250BattleFeel::Get().BackstepSpeedCm < PA250BattleFeel::Get().WalkSpeedCm,
		TEXT("feel: a man facing his view steps back slower than he walks forward (playtest 2026-11-17: men ran backwards)"));
	{
		const FPA250BattleFeel& F = PA250BattleFeel::Get();
		const int32 Steps = FMath::RoundToInt(F.SpyglassZoomSteps);
		const float W = GetDefault<ARFCharacter>()->GetAimedFOV(), N = F.SpyglassFovNarrowDeg;
		Check(N > 0.f && N < W && Steps >= 1
			&& F.SpyglassRaiseSeconds > 0.f && F.SpyglassClipEyeSeconds > 0.f && F.SpyglassFieldFraction > 0.f && F.SpyglassFieldFraction <= 1.f,
			*FString::Printf(TEXT("feel: the gunner's glass opens at a rifleman's aim (%.0f deg) and zooms in %d steps to %.0f, raised in %.2f s, the file's"),
				W, Steps, N, F.SpyglassRaiseSeconds));
		bool bEven = true;
		for (int32 i = 1; i < Steps; ++i)
		{
			const float R0 = ARFCharacter::SpyglassFovAt(i, Steps, W, N) / ARFCharacter::SpyglassFovAt(i - 1, Steps, W, N);
			const float R1 = ARFCharacter::SpyglassFovAt(i + 1, Steps, W, N) / ARFCharacter::SpyglassFovAt(i, Steps, W, N);
			bEven &= FMath::IsNearlyEqual(R0, R1, 1.e-3f) && R0 < 1.f;
		}
		Check(FMath::IsNearlyEqual(ARFCharacter::SpyglassFovAt(0, Steps, W, N), W) && FMath::IsNearlyEqual(ARFCharacter::SpyglassFovAt(Steps, Steps, W, N), N)
			&& FMath::IsNearlyEqual(ARFCharacter::SpyglassFovAt(-3, Steps, W, N), W) && FMath::IsNearlyEqual(ARFCharacter::SpyglassFovAt(Steps + 5, Steps, W, N), N) && bEven,
			TEXT("feel: the glass zooms wide to narrow in even steps and stops at both ends (playtest 2026-11-18: start out, zoom in)"));
	}
	Check(D.FrameRateBar > 0.f && D.ShadowCastM >= 0.f, TEXT("look: the frame-rate bar and the soldier budget are the file's"));
	Check(!PA250BattleLook::LoadFromFile(PA250BattleLook::DataPath() + TEXT(".absent")) && !PA250BattleLook::Get().bLoaded && !PA250BattleLook::Apply(nullptr, 12),
		TEXT("look: without the file nothing is applied"));
	PA250BattleLook::LoadFromFile(PA250BattleLook::DataPath());
}

void PA250FeelCases::RunIntensity(FCheck Check)
{
	const TArray<TSharedPtr<FJsonObject>> Docs = Vectors(TEXT("music_layers_"));
	Check(Docs.Num() >= 2, TEXT("music: the layer vectors are found"));
	bool bAll = true;
	for (const TSharedPtr<FJsonObject>& V : Docs)
	{
		const float MW = static_cast<float>(V->GetNumberField(TEXT("morale_weight"))), TW = static_cast<float>(V->GetNumberField(TEXT("ticket_weight")));
		const TArray<TSharedPtr<FJsonValue>>& Layers = V->GetArrayField(TEXT("layers"));
		for (const TSharedPtr<FJsonValue>& C : V->GetArrayField(TEXT("cases")))
		{
			const TSharedPtr<FJsonObject> J = C->AsObject();
			const float I = PA250Music::Intensity(static_cast<float>(J->GetNumberField(TEXT("steady_share"))), static_cast<float>(J->GetNumberField(TEXT("reserve_share"))), MW, TW);
			bool bSame = FMath::IsNearlyEqual(I, static_cast<float>(J->GetNumberField(TEXT("intensity"))), 1.e-3f);
			const TArray<TSharedPtr<FJsonValue>>& Gains = J->GetArrayField(TEXT("gains"));
			for (int32 i = 0; bSame && i < Layers.Num(); ++i)
			{
				const TSharedPtr<FJsonObject> L = Layers[i]->AsObject();
				bSame = Gains.IsValidIndex(i) && FMath::IsNearlyEqual(PA250Music::LayerGain(I, static_cast<float>(L->GetNumberField(TEXT("from"))),
					static_cast<float>(L->GetNumberField(TEXT("full")))), static_cast<float>(Gains[i]->AsNumber()), 1.e-3f);
			}
			if (!bSame)
			{
				bAll = false;
				UE_LOG(LogTemp, Display, TEXT("PA250.Combat music vector %s: %s"), *V->GetStringField(TEXT("name")), *J->GetStringField(TEXT("what")));
			}
		}
	}
	Check(bAll, TEXT("music: layers follow morale and tickets"));
	const FPA250BattleFeel& F = PA250BattleFeel::Get();
	Check(F.MusicLayers.Num() >= 1 && F.MusicLayers[0].From == 0.f && F.MusicLayers[0].Full == 0.f && F.MusicFadeSeconds >= 0.f && F.MusicVolume > 0.f &&
		FMath::IsNearlyEqual(F.MusicMoraleWeight + F.MusicTicketWeight, 1.f), TEXT("music: the layers and weights are the file's, and the first layer always plays"));
	Check(FMath::IsNearlyEqual(PA250Intensity::ShakeScaleAt(F.RoundShotShakeInnerM, F.RoundShotShakeInnerM, F.RoundShotShakeOuterM, F.RoundShotShakeScale), F.RoundShotShakeScale) &&
		PA250Intensity::ShakeScaleAt(F.RoundShotShakeOuterM, F.RoundShotShakeInnerM, F.RoundShotShakeOuterM, F.RoundShotShakeScale) == 0.f &&
		FMath::IsNearlyEqual(PA250Intensity::ShakeScaleAt(0.5f * (F.RoundShotShakeInnerM + F.RoundShotShakeOuterM), F.RoundShotShakeInnerM, F.RoundShotShakeOuterM, 1.f), 0.5f),
		TEXT("feel: round shot shakes in full close by, not at all far off, and evenly between"));
	Check(F.NearMissRadiusCm > 0.f && F.NearMissMinShotCm > 0.f && F.NearMissFovPunchDeg > 0.f && F.NearMissFlinch > 0.f && F.NearMissShakeScale > 0.f && !F.NearMissCrackBank.IsEmpty() &&
		F.ShakeSeconds > 0.f && F.ShakeFrequencyHz > 0.f && F.RoundShotDirtSeconds > 0.f, TEXT("feel: the near miss and the round shot are the file's"));
	// A near miss counts inside the radius and from far enough off; not outside it, not from close by.
	PA250Intensity::ResetCounts();
	const FVector Ear = FVector::ZeroVector;
	const bool bIn = PA250Intensity::NearMiss(nullptr, FVector(F.NearMissMinShotCm + 100.f, 0.f, 0.f), FVector(0.f, F.NearMissRadiusCm - 10.f, 0.f), Ear);
	const bool bOut = PA250Intensity::NearMiss(nullptr, FVector(F.NearMissMinShotCm + 100.f, 0.f, 0.f), FVector(0.f, F.NearMissRadiusCm + 10.f, 0.f), Ear);
	const bool bClose = PA250Intensity::NearMiss(nullptr, FVector(F.NearMissMinShotCm - 100.f, 0.f, 0.f), FVector(0.f, 10.f, 0.f), Ear);
	Check(bIn && !bOut && !bClose && PA250Intensity::NearMissCount() == 1, TEXT("feel: near miss inside the radius flinches, outside it does not"));
	bool bShouts = F.Shouts.Num() == 4;
	for (const TCHAR* Event : {TEXT("charge"), TEXT("rout"), TEXT("officer_down"), TEXT("colours_taken")})
	{
		const FPA250Shout* S = F.Shouts.Find(Event);
		bShouts &= S && !S->Key.IsEmpty() && !S->Text.IsEmpty() && S->CooldownSeconds > 0.f;
	}
	Check(bShouts, TEXT("feel: each event has its shout and its cooldown in the file"));
	const float Cool = F.Shouts.Contains(TEXT("rout")) ? F.Shouts[TEXT("rout")].CooldownSeconds : 8.f;
	Check(PA250Intensity::MayShout(100.f, -1000.f, Cool) && !PA250Intensity::MayShout(100.f + Cool * 0.5f, 100.f, Cool) && PA250Intensity::MayShout(100.f + Cool, 100.f, Cool),
		TEXT("feel: each event shouts once inside its cooldown"));
	Check(F.CorpsesMax > 0 && F.CorpseSeconds >= 3.f && F.CorpseFreezeAfterSeconds > 0.f && ARFCharacter::GetMaxCorpses() == F.CorpsesMax &&
		FMath::IsNearlyEqual(ARFCharacter::GetCorpseSeconds(), F.CorpseSeconds), TEXT("feel: corpses never exceed the budget in the file"));
}

void PA250FeelCases::RunHero(FCheck Check)
{
	// The persons file, as the game loads it (this commandlet runs without a campaign).
	FString PersonsText;
	const FString PersonsPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/persons_boston_1775.json")));
	const bool bPersons = FFileHelper::LoadFileToString(PersonsText, *PersonsPath) && pa250::load_persons(std::string(TCHAR_TO_UTF8(*PersonsText))).empty();
	Check(bPersons, TEXT("hero: the persons file loads"));
	const pa250::HeroRules R = pa250::hero_rules();
	Check(R.loaded && R.score_threshold > 0 && R.meter_seconds > 0.f, TEXT("hero: the rules are the persons data's"));
	FPA250HeroState S;
	// In the ranks: kills, a post and a rally add up to the threshold in the data.
	int32 Kills = 0;
	PA250Hero::Note(S, EPA250HeroEvent::Capture, R);
	PA250Hero::Note(S, EPA250HeroEvent::Rally, R);
	while (S.Score < R.score_threshold && Kills < 1000)
	{
		PA250Hero::Note(S, EPA250HeroEvent::Kill, R);
		++Kills;
	}
	Check(S.Score == R.score_capture + R.score_rally + Kills * R.score_kill && S.Score >= R.score_threshold && S.Score - R.score_kill < R.score_threshold,
		TEXT("hero: score from kills, captures and rallies meets the threshold in data"));
	Check(PA250Hero::CanOffer(S, R, true, true) && !PA250Hero::CanOffer(S, R, false, true), TEXT("hero: no general, no hero (and the enemy's general, whose side has no kit, is never offered)"));
	Check(!PA250Hero::CanOffer(S, R, true, false), TEXT("hero: a fallen general is not offered"));
	pa250::HeroRules None = R;
	None.loaded = false;
	FPA250HeroState Idle;
	PA250Hero::Note(Idle, EPA250HeroEvent::Kill, None);
	Check(Idle.Score == 0 && !PA250Hero::CanOffer(S, None, true, true), TEXT("hero: without the rules nothing is scored and nothing offered"));
	PA250Hero::Begin(S, R);
	Check(S.bPlaying && S.Score == 0 && S.Meter == R.meter_seconds && S.Takeovers == 1 && !PA250Hero::CanOffer(S, R, true, true),
		TEXT("hero: taking him spends the score and fills the meter"));
	const bool bOut = PA250Hero::Step(S, 10.f);
	PA250Hero::Note(S, EPA250HeroEvent::Kill, R);
	const float AfterKill = S.Meter;
	for (int32 i = 0; i < 50; ++i) PA250Hero::Note(S, EPA250HeroEvent::Capture, R);
	Check(!bOut && FMath::IsNearlyEqual(AfterKill, R.meter_seconds - 10.f + R.refill_kill_seconds) && S.Meter == R.meter_seconds && S.Score == 0,
		TEXT("hero: the meter drains and refills, never past full, and scores nothing while he is played"));
	Check(PA250Hero::Step(S, R.meter_seconds + 1.f) && S.Meter == 0.f, TEXT("hero: the meter runs out"));
	PA250Hero::End(S, false);
	Check(!S.bPlaying && !S.bFellWhilePlayed && FMath::IsNearlyEqual(S.SecondsPlayed, R.meter_seconds + 11.f), TEXT("hero: given back, the time played is kept"));
	PA250Hero::Begin(S, R);
	PA250Hero::End(S, true);
	Check(S.bFellWhilePlayed && S.Takeovers == 2 && !S.bPlaying, TEXT("hero: a fall while played is remembered for the report"));
	// The screen's words come from the persons data.
	const pa250::CommanderRules C = pa250::commander_rules();
	const FString Cost = PA250Hero::FallCostText(C);
	const int32 Total = FMath::Max(1, C.fate_wounded + C.fate_killed + C.fate_captured);
	Check(Cost.Contains(FString::Printf(TEXT("wounded %d in 100"), FMath::RoundToInt(100.f * C.fate_wounded / Total))) &&
		Cost.Contains(FString::Printf(TEXT("killed %d in 100"), FMath::RoundToInt(100.f * C.fate_killed / Total))) &&
		Cost.Contains(FString::Printf(TEXT("away %d days"), C.wounded_days)) && Cost.Contains(FString::Printf(TEXT("at most %d units"), C.detachment_unit_cap)) &&
		Cost.Contains(FString::Printf(TEXT("%+d if the day is lost"), C.army_morale_on_fall)) && C.wounded_days > 0 && C.detachment_unit_cap > 0,
		TEXT("hero: the screen names the fate odds, the days away, the detachment and the morale loss"));
	pa250::SideSetup Side;
	Side.hero_health_scale = R.health_scale;
	Side.hero_aura_radius_m = R.aura_radius_m;
	Side.hero_rally_radius_m = R.rally_radius_m;
	Side.hero_rally_cooldown_seconds = R.rally_cooldown_seconds;
	if (!R.trait_abilities.empty()) Side.hero_ability = R.trait_abilities.begin()->second;
	const FString Kit = PA250Hero::KitText(Side, R);
	Check(Kit.Contains(FString::Printf(TEXT("%.0f seconds"), R.meter_seconds)) && Kit.Contains(FString::Printf(TEXT("within %.0f m"), R.aura_radius_m)) &&
		Kit.Contains(FString::Printf(TEXT("Sword: %.0f damage"), R.sword_damage)) && Kit.Contains(FString::Printf(TEXT("Pistol: %.0f damage"), R.pistol_damage)) &&
		(R.trait_abilities.empty() || Kit.Contains(UTF8_TO_TCHAR(Side.hero_ability.label.c_str()))),
		TEXT("hero: the screen names his kit and his ability"));
	Check(R.trait_abilities.size() == 6, TEXT("hero: each of the six traits has an ability"));
}

void PA250FeelCases::RunInput(FCheck Check)
{
	const TArray<TSharedPtr<FJsonObject>> Docs = Vectors(TEXT("input_buffer_"));
	Check(Docs.Num() >= 3, TEXT("input: the buffer vectors are found"));
	bool bAll = true;
	for (const TSharedPtr<FJsonObject>& V : Docs)
	{
		const float Window = static_cast<float>(V->GetNumberField(TEXT("window_s")));
		for (const TSharedPtr<FJsonValue>& C : V->GetArrayField(TEXT("cases")))
		{
			const TSharedPtr<FJsonObject> J = C->AsObject();
			FPA250InputBuffer Buffer;
			TArray<double> Fired;
			for (const TSharedPtr<FJsonValue>& E : J->GetArrayField(TEXT("events")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Pair = E->AsArray();
				const double At = Pair[1]->AsNumber();
				if (Pair[0]->AsString() == TEXT("press")) Buffer.Press(EPA250BufferedAction::Fire, At);
				else if (Buffer.Consume(EPA250BufferedAction::Fire, At, Window)) Fired.Add(At);
			}
			const TArray<TSharedPtr<FJsonValue>>& Want = J->GetArrayField(TEXT("fires_at"));
			bool bSame = Want.Num() == Fired.Num();
			for (int32 i = 0; bSame && i < Want.Num(); ++i) bSame = FMath::IsNearlyEqual(Want[i]->AsNumber(), Fired[i], 1.e-6);
			if (!bSame)
			{
				bAll = false;
				UE_LOG(LogTemp, Display, TEXT("PA250.Combat input vector %s: %s fired %d times, want %d"), *V->GetStringField(TEXT("name")), *J->GetStringField(TEXT("what")),
					Fired.Num(), Want.Num());
			}
		}
	}
	Check(bAll, TEXT("input: a press inside the window fires when it can, outside it is dropped, and is spent once"));
	FPA250InputBuffer Buffer;
	Buffer.Press(EPA250BufferedAction::Order, 1.0, 3);
	Buffer.Press(EPA250BufferedAction::Reload, 1.0);
	int32 Number = 0;
	Check(!Buffer.Consume(EPA250BufferedAction::Fire, 1.1, 1.f) && Buffer.Consume(EPA250BufferedAction::Order, 1.2, 0.5f, &Number) && Number == 3 &&
		Buffer.Pending(EPA250BufferedAction::Reload) && !Buffer.Live(EPA250BufferedAction::Reload, 2.0, 0.4f) && !Buffer.Pending(EPA250BufferedAction::Reload),
		TEXT("input: each action has its own press, an order keeps its number, and an old press is dropped"));
	const FPA250BattleFeel& F = PA250BattleFeel::Get();
	Check(F.BufferFireSeconds > 0.f && F.BufferReloadSeconds > 0.f && F.BufferVaultSeconds > 0.f && F.BufferOrderSeconds > 0.f && F.BufferThrustSeconds > 0.f,
		TEXT("input: the windows are the file's"));
	Check(F.WalkSpeedCm > 0.f && F.SprintSpeedCm > F.WalkSpeedCm && F.MoveMaxAcceleration > 0.f && F.MoveBrakingDeceleration > 0.f && F.RotationRateYawDeg > 0.f &&
		F.CameraLagSpeed > 0.f && F.FireLockSeconds > 0.f && F.BufferFireSeconds <= F.FireLockSeconds,
		TEXT("input: the movement values are the file's"));
	const ARFCharacter* Soldier = GetDefault<ARFCharacter>();
	Check(FMath::IsNearlyEqual(Soldier->WalkSpeed, F.WalkSpeedCm) && FMath::IsNearlyEqual(Soldier->SprintSpeed, F.SprintSpeedCm),
		TEXT("input: a soldier's walk and sprint speed are the file's"));
}

void PA250FeelCases::RunTraversal(FCheck Check)
{
	const TArray<TSharedPtr<FJsonObject>> Docs = Vectors(TEXT("traversal_classify_"));
	Check(Docs.Num() >= 3, TEXT("traversal: the classify vectors are found"));
	bool bAll = true;
	for (const TSharedPtr<FJsonObject>& V : Docs)
	{
		const TSharedPtr<FJsonObject> R = V->GetObjectField(TEXT("rules"));
		auto N = [&R](const TCHAR* Key) { return static_cast<float>(R->GetNumberField(Key)); };
		for (const TSharedPtr<FJsonValue>& C : V->GetArrayField(TEXT("cases")))
		{
			const TSharedPtr<FJsonObject> J = C->AsObject();
			const EPA250Traversal Got = PA250BattleFeel::ClassifyTraversal(static_cast<float>(J->GetNumberField(TEXT("height_cm"))),
				static_cast<float>(J->GetNumberField(TEXT("depth_cm"))), N(TEXT("hurdle_min_cm")), N(TEXT("hurdle_max_height_cm")),
				N(TEXT("vault_max_height_cm")), N(TEXT("vault_max_depth_cm")), N(TEXT("mantle_max_height_cm")), N(TEXT("mantle_max_depth_cm")));
			if (J->GetStringField(TEXT("action")) != PA250BattleFeel::TraversalName(Got))
			{
				bAll = false;
				UE_LOG(LogTemp, Display, TEXT("PA250.Combat traversal vector %s: %s gave %s, want %s"), *V->GetStringField(TEXT("name")),
					*J->GetStringField(TEXT("what")), PA250BattleFeel::TraversalName(Got), *J->GetStringField(TEXT("action")));
			}
		}
	}
	Check(bAll, TEXT("traversal: classification follows the file"));
	const FPA250BattleFeel& F = PA250BattleFeel::Get();
	Check(PA250BattleFeel::ClassifyTraversal(110.f, 80.f, F) == EPA250Traversal::Vault && PA250BattleFeel::ClassifyTraversal(120.f, 90.f, F) == EPA250Traversal::Vault,
		TEXT("traversal: the stone wall of the levels and a breastwork are vaulted, by the loaded file"));
	Check(PA250BattleFeel::ClassifyTraversal(300.f, 30.f, F) == EPA250Traversal::None && PA250BattleFeel::ClassifyTraversal(F.MantleMaxHeightCm + 1.f, 30.f, F) == EPA250Traversal::None,
		TEXT("traversal: too high stays cover"));
	Check(F.TraversalMinSpeedCmS > F.RunMaxCmS - 100.f && F.TraversalCheckHz > 0.f && F.TraversalCapsuleHalfHeightCm > 0.f &&
		F.TraversalMaxApproachAngleDeg > 0.f && F.TraversalLandingClearCm > 0.f && F.TraversalStartLeadSeconds > 0.f,
		TEXT("traversal: only a man near a sprint traverses by himself, and the rest of the section is read"));

	// Fitting a clip to an obstacle (pure). A made-up clip: the hand meets the wall 30 cm short of it
	// at 1.0 s, on a wall 100 cm high; he lands 150 cm past the near face at 1.6 s.
	FPA250TraversalClip Clip;
	Clip.Action = EPA250Traversal::Vault;
	Clip.Gait = TEXT("run");
	FPA250TraversalWindow Plant;
	Plant.Name = TEXT("PA250Warp0");
	Plant.Target = TEXT("FrontLedge");
	Plant.EndS = 1.f;
	Plant.RootCm = FVector(-30.0, 0.0, 0.0);
	Plant.bHasPoint = true;
	Plant.PointCm = FVector(0.0, 0.0, 100.0);
	FPA250TraversalWindow Land;
	Land.Name = TEXT("PA250Warp1");
	Land.Target = TEXT("BackFloor");
	Land.StartS = 1.f;
	Land.EndS = 1.6f;
	Land.RootCm = FVector(150.0, 0.0, 0.0);
	Clip.Windows = {Plant, Land};
	Clip.Approach = {FVector2D(0.0, -400.0), FVector2D(0.5, -200.0), FVector2D(1.0, -30.0), FVector2D(1.6, 150.0)};
	Check(FMath::IsNearlyEqual(PA250Traversal::LedgeForwardCm(Clip), 0.f) && FMath::IsNearlyEqual(PA250Traversal::AuthoredHeightCm(Clip), 100.f),
		TEXT("traversal: a clip's wall is where its first warp point is"));
	Check(FMath::IsNearlyEqual(PA250Traversal::StartTimeFor(Clip, 500.f, 0.25f), 0.f) && FMath::IsNearlyEqual(PA250Traversal::StartTimeFor(Clip, 200.f, 0.25f), 0.5f, 0.01f) &&
		FMath::IsNearlyEqual(PA250Traversal::StartTimeFor(Clip, 300.f, 0.25f), 0.25f, 0.01f),
		TEXT("traversal: the clip is entered where its run-up is as long as his distance from the wall"));
	Check(FMath::IsNearlyEqual(PA250Traversal::StartTimeFor(Clip, 10.f, 0.25f), 0.75f, 0.01f),
		TEXT("traversal: never entered later than the lead before the first window ends"));
	FPA250Obstacle Wall;
	Wall.FrontLedge = FVector(1000.0, 0.0, 110.0);
	Wall.Across = FVector::XAxisVector;
	Wall.HeightCm = 110.f;
	Wall.DepthCm = 80.f;
	Wall.BackFloor = FVector(1150.0, 0.0, 3.0);
	Check(PA250Traversal::WindowTarget(Clip, 0, Wall, 70.f).Equals(FVector(970.0, 0.0, 0.0), 0.5),
		TEXT("traversal: at the hand plant his feet are on the ground before a wall higher than the clip's"));
	Check(PA250Traversal::WindowTarget(Clip, 1, Wall, 70.f).Equals(FVector(1150.0, 0.0, 3.0), 0.5),
		TEXT("traversal: he lands on the ground beyond, where the clip lands"));
	Wall.DepthCm = 120.f;
	Check(PA250Traversal::WindowTarget(Clip, 1, Wall, 70.f).Equals(FVector(1190.0, 0.0, 3.0), 0.5),
		TEXT("traversal: over a deeper wall he lands clear of its far face"));
	FPA250TraversalClip Deep = Clip;
	Deep.Windows[1].Target = TEXT("BackLedge");
	Check(PA250Traversal::WindowTarget(Deep, 1, Wall, 70.f).Equals(FVector(1120.0, 0.0, 110.0), 0.5),
		TEXT("traversal: a far-edge window goes to the top of the far face"));
	FPA250TraversalClip Walk = Clip;
	Walk.Gait = TEXT("walk");
	FPA250TraversalClip Mantle = Clip;
	Mantle.Action = EPA250Traversal::Mantle;
	const TArray<FPA250TraversalClip> Some = {Clip, Walk, Mantle};
	const FPA250TraversalClip* Stand = PA250Traversal::PickClip(Some, EPA250Traversal::Vault, TEXT("stand"), 0);
	const FPA250TraversalClip* RunMantle = PA250Traversal::PickClip(Some, EPA250Traversal::Mantle, TEXT("walk"), 3);
	Check(Stand && Stand->Gait == FName(TEXT("walk")) && RunMantle && RunMantle->Gait == FName(TEXT("run")) &&
		!PA250Traversal::PickClip(Some, EPA250Traversal::Hurdle, TEXT("run"), 0),
		TEXT("traversal: a clip is picked by action and gait, the nearest gait when his own has none"));

	// A clip that ends short of a deep wall is passed over for one that clears it.
	FPA250TraversalClip Short = Clip;
	Short.Gait = TEXT("stand");
	Short.BlendOutS = 1.3f; // by then its root is 60 cm past the near face
	FPA250TraversalClip Long = Clip;
	Long.BlendOutS = 1.6f; // 150 cm past
	const TArray<FPA250TraversalClip> Two = {Short, Long};
	Check(FMath::IsNearlyEqual(PA250Traversal::BeyondAtBlendOutCm(Short), 60.f, 1.f) && FMath::IsNearlyEqual(PA250Traversal::BeyondAtBlendOutCm(Long), 150.f, 1.f),
		TEXT("traversal: how far a clip carries him past the near face is read from its approach"));
	const FPA250TraversalClip* Thin = PA250Traversal::PickClip(Two, EPA250Traversal::Vault, TEXT("stand"), 0, 40.f);
	const FPA250TraversalClip* Thick = PA250Traversal::PickClip(Two, EPA250Traversal::Vault, TEXT("stand"), 0, 115.f);
	const FPA250TraversalClip* TooThick = PA250Traversal::PickClip(Two, EPA250Traversal::Vault, TEXT("stand"), 0, 400.f);
	Check(Thin && Thin->Gait == FName(TEXT("stand")) && Thick && Thick->Gait == FName(TEXT("run")) && TooThick && TooThick->Gait == FName(TEXT("stand")),
		TEXT("traversal: a clip that would leave him inside the wall is passed over, unless none clears it"));

	// Where the bots' crossing links go along a wall (pure).
	TArray<TPair<FVector, FVector>> Ends;
	PA250Traversal::LinkEnds(FVector(0.0, 0.0, 50.0), FVector::XAxisVector, 1300.f, 80.f, 600.f, 70.f, Ends);
	Check(Ends.Num() == 2 && Ends[0].Key.Equals(FVector(-325.0, 110.0, 50.0), 0.5) && Ends[0].Value.Equals(FVector(-325.0, -110.0, 50.0), 0.5) &&
		FMath::IsNearlyEqual(Ends[1].Key.X, 325.0, 0.5),
		TEXT("traversal: a wall gets a link every spacing of its length, an end clear of each face"));
	Ends.Reset();
	PA250Traversal::LinkEnds(FVector::ZeroVector, FVector::YAxisVector, 288.f, 80.f, 600.f, 70.f, Ends);
	Check(Ends.Num() == 1 && FMath::IsNearlyEqual(FMath::Abs(Ends[0].Key.X), 110.0, 0.5) && FMath::IsNearlyZero(Ends[0].Key.Y, 0.5),
		TEXT("traversal: a short wall gets one link at its middle, whichever way it is turned"));
	Check(GetDefault<UPA250NavArea_Traversal>()->DefaultCost == FMath::Max(1.f, F.BotLinkCost) && F.BotLinkCost > 1.f,
		TEXT("traversal: a crossing costs a bot's path what the file says"));

	// What the pipeline baked (anim_clips.json; the montages are not loaded here).
	TArray<FPA250TraversalClip> Baked;
	TArray<FString> Errors;
	const bool bRead = PA250Traversal::LoadClips(PA250AnimClips::DataPath(), false, Baked, &Errors);
	for (const FString& E : Errors) UE_LOG(LogTemp, Display, TEXT("PA250.Combat traversal clips: %s"), *E);
	Check(bRead && Baked.Num() >= 3, TEXT("traversal: the baked clips are in anim_clips.json"));
	bool bEach = true, bSane = true;
	for (const EPA250Traversal Kind : {EPA250Traversal::Hurdle, EPA250Traversal::Vault, EPA250Traversal::Mantle})
	{
		bEach &= PA250Traversal::PickClip(Baked, Kind, TEXT("run"), 0) != nullptr;
	}
	for (const FPA250TraversalClip& C : Baked)
	{
		// Every clip runs up to a wall of sensible height and passes it.
		const float Ledge = PA250Traversal::LedgeForwardCm(C), High = PA250Traversal::AuthoredHeightCm(C);
		const bool bGood = C.Windows.Num() && C.Windows[0].bHasPoint && High > 30.f && High < 200.f && C.Approach[0].Y < Ledge - 20.0 &&
			C.Approach.Last().Y > Ledge && C.LengthS > 0.5f && C.BlendOutS > C.Windows[0].EndS && C.BlendOutS <= C.LengthS;
		if (!bGood) UE_LOG(LogTemp, Display, TEXT("PA250.Combat traversal clip %s: ledge %.0f height %.0f run-up from %.0f to %.0f"), *C.Asset, Ledge, High, C.Approach[0].Y, C.Approach.Last().Y);
		bSane &= bGood;
	}
	Check(bEach, TEXT("traversal: there is a run-up clip for a hurdle, a vault and a mantle"));
	Check(bSane, TEXT("traversal: every baked clip runs up to its wall and passes it"));
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
		Check(PA250BattleFeel::Get().StopSettleSeconds > 0.f && PA250BattleFeel::Get().StopSettleSeconds <= 1.5f, TEXT("anim: the time a halted man keeps his stop clips is the file's (playtest 2026-11-17: he jogged on the spot)"));
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
