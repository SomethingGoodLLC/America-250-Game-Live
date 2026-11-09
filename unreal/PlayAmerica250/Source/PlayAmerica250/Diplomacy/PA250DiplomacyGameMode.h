// PA250DiplomacyGameMode.h — L_DiplomacyRoom: candlelit parlor, framed portrait fed by a MediaTexture,
// envoy dialogue over the negotiation service, sim-validated terms, Accept / Reject / Leave.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PA250DiplomacyClient.h"
#include "PA250DiplomacyGameMode.generated.h"

class UAudioComponent;
class UMediaPlayer;
class UMediaSoundComponent;
class UMaterialInterface;
class UPA250DiplomacyWidget;
class UStaticMeshComponent;
class USoundWaveProcedural;

UCLASS()
class PLAYAMERICA250_API APA250DiplomacyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APA250DiplomacyGameMode();

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
	TObjectPtr<UMaterialInterface> VideoMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PortraitMesh;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceComponent;
	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> VoiceWave;
};
