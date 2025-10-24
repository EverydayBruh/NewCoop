#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OpusTypes.h"
#include "AudioReplicatorDebugTypes.h"
#include "AudioReplicatorComponent.generated.h"

// Blueprint delegates for monitoring replicated Opus sessions.
class UAudioReplicatorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOpusTransferStarted, FGuid, SessionId, FOpusStreamHeader, Header);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOpusChunkReceived, FGuid, SessionId, FOpusChunk, Chunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOpusTransferEnded, UAudioReplicatorComponent*, Source, FGuid, SessionId);

USTRUCT()
struct FOutgoingTransfer
{
    GENERATED_BODY()
    FGuid SessionId;
    FOpusStreamHeader Header;
    TArray<FOpusChunk> Chunks;
    int32 NextIndex = 0;
    bool bHeaderSent = false;
    bool bEndSent = false;
};

USTRUCT()
struct FIncomingTransfer
{
    GENERATED_BODY()
    FOpusStreamHeader Header;
    TArray<FOpusPacket> Packets; // Accumulated packets for eventual decoding.
    int32 Received = 0;
    bool bStarted = false;
    bool bEnded = false;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class AUDIOREPLICATOR_API UAudioReplicatorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAudioReplicatorComponent();

    // Maximum amount of chunks to send per tick to avoid network spam.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Net")
    int32 MaxPacketsPerTick = 32;

    // Multicast events exposed to gameplay code.
    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusTransferStarted OnTransferStarted;

    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusChunkReceived OnChunkReceived;

    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusTransferEnded OnTransferEnded;

    // == Blueprint API: transfer lifecycle ==
    // 1) Broadcast already encoded Opus packets (client-side call).
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool StartBroadcastOpus(const TArray<FOpusPacket>& Packets, FOpusStreamHeader Header, FGuid SessionId, FGuid& OutSessionId);

    // 2) Broadcast from a WAV file (encode locally, then stream).
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool StartBroadcastFromWav(const FString& WavPath, int32 Bitrate, int32 FrameMs, FGuid SessionId, FGuid& OutSessionId);

    // Abort an active transfer early if required.
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    void CancelBroadcast(const FGuid& SessionId);

    // Access the received data for a session (e.g., to decode and save a WAV).
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool GetReceivedPackets(const FGuid& SessionId, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const;

    // Debug helpers that expose the current state of transfers without having to gather data manually.
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Debug")
    bool GetOutgoingDebugInfo(const FGuid& SessionId, FAudioReplicatorOutgoingDebug& OutDebug) const;

    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Debug")
    bool GetIncomingDebugInfo(const FGuid& SessionId, FAudioReplicatorIncomingDebug& OutDebug) const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // === SERVER RPC ===
    UFUNCTION(Server, Reliable)
    void Server_StartTransfer(const FGuid& SessionId, const FOpusStreamHeader& Header);

    UFUNCTION(Server, Reliable)
    void Server_SendChunk(const FGuid& SessionId, const FOpusChunk& Chunk);

    UFUNCTION(Server, Reliable)
    void Server_EndTransfer(const FGuid& SessionId);

    // === MULTICAST RPC ===
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_StartTransfer(const FGuid& SessionId, const FOpusStreamHeader& Header);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_SendChunk(const FGuid& SessionId, const FOpusChunk& Chunk);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_EndTransfer(const FGuid& SessionId);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // Pending outgoing transfers owned by the local client.
    UPROPERTY()
    TMap<FGuid, FOutgoingTransfer> Outgoing;

    // Incoming transfers assembled on this instance.
    UPROPERTY()
    TMap<FGuid, FIncomingTransfer> Incoming;

    // Helper: convert packets into indexed chunks for replication.
    static void BuildChunks(const TArray<FOpusPacket>& Packets, TArray<FOpusChunk>& OutChunks);

    // Helper: encode a WAV file into Opus packets on the client.
    bool EncodeWavToOpusPackets(const FString& WavPath, int32 Bitrate, int32 FrameMs, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const;

    bool IsOwnerClient() const;
};
