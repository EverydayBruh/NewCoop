#include "PcmWavUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Lightweight utilities for reading and writing PCM16 WAV (RIFF/WAVE) files.
//
// This module provides two primary functions:
// - PcmWav::LoadWavFileToPcm16: Parse a WAV file on disk and extract
//   interleaved PCM16 samples, sample rate, and channel count.
// - PcmWav::SavePcm16ToWavFile: Serialize interleaved PCM16 samples to a
//   standard RIFF/WAVE file on disk.
//
// Notes and assumptions:
// - Only uncompressed PCM format (AudioFormat = 1) is supported.
// - Only 16-bit samples are supported.
// - Only mono or stereo (1 or 2 channels) is supported.
// - Endianness: WAV is little-endian; helpers read/write LE explicitly.
// - The code performs basic validation of RIFF/WAVE headers and chunk bounds
//   and logs warnings via UE_LOG on failure, returning false.
//
// The implementation avoids allocations where possible and uses simple
// byte-wise parsing of the RIFF chunk structure.

namespace
{
    // Read 16/32-bit unsigned integers in little-endian from a byte pointer.
    inline uint16 ReadU16LE(const uint8* p) { return (uint16)(p[0] | (p[1] << 8)); }
    inline uint32 ReadU32LE(const uint8* p) { return (uint32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

    // Append 16/32-bit unsigned integers to a byte buffer in little-endian order.
    inline void WriteU16LE(TArray<uint8>& Out, uint16 v)
    {
        Out.Add((uint8)(v & 0xFF));
        Out.Add((uint8)((v >> 8) & 0xFF));
    }
    inline void WriteU32LE(TArray<uint8>& Out, uint32 v)
    {
        Out.Add((uint8)(v & 0xFF));
        Out.Add((uint8)((v >> 8) & 0xFF));
        Out.Add((uint8)((v >> 16) & 0xFF));
        Out.Add((uint8)((v >> 24) & 0xFF));
    }

    // Compare 4-byte ASCII tag at p with a null-terminated C-string tag.
    inline bool Match4(const uint8* p, const char* tag)
    {
        return p[0] == (uint8)tag[0] && p[1] == (uint8)tag[1] && p[2] == (uint8)tag[2] && p[3] == (uint8)tag[3];
    }
}

namespace PcmWav
{
    FString ResolveProjectPath_V3(const FString& InPath)
    {
        // 1) Sanitize the incoming string and normalize slashes.
        FString P = InPath;
        P.TrimStartAndEndInline();
        FPaths::NormalizeFilename(P); // converts backslashes to '/' and removes './' sequences

        // 2) If the path is already absolute, clean it up and return as-is.
        if (!FPaths::IsRelative(P))
        {
            FString Abs = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*P);
            FPaths::CollapseRelativeDirectories(Abs);
            return Abs;
        }

        // 3) Build absolute project directories that will act as base roots.
        auto AbsDir = [](const FString& Dir)
        {
            FString D = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Dir);
            FPaths::NormalizeDirectoryName(D);
            return D;
        };

        const FString SavedAbs   = AbsDir(FPaths::ProjectSavedDir());
        const FString ContentAbs = AbsDir(FPaths::ProjectContentDir());
        const FString ProjectAbs = AbsDir(FPaths::ProjectDir());

        // 4) Pick the appropriate base directory by prefix. Defaults to Saved/.
        FString Rel = P;
        FString BaseAbs;

        if (Rel.StartsWith(TEXT("Saved/"), ESearchCase::IgnoreCase))
        {
            Rel.RightChopInline(6);
            BaseAbs = SavedAbs;
        }
        else if (Rel.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
        {
            Rel.RightChopInline(8);
            BaseAbs = ContentAbs;
        }
        else if (Rel.StartsWith(TEXT("Project/"), ESearchCase::IgnoreCase))
        {
            Rel.RightChopInline(8);
            BaseAbs = ProjectAbs;
        }
        else
        {
            BaseAbs = SavedAbs; // fallback: Saved/
        }

        // 5) Combine the base path with the relative portion and collapse any dot segments.
        FString Full = FPaths::Combine(BaseAbs, Rel);
        FPaths::CollapseRelativeDirectories(Full);
        return Full;
    }


    /**
     * Load a WAV (RIFF/WAVE) file from disk and decode interleaved PCM16 samples.
     *
     * Supported formats:
     * - AudioFormat = 1 (PCM)
     * - BitsPerSample = 16
     * - Channels = 1 or 2
     *
     * On success, fills OutPcm with interleaved int16 samples, sets OutSR to the
     * sample rate and OutCh to the channel count, and returns true. On failure,
     * logs a warning and returns false with outputs cleared.
     */
    bool LoadWavFileToPcm16(const FString& InPath, TArray<int16>& OutPcm, int32& OutSR, int32& OutCh)
    {
        OutPcm.Reset(); OutSR = 0; OutCh = 0;

        const FString Path = ResolveProjectPath_V3(InPath);
        UE_LOG(LogTemp, Display, TEXT("LoadWavFileToPcm16: '%s' -> '%s'"), *InPath, *Path);

        if (!FPaths::FileExists(Path))
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: file not found: %s"), *Path);
            return false;
        }


        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path))
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: read failed: %s"), *Path);
            return false;
        }

        const uint8* p = Bytes.GetData();
        const uint8* end = p + Bytes.Num();

        // Validate RIFF/WAVE header
        if (!Match4(p, "RIFF"))
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: not RIFF %s"), *Path);
            return false;
        }
        uint32 riffSize = ReadU32LE(p + 4); (void)riffSize;
        if (!Match4(p + 8, "WAVE"))
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: not WAVE %s"), *Path);
            return false;
        }
        const uint8* cursor = p + 12;

        // Scan for required chunks: "fmt " and "data"
        bool haveFmt = false, haveData = false;
        int32 Channels = 0;
        int32 SampleRate = 0;
        int32 BitsPerSample = 0;
        const uint8* dataPtr = nullptr;
        uint32 dataSize = 0;

        while (cursor + 8 <= end)
        {
            const uint8* chunkId = cursor;
            uint32 chunkSize = ReadU32LE(cursor + 4);
            const uint8* chunkData = cursor + 8;
            const uint8* next = chunkData + chunkSize;
            if (next > end)
            {
                UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: truncated chunk"));
                return false;
            }

            if (Match4(chunkId, "fmt "))
            {
                // PCM format chunk (at least 16 bytes for PCM)
                if (chunkSize < 16)
                {
                    UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: fmt chunk too small"));
                    return false;
                }
                uint16 audioFormat = ReadU16LE(chunkData + 0);
                uint16 numChannels = ReadU16LE(chunkData + 2);
                uint32 sampleRate = ReadU32LE(chunkData + 4);
                /*uint32 byteRate    =*/ ReadU32LE(chunkData + 8);
                /*uint16 blockAlign  =*/ ReadU16LE(chunkData + 12);
                uint16 bitsPerSample = ReadU16LE(chunkData + 14);

                if (audioFormat != 1 /*PCM*/)
                {
                    UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: only PCM supported (format=%u)"), (unsigned)audioFormat);
                    return false;
                }
                if (bitsPerSample != 16)
                {
                    UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: only 16-bit PCM supported (bps=%u)"), (unsigned)bitsPerSample);
                    return false;
                }
                if (numChannels != 1 && numChannels != 2)
                {
                    UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: unsupported channels=%u"), (unsigned)numChannels);
                    return false;
                }

                Channels = (int32)numChannels;
                SampleRate = (int32)sampleRate;
                BitsPerSample = (int32)bitsPerSample;
                haveFmt = true;
            }
            else if (Match4(chunkId, "data"))
            {
                dataPtr = chunkData;
                dataSize = chunkSize;
                haveData = true;
            }

            // Chunks are word-aligned: advance by size plus pad byte if size is odd.
            cursor = next + (chunkSize & 1 ? 1 : 0);
        }

        if (!haveFmt || !haveData || dataPtr == nullptr)
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: missing fmt or data chunk"));
            return false;
        }

        if (BitsPerSample != 16 || Channels <= 0 || SampleRate <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: bad fmt parameters"));
            return false;
        }

        if (dataPtr + dataSize > end)
        {
            UE_LOG(LogTemp, Warning, TEXT("LoadWavFileToPcm16: data size out of bounds"));
            return false;
        }

        // Copy PCM payload as int16 little-endian samples (interleaved by channel).
        const int32 SampleCount = (int32)(dataSize / sizeof(int16));
        OutPcm.SetNumUninitialized(SampleCount);
        FMemory::Memcpy(OutPcm.GetData(), dataPtr, SampleCount * sizeof(int16));

        OutSR = SampleRate;
        OutCh = Channels;

        return true;
    }

    /**
     * Save interleaved PCM16 samples to a WAV (RIFF/WAVE) file on disk.
     *
     * Inputs:
     * - Pcm: interleaved int16 samples (mono or stereo).
     * - SR: sample rate in Hz (> 0).
     * - Ch: channel count (1 or 2).
     *
     * Returns true on success, false on failure (with a warning log).
     */
    bool SavePcm16ToWavFile(const FString& InPath, const TArray<int16>& Pcm, int32 SR, int32 Ch)
    {
        if (SR <= 0 || (Ch != 1 && Ch != 2))
        {
            UE_LOG(LogTemp, Warning, TEXT("SavePcm16ToWavFile: bad params SR=%d Ch=%d"), SR, Ch);
            return false;
        }

        const uint32 BitsPerSample = 16;
        const uint32 BlockAlign = (BitsPerSample / 8) * (uint32)Ch;
        const uint32 ByteRate = (uint32)SR * BlockAlign;
        const uint32 DataBytes = (uint32)(Pcm.Num() * sizeof(int16));
        const uint32 FmtChunkSize = 16; // PCM fmt chunk payload size
        // RIFF chunk size (file size - 8): "WAVE" (4) + fmt chunk (8+N) + data chunk (8+M)
        const uint32 RiffSize = 4 /*WAVE*/ + (8 + FmtChunkSize) + (8 + DataBytes);

        TArray<uint8> Out;
        Out.Reserve(12 + 8 + FmtChunkSize + 8 + DataBytes);

        // RIFF header
        Out.Append((const uint8*)"RIFF", 4);
        WriteU32LE(Out, RiffSize);
        Out.Append((const uint8*)"WAVE", 4);

        // fmt chunk (PCM)
        Out.Append((const uint8*)"fmt ", 4);
        WriteU32LE(Out, FmtChunkSize);
        WriteU16LE(Out, 1);                 // AudioFormat = PCM
        WriteU16LE(Out, (uint16)Ch);        // NumChannels
        WriteU32LE(Out, (uint32)SR);        // SampleRate
        WriteU32LE(Out, ByteRate);          // ByteRate
        WriteU16LE(Out, (uint16)BlockAlign);// BlockAlign
        WriteU16LE(Out, (uint16)BitsPerSample); // BitsPerSample

        // data chunk header
        Out.Append((const uint8*)"data", 4);
        WriteU32LE(Out, DataBytes);

        // PCM payload
        if (DataBytes > 0)
        {
            const uint8* Raw = reinterpret_cast<const uint8*>(Pcm.GetData());
            Out.Append(Raw, DataBytes);
        }

        // Ensure output directory exists before writing the file. Resolve relative
        // paths against ProjectSavedDir rather than Engine/Binaries CWD.
        const FString FullPath = ResolveProjectPath_V3(InPath);
        const FString Dir = FPaths::GetPath(FullPath);
        IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);

        if (!FFileHelper::SaveArrayToFile(Out, *FullPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("SavePcm16ToWavFile: failed to save %s"), *FullPath);
            return false;
        }

        return true;
    }
}
