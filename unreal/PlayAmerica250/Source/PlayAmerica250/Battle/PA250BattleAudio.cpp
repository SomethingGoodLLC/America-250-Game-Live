#include "PA250BattleAudio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TMap<FString, TArray<TStrongObjectPtr<USoundBase>>>& Banks()
{
	static TMap<FString, TArray<TStrongObjectPtr<USoundBase>>> B;
	return B;
}

TMap<FString, int32>& Counts()
{
	static TMap<FString, int32> C;
	return C;
}

const TArray<TStrongObjectPtr<USoundBase>>& LoadBank(const FString& Key, const FString& Folder, const FString& Name)
{
	if (const TArray<TStrongObjectPtr<USoundBase>>* Found = Banks().Find(Key)) return *Found;
	TArray<TStrongObjectPtr<USoundBase>>& List = Banks().Add(Key);
	// Single asset "<name>", then variants "<name>_1".."<name>_N" (stop at the first gap after 1).
	if (USoundBase* One = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		List.Emplace(One);
	}
	for (int32 i = 1; i <= 16; ++i)
	{
		const FString N = FString::Printf(TEXT("%s_%d"), *Name, i);
		USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *N, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!S) break;
		List.Emplace(S);
	}
	return List;
}

USoundAttenuation* MakeAttenuation(float InnerCm, float FalloffCm, bool bMuffle)
{
	USoundAttenuation* A = NewObject<USoundAttenuation>();
	A->AddToRoot();
	FSoundAttenuationSettings& S = A->Attenuation;
	S.bAttenuate = true;
	S.bSpatialize = true;
	S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	S.AttenuationShape = EAttenuationShape::Sphere;
	S.AttenuationShapeExtents = FVector(InnerCm, 0.f, 0.f);
	S.FalloffDistance = FalloffCm;
	S.bAttenuateWithLPF = bMuffle;
	S.LPFRadiusMin = InnerCm * 2.f;
	S.LPFRadiusMax = FalloffCm * 0.6f;
	return A;
}

double LastShotWindowStart = -1.0;
int32 ShotsInWindow = 0;
TMap<FString, double> LastVoiceTime;
}

USoundAttenuation* FPA250BattleAudio::AttenuationFor(EPA250SoundRange Range)
{
	static USoundAttenuation* Gun = MakeAttenuation(1500.f, 40000.f, true);
	static USoundAttenuation* Voice = MakeAttenuation(800.f, 6000.f, true);
	static USoundAttenuation* Near = MakeAttenuation(250.f, 2500.f, false);
	static USoundAttenuation* Whiz = MakeAttenuation(150.f, 1200.f, false);
	switch (Range)
	{
	case EPA250SoundRange::Gunfire: return Gun;
	case EPA250SoundRange::Voice: return Voice;
	case EPA250SoundRange::Whiz: return Whiz;
	default: return Near;
	}
}

USoundBase* FPA250BattleAudio::Pick(const FString& Bank)
{
	const TArray<TStrongObjectPtr<USoundBase>>& List = LoadBank(Bank, TEXT("/Game/PA250/Audio/SFX"), Bank);
	return List.Num() ? List[FMath::RandHelper(List.Num())].Get() : nullptr;
}

USoundBase* FPA250BattleAudio::Voice(FName Faction, const FString& Key)
{
	const FString Side = Faction.ToString().ToLower();
	const TArray<TStrongObjectPtr<USoundBase>>& List =
		LoadBank(Side + TEXT("/") + Key, FString::Printf(TEXT("/Game/PA250/Audio/Voice/%s"), *Side), TEXT("VO_") + Key);
	return List.Num() ? List[FMath::RandHelper(List.Num())].Get() : nullptr;
}

bool FPA250BattleAudio::GetListener(const UObject* Ctx, FVector& Out)
{
	const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return false;
	Out = PC->PlayerCameraManager->GetCameraLocation();
	return true;
}

bool FPA250BattleAudio::PlayAt(const UObject* Ctx, const FString& Bank, const FVector& Location, EPA250SoundRange Range,
	float Volume, float PitchJitter)
{
	Counts().FindOrAdd(Bank)++;
	USoundBase* Sound = Pick(Bank);
	if (!Sound || !Ctx || !Ctx->GetWorld()) return false;
	const float Pitch = 1.f + FMath::FRandRange(-PitchJitter, PitchJitter);
	UGameplayStatics::PlaySoundAtLocation(Ctx, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, AttenuationFor(Range));
	return true;
}

bool FPA250BattleAudio::PlayAtOr(const UObject* Ctx, const FString& Bank, const FString& Fallback, const FVector& Location,
	EPA250SoundRange Range, float Volume, float PitchJitter)
{
	return PlayAt(Ctx, Pick(Bank) ? Bank : Fallback, Location, Range, Volume, PitchJitter);
}

bool FPA250BattleAudio::PlayMusketShot(const UObject* Ctx, const FVector& Muzzle)
{
	const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
	Counts().FindOrAdd(TEXT("musket_shot"))++;
	if (!World) return false;
	// A volley is 6-8 shots in one frame: let ~5 through per 80 ms so it reads as a volley,
	// not one clipped wall of noise. The skipped ones are masked by the others.
	const double Now = World->GetTimeSeconds();
	if (Now - LastShotWindowStart > 0.08)
	{
		LastShotWindowStart = Now;
		ShotsInWindow = 0;
	}
	if (++ShotsInWindow > 5) return false;
	FVector Listener;
	const bool bFar = GetListener(Ctx, Listener) && FVector::Dist(Listener, Muzzle) > 6000.f;
	return PlayAt(Ctx, bFar ? TEXT("musket_distant") : TEXT("musket_fire"), Muzzle, EPA250SoundRange::Gunfire, bFar ? 0.9f : 1.f, 0.05f);
}

bool FPA250BattleAudio::PlayVoice(const UObject* Ctx, FName Faction, const FString& Key, const FVector& Location)
{
	const FString CountKey = FString::Printf(TEXT("vo:%s/%s"), *Faction.ToString().ToLower(), *Key);
	Counts().FindOrAdd(CountKey)++;
	const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
	USoundBase* Sound = Voice(Faction, Key);
	if (!Sound || !World) return false;
	const double Now = World->GetTimeSeconds();
	double& Last = LastVoiceTime.FindOrAdd(CountKey);
	if (Last > 0.0 && Now - Last < 1.5 && Now >= Last) return false; // same order from several squads at once: say it once
	Last = Now;
	UGameplayStatics::PlaySoundAtLocation(Ctx, Sound, Location, FRotator::ZeroRotator, 1.f, FMath::FRandRange(0.97f, 1.03f), 0.f,
		AttenuationFor(EPA250SoundRange::Voice));
	return true;
}

bool FPA250BattleAudio::Play2D(const UObject* Ctx, const FString& Bank, float Volume)
{
	Counts().FindOrAdd(Bank)++;
	USoundBase* Sound = Pick(Bank);
	if (!Sound || !Ctx || !Ctx->GetWorld()) return false;
	UGameplayStatics::PlaySound2D(Ctx, Sound, Volume, FMath::FRandRange(0.97f, 1.03f));
	return true;
}

bool FPA250BattleAudio::PlayBattleBegins(const UObject* Ctx)
{
	return Play2D(Ctx, TEXT("battle_begins"));
}

namespace
{
TStrongObjectPtr<UAudioComponent>& CampaignMusic()
{
	static TStrongObjectPtr<UAudioComponent> C;
	return C;
}
} // namespace

bool FPA250BattleAudio::StartCampaignMusic(const UObject* Ctx, float Volume)
{
	Counts().FindOrAdd(TEXT("campaign_music"))++;
	if (CampaignMusic() && CampaignMusic()->IsPlaying()) return true;
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/PA250/Audio/Music/campaign_theme.campaign_theme"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Sound || !Ctx || !Ctx->GetWorld()) return false;
	UAudioComponent* Comp = UGameplayStatics::SpawnSound2D(Ctx, Sound, Volume, 1.f, 0.f, nullptr, false, true);
	if (!Comp) return false;
	Comp->FadeIn(2.f, Volume);
	CampaignMusic() = TStrongObjectPtr<UAudioComponent>(Comp);
	return true;
}

void FPA250BattleAudio::StopCampaignMusic()
{
	if (CampaignMusic())
	{
		CampaignMusic()->FadeOut(1.f, 0.f);
		CampaignMusic().Reset();
	}
}

namespace
{
TStrongObjectPtr<UAudioComponent>& Ambience()
{
	static TStrongObjectPtr<UAudioComponent> C;
	return C;
}
} // namespace

bool FPA250BattleAudio::StartAmbience(const UObject* Ctx, float Volume)
{
	Counts().FindOrAdd(TEXT("ambience"))++;
	if (Ambience() && Ambience()->IsPlaying()) return true;
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/PA250/Audio/SFX/ambience_dawn.ambience_dawn"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Sound || !Ctx || !Ctx->GetWorld()) return false;
	if (USoundWave* Wave = Cast<USoundWave>(Sound)) Wave->bLooping = true; // a bed, whatever the import flag says
	UAudioComponent* Comp = UGameplayStatics::SpawnSound2D(Ctx, Sound, Volume, 1.f, 0.f, nullptr, false, true);
	if (!Comp) return false;
	Comp->FadeIn(3.f, Volume);
	Ambience() = TStrongObjectPtr<UAudioComponent>(Comp);
	return true;
}

void FPA250BattleAudio::StopAmbience()
{
	if (Ambience())
	{
		Ambience()->FadeOut(1.5f, 0.f);
		Ambience().Reset();
	}
}

bool FPA250BattleAudio::PlayFootstep(const UObject* Ctx, const FVector& Location, bool bOwn)
{
	FVector Listener;
	if (!bOwn && (!GetListener(Ctx, Listener) || FVector::DistSquared(Listener, Location) > FootstepHearCm * FootstepHearCm)) return false;
	return PlayAt(Ctx, TEXT("footstep_grass"), Location, EPA250SoundRange::Near, bOwn ? 0.55f : 0.4f, 0.12f);
}

int32 FPA250BattleAudio::GetPlayCount(const FString& BankOrKey)
{
	return Counts().FindRef(BankOrKey);
}

void FPA250BattleAudio::ResetPlayCounts()
{
	Counts().Reset();
	LastVoiceTime.Reset();
	ShotsInWindow = 0;
	LastShotWindowStart = -1.0;
}
