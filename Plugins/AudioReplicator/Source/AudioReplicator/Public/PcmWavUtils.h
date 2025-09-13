#pragma once
#include "CoreMinimal.h"

namespace PcmWav
{
    // ������ WAV (RIFF PCM 16-bit) � PCM16 (������ int16), ���������� SR, Ch, OK
    bool LoadWavFileToPcm16(const FString& Path, TArray<int16>& OutPcm, int32& OutSR, int32& OutCh);

    // ��������� PCM16 ��� WAV (RIFF PCM 16-bit)
    bool SavePcm16ToWavFile(const FString& Path, const TArray<int16>& Pcm, int32 SR, int32 Ch);
}
