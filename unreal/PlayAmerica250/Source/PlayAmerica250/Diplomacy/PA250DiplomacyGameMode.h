// PA250DiplomacyGameMode.h — L_DiplomacyRoom: candlelit parlor, framed portrait fed by a MediaTexture,
// envoy dialogue over the negotiation service, sim-validated terms, Accept / Reject / Leave.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PA250DiplomacyClient.h"
#include "PA250MicRecorder.h"
#include "PA250DiplomacyGameMode.generated.h"

class UAudioComponent;
class UMediaPlayer;
class UMediaSoundComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPA250EnvoyFaceData;
class UPA250DiplomacyWidget;
class UStaticMeshComponent;
class USoundWaveProcedural;

UCLASS()
class PLAYAMERICA250_API APA250DiplomacyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APA250DiplomacyGameMode();
	virtual void Tick(float DeltaSeconds) override;

	// ---- view model (read by the widget) ----
	FString EnvoyName;
	FString EnvoyTitle;
	TArray<FString> Transcript;
	TArray<FString> TermLines;      // sim-validated clauses, human readable
	FString PendingProposalJson;    // {"spoken_line","clauses":[...]} that Accept will submit
	FString StatusLine;
	EPA250DiplomacyState GetUiState() const { return Client ? Client->GetState() : EPA250DiplomacyState::Idle; }
	bool bBusy = false;             // waiting for the envoy's reply

	// ---- actions (buttons and scripts) ----
	void Send(const FString& Text);
	void Accept();
	void Reject();
	void Leave();

	// ---- push-to-talk: click to start, click to stop; the clip is transcribed and sent like typed text ----
	void ToggleMic();
	bool IsMicRecording() const { return Mic && Mic->IsRecording(); }
	bool bTranscribing = false;
	float GetMicSeconds() const { return Mic ? Mic->GetSeconds() : 0.f; }
	float GetMicLevel() const { return Mic ? Mic->GetLevel() : 0.f; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void HandleServiceMessage(const TSharedPtr<FJsonObject>& Msg);
	void HandleState(EPA250DiplomacyState NewState);
	void HandleProposal(const TSharedPtr<FJsonObject>& Msg);
	void PlayLine(const FString& AudioFile, const FString& VideoFile);
	UFUNCTION()
	void OnMediaEnded();
	UFUNCTION()
	void OnMediaOpened(FString OpenedUrl);
	UFUNCTION()
	void OnMediaOpenFailed(FString FailedUrl);

	// ---- envoy voice: WAV -> USoundWaveProcedural on an audio component at the portrait (not MediaPlayer) ----
	bool PlayVoiceWav(const FString& File);
	void PollVoice();
	void StopVoice(const TCHAR* Why);
	void StopVideo();
	FString VoiceFile;
	float VoiceSeconds = 0.f;
	// ---- lip-sync flipbook on the painting (M_PortraitStill "Frame": 0 = rest, 1..15 wider open) ----
	void UpdateLipSync(int32 BytesLeft);
	void SetLipFrame(int32 Frame);
	TArray<float> VoiceEnvelope;   // 30 Hz loudness of the current line, ~0..1.3
	float VoiceBytesPerSecond = 1.f;
	float LipOpen = 0.f;
	int32 LipFrame = 0;
	int32 LipChanges = 0;
	uint32 LipCellsUsed = 0;
	int32 VoiceBytes = 0;
	double VoiceStartTime = 0.0;
	double VoiceDrainedTime = 0.0;
	FTimerHandle VoiceTimer;
	FString VideoFilePlaying;
	double VideoStartTime = 0.0;
	float VideoPeakEnvelope = 0.f;  // MP4 voice level seen by MediaSound (proves the muxed audio reached the audio engine)
	FTimerHandle VideoTimer;

	// ---- -PA250VoiceTest: open the room WITH sound, send one line, wait for "EnvoyVoice finished", exit ----
	void VoiceTestFinish(bool bPassed, const FString& Why);
	bool bVoiceTest = false;
	bool bVoiceTestSent = false;
	bool bVoiceTestDone = false;
	FString VoiceTestVideo;  // -PA250VoiceTestVideo=<mp4>: play this clip for the line (tests the MP4 path)
	FTimerHandle VoiceTestTimer;

	// ---- mic ----
	void SendMicClip(const TArray<uint8>& Wav, float Seconds, float Peak);
	TUniquePtr<FPA250MicRecorder> Mic;
	FTimerHandle MicTimer;
	// -PA250MicTest: record 3 s from the real mic, transcribe, log, exit. -PA250MicTestWav=<file>: inject a clip.
	bool bMicTest = false;
	FString MicTestWav;

	// ---- talking painting (M_PortraitWarp): speech from viseme curves, plus breathing, drift, eyes, blinks ----
	// pa250.LipSync 1 (default) warps the painting; 0 uses the 16-cell flipbook (M_PortraitStill) instead.
	struct FSpring
	{
		float X = 0.f, V = 0.f;
		// Critically damped (no overshoot, no flicker), solved exactly: stable at any frame time. A simple
		// Euler step diverges once Omega * Dt > 1 (a slow frame), which threw the eye warp off the image.
		void Step(float Target, float Omega, float Dt)
		{
			const float D = X - Target;
			const float C = V + Omega * D;
			const float E = FMath::Exp(-Omega * Dt);
			X = Target + (D + C * Dt) * E;
			V = (V - Omega * C * Dt) * E;
		}
	};
	struct FVisemeTrack
	{
		float Fps = 30.f;
		TArray<float> Jaw, Wide, Round, Press;
		bool IsValid() const { return Jaw.Num() > 0; }
		float At(const TArray<float>& C, float T) const;
	};
	void UpdateFace(float Dt);
	void SetupFace();
	float VoicePlaybackSeconds() const;
	bool bWarp = false;
	FVisemeTrack CurrentVisemes;
	FSpring SJaw, SWide, SRound, SPress, SNod, SEyeX, SEyeY;
	float LifeTime = 0.f;
	float NextBlinkAt = 2.5f, BlinkStart = -10.f, NextSaccadeAt = 1.5f, SaccadeX = 0.f, SaccadeY = 0.f;
	int32 WarpMovedTicks = 0;  // ticks with the jaw visibly open during speech (the voice test checks it)

	// ---- one reply = several sentence chunks, played back to back ----
	struct FPendingChunk
	{
		FString Audio, Video, ReplyId;
		int32 Chunk = 0;
		TSharedPtr<FJsonObject> Visemes, Timing;
	};
	void StartChunk(const FPendingChunk& C);
	bool PlayNextQueued();
	TArray<FPendingChunk> PendingChunks;
	FString CurrentReplyId;
	TSharedPtr<FJsonObject> CurrentTiming;
	bool bLatencyLogged = true;
	double ChunkReceivedEpoch = 0.0;

	// ---- waiting for a reply: considering idle; stock lip-synced clips (greeting, consider, acceptance, refusal) ----
	bool PlayStock(const FString& Name);
	bool IsWaitingForReply() const;
	double WaitStart = 0.0;
	int32 RepliesSinceConsider = 99;
	bool bConsiderThisReply = false;
	bool bGreeted = false;
	bool bLeaveAfterStock = false;
	FString StockPlaying;
	TMap<FString, FString> StockClips, StockTexts;
	FTimerHandle LeaveTimer;

	// ---- -PA250LipCompare=<wav>: the same line twice, flipbook then warp, frames captured for a side-by-side video ----
	void LipCompareStep();
	FString LipCompareWav;
	int32 LipComparePass = 0;  // 0 waiting, 1 flipbook, 2 warp, 3 done
	double LipCompareLastShot = 0.0;
	int32 LipCompareFrame = 0;
	FString LipCompareTimes;

	// ---- fire and candle flicker (lights tagged FireLight / CandleLight) ----
	void Flicker();
	FTimerHandle FlickerTimer;
	struct FFlickerLight { TWeakObjectPtr<class ULightComponent> Light; float Base = 0.f; float Seed = 0.f; bool bFire = false; };
	TArray<FFlickerLight> FlickerLights;
	void SetupMedia();
	FString DescribeClause(const TSharedPtr<FJsonObject>& Clause) const;
	void ReturnToCampaign();

	// ---- headless script (selftest=diplomacy and -PA250Autoplay) ----
	void ScriptAdvance();
	void ScriptCheckAndFinish();
	TArray<FString> ScriptLines;
	int32 ScriptIndex = -1;
	bool bScripted = false;
	bool bSelftest = false;
	bool bExpectUnavailable = false;
	FString HashAtStart;
	FString HashBeforeAccept;
	bool bJailbreakHadClauses = false;
	bool bGotCeasefire = false;
	bool bGotResponse = false;
	bool bGotProposal = false;
	bool bPlayerSpoke = false;
	FTimerHandle ScriptTimer;

	UPROPERTY(Transient)
	TObjectPtr<UPA250DiplomacyClient> Client;
	UPROPERTY(Transient)
	TObjectPtr<UPA250DiplomacyWidget> Widget;
	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer;
	UPROPERTY(Transient)
	TObjectPtr<UMediaSoundComponent> MediaSound;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> StillMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TalkMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WarpMID;
	UPROPERTY(Transient)
	TObjectPtr<UPA250EnvoyFaceData> Face;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VideoMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PortraitMesh;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceComponent;
	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> VoiceWave;
};
