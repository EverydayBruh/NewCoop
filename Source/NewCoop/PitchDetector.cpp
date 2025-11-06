#include "PitchDetector.h"

#include "Engine/Engine.h"
#include "AudioMixerDevice.h"    // Audio::FMixerDevice
#include "Async/Async.h"
#include <cmath>

UPitchDetector::UPitchDetector()
	: SamplesAccum(0)
	, bJobPending(false)
{
	PrimaryComponentTick.bCanEverTick = false;
	MonoRing.SetNumZeroed(48000 * 2);     // ~2 сек кольцевого буфера
	Listener = MakeShared<FSubmixListener, ESPMode::ThreadSafe>(this);
}

void UPitchDetector::BeginPlay()
{
	Super::BeginPlay();
}

void UPitchDetector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}

void UPitchDetector::Start()
{
	if (bRunning || !TargetSubmix) return;
	SamplesAccum.store(0, std::memory_order_relaxed);
	bJobPending.store(false, std::memory_order_relaxed);
	Register();
	bRunning = true;
}

void UPitchDetector::Stop()
{
	if (!bRunning) return;
	bRunning = false;
	Unregister();
}

void UPitchDetector::GetPitch(float& OutPitchHz, float& OutConfidence) const
{
	FScopeLock L(&CS);
	OutPitchHz = LastPitch;
	OutConfidence = LastProb;
}

void UPitchDetector::Register()
{
	if (!TargetSubmix || !GEngine || !Listener.IsValid()) return;

	FAudioDeviceHandle ADH = GEngine->GetActiveAudioDevice();
	if (FAudioDevice* AD = ADH.GetAudioDevice())
	{
		// Как просили — без проверок AudioMixer
		Audio::FMixerDevice* MD = static_cast<Audio::FMixerDevice*>(AD);
		if (MD)
		{
			Listener->Owner = this; // активируем слабую ссылку
			MD->RegisterSubmixBufferListener(Listener.ToSharedRef(), *TargetSubmix);
		}
	}
}

void UPitchDetector::Unregister()
{
	if (!TargetSubmix || !GEngine || !Listener.IsValid()) return;

	FAudioDeviceHandle ADH = GEngine->GetActiveAudioDevice();
	if (FAudioDevice* AD = ADH.GetAudioDevice())
	{
		Audio::FMixerDevice* MD = static_cast<Audio::FMixerDevice*>(AD);
		if (MD)
		{
			// Сначала логически отцепляемся — коллбек станет no-op
			Listener->Owner = nullptr;
			MD->UnregisterSubmixBufferListener(Listener.ToSharedRef(), *TargetSubmix);
		}
	}
}

void UPitchDetector::FSubmixListener::OnNewSubmixBuffer(const USoundSubmix* /*OwningSubmix*/,
	float* AudioData, int32 NumSamples, int32 NumChannels, const int32 InSampleRate, double /*AudioClock*/)
{
	if (!Owner.IsValid()) return;
	Owner->OnSubmixThreadSafe(AudioData, NumSamples, NumChannels, InSampleRate);
}

void UPitchDetector::OnSubmixThreadSafe(float* AudioData, int32 NumSamples, int32 NumChannels, int32 InSampleRate)
{
	if (!AudioData || NumSamples <= 0) return;

	SampleRate = InSampleRate;

	// КОПИРУЕМ во временный моно-буфер (входной буфер сабмикса не трогаем)
	const int32 Frames = (NumChannels > 0) ? (NumSamples / NumChannels) : 0;
	if (Frames <= 0) return;

	TempFrame.SetNumUninitialized(Frames);

	if (NumChannels == 1)
	{
		FMemory::Memcpy(TempFrame.GetData(), AudioData, sizeof(float) * Frames);
	}
	else
	{
		for (int32 f = 0; f < Frames; ++f)
		{
			double s = 0.0;
			const int32 Base = f * NumChannels;
			for (int32 c = 0; c < NumChannels; ++c) s += AudioData[Base + c];
			TempFrame[f] = static_cast<float>(s / NumChannels);
		}
	}

	// запись в кольцевой буфер (под локом)
	{
		FScopeLock L(&CS);
		for (int32 i = 0; i < Frames; ++i)
		{
			MonoRing[RingWrite] = TempFrame[i];
			RingWrite = (RingWrite + 1) % MonoRing.Num();
		}
	}

	// учёт хоппинга
	const int32 Acc = SamplesAccum.fetch_add(Frames, std::memory_order_relaxed) + Frames;
	if (Acc >= HopSize)
	{
		// пробуем стартовать фоновую задачу анализа (единовременно)
		TryDispatchAnalysis();

		// уменьшаем счётчик (можно оставить остаток для точного хопа)
		SamplesAccum.store(Acc - HopSize, std::memory_order_relaxed);
	}
}

void UPitchDetector::EnsureHann(int32 N)
{
	if (Hann.Num() == N)
	{
		return;
	}
	Hann.SetNumUninitialized(N);
	for (int32 n = 0; n < N; ++n)
	{
		Hann[n] = 0.5f * (1.f - cosf(2.f * PI * float(n) / float(N - 1)));
	}
}

bool UPitchDetector::SnapshotLatestFrame(TArray<float>& OutFrame, int32& OutFs)
{
	const int32 N = FMath::Clamp(WindowSize, 512, 8192);

	OutFrame.SetNumUninitialized(N);

	int32 Start = 0;
	{
		FScopeLock L(&CS);
		// если кольцо пустое — откажемся
		if (MonoRing.Num() < N) return false;

		Start = RingWrite - N;
		if (Start < 0) Start += MonoRing.Num();

		for (int32 i = 0; i < N; ++i)
		{
			OutFrame[i] = MonoRing[(Start + i) % MonoRing.Num()];
		}
		OutFs = SampleRate;
	}

	return true;
}

void UPitchDetector::TryDispatchAnalysis()
{
	// не допускаем одновременных задач
	bool Expected = false;
	if (!bJobPending.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
	{
		return; // уже есть активная задача
	}

	// снимаем слепок кадра из кольца
	TArray<float> Frame;
	int32 Fs = 48000;
	if (!SnapshotLatestFrame(Frame, Fs))
	{
		bJobPending.store(false, std::memory_order_release);
		return;
	}

	// Кинем задачу в пул потоков
	Async(EAsyncExecution::ThreadPool, [this, Frame = MoveTemp(Frame), Fs]()
		{
			// локальная копия окна Хэнна (чтобы не гоняться за shared-массивом)
			TArray<float> LocalFrame = Frame;
			TArray<float> LocalHann;
			LocalHann.SetNumUninitialized(LocalFrame.Num());
			for (int32 n = 0; n < LocalFrame.Num(); ++n)
			{
				LocalHann[n] = 0.5f * (1.f - cosf(2.f * PI * float(n) / float(LocalFrame.Num() - 1)));
				LocalFrame[n] *= LocalHann[n];
			}

			float Hz = 0.f, Prob = 0.f;
			const bool bOk = PYIN_Estimate(LocalFrame.GetData(), LocalFrame.Num(), Fs, Hz, Prob);

			if (bOk)
			{
				// сглаживание (безопасно — чистая математика)
				float NewSmoothed = Hz;
				if (bSmooth)
				{
					if (SmoothedPitch > 0.f)
					{
						NewSmoothed = SmoothAlpha * SmoothedPitch + (1.f - SmoothAlpha) * Hz;
					}
				}

				{
					FScopeLock L(&CS);
					SmoothedPitch = NewSmoothed;
					LastPitch = NewSmoothed;
					LastProb = Prob;
				}

				const float PitchToSend = NewSmoothed;
				const float ProbToSend = Prob;

				// Делегат — строго на GameThread
				AsyncTask(ENamedThreads::GameThread, [this, PitchToSend, ProbToSend]()
					{
						if (IsValid(this))
						{
							OnPitchDetected.Broadcast(PitchToSend, ProbToSend);
						}
					});
			}
			else
			{
				FScopeLock L(&CS);
				LastPitch = 0.f;
				LastProb = 0.f;
			}

			// снимаем флаг «задача активна»
			bJobPending.store(false, std::memory_order_release);
		});
}

// --- pYIN-lite: CMND-YIN + вероятностный порог + параболическая интерполяция ---
bool UPitchDetector::PYIN_Estimate(const float* x, int32 N, int32 Fs, float& OutHz, float& OutProb)
{
	OutHz = 0.f;
	OutProb = 0.f;

	if (N < 512)
	{
		return false;
	}

	int32 TauMin = FMath::FloorToInt(float(Fs) / MaxPitchHz);
	int32 TauMax = FMath::CeilToInt(float(Fs) / MinPitchHz);
	TauMin = FMath::Clamp(TauMin, 2, N / 2);
	TauMax = FMath::Clamp(TauMax, TauMin + 2, N / 2);

	// difference function d(tau)
	TArray<double> Diff;
	Diff.SetNumZeroed(TauMax + 1);
	for (int32 tau = 1; tau <= TauMax; ++tau)
	{
		double s = 0.0;
		const int32 M = N - tau;
		for (int32 i = 0; i < M; ++i)
		{
			const double d = (double)x[i] - (double)x[i + tau];
			s += d * d;
		}
		Diff[tau] = s;
	}

	// CMND-YIN
	TArray<float> Yin;
	Yin.SetNumZeroed(TauMax + 1);
	double Running = 0.0;
	for (int32 tau = 1; tau <= TauMax; ++tau)
	{
		Running += Diff[tau];
		Yin[tau] = (Running > 1e-12) ? (float)(Diff[tau] * tau / Running) : 1.f;
	}

	// Probabilities = (1 - Yin)
	TArray<float> Prob;
	Prob.SetNumZeroed(TauMax + 1);
	for (int32 tau = TauMin; tau <= TauMax; ++tau)
	{
		float p = 1.f - Yin[tau];
		Prob[tau] = FMath::Clamp(p, 0.f, 1.f);
	}

	// выбираем минимум Yin с макс. вероятностью (>= порога)
	int32 BestTau = 0;
	float BestP = ProbabilityThreshold;

	for (int32 tau = TauMin + 1; tau < TauMax - 1; ++tau)
	{
		if (Yin[tau] < Yin[tau - 1] && Yin[tau] <= Yin[tau + 1])
		{
			if (Prob[tau] > BestP)
			{
				BestP = Prob[tau];
				BestTau = tau;
			}
		}
	}

	// fallback: максимальная вероятность по всему диапазону
	if (BestTau == 0)
	{
		for (int32 tau = TauMin; tau <= TauMax; ++tau)
		{
			if (Prob[tau] > BestP)
			{
				BestP = Prob[tau];
				BestTau = tau;
			}
		}
	}

	if (BestTau == 0)
	{
		return false;
	}

	// --- БЕЗОПАСНАЯ интерполяция ---
	float RefinedTau = (float)BestTau;

	const bool bCanInterpolate =
		(BestTau > TauMin) && (BestTau < TauMax) &&
		((TauMax - TauMin) >= 2);

	if (bCanInterpolate)
	{
		float Off = 0.f;
		const float y1 = Yin[BestTau - 1];
		const float y2 = Yin[BestTau];
		const float y3 = Yin[BestTau + 1];
		ParabolicInterp(y1, y2, y3, Off);

		// ограничим смещение
		Off = FMath::Clamp(Off, -0.5f, 0.5f);
		RefinedTau = float(BestTau) + Off;
	}

	OutHz = float(Fs) / RefinedTau;
	OutProb = BestP;

	if (OutHz < MinPitchHz || OutHz > MaxPitchHz)
	{
		return false;
	}

	return true;
}

float UPitchDetector::ParabolicInterp(float y1, float y2, float y3, float& PeakOffset)
{
	const float Den = (y1 - 2.f * y2 + y3);
	if (FMath::Abs(Den) < 1e-9f)
	{
		PeakOffset = 0.f;
		return y2;
	}

	PeakOffset = 0.5f * (y1 - y3) / Den;
	return y2 - 0.25f * (y1 - y3) * PeakOffset;
}
