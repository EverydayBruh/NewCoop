#pragma once
#include "CoreMinimal.h"

namespace PcmWav
{
    /**
     * Resolve a relative or absolute WAV path against the project directories.
     *
     * Relative paths are interpreted as Saved/, Content/ or Project/ sub-paths.
     * The returned value is an absolute, normalized path on disk.
     */
    FString ResolveProjectPath_V3(const FString& Path);

    /**
     * Load a WAV (RIFF PCM 16-bit) file and output interleaved PCM16 samples.
     */
    bool LoadWavFileToPcm16(const FString& Path, TArray<int16>& OutPcm, int32& OutSR, int32& OutCh);

    /**
     * Serialize interleaved PCM16 samples to a standard WAV (RIFF PCM 16-bit) file.
     */
    bool SavePcm16ToWavFile(const FString& Path, const TArray<int16>& Pcm, int32 SR, int32 Ch);
}
