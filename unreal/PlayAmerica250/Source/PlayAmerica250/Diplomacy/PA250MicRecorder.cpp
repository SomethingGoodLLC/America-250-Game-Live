#include "PA250MicRecorder.h"

FPA250MicRecorder::~FPA250MicRecorder()
{
	Abort();
}

bool FPA250MicRecorder::Start(FString& OutError)
{
	if (bRecording)
	{
		return true;
	}
	Audio::FCaptureDeviceInfo Info;
	if (!Capture.GetCaptureDeviceInfo(Info))
	{
		OutError = TEXT("no microphone found");
		return false;
	}
	DeviceName = Info.DeviceName;
	{
		FScopeLock L(&Lock);
		Samples.Reset();
		SampleRate = Info.PreferredSampleRate;
	}
	Level.store(0.f);
	Audio::FAudioCaptureDeviceParams Params;  // default device, its own rate and channel count, float32
	// OpenAudioCaptureStream is not exported from AudioCaptureCore in 5.8; the float callback variant is.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bOpened = Capture.OpenCaptureStream(Params,
		[this](const float* In, int32 NumFrames, int32 NumChannels, int32 Rate, double, bool) {
			const int32 MaxSamples = (int32)(MaxSeconds * (float)Rate);
			float Peak = 0.f;
			FScopeLock L(&Lock);
			SampleRate = Rate;
			const int32 Room = FMath::Max(0, MaxSamples - Samples.Num());
			const int32 N = FMath::Min(NumFrames, Room);
			Samples.Reserve(Samples.Num() + N);
			for (int32 f = 0; f < N; ++f)
			{
				float S = 0.f;
				for (int32 c = 0; c < NumChannels; ++c)
				{
					S += In[f * NumChannels + c];
				}
				S /= (float)FMath::Max(1, NumChannels);
				Peak = FMath::Max(Peak, FMath::Abs(S));
				Samples.Add(S);
			}
			Level.store(FMath::Max(Peak, Level.load() * 0.85f));
		}, 1024);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!bOpened || !Capture.StartStream())
	{
		Capture.AbortStream();
		OutError = FString::Printf(TEXT("could not open microphone \"%s\""), *DeviceName);
		return false;
	}
	bRecording = true;
	return true;
}

float FPA250MicRecorder::GetSeconds() const
{
	FScopeLock L(&Lock);
	return SampleRate > 0 ? (float)Samples.Num() / (float)SampleRate : 0.f;
}

void FPA250MicRecorder::Abort()
{
	if (Capture.IsStreamOpen())
	{
		Capture.AbortStream();
	}
	bRecording = false;
}

bool FPA250MicRecorder::Stop(TArray<uint8>& OutWav, float& OutSeconds, float& OutPeak)
{
	if (!bRecording)
	{
		return false;
	}
	Capture.StopStream();
	Capture.CloseStream();
	bRecording = false;

	TArray<float> Mono;
	int32 Rate = 0;
	{
		FScopeLock L(&Lock);
		Mono = MoveTemp(Samples);
		Samples.Reset();
		Rate = SampleRate;
	}
	OutSeconds = Rate > 0 ? (float)Mono.Num() / (float)Rate : 0.f;
	OutPeak = 0.f;
	TArray<int16> Pcm;
	Pcm.SetNumUninitialized(Mono.Num());
	for (int32 i = 0; i < Mono.Num(); ++i)
	{
		OutPeak = FMath::Max(OutPeak, FMath::Abs(Mono[i]));
		Pcm[i] = (int16)FMath::Clamp(FMath::RoundToInt(Mono[i] * 32767.f), -32768, 32767);
	}

	// RIFF/WAVE header, mono, 16-bit
	const uint32 DataBytes = (uint32)Pcm.Num() * 2;
	OutWav.Reset(44 + DataBytes);
	auto Put32 = [&OutWav](uint32 V) { for (int32 b = 0; b < 4; ++b) OutWav.Add((uint8)(V >> (8 * b))); };
	auto Put16 = [&OutWav](uint16 V) { OutWav.Add((uint8)V); OutWav.Add((uint8)(V >> 8)); };
	auto Tag = [&OutWav](const char* T) { for (int32 b = 0; b < 4; ++b) OutWav.Add((uint8)T[b]); };
	Tag("RIFF"); Put32(36 + DataBytes); Tag("WAVE");
	Tag("fmt "); Put32(16); Put16(1); Put16(1); Put32((uint32)Rate); Put32((uint32)Rate * 2); Put16(2); Put16(16);
	Tag("data"); Put32(DataBytes);
	OutWav.Append((const uint8*)Pcm.GetData(), DataBytes);
	return true;
}
