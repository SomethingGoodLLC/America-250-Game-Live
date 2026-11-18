#include "PA250Intensity.h"

#include "Battle/PA250BattleAudio.h"
#include "Battle/PA250BattleFeel.h"
#include "Battle/PA250BattleGameMode.h"
#include "Battle/PA250MeleeComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "RF/RFCharacter.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

namespace
{
struct FCounts
{
	int32 NearMisses = 0;
	int32 Shakes = 0;
	int32 Dirt = 0;
	TSet<FString> MissingLogged;
};

FCounts& Counts()
{
	static FCounts C;
	return C;
}

/** A sound that is not on disk is said once, not heard. */
void NoteMissing(const FString& Bank, const TCHAR* What)
{
	if (Counts().MissingLogged.Contains(Bank)) return;
	Counts().MissingLogged.Add(Bank);
	UE_LOG(LogTemp, Display, TEXT("PA250 intensity: MISSING sound \"%s\" (%s): it is silent"), *Bank, What);
}
}

int32 PA250Intensity::NearMissCount() { return Counts().NearMisses; }
int32 PA250Intensity::ShakeCount() { return Counts().Shakes; }
int32 PA250Intensity::DirtCount() { return Counts().Dirt; }
void PA250Intensity::ResetCounts() { Counts() = FCounts(); }

UPA250FeelShake::UPA250FeelShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPerlinNoiseCameraShakePattern>(TEXT("RootShakePattern")))
{
	// From data; the loader has already logged it if the file gave nothing (then this shake is nothing).
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	bSingleInstance = true; // a second ball restarts it, never stacks
	if (UPerlinNoiseCameraShakePattern* Noise = Cast<UPerlinNoiseCameraShakePattern>(GetRootShakePattern()))
	{
		Noise->Duration = FMath::Max(0.05f, Feel.ShakeSeconds);
		Noise->BlendInTime = 0.03f;
		Noise->BlendOutTime = FMath::Max(0.02f, Feel.ShakeSeconds * 0.6f);
		Noise->X.Amplitude = Noise->Y.Amplitude = Noise->Z.Amplitude = Feel.ShakeLocationCm;
		Noise->X.Frequency = Noise->Y.Frequency = Noise->Z.Frequency = Feel.ShakeFrequencyHz;
		Noise->Pitch.Amplitude = Noise->Yaw.Amplitude = Feel.ShakeRotationDeg;
		Noise->Roll.Amplitude = Feel.ShakeRotationDeg * 0.5f;
		Noise->Pitch.Frequency = Noise->Yaw.Frequency = Noise->Roll.Frequency = Feel.ShakeFrequencyHz;
	}
}

void PA250Intensity::ImpactNear(UWorld* World, float Fraction)
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ARFCharacter* Player = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (Player && !Player->IsDead() && Fraction > 0.f) Player->AddSuppression(Feel.SuppressionImpactAdd * FMath::Clamp(Fraction, 0.f, 1.f));
}

void PA250Intensity::Shake(UWorld* World, float Scale)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || Scale <= 0.01f) return;
	PC->ClientStartCameraShake(UPA250FeelShake::StaticClass(), Scale);
	++Counts().Shakes;
}

bool PA250Intensity::NearMiss(ARFCharacter* Player, const FVector& ShotFrom, const FVector& Closest, const FVector& Listener)
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	if (Feel.NearMissRadiusCm <= 0.f || FVector::Dist(Closest, Listener) >= Feel.NearMissRadiusCm || FVector::Dist(ShotFrom, Listener) <= Feel.NearMissMinShotCm)
	{
		return false;
	}
	++Counts().NearMisses;
	if (Player && !Player->IsDead())
	{
		Player->PunchFOV(Feel.NearMissFovPunchDeg);
		// His body starts away from the ball's path.
		if (Player->MeleeComponent && Feel.NearMissFlinch > 0.f)
		{
			FVector Away = (Player->GetActorLocation() - Closest).GetSafeNormal2D();
			if (Away.IsNearlyZero()) Away = -Player->GetActorForwardVector();
			Player->MeleeComponent->Flinch(Away, Feel.NearMissFlinch);
		}
		Shake(Player->GetWorld(), Feel.NearMissShakeScale);
		// Suppression (battle_feel.intensity.suppression): event-driven, no traces per tick. It blurs
		// his view and sways his aim, and falls away (ARFCharacter::TickSuppression).
		Player->AddSuppression(Feel.SuppressionNearMissAdd);
	}
	// The crack of a close ball, over the whiz the projectile already plays.
	if (!Feel.NearMissCrackBank.IsEmpty())
	{
		if (FPA250BattleAudio::Pick(Feel.NearMissCrackBank)) FPA250BattleAudio::PlayAt(Player, Feel.NearMissCrackBank, Closest, EPA250SoundRange::Whiz, 1.f, 0.08f);
		else NoteMissing(Feel.NearMissCrackBank, TEXT("the crack of a near miss"));
	}
	return true;
}

void PA250Intensity::RoundShotStrike(UWorld* World, const FVector& At)
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	FVector Listener;
	if (!World || !FPA250BattleAudio::GetListener(World, Listener)) return;
	const float DistM = static_cast<float>(FVector::Dist(At, Listener)) / 100.f;
	Shake(World, ShakeScaleAt(DistM, Feel.RoundShotShakeInnerM, Feel.RoundShotShakeOuterM, Feel.RoundShotShakeScale));
	if (DistM <= Feel.RoundShotDirtRadiusM && Feel.RoundShotDirtSeconds > 0.f)
	{
		if (APA250BattleGameMode* GM = World->GetAuthGameMode<APA250BattleGameMode>())
		{
			GM->ShowDirt(Feel.RoundShotDirtSeconds);
			++Counts().Dirt;
		}
	}
}

int32 FPA250BattleMusic::Start(const UObject* Ctx)
{
	Stop();
	Missing.Reset();
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	for (const FPA250MusicLayer& L : Feel.MusicLayers)
	{
		USoundBase* Sound = FPA250BattleAudio::Pick(L.Bank);
		if (!Sound)
		{
			Missing.Add(L.Bank);
			NoteMissing(L.Bank, TEXT("a layer of the battle music"));
			continue;
		}
		if (USoundWave* Wave = Cast<USoundWave>(Sound)) Wave->bLooping = true;
		// All started in the same frame, so the layers of the piece stay in step.
		UAudioComponent* Comp = UGameplayStatics::SpawnSound2D(Ctx, Sound, 1.f, 1.f, 0.f, nullptr, false, true);
		if (!Comp) continue;
		FLayer Layer;
		Layer.Sound = TStrongObjectPtr<UAudioComponent>(Comp);
		Layer.From = L.From;
		Layer.Full = L.Full;
		Comp->SetVolumeMultiplier(0.001f);
		Layers.Add(MoveTemp(Layer));
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 music: %d of %d layers playing%s"), Layers.Num(), Feel.MusicLayers.Num(),
		Missing.Num() ? *FString::Printf(TEXT("; MISSING %s"), *FString::Join(Missing, TEXT(", "))) : TEXT(""));
	return Layers.Num();
}

void FPA250BattleMusic::Update(float SteadyShare, float ReserveShare)
{
	const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
	LastIntensity = PA250Music::Intensity(SteadyShare, ReserveShare, Feel.MusicMoraleWeight, Feel.MusicTicketWeight);
	for (FLayer& L : Layers)
	{
		const float Gain = PA250Music::LayerGain(LastIntensity, L.From, L.Full);
		if (FMath::IsNearlyEqual(Gain, L.Gain, 0.02f) || !L.Sound.IsValid()) continue;
		L.Gain = Gain;
		L.Sound->AdjustVolume(Feel.MusicFadeSeconds, FMath::Max(0.001f, Gain * Feel.MusicVolume));
	}
}

void FPA250BattleMusic::Stop()
{
	for (FLayer& L : Layers)
	{
		if (L.Sound.IsValid()) L.Sound->FadeOut(1.5f, 0.f);
	}
	Layers.Reset();
}
