#pragma once
#include "CoreMinimal.h"

namespace PcmWav
{
    // Читает WAV (RIFF PCM 16-bit) в PCM16 (массив int16), возвращает SR, Ch, OK
    bool LoadWavFileToPcm16(const FString& Path, TArray<int16>& OutPcm, int32& OutSR, int32& OutCh);

    // Сохраняет PCM16 как WAV (RIFF PCM 16-bit)
    bool SavePcm16ToWavFile(const FString& Path, const TArray<int16>& Pcm, int32 SR, int32 Ch);
}
