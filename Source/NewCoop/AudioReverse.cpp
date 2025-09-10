#include "AudioReverse.h"
#include "Sound/SoundWave.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogAudioReverse, Log, All);

// ---------------- UReversedProcedural ----------------

void UReversedProcedural::InitFromPCM(const TArray<uint8>& InPCMBytes, int32 InSampleRate, int32 InNumChannels, float InDurationSeconds)
{
    PCMData = InPCMBytes;
    ReadOffsetBytes = 0;

    SetSampleRate(InSampleRate);
    NumChannels = InNumChannels;
    Duration = InDurationSeconds;
    bLooping = false;

    UE_LOG(LogAudioReverse, Log, TEXT("UReversedProcedural::InitFromPCM: sampleRate=%d channels=%d duration=%.3f bytes=%d"),
        InSampleRate, InNumChannels, InDurationSeconds, PCMData.Num());
}

int32 UReversedProcedural::GeneratePCMData(uint8* OutAudio, const int32 NumBytes)
{
    if (PCMData.Num() == 0 || ReadOffsetBytes >= PCMData.Num())
    {
        return 0;
    }

    const int32 Remaining = PCMData.Num() - ReadOffsetBytes;
    const int32 ToCopy = FMath::Min(Remaining, NumBytes);
    FMemory::Memcpy(OutAudio, PCMData.GetData() + ReadOffsetBytes, ToCopy);
    ReadOffsetBytes += ToCopy;
    return ToCopy;
}

bool UReversedProcedural::InitFromSoundWave(USoundWave* InSound)
{
    if (!InSound)
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("InitFromSoundWave: InSound is nullptr"));
        return false;
    }

    if (!InSound->IsPlayable())
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("InitFromSoundWave: InSound is not playable"));
        return false;
    }


    FAudioDeviceHandle AudioDevice = GEngine ? GEngine->GetMainAudioDevice() : FAudioDeviceHandle();
    if (!AudioDevice.IsValid())
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("InitFromSoundWave: no AudioDevice available"));
        return false;
    }

    FName Format = TEXT("PCM");
    UE_LOG(LogAudioReverse, Log, TEXT("InitFromSoundWave: Initializing with format %s"), *Format.ToString());
    InSound->InitAudioResource(Format);


    if (!InSound->RawPCMData || InSound->RawPCMDataSize <= 0)
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("InitFromSoundWave: InSound or RawPCMData is empty!"));
        return false;
    }

    UE_LOG(LogAudioReverse, Log, TEXT("InitFromSoundWave: RawPCMData present, size=%d"), InSound->RawPCMDataSize);

    TArray<uint8> RawBytes;
    RawBytes.Append(InSound->RawPCMData, InSound->RawPCMDataSize);

    const int32 NumChannelsLocal = InSound->NumChannels;
    const int32 LocalSampleRate = InSound->GetSampleRateForCurrentPlatform();
    const float SoundDuration = InSound->Duration;


    const int32 BytesPerSample = sizeof(int16); // ќжидаем 16-битный PCM
    const int32 FrameSize = NumChannelsLocal * BytesPerSample;
    if (FrameSize <= 0 || (RawBytes.Num() % FrameSize) != 0)
    {

        const int32 FloatBytesPerSample = sizeof(float);
        const int32 FloatFrameSize = NumChannelsLocal * FloatBytesPerSample;
        if (FloatFrameSize > 0 && (RawBytes.Num() % FloatFrameSize) == 0)
        {
            UE_LOG(LogAudioReverse, Log, TEXT("InitFromSoundWave: Detected 32-bit float format, converting to 16-bit PCM"));
            TArray<uint8> ConvertedPCM;
            ConvertedPCM.SetNumUninitialized(RawBytes.Num() / 2);
            for (int32 i = 0; i < RawBytes.Num() / FloatBytesPerSample; ++i)
            {
                float* FloatData = reinterpret_cast<float*>(RawBytes.GetData());
                int16* ConvertedPCMData = reinterpret_cast<int16*>(ConvertedPCM.GetData());
                ConvertedPCMData[i] = static_cast<int16>(FloatData[i] * 32767.0f);
            }
            RawBytes = MoveTemp(ConvertedPCM);
        }
        else
        {
            UE_LOG(LogAudioReverse, Warning, TEXT("InitFromSoundWave: unexpected PCM layout (FrameSize=%d, totalBytes=%d)"), FrameSize, RawBytes.Num());
            return false;
        }
    }

    const int32 NumFrames = RawBytes.Num() / FrameSize;
    TArray<uint8> Reversed;
    Reversed.SetNumUninitialized(RawBytes.Num());
    for (int32 i = 0; i < NumFrames; ++i)
    {
        const int32 SrcOff = i * FrameSize;
        const int32 DstOff = (NumFrames - 1 - i) * FrameSize;
        FMemory::Memcpy(Reversed.GetData() + DstOff, RawBytes.GetData() + SrcOff, FrameSize);
    }

    InitFromPCM(Reversed, LocalSampleRate, NumChannelsLocal, SoundDuration);
    return true;
}

// ---------------- UAudioReverse (Blueprint function) ----------------

USoundWaveProcedural* UAudioReverse::CreateReversedSoundWave(USoundWave* Source)
{
    if (!Source)
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("CreateReversedSoundWave: Source is nullptr"));
        return nullptr;
    }

    UE_LOG(LogAudioReverse, Log, TEXT("CreateReversedSoundWave: requested for SoundWave %s"), *Source->GetName());

    if (!Source->IsPlayable())
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("CreateReversedSoundWave: Source is not playable"));
        return nullptr;
    }

    FAudioDeviceHandle AudioDevice = GEngine ? GEngine->GetMainAudioDevice() : FAudioDeviceHandle();
    if (!AudioDevice.IsValid())
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("CreateReversedSoundWave: no AudioDevice available"));
        return nullptr;
    }

    FName Format = TEXT("PCM");
    UE_LOG(LogAudioReverse, Log, TEXT("CreateReversedSoundWave: Initializing with format %s"), *Format.ToString());
    Source->InitAudioResource(Format);

    if (!Source->RawPCMData || Source->RawPCMDataSize <= 0)
    {
        UE_LOG(LogAudioReverse, Warning, TEXT("CreateReversedSoundWave: failed to obtain RawPCMData after initialization with format %s"), *Format.ToString());
        return nullptr;
    }

    UE_LOG(LogAudioReverse, Log, TEXT("CreateReversedSoundWave: RawPCMData present, size=%d"), Source->RawPCMDataSize);

    TArray<uint8> RawBytes;
    RawBytes.Append(Source->RawPCMData, Source->RawPCMDataSize);

    const int32 NumChannelsLocal = Source->NumChannels;
    const int32 LocalSampleRate = Source->GetSampleRateForCurrentPlatform(); 
    const float SoundDuration = Source->Duration;

    const int32 BytesPerSample = sizeof(int16);
    const int32 FrameSize = NumChannelsLocal * BytesPerSample;
    if (FrameSize <= 0 || (RawBytes.Num() % FrameSize) != 0)
    {

        const int32 FloatBytesPerSample = sizeof(float);
        const int32 FloatFrameSize = NumChannelsLocal * FloatBytesPerSample;
        if (FloatFrameSize > 0 && (RawBytes.Num() % FloatFrameSize) == 0)
        {
            UE_LOG(LogAudioReverse, Log, TEXT("CreateReversedSoundWave: Detected 32-bit float format, converting to 16-bit PCM"));
            TArray<uint8> ConvertedPCM;
            ConvertedPCM.SetNumUninitialized(RawBytes.Num() / 2);
            for (int32 i = 0; i < RawBytes.Num() / FloatBytesPerSample; ++i)
            {
                float* FloatData = reinterpret_cast<float*>(RawBytes.GetData());
                int16* ConvertedPCMData = reinterpret_cast<int16*>(ConvertedPCM.GetData());
                ConvertedPCMData[i] = static_cast<int16>(FloatData[i] * 32767.0f);
            }
            RawBytes = MoveTemp(ConvertedPCM);
        }
        else
        {
            UE_LOG(LogAudioReverse, Warning, TEXT("CreateReversedSoundWave: unexpected PCM layout after initialization (FrameSize=%d total=%d)"), FrameSize, RawBytes.Num());
            return nullptr;
        }
    }

    const int32 NumFrames = RawBytes.Num() / FrameSize;
    TArray<uint8> Reversed;
    Reversed.SetNumUninitialized(RawBytes.Num());
    for (int32 i = 0; i < NumFrames; ++i)
    {
        const int32 SrcOff = i * FrameSize;
        const int32 DstOff = (NumFrames - 1 - i) * FrameSize;
        FMemory::Memcpy(Reversed.GetData() + DstOff, RawBytes.GetData() + SrcOff, FrameSize);
    }

    UReversedProcedural* Proc = NewObject<UReversedProcedural>();
    Proc->InitFromPCM(Reversed, LocalSampleRate, NumChannelsLocal, SoundDuration);
    return Proc;
}