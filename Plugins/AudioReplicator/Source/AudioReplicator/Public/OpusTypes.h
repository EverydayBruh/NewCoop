#pragma once
#include "CoreMinimal.h"
#include "OpusTypes.generated.h"

USTRUCT(BlueprintType)
struct FOpusPacket
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    TArray<uint8> Data;
};

USTRUCT(BlueprintType)
struct FOpusStreamHeader
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    int32 SampleRate = 48000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    int32 Channels = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    int32 Bitrate = 32000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    int32 FrameMs = 20;

    // Optional but handy for client-side buffering and progress tracking.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    int32 NumPackets = 0;
};

USTRUCT(BlueprintType)
struct FOpusChunk
{
    GENERATED_BODY()

    // Sequential frame index starting from zero.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    int32 Index = 0;

    // Single Opus frame payload.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator")
    FOpusPacket Packet;
};
