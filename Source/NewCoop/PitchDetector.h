#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ISubmixBufferListener.h"
#include "Sound/SoundSubmix.h"
#include <atomic>
#include "PitchDetector.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPitchDetected, float, PitchHz, float, Confidence);

/**
 * UPitchDetector — детектор высоты тона (pYIN-lite) из выбранного Sound Submix.
 * Возвращает Pitch (Гц) и Confidence [0..1].
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class NEWCOOP_API UPitchDetector : public UActorComponent
{
	GENERATED_BODY()

public:
	UPitchDetector();

	/** Сабмикс, куда отправлён микрофон (AudioCapture → Submix Sends). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pitch|Input")
	TObjectPtr<USoundSubmix> TargetSubmix = nullptr;

	/** Размер окна анализа, сэмплы (рекоменд. 2048; 512..8192). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Analysis", meta = (ClampMin = "512", ClampMax = "8192"))
	int32 WindowSize = 2048;

	/** Шаг (hop) между окнами, сэмплы (рекоменд. 256..512). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Analysis", meta = (ClampMin = "64"))
	int32 HopSize = 512;

	/** Диапазон поиска частоты (Гц). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Analysis", meta = (ClampMin = "10.0"))
	float MinPitchHz = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Analysis", meta = (ClampMin = "20.0"))
	float MaxPitchHz = 1000.f;

	/** Порог вероятности pYIN (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Analysis", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProbabilityThreshold = 0.8f;

	/** Экспоненциальное сглаживание частоты. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Smoothing")
	bool bSmooth = true;

	/** Коэффициент сглаживания [0..1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pitch|Smoothing", meta = (EditCondition = "bSmooth", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothAlpha = 0.6f;

	/** Управление. */
	UFUNCTION(BlueprintCallable, Category = "Pitch") void Start();
	UFUNCTION(BlueprintCallable, Category = "Pitch") void Stop();
	UFUNCTION(BlueprintCallable, Category = "Pitch") bool IsRunning() const { return bRunning; }

	/** Текущее значение. */
	UFUNCTION(BlueprintCallable, Category = "Pitch") void GetPitch(float& OutPitchHz, float& OutConfidence) const;

	/** Делегат на новое значение. */
	UPROPERTY(BlueprintAssignable, Category = "Pitch")
	FOnPitchDetected OnPitchDetected;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Внутренний слушатель сабмикса. Хранит слабую ссылку на компонент. */
	struct FSubmixListener : public ISubmixBufferListener
	{
		TWeakObjectPtr<UPitchDetector> Owner;
		explicit FSubmixListener(UPitchDetector* InOwner) : Owner(InOwner) {}

		virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix,
			float* AudioData, int32 NumSamples, int32 NumChannels,
			const int32 SampleRate, double AudioClock) override;

		virtual bool IsRenderingAudio() const override { return true; }

		virtual const FString& GetListenerName() const override
		{
			static FString Name(TEXT("UPitchDetector"));
			return Name;
		}
	};

	TSharedPtr<FSubmixListener, ESPMode::ThreadSafe> Listener;

	// --- синхронизация / буферы ---
	mutable FCriticalSection CS;   // для доступа к MonoRing/LastPitch/LastProb
	TArray<float> MonoRing;        // кольцевой буфер (моно float)
	int32         RingWrite = 0;
	int32         SampleRate = 48000;
	bool          bRunning = false;

	// окно Хэнна (используем только в воркере)
	TArray<float> Hann;
	void EnsureHann(int32 N);

	// состояние выхода
	float LastPitch = 0.f;
	float LastProb = 0.f;
	float SmoothedPitch = 0.f;

	// временный буфер под моно-кадр для микса (аудио-поток)
	TArray<float> TempFrame;

	// учёт хоппинга и одиночной задачи анализа
	std::atomic<int32> SamplesAccum;     // накоплено новых сэмплов
	std::atomic<bool>  bJobPending;      // есть запущенная задача анализа

	// регистрация слушателя
	void Register();
	void Unregister();

	// аудио-поток: приём новых данных и запись в кольцо
	void OnSubmixThreadSafe(float* AudioData, int32 NumSamples, int32 NumChannels, int32 InSampleRate);

	// попытка запустить анализ в отдельном потоке (если накопили >= HopSize и нет активной задачи)
	void TryDispatchAnalysis();

	// сбор окна из кольцевого буфера в локальный массив (без окна Хэнна)
	bool SnapshotLatestFrame(TArray<float>& OutFrame, int32& OutFs);

	// pYIN-lite
	bool PYIN_Estimate(const float* x, int32 N, int32 Fs, float& OutHz, float& OutProb);

	// утилиты
	static float ParabolicInterp(float y1, float y2, float y3, float& PeakOffset);
};
