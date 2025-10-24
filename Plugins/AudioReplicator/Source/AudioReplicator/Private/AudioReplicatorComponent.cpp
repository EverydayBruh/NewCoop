#include "AudioReplicatorComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "AudioReplicatorBPLibrary.h" // leverage local blueprint helpers for encoding/decoding

UAudioReplicatorComponent::UAudioReplicatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);
}

void UAudioReplicatorComponent::BeginPlay()
{
    Super::BeginPlay();
}

bool UAudioReplicatorComponent::IsOwnerClient() const
{
    const AActor* Owner = GetOwner();
    if (!Owner) return false;
    const ENetMode NetMode = GetNetMode();
    if (NetMode == NM_Standalone)
    {
        // Standalone games have no networking but should still allow local broadcasts.
        return true;
    }

    const ENetRole LocalRole = Owner->GetLocalRole();
    if (LocalRole == ROLE_AutonomousProxy || LocalRole == ROLE_SimulatedProxy)
    {
        return true;
    }

    if (LocalRole == ROLE_Authority && Owner->HasLocalNetOwner())
    {
        // Listen servers report ROLE_Authority but the owning controller is local; treat as client-owned.
        return true;
    }

    return false;
}

void UAudioReplicatorComponent::BuildChunks(const TArray<FOpusPacket>& Packets, TArray<FOpusChunk>& OutChunks)
{
    OutChunks.Reset(Packets.Num());
    int32 idx = 0;
    for (const FOpusPacket& P : Packets)
    {
        FOpusChunk C;
        C.Index = idx++;
        C.Packet = P;
        OutChunks.Add(MoveTemp(C));
    }
}

bool UAudioReplicatorComponent::EncodeWavToOpusPackets(const FString& WavPath, int32 Bitrate, int32 FrameMs, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const
{
    int32 SR = 0, Ch = 0;
    TArray<int32> Pcm;
    if (!UAudioReplicatorBPLibrary::LoadWavToPcm16(WavPath, Pcm, SR, Ch))
        return false;

    OutHeader.SampleRate = SR;
    OutHeader.Channels = Ch;
    OutHeader.Bitrate = Bitrate;
    OutHeader.FrameMs = FrameMs;

    if (!UAudioReplicatorBPLibrary::EncodePcm16ToOpusPackets(Pcm, SR, Ch, Bitrate, FrameMs, OutPackets))
        return false;

    OutHeader.NumPackets = OutPackets.Num();
    return true;
}

bool UAudioReplicatorComponent::StartBroadcastOpus(const TArray<FOpusPacket>& Packets, FOpusStreamHeader Header, FGuid SessionId, FGuid& OutSessionId)
{
    if (!IsOwnerClient())
    {
        UE_LOG(LogTemp, Warning, TEXT("StartBroadcastOpus: must be called on owning client"));
        return false;
    }
    if (Packets.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("StartBroadcastOpus: empty packet list"));
        return false;
    }

    FGuid EffectiveSessionId = SessionId;
    if (!EffectiveSessionId.IsValid())
    {
        EffectiveSessionId = FGuid::NewGuid();
        while (Outgoing.Contains(EffectiveSessionId))
        {
            EffectiveSessionId = FGuid::NewGuid();
        }
    }
    else if (Outgoing.Contains(EffectiveSessionId))
    {
        UE_LOG(LogTemp, Warning, TEXT("StartBroadcastOpus: session %s is already active"), *EffectiveSessionId.ToString());
        return false;
    }

    OutSessionId = EffectiveSessionId;

    FOutgoingTransfer Tr;
    Tr.SessionId = EffectiveSessionId;
    Tr.Header = Header;
    Tr.Header.NumPackets = Packets.Num();
    BuildChunks(Packets, Tr.Chunks);

    Outgoing.Add(EffectiveSessionId, MoveTemp(Tr));

    // Send the header right away
    Server_StartTransfer(EffectiveSessionId, Header);
    Outgoing[EffectiveSessionId].bHeaderSent = true;

    return true;
}

bool UAudioReplicatorComponent::StartBroadcastFromWav(const FString& WavPath, int32 Bitrate, int32 FrameMs, FGuid SessionId, FGuid& OutSessionId)
{
    TArray<FOpusPacket> Packets;
    FOpusStreamHeader Header;
    if (!EncodeWavToOpusPackets(WavPath, Bitrate, FrameMs, Packets, Header))
        return false;
    return StartBroadcastOpus(Packets, Header, SessionId, OutSessionId);
}

void UAudioReplicatorComponent::CancelBroadcast(const FGuid& SessionId)
{
    if (FOutgoingTransfer* Tr = Outgoing.Find(SessionId))
    {
        // Send the end marker if it has not been sent yet
        if (!Tr->bEndSent && Tr->bHeaderSent)
        {
            Server_EndTransfer(SessionId);
            Tr->bEndSent = true;
        }
        Outgoing.Remove(SessionId);
    }
}

bool UAudioReplicatorComponent::GetReceivedPackets(const FGuid& SessionId, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const
{
    if (const FIncomingTransfer* In = Incoming.Find(SessionId))
    {
        OutPackets = In->Packets;
        OutHeader = In->Header;
        return true;
#if 0
        // Optional: clear the cache after reading
        Incoming.Remove(SessionId);
#endif
    }
    return false;
}

bool UAudioReplicatorComponent::GetOutgoingDebugInfo(const FGuid& SessionId, FAudioReplicatorOutgoingDebug& OutDebug) const
{
    if (const FOutgoingTransfer* Tr = Outgoing.Find(SessionId))
    {
        OutDebug = FAudioReplicatorOutgoingDebug();
        OutDebug.SessionId = SessionId;
        OutDebug.Header = Tr->Header;
        OutDebug.TotalChunks = Tr->Chunks.Num();
        OutDebug.SentChunks = FMath::Clamp(Tr->NextIndex, 0, OutDebug.TotalChunks);
        OutDebug.PendingChunks = FMath::Max(0, OutDebug.TotalChunks - OutDebug.SentChunks);
        OutDebug.NextChunkIndex = FMath::Clamp(Tr->NextIndex, 0, OutDebug.TotalChunks);
        OutDebug.bHeaderSent = Tr->bHeaderSent;
        OutDebug.bEndSent = Tr->bEndSent;

        OutDebug.Chunks.Reset(OutDebug.TotalChunks);
        OutDebug.PendingChunkIndices.Reset();

        int32 TotalBytes = 0;
        for (int32 i = 0; i < Tr->Chunks.Num(); ++i)
        {
            const FOpusChunk& Chunk = Tr->Chunks[i];

            FAudioReplicatorChunkDebug ChunkDebug;
            ChunkDebug.Index = Chunk.Index;
            ChunkDebug.SizeBytes = Chunk.Packet.Data.Num();
            ChunkDebug.bIsSent = (i < Tr->NextIndex);
            ChunkDebug.bIsReceived = false;

            TotalBytes += ChunkDebug.SizeBytes;
            if (!ChunkDebug.bIsSent)
            {
                OutDebug.PendingChunkIndices.Add(ChunkDebug.Index);
            }

            OutDebug.Chunks.Add(ChunkDebug);
        }

        OutDebug.TotalBytes = TotalBytes;
        OutDebug.EstimatedDurationSec = (Tr->Header.FrameMs > 0)
            ? (OutDebug.TotalChunks * Tr->Header.FrameMs) / 1000.0f
            : 0.0f;

        if (OutDebug.EstimatedDurationSec > 0.0f)
        {
            OutDebug.EstimatedBitrateKbps = (TotalBytes * 8.0f / OutDebug.EstimatedDurationSec) / 1000.0f;
        }
        else
        {
            OutDebug.EstimatedBitrateKbps = 0.0f;
        }

        OutDebug.bTransferComplete = (OutDebug.TotalChunks > 0)
            ? (OutDebug.SentChunks >= OutDebug.TotalChunks && Tr->bEndSent)
            : Tr->bEndSent;

        return true;
    }
    return false;
}

bool UAudioReplicatorComponent::GetIncomingDebugInfo(const FGuid& SessionId, FAudioReplicatorIncomingDebug& OutDebug) const
{
    if (const FIncomingTransfer* In = Incoming.Find(SessionId))
    {
        OutDebug = FAudioReplicatorIncomingDebug();
        OutDebug.SessionId = SessionId;
        OutDebug.Header = In->Header;
        OutDebug.bStarted = In->bStarted;
        OutDebug.bEnded = In->bEnded;
        OutDebug.ReceivedChunks = In->Received;

        OutDebug.ExpectedChunks = (In->Header.NumPackets > 0) ? In->Header.NumPackets : 0;
        const int32 DisplayChunkCount = (OutDebug.ExpectedChunks > 0) ? OutDebug.ExpectedChunks : In->Packets.Num();

        OutDebug.Chunks.Reset(DisplayChunkCount);
        OutDebug.MissingChunkIndices.Reset();

        int32 UniqueChunks = 0;
        int32 TotalBytes = 0;

        for (int32 Index = 0; Index < DisplayChunkCount; ++Index)
        {
            FAudioReplicatorChunkDebug ChunkDebug;
            ChunkDebug.Index = Index;
            ChunkDebug.bIsSent = false;
            ChunkDebug.bIsReceived = false;

            if (Index < In->Packets.Num())
            {
                const FOpusPacket& Packet = In->Packets[Index];
                ChunkDebug.SizeBytes = Packet.Data.Num();
                if (ChunkDebug.SizeBytes > 0)
                {
                    ChunkDebug.bIsReceived = true;
                    ++UniqueChunks;
                    TotalBytes += ChunkDebug.SizeBytes;
                }
            }

            if (!ChunkDebug.bIsReceived && OutDebug.ExpectedChunks > 0)
            {
                OutDebug.MissingChunkIndices.Add(Index);
            }

            OutDebug.Chunks.Add(ChunkDebug);
        }

        OutDebug.UniqueChunks = UniqueChunks;
        OutDebug.TotalBytes = TotalBytes;
        OutDebug.MissingChunks = (OutDebug.ExpectedChunks > 0)
            ? FMath::Max(0, OutDebug.ExpectedChunks - UniqueChunks)
            : 0;

        OutDebug.EstimatedDurationSec = (In->Header.FrameMs > 0)
            ? (UniqueChunks * In->Header.FrameMs) / 1000.0f
            : 0.0f;

        if (OutDebug.EstimatedDurationSec > 0.0f)
        {
            OutDebug.EstimatedBitrateKbps = (TotalBytes * 8.0f / OutDebug.EstimatedDurationSec) / 1000.0f;
        }
        else
        {
            OutDebug.EstimatedBitrateKbps = 0.0f;
        }

        OutDebug.bReadyToAssemble = OutDebug.bEnded && (OutDebug.ExpectedChunks == 0 || OutDebug.MissingChunks == 0);

        return true;
    }
    return false;
}

void UAudioReplicatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsOwnerClient()) return;

    // Pump outgoing queues
    TArray<FGuid> ToFinish;
    for (auto& KV : Outgoing)
    {
        FOutgoingTransfer& Tr = KV.Value;
        if (!Tr.bHeaderSent)
            continue;

        int32 SentThisTick = 0;
        while (Tr.NextIndex < Tr.Chunks.Num() && SentThisTick < MaxPacketsPerTick)
        {
            Server_SendChunk(Tr.SessionId, Tr.Chunks[Tr.NextIndex]);
            Tr.NextIndex++;
            SentThisTick++;
        }

        if (Tr.NextIndex >= Tr.Chunks.Num() && !Tr.bEndSent)
        {
            Server_EndTransfer(Tr.SessionId);
            Tr.bEndSent = true;
            ToFinish.Add(Tr.SessionId);
        }
    }

    for (const FGuid& S : ToFinish)
    {
        Outgoing.Remove(S);
    }
}

// ================= SERVER RPC =================

void UAudioReplicatorComponent::Server_StartTransfer_Implementation(const FGuid& SessionId, const FOpusStreamHeader& Header)
{
    Multicast_StartTransfer(SessionId, Header);
}

void UAudioReplicatorComponent::Server_SendChunk_Implementation(const FGuid& SessionId, const FOpusChunk& Chunk)
{
    Multicast_SendChunk(SessionId, Chunk);
}

void UAudioReplicatorComponent::Server_EndTransfer_Implementation(const FGuid& SessionId)
{
    Multicast_EndTransfer(SessionId);
}

// ================= MULTICAST RPC =================

void UAudioReplicatorComponent::Multicast_StartTransfer_Implementation(const FGuid& SessionId, const FOpusStreamHeader& Header)
{
    FIncomingTransfer& In = Incoming.FindOrAdd(SessionId);
    In.Header = Header;
    In.Packets.Reset(Header.NumPackets > 0 ? Header.NumPackets : 0);
    In.Received = 0;
    In.bStarted = true;
    In.bEnded = false;

    OnTransferStarted.Broadcast(SessionId, Header);
}

void UAudioReplicatorComponent::Multicast_SendChunk_Implementation(const FGuid& SessionId, const FOpusChunk& Chunk)
{
    FIncomingTransfer& In = Incoming.FindOrAdd(SessionId);
    if (!In.bStarted)
    {
        // Safety guard: mark the transfer as started even if the header went missing
        In.bStarted = true;
    }

    // Ensure the array has enough room
    if (In.Header.NumPackets > 0 && In.Packets.Num() < In.Header.NumPackets)
        In.Packets.SetNum(In.Header.NumPackets);

    if (In.Header.NumPackets > 0 && Chunk.Index >= 0 && Chunk.Index < In.Packets.Num())
    {
        In.Packets[Chunk.Index] = Chunk.Packet;
    }
    else
    {
        // When NumPackets is unknown, append sequentially
        In.Packets.Add(Chunk.Packet);
    }

    In.Received++;
    OnChunkReceived.Broadcast(SessionId, Chunk);
}

void UAudioReplicatorComponent::Multicast_EndTransfer_Implementation(const FGuid& SessionId)
{
    if (FIncomingTransfer* In = Incoming.Find(SessionId))
    {
        In->bEnded = true;
    }
    OnTransferEnded.Broadcast(this, SessionId);
}

// No replicated properties yet, but keep the hook for future use
void UAudioReplicatorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}
