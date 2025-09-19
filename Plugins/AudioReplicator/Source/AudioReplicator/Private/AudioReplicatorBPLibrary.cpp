#include "AudioReplicatorBPLibrary.h"
#include "OpusCodec.h"
#include "PcmWavUtils.h"
#include "Chunking.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

static void Int32ToInt16(const TArray<int32>& In, TArray<int16>& Out)
{
    Out.Reset(In.Num());
    Out.AddUninitialized(In.Num());
    for (int32 i = 0; i < In.Num(); ++i)
    {
        int32 v = FMath::Clamp(In[i], -32768, 32767);
        Out[i] = (int16)v;
    }
}
static void Int16ToInt32(const TArray<int16>& In, TArray<int32>& Out)
{
    Out.Reset(In.Num());
    Out.AddUninitialized(In.Num());
    for (int32 i = 0; i < In.Num(); ++i) Out[i] = (int32)In[i];
}

static void WrapPackets(const TArray<TArray<uint8>>& In, TArray<FOpusPacket>& Out)
{
    Out.Reset(In.Num());
    for (const auto& P : In)
    {
        FOpusPacket W; W.Data = P;
        Out.Add(MoveTemp(W));
    }
}
static void UnwrapPackets(const TArray<FOpusPacket>& In, TArray<TArray<uint8>>& Out)
{
    Out.Reset(In.Num());
    for (const auto& W : In)
    {
        Out.Add(W.Data);
    }
}

FString UAudioReplicatorBPLibrary::ResolveProjectPath(const FString& Path)
{
    return PcmWav::ResolveProjectPath_V3(Path);
}

bool UAudioReplicatorBPLibrary::ProjectFileExists(const FString& Path)
{
    const FString FullPath = PcmWav::ResolveProjectPath_V3(Path);
    return FPaths::FileExists(FullPath);
}

bool UAudioReplicatorBPLibrary::ProjectDirectoryExists(const FString& Path)
{
    const FString FullPath = PcmWav::ResolveProjectPath_V3(Path);
    return IFileManager::Get().DirectoryExists(*FullPath);
}

bool UAudioReplicatorBPLibrary::LoadWavToPcm16(const FString& WavPath, TArray<int32>& OutPcm16, int32& OutSampleRate, int32& OutChannels)
{
    TArray<int16> Pcm;
    if (!PcmWav::LoadWavFileToPcm16(WavPath, Pcm, OutSampleRate, OutChannels)) return false;
    Int16ToInt32(Pcm, OutPcm16);
    return true;
}

bool UAudioReplicatorBPLibrary::EncodePcm16ToOpusPackets(const TArray<int32>& Pcm16, int32 SR, int32 Ch, int32 Bitrate, int32 FrameMs, TArray<FOpusPacket>& OutPackets)
{
    const int32 FrameSize = (SR / 1000) * FrameMs; // per channel
    TArray<int16> Pcm16s; Int32ToInt16(Pcm16, Pcm16s);

    auto Codec = FOpusCodec::Create(SR, Ch, Bitrate);
    if (!Codec) return false;

    TArray<TArray<uint8>> RawPackets;
    if (!Codec->EncodePcm16ToPackets(Pcm16s, FrameSize, RawPackets)) return false;

    WrapPackets(RawPackets, OutPackets);
    return true;
}

void UAudioReplicatorBPLibrary::PackOpusPackets(const TArray<FOpusPacket>& Packets, TArray<uint8>& OutBuffer)
{
    Chunking::PackWithLengths(Packets, OutBuffer);
}

bool UAudioReplicatorBPLibrary::UnpackOpusPackets(const TArray<uint8>& Buffer, TArray<FOpusPacket>& OutPackets)
{
    return Chunking::UnpackWithLengths(Buffer, OutPackets);
}

bool UAudioReplicatorBPLibrary::DecodeOpusPacketsToPcm16(const TArray<FOpusPacket>& Packets, int32 SR, int32 Ch, TArray<int32>& OutPcm16)
{
    auto Codec = FOpusCodec::Create(SR, Ch, 32000);
    if (!Codec) return false;

    TArray<TArray<uint8>> RawPackets;
    UnwrapPackets(Packets, RawPackets);

    TArray<int16> Pcm;
    if (!Codec->DecodePacketsToPcm16(RawPackets, Pcm)) return false;
    Int16ToInt32(Pcm, OutPcm16);
    return true;
}

bool UAudioReplicatorBPLibrary::SavePcm16ToWav(const FString& OutPath, const TArray<int32>& Pcm16, int32 SR, int32 Ch)
{
    TArray<int16> Pcm16s; Int32ToInt16(Pcm16, Pcm16s);
    return PcmWav::SavePcm16ToWavFile(OutPath, Pcm16s, SR, Ch);
}

bool UAudioReplicatorBPLibrary::TranscodeWavToOpusAndBack(const FString& InWavPath, const FString& OutWavPath, int32 Bitrate, int32 FrameMs)
{
    int32 SR = 0, Ch = 0;
    TArray<int32> Pcm;
    if (!LoadWavToPcm16(InWavPath, Pcm, SR, Ch)) return false;

    TArray<FOpusPacket> Packets;
    if (!EncodePcm16ToOpusPackets(Pcm, SR, Ch, Bitrate, FrameMs, Packets)) return false;

    TArray<int32> DecPcm;
    if (!DecodeOpusPacketsToPcm16(Packets, SR, Ch, DecPcm)) return false;

    return SavePcm16ToWav(OutWavPath, DecPcm, SR, Ch);
}


static FString FmtF(double V, int32 Dec = 2) { return FString::Printf(TEXT("%.*f"), Dec, V); }

FString UAudioReplicatorBPLibrary::FormatAudioTestReport(
    int32 SampleRate,
    int32 Channels,
    int32 FrameMs,
    int32 BitrateKbps,
    int32 PcmSamplesTotal,
    int32 DecPcmSamplesTotal,
    int32 BufferBytes,
    int32 PacketCount)
{
    // Sanity checks and preparation
    const int32 Ch = FMath::Max(1, Channels);
    const int32 SR = FMath::Max(1, SampleRate);
    const int32 FrmMs = FMath::Clamp(FrameMs, 2, 120); // typically 2.5/5/10/20/40/60
    const double Den = double(SR) * double(Ch);

    const int32 FrameSampPerCh = (SR / 1000) * FrmMs;
    const int32 FrameSampTotal = FrameSampPerCh * Ch;

    // Durations
    const double DurInSec = Den > 0.0 ? double(PcmSamplesTotal) / Den : 0.0;
    const double DurOutSec = (DecPcmSamplesTotal >= 0 && Den > 0.0) ? double(DecPcmSamplesTotal) / Den : -1.0;

    // Tail samples that do not align with a full frame
    const int32 TailSamples = (FrameSampTotal > 0) ? (PcmSamplesTotal % FrameSampTotal) : 0;
    const double TailMs = Den > 0.0 ? (double)TailSamples * 1000.0 / Den : 0.0;

    // Sizes and compression
    const int64 PcmBytes = int64(PcmSamplesTotal) * 2; // int16
    const double Ratio = (PcmBytes > 0) ? (double)BufferBytes / (double)PcmBytes : 0.0; // <1 is better
    const double SavedPct = (PcmBytes > 0) ? (1.0 - Ratio) * 100.0 : 0.0;

    // Packets
    const double AvgPktBytes = (PacketCount > 0) ? double(BufferBytes) / double(PacketCount) : 0.0;
    const double PktsPerSec = (DurInSec > 0.0) ? double(PacketCount) / DurInSec : 0.0;
    const double ExpPktCount = (DurInSec > 0.0 && FrmMs > 0) ? DurInSec * (1000.0 / double(FrmMs)) : 0.0;
    const double PktCountDiff = double(PacketCount) - ExpPktCount;

    // "Effective" average bitrate based on the resulting buffer
    const double EffKbps = (DurInSec > 0.0) ? (double(BufferBytes) * 8.0 / DurInSec) / 1000.0 : 0.0;

    // Summary
    FString Out;
    Out += TEXT("=== Audio Replicator · Local Test ===\n");
    Out += FString::Printf(TEXT("SR=%d Hz  Ch=%d  Frame=%d ms  Target Bitrate≈%d bps\n"),
        SR, Ch, FrmMs, BitrateKbps);
    Out += FString::Printf(TEXT("PCM: Samples=%d  Bytes=%lld  Dur≈%s s\n"),
        PcmSamplesTotal, (long long)PcmBytes, *FmtF(DurInSec, 3));
    if (DurOutSec >= 0.0)
    {
        Out += FString::Printf(TEXT("Decoded: Samples=%d  Dur≈%s s  Δdur≈%s s\n"),
            DecPcmSamplesTotal,
            *FmtF(DurOutSec, 3),
            *FmtF(DurOutSec - DurInSec, 3));
    }
    Out += FString::Printf(TEXT("Tail (non-aligned to frame): %d samp  ≈%s ms\n"),
        TailSamples, *FmtF(TailMs, 2));

    Out += TEXT("\n--- Compression ---\n");
    Out += FString::Printf(TEXT("Opus Buffer: %d bytes  Packets: %d  AvgPkt≈%s B\n"),
        BufferBytes, PacketCount, *FmtF(AvgPktBytes, 1));
    Out += FString::Printf(TEXT("Ratio (buf/pcm)≈ %s   Saved≈ %s %%\n"),
        *FmtF(Ratio, 3), *FmtF(SavedPct, 1));
    Out += FString::Printf(TEXT("Eff. bitrate≈ %s kbps (based on buffer size and duration)\n"),
        *FmtF(EffKbps, 1));

    Out += TEXT("\n--- Packetization ---\n");
    Out += FString::Printf(TEXT("Pkts/sec≈ %s   Expected≈ %s   Δ≈ %s\n"),
        *FmtF(PktsPerSec, 2), *FmtF(ExpPktCount, 1), *FmtF(PktCountDiff, 1));

    Out += TEXT("\nHint: Δ≈0 and a small tail are expected. Large |Δ| or tail → check frame alignment and FrameMs.\n");
    return Out;
}

FString UAudioReplicatorBPLibrary::OpusStreamHeaderToString(const FOpusStreamHeader& Header)
{
    return FString::Printf(TEXT("Opus Header: SR=%d Hz  Ch=%d  Bitrate=%d bps  Frame=%d ms  Packets=%d"),
        Header.SampleRate,
        Header.Channels,
        Header.Bitrate,
        Header.FrameMs,
        Header.NumPackets);
}

static FString JoinIntArray(const TArray<int32>& Values)
{
    FString Result;
    for (int32 i = 0; i < Values.Num(); ++i)
    {
        Result += FString::Printf(TEXT("%d"), Values[i]);
        if (i + 1 < Values.Num())
        {
            Result += TEXT(", ");
        }
    }
    return Result;
}

FString UAudioReplicatorBPLibrary::FormatOutgoingDebugReport(const FAudioReplicatorOutgoingDebug& DebugInfo)
{
    FString Out;
    Out += TEXT("=== Audio Replicator · Outgoing ===\n");
    Out += FString::Printf(TEXT("Session: %s\n"), *DebugInfo.SessionId.ToString(EGuidFormats::DigitsWithHyphens));
    Out += FString::Printf(TEXT("%s\n"), *OpusStreamHeaderToString(DebugInfo.Header));
    Out += FString::Printf(TEXT("Chunks: total=%d  sent=%d  pending=%d  next=%d\n"),
        DebugInfo.TotalChunks,
        DebugInfo.SentChunks,
        DebugInfo.PendingChunks,
        DebugInfo.NextChunkIndex);
    Out += FString::Printf(TEXT("Buffer: %d bytes  Dur≈%s s  Bitrate≈%s kbps\n"),
        DebugInfo.TotalBytes,
        *FmtF(DebugInfo.EstimatedDurationSec, 3),
        *FmtF(DebugInfo.EstimatedBitrateKbps, 2));
    Out += FString::Printf(TEXT("HeaderSent=%s  EndSent=%s  Completed=%s\n"),
        DebugInfo.bHeaderSent ? TEXT("true") : TEXT("false"),
        DebugInfo.bEndSent ? TEXT("true") : TEXT("false"),
        DebugInfo.bTransferComplete ? TEXT("true") : TEXT("false"));

    if (DebugInfo.PendingChunkIndices.Num() > 0)
    {
        Out += FString::Printf(TEXT("Pending indices: %s\n"), *JoinIntArray(DebugInfo.PendingChunkIndices));
    }

    Out += TEXT("\n--- Chunk Details ---\n");
    for (const FAudioReplicatorChunkDebug& Chunk : DebugInfo.Chunks)
    {
        Out += FString::Printf(TEXT("[%d] size=%d B  sent=%s\n"),
            Chunk.Index,
            Chunk.SizeBytes,
            Chunk.bIsSent ? TEXT("yes") : TEXT("no"));
    }

    return Out;
}

FString UAudioReplicatorBPLibrary::FormatIncomingDebugReport(const FAudioReplicatorIncomingDebug& DebugInfo)
{
    FString Out;
    Out += TEXT("=== Audio Replicator · Incoming ===\n");
    Out += FString::Printf(TEXT("Session: %s\n"), *DebugInfo.SessionId.ToString(EGuidFormats::DigitsWithHyphens));
    Out += FString::Printf(TEXT("%s\n"), *OpusStreamHeaderToString(DebugInfo.Header));
    Out += FString::Printf(TEXT("State: Started=%s  Ended=%s  Ready=%s\n"),
        DebugInfo.bStarted ? TEXT("true") : TEXT("false"),
        DebugInfo.bEnded ? TEXT("true") : TEXT("false"),
        DebugInfo.bReadyToAssemble ? TEXT("true") : TEXT("false"));
    Out += FString::Printf(TEXT("Chunks: received-msgs=%d  unique=%d  expected=%d  missing=%d\n"),
        DebugInfo.ReceivedChunks,
        DebugInfo.UniqueChunks,
        DebugInfo.ExpectedChunks,
        DebugInfo.MissingChunks);
    Out += FString::Printf(TEXT("Buffer: %d bytes  Dur≈%s s  Bitrate≈%s kbps\n"),
        DebugInfo.TotalBytes,
        *FmtF(DebugInfo.EstimatedDurationSec, 3),
        *FmtF(DebugInfo.EstimatedBitrateKbps, 2));

    if (DebugInfo.MissingChunkIndices.Num() > 0)
    {
        Out += FString::Printf(TEXT("Missing indices: %s\n"), *JoinIntArray(DebugInfo.MissingChunkIndices));
    }

    Out += TEXT("\n--- Chunk Details ---\n");
    for (const FAudioReplicatorChunkDebug& Chunk : DebugInfo.Chunks)
    {
        Out += FString::Printf(TEXT("[%d] size=%d B  received=%s\n"),
            Chunk.Index,
            Chunk.SizeBytes,
            Chunk.bIsReceived ? TEXT("yes") : TEXT("no"));
    }

    return Out;
}
