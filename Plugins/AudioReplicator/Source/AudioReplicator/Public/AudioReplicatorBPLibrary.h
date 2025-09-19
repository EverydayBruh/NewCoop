#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OpusTypes.h"
#include "AudioReplicatorDebugTypes.h"
#include "AudioReplicatorBPLibrary.generated.h"

UCLASS()
class AUDIOREPLICATOR_API UAudioReplicatorBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static bool LoadWavToPcm16(const FString& WavPath, TArray<int32>& OutPcm16, int32& OutSampleRate, int32& OutChannels);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static bool EncodePcm16ToOpusPackets(const TArray<int32>& Pcm16, int32 SampleRate, int32 Channels, int32 Bitrate, int32 FrameMs, TArray<FOpusPacket>& OutPackets);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static void PackOpusPackets(const TArray<FOpusPacket>& Packets, TArray<uint8>& OutBuffer);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static bool UnpackOpusPackets(const TArray<uint8>& Buffer, TArray<FOpusPacket>& OutPackets);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static bool DecodeOpusPacketsToPcm16(const TArray<FOpusPacket>& Packets, int32 SampleRate, int32 Channels, TArray<int32>& OutPcm16);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static bool SavePcm16ToWav(const FString& OutPath, const TArray<int32>& Pcm16, int32 SampleRate, int32 Channels);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Local")
    static bool TranscodeWavToOpusAndBack(const FString& InWavPath, const FString& OutWavPath, int32 Bitrate = 32000, int32 FrameMs = 20);

    UFUNCTION(BlueprintPure, Category = "AudioReplicator|Paths")
    static FString ResolveProjectPath(const FString& Path);

    UFUNCTION(BlueprintPure, Category = "AudioReplicator|Paths")
    static bool ProjectFileExists(const FString& Path);

    UFUNCTION(BlueprintPure, Category = "AudioReplicator|Paths")
    static bool ProjectDirectoryExists(const FString& Path);

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Debug")
    static FString FormatAudioTestReport(
        int32 SampleRate,
        int32 Channels,
        int32 FrameMs /*=20*/,
        int32 BitrateKbps /*=32*/,
        int32 PcmSamplesTotal,
        int32 DecPcmSamplesTotal /*=-1 if unknown*/,
        int32 BufferBytes,
        int32 PacketCount);

    UFUNCTION(BlueprintPure, Category = "AudioReplicator|Debug")
    static FString OpusStreamHeaderToString(const FOpusStreamHeader& Header);

    UFUNCTION(BlueprintPure, Category = "AudioReplicator|Debug")
    static FString FormatOutgoingDebugReport(const FAudioReplicatorOutgoingDebug& DebugInfo);

    UFUNCTION(BlueprintPure, Category = "AudioReplicator|Debug")
    static FString FormatIncomingDebugReport(const FAudioReplicatorIncomingDebug& DebugInfo);
};
