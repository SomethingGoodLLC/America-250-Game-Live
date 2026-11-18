#include "PA250BattleFX.h"
#include "PA250BattleAudio.h"
#include "PA250BattleFeel.h"
#include "PA250BattleLook.h"
#include "PA250Intensity.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
TMap<FString, FPA250BattleFXEntry> GFx;
TMap<FString, TPair<FString, FString>> GOrderCues; // word -> (drum, fife)
TSet<FString> GMissingNiagara;
bool GFxLoaded = false;
int32 GCounts[5] = {};
float GLastShake[5] = {};

int32 Idx(EPA250BattleFXEvent E) { return static_cast<int32>(E); }

void EnsureFx()
{
	if (GFxLoaded) return;
	GFxLoaded = true;
	GFx.Reset();
	GOrderCues.Reset();
	FString Text;
	const FString Path = PA250BattleLook::DataPath();
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 FX: cannot read battle_look at %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;
	const TSharedPtr<FJsonObject>* Audio = nullptr;
	const TSharedPtr<FJsonObject>* Orders = nullptr;
	if (Root->TryGetObjectField(TEXT("audio"), Audio) && (*Audio)->TryGetObjectField(TEXT("orders"), Orders))
	{
		for (const auto& Kv : (*Orders)->Values)
		{
			const TSharedPtr<FJsonObject> Cue = Kv.Value->AsObject();
			if (!Cue.IsValid()) continue;
			FString Drum, Fife;
			Cue->TryGetStringField(TEXT("drum"), Drum);
			Cue->TryGetStringField(TEXT("fife"), Fife);
			GOrderCues.Emplace(Kv.Key, TPair<FString, FString>(Drum, Fife));
		}
	}
	const TSharedPtr<FJsonObject>* FxObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("fx"), FxObj) || !FxObj || !FxObj->IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 FX: battle_look has no fx block"));
		return;
	}
	for (const auto& Kv : (*FxObj)->Values)
	{
		if (Kv.Key == TEXT("note")) continue;
		const TSharedPtr<FJsonObject> E = Kv.Value->AsObject();
		if (!E.IsValid()) continue;
		FPA250BattleFXEntry Entry;
		Entry.bLoaded = true;
		E->TryGetStringField(TEXT("sound"), Entry.SoundBank);           // null leaves it empty
		E->TryGetStringField(TEXT("fallback_sound"), Entry.FallbackSound);
		E->TryGetNumberField(TEXT("shake_scale"), Entry.ShakeScale);
		E->TryGetNumberField(TEXT("inner_m"), Entry.InnerM);
		E->TryGetNumberField(TEXT("outer_m"), Entry.OuterM);
		E->TryGetStringField(TEXT("niagara"), Entry.NiagaraPath);
		GFx.Emplace(Kv.Key, Entry);
	}
}
}

namespace PA250BattleFX
{
FPA250BattleFXEntry Lookup(EPA250BattleFXEvent Event)
{
	EnsureFx();
	if (const FPA250BattleFXEntry* E = GFx.Find(EventName(Event))) return *E;
	return {};
}

void Fire(UWorld* World, EPA250BattleFXEvent Event, const FVector& At, float Volume, float ListenerDistM)
{
	EnsureFx();
	const int32 I = Idx(Event);
	++GCounts[I];
	const FPA250BattleFXEntry Entry = Lookup(Event);
	const bool bDud = Event == EPA250BattleFXEvent::ShellDud;
	const bool bRoundShot = Event == EPA250BattleFXEvent::RoundShotGroundStrike;

	if (World && !Entry.SoundBank.IsEmpty())
	{
		if (Entry.FallbackSound.IsEmpty()) FPA250BattleAudio::PlayAt(World, Entry.SoundBank, At, EPA250SoundRange::Gunfire, Volume, 0.05f);
		else FPA250BattleAudio::PlayAtOr(World, Entry.SoundBank, Entry.FallbackSound, At, EPA250SoundRange::Gunfire, Volume, 0.05f);
	}

	if (World && !bDud && !Entry.NiagaraPath.IsEmpty() && !GMissingNiagara.Contains(Entry.NiagaraPath))
	{
		if (UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *Entry.NiagaraPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sys, At, FRotator::ZeroRotator);
		}
		else
		{
			GMissingNiagara.Add(Entry.NiagaraPath); // said once; the event still has its sound and shake
			UE_LOG(LogTemp, Warning, TEXT("PA250 FX: PLACEHOLDER %s has no Niagara system at %s (listed missing in asset_manifest.json)"),
				EventName(Event), *Entry.NiagaraPath);
		}
	}

	float DistM = ListenerDistM;
	if (DistM < 0.f)
	{
		FVector Listener;
		DistM = (World && FPA250BattleAudio::GetListener(World, Listener)) ? static_cast<float>(FVector::Dist(At, Listener)) / 100.f : 1.0e6f;
	}
	float Shake = 0.f;
	if (bRoundShot)
	{
		// One owner for the round shot's shake and dirt: battle_feel.intensity.round_shot.
		const FPA250BattleFeel& Feel = PA250BattleFeel::Get();
		Shake = PA250Intensity::ShakeScaleAt(DistM, Feel.RoundShotShakeInnerM, Feel.RoundShotShakeOuterM, Feel.RoundShotShakeScale);
		if (World) PA250Intensity::RoundShotStrike(World, At);
	}
	else if (!bDud && Entry.ShakeScale > 0.f)
	{
		Shake = PA250Intensity::ShakeScaleAt(DistM, Entry.InnerM, Entry.OuterM, Entry.ShakeScale);
		if (Shake > 0.f && World) PA250Intensity::Shake(World, Shake);
	}
	GLastShake[I] = Shake;
	// A strike close by suppresses him as a near miss does (intensity.suppression.impact_add), by how
	// hard it shook him (1 inside the inner radius).
	if (World && Shake > 0.f)
	{
		const float Full = bRoundShot ? PA250BattleFeel::Get().RoundShotShakeScale : Entry.ShakeScale;
		if (Full > 0.f && Event != EPA250BattleFXEvent::CannonFired && Event != EPA250BattleFXEvent::CanisterFired) PA250Intensity::ImpactNear(World, Shake / Full);
	}
}

bool OrderCue(const FString& Key, FString& OutDrum, FString& OutFife)
{
	EnsureFx();
	const TPair<FString, FString>* Cue = GOrderCues.Find(Key);
	if (!Cue) return false;
	OutDrum = Cue->Key;
	OutFife = Cue->Value;
	return true;
}

int32 FireCount(EPA250BattleFXEvent Event) { return GCounts[Idx(Event)]; }
float LastShakeScale(EPA250BattleFXEvent Event) { return GLastShake[Idx(Event)]; }
int32 MissingNiagaraCount() { return GMissingNiagara.Num(); }

void ResetCounts()
{
	for (int32& C : GCounts) C = 0;
	for (float& S : GLastShake) S = 0.f;
}

void ReloadForTest()
{
	GFxLoaded = false;
	GMissingNiagara.Reset();
	EnsureFx();
}
}
