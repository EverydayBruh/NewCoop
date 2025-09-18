#pragma once

#include "CoreMinimal.h"
#include "OpusTypes.h"
#include "AudioReplicatorDebugTypes.generated.h"

/**
 * Per-chunk debug information that can be used to inspect replication progress.
 */
USTRUCT(BlueprintType)
struct FAudioReplicatorChunkDebug
{
    GENERATED_BODY()

    // Index of the chunk within the opus stream.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 Index = 0;

    // Size of the payload for this chunk in bytes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 SizeBytes = 0;

    // True if this chunk has been sent from the owner in the current session.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bIsSent = false;

    // True if this chunk has been received locally.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bIsReceived = false;
};

/**
 * Aggregated state for an outgoing transfer that is useful during debugging.
 */
USTRUCT(BlueprintType)
struct FAudioReplicatorOutgoingDebug
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    FGuid SessionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    FOpusStreamHeader Header;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 TotalChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 SentChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 PendingChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 TotalBytes = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    float EstimatedDurationSec = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    float EstimatedBitrateKbps = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bHeaderSent = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bEndSent = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 NextChunkIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bTransferComplete = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    TArray<int32> PendingChunkIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    TArray<FAudioReplicatorChunkDebug> Chunks;
};

/**
 * Aggregated state for an incoming transfer that is useful during debugging.
 */
USTRUCT(BlueprintType)
struct FAudioReplicatorIncomingDebug
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    FGuid SessionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    FOpusStreamHeader Header;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bStarted = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bEnded = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 ReceivedChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 UniqueChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 ExpectedChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 MissingChunks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    int32 TotalBytes = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    float EstimatedDurationSec = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    float EstimatedBitrateKbps = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    bool bReadyToAssemble = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    TArray<int32> MissingChunkIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Debug")
    TArray<FAudioReplicatorChunkDebug> Chunks;
};

