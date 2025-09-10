#include "WavToolsBPLibrary.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogWavTools, Log, All);

static bool IsAbsolutePath(const FString& Path)
{
    return FPaths::IsRelative(Path) == false;
}

static int32 FindChunk(const TArray<uint8>& Bytes, const char* FourCC, int32 StartAt = 12)
{
    const int32 Size = Bytes.Num();
    for (int32 i = StartAt; i + 8 <= Size; )
    {
        const char* Tag = (const char*)&Bytes[i];
        const uint32 ChunkSize = *(const uint32*)&Bytes[i + 4];
        if (FMemory::Memcmp(Tag, FourCC, 4) == 0)
        {
            return i;
        }
        i += 8 + ChunkSize + (ChunkSize & 1); // выравнивание до 2 байт
    }
    return INDEX_NONE;
}

bool UWavToolsBPLibrary::InspectWavAtSavedPath(const FString& RelativeOrFullPath)
{
    // 1) Сконструировать полный путь
    FString FullPath;
    if (IsAbsolutePath(RelativeOrFullPath))
    {
        FullPath = RelativeOrFullPath;
    }
    else
    {
        FullPath = FPaths::Combine(FPaths::ProjectSavedDir(), RelativeOrFullPath);
    }
    FullPath = FPaths::ConvertRelativePathToFull(FullPath);

    // 2) Считать файл в память
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] Cannot read file: %s"), *FullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("WAV not found or unreadable"));
        return false;
    }

    if (Bytes.Num() < 44)
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] File too small to be a WAV: %s"), *FullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Too small for WAV header"));
        return false;
    }

    // 3) Проверка RIFF/WAVE
    const char* riff = (const char*)Bytes.GetData();
    if (FMemory::Memcmp(riff, "RIFF", 4) != 0 || FMemory::Memcmp(riff + 8, "WAVE", 4) != 0)
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] Not a RIFF/WAVE file: %s"), *FullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Not a RIFF/WAVE"));
        return false;
    }

    // 4) Найти fmt  и data чанки
    const int32 FmtPos = FindChunk(Bytes, "fmt ");
    const int32 DataPos = FindChunk(Bytes, "data");

    if (FmtPos == INDEX_NONE || DataPos == INDEX_NONE)
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] Missing fmt or data chunk: %s"), *FullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Missing fmt/data chunk"));
        return false;
    }

    const uint32 FmtSize = *(const uint32*)&Bytes[FmtPos + 4];
    const uint8* FmtData = &Bytes[FmtPos + 8];

    if (FmtSize < 16) // PCM минимум
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] fmt chunk too small: %s"), *FullPath);
        return false;
    }

    const uint16 AudioFormat = *(const uint16*)&FmtData[0];  // 1 = PCM
    const uint16 NumChannels = *(const uint16*)&FmtData[2];
    const uint32 SampleRate = *(const uint32*)&FmtData[4];
    // const uint32 ByteRate    = *(const uint32*)&FmtData[8];
    // const uint16 BlockAlign  = *(const uint16*)&FmtData[12];
    const uint16 BitsPerSample = *(const uint16*)&FmtData[14];

    const uint32 DataSize = *(const uint32*)&Bytes[DataPos + 4];

    // 5) Оценить длительность
    double DurationSec = 0.0;
    if (SampleRate > 0 && NumChannels > 0 && BitsPerSample > 0)
    {
        const double BytesPerSecond = (double)SampleRate * NumChannels * (BitsPerSample / 8.0);
        if (BytesPerSecond > 0.0)
        {
            DurationSec = (double)DataSize / BytesPerSecond;
        }
    }

    // 6) Печать в лог и на экран
    UE_LOG(LogWavTools, Display, TEXT("[WavTools] File: %s"), *FullPath);
    UE_LOG(LogWavTools, Display, TEXT("[WavTools] Format=%s, Channels=%d, SampleRate=%u Hz, BitsPerSample=%d, Data=%u B, Duration=%.3f s"),
        (AudioFormat == 1 ? TEXT("PCM") : TEXT("Non-PCM")),
        (int32)NumChannels, SampleRate, (int32)BitsPerSample, DataSize, DurationSec);

    if (GEngine)
    {
        FString Msg = FString::Printf(TEXT("WAV: %s | %s | %d ch | %u Hz | %d bit | %.2f s"),
            *FPaths::GetCleanFilename(FullPath),
            (AudioFormat == 1 ? TEXT("PCM") : TEXT("Non-PCM")),
            (int32)NumChannels, SampleRate, (int32)BitsPerSample, DurationSec);
        GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Green, Msg);
    }

    return true;
}


static uint32 ReadLE32(const uint8* p) { uint32 v; FMemory::Memcpy(&v, p, 4); return v; }
static uint16 ReadLE16(const uint8* p) { uint16 v; FMemory::Memcpy(&v, p, 2); return v; }
static void   WriteLE32(uint8* p, uint32 v) { FMemory::Memcpy(p, &v, 4); }

bool UWavToolsBPLibrary::ReverseWavAtSavedPath(const FString& RelativeOrFullPath, FString& OutReversedFullPath, bool bOverwriteExisting)
{
    // 1) Полный путь на диск (Saved/ или абсолютный)
    FString InFullPath = RelativeOrFullPath;
    if (FPaths::IsRelative(InFullPath))
    {
        InFullPath = FPaths::Combine(FPaths::ProjectSavedDir(), InFullPath);
    }
    InFullPath = FPaths::ConvertRelativePathToFull(InFullPath);

    // 2) Чтение файла в память
    TArray<uint8> InBytes;
    if (!FFileHelper::LoadFileToArray(InBytes, *InFullPath) || InBytes.Num() < 12)
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] Cannot read WAV: %s"), *InFullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red, TEXT("WAV not found/unreadable"));
        return false;
    }

    // 3) Проверка RIFF/WAVE
    if (InBytes.Num() < 12 || FMemory::Memcmp(InBytes.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(InBytes.GetData() + 8, "WAVE", 4) != 0)
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] Not a RIFF/WAVE: %s"), *InFullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red, TEXT("Not a RIFF/WAVE"));
        return false;
    }

    // 4) Будущий выходной массив
    TArray<uint8> OutBytes;
    OutBytes.Reserve(InBytes.Num());
    // Скопировать RIFF+WAVE (12 байт). Размер RIFF обновим позже.
    OutBytes.Append(InBytes.GetData(), 12);

    // Поля из fmt
    uint16 NumChannels = 0;
    uint16 BitsPerSample = 0;
    uint32 SampleRate = 0;
    uint16 AudioFormat = 0;

    // 5) Обход чанков в памяти
    int32 pos = 12;
    const int32 N = InBytes.Num();

    auto AppendChunkHeader = [&](const char* FourCC, uint32 Size)
        {
            OutBytes.Append((const uint8*)FourCC, 4);
            const int32 idx = OutBytes.AddUninitialized(4);
            WriteLE32(OutBytes.GetData() + idx, Size);
        };

    while (pos + 8 <= N)
    {
        const char* Tag = (const char*)(InBytes.GetData() + pos);
        uint32 ChunkSize = ReadLE32(InBytes.GetData() + pos + 4);
        int32 DataStart = pos + 8;
        if (DataStart + (int32)ChunkSize > N) {
            UE_LOG(LogWavTools, Error, TEXT("[WavTools] Broken chunk size, abort"));
            return false;
        }

        // fmt
        if (FMemory::Memcmp(Tag, "fmt ", 4) == 0)
        {
            if (ChunkSize < 16) { UE_LOG(LogWavTools, Error, TEXT("[WavTools] fmt too small")); return false; }

            const uint8* Fmt = InBytes.GetData() + DataStart;
            AudioFormat = ReadLE16(Fmt + 0); // 1 = PCM
            NumChannels = ReadLE16(Fmt + 2);
            SampleRate = ReadLE32(Fmt + 4);
            BitsPerSample = ReadLE16(Fmt + 14);

            // Копируем fmt как есть
            AppendChunkHeader("fmt ", ChunkSize);
            OutBytes.Append(Fmt, ChunkSize);
        }
        // data
        else if (FMemory::Memcmp(Tag, "data", 4) == 0)
        {
            // Проверим, что fmt уже прочитан
            if (NumChannels == 0 || BitsPerSample == 0)
            {
                UE_LOG(LogWavTools, Warning, TEXT("[WavTools] 'data' before 'fmt ' — attempting default parse"));
            }

            if (AudioFormat != 1)
            {
                UE_LOG(LogWavTools, Warning, TEXT("[WavTools] Non-PCM data — reversing bytes may produce noise"));
            }

            // Реверс по фреймам
            const int32 BytesPerSample = BitsPerSample / 8;
            const int32 FrameSize = (NumChannels > 0 && BytesPerSample > 0) ? (NumChannels * BytesPerSample) : 0;

            if (FrameSize <= 0 || (ChunkSize % FrameSize) != 0)
            {
                UE_LOG(LogWavTools, Error, TEXT("[WavTools] Bad frame geometry (channels/bits), abort"));
                return false;
            }

            const int32 NumFrames = (int32)ChunkSize / FrameSize;
            TArray<uint8> Reversed;
            Reversed.SetNumUninitialized(ChunkSize);

            const uint8* InAudio = InBytes.GetData() + DataStart;

            // Копируем фреймы в обратном порядке
            for (int32 i = 0; i < NumFrames; ++i)
            {
                const int32 Src = i * FrameSize;
                const int32 Dst = (NumFrames - 1 - i) * FrameSize;
                FMemory::Memcpy(Reversed.GetData() + Dst, InAudio + Src, FrameSize);
            }

            // Записать data-чанк
            AppendChunkHeader("data", ChunkSize);
            OutBytes.Append(Reversed.GetData(), ChunkSize);
        }
        // любые другие чанки — копируем как есть
        else
        {
            AppendChunkHeader(Tag, ChunkSize);
            OutBytes.Append(InBytes.GetData() + DataStart, ChunkSize);
        }

        // padding (чётность размера)
        if ((ChunkSize & 1) != 0)
        {
            // во входе пропускаем 1 байт выравнивания
            if (DataStart + (int32)ChunkSize < N) { /* вход мог иметь 1 паддинг-байт */ }
            pos = DataStart + ChunkSize + 1;

            // в выход добавляем 0 как паддинг
            OutBytes.Add(0);
        }
        else
        {
            pos = DataStart + ChunkSize;
        }
    }

    // 6) Обновить RIFF size (offset 4: размер всего файла минус 8)
    if (OutBytes.Num() >= 8)
    {
        const uint32 RiffSize = (uint32)(OutBytes.Num() - 8);
        WriteLE32(OutBytes.GetData() + 4, RiffSize);
    }

    // 7) Имя выходного файла
    const FString OutDir = FPaths::GetPath(InFullPath);
    const FString Base = FPaths::GetBaseFilename(InFullPath, /*bRemovePath=*/false);
    const FString Stem = FPaths::GetBaseFilename(InFullPath); // без расширения и пути
    const FString OutName = Stem + TEXT("_reversed.wav");
    FString OutFullPath = FPaths::Combine(OutDir, OutName);

    if (!bOverwriteExisting)
    {
        // сделаем уникальное имя, если занято
        int32 Suffix = 1;
        while (FPaths::FileExists(OutFullPath))
        {
            OutFullPath = FPaths::Combine(OutDir, Stem + FString::Printf(TEXT("_reversed_%d.wav"), Suffix++));
        }
    }

    // 8) Сохранение
    if (!FFileHelper::SaveArrayToFile(OutBytes, *OutFullPath))
    {
        UE_LOG(LogWavTools, Error, TEXT("[WavTools] Failed to save: %s"), *OutFullPath);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red, TEXT("Failed to save reversed WAV"));
        return false;
    }

    OutReversedFullPath = OutFullPath;

    UE_LOG(LogWavTools, Display, TEXT("[WavTools] Reversed saved: %s"), *OutFullPath);
    if (GEngine)
    {
        const FString Msg = FString::Printf(TEXT("Saved: %s"), *FPaths::GetCleanFilename(OutFullPath));
        GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Green, Msg);
    }
    return true;
}
