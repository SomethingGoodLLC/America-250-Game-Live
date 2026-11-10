// PA250MicRecorder.h — push-to-talk capture for the diplomacy room: default input device -> mono 16-bit WAV.
// Display/input only: the clip is transcribed by the negotiation service and sent as an ordinary player_utterance.
#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureCore.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class PLAYAMERICA250_API FPA250MicRecorder
{
public:
	~FPA250MicRecorder();

	/** Opens the default input device and starts recording. False (with a reason) if no device or it will not open. */
	bool Start(FString& OutError);
	/** Stops recording; OutWav is a mono 16-bit PCM WAV at the device rate. */
	bool Stop(TArray<uint8>& OutWav, float& OutSeconds, float& OutPeak);
	void Abort();

	bool IsRecording() const { return bRecording; }
	float GetSeconds() const;
	/** Recent input level 0..1 (for the button meter). */
	float GetLevel() const { return Level.load(); }
	FString GetDeviceName() const { return DeviceName; }

	static constexpr float MaxSeconds = 60.f;

private:
	Audio::FAudioCapture Capture;
	mutable FCriticalSection Lock;
	TArray<float> Samples;  // mono
	int32 SampleRate = 0;
	FString DeviceName;
	std::atomic<float> Level{0.f};
	bool bRecording = false;
};
