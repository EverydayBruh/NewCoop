#include "AudioReplicatorComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "AudioReplicatorBPLibrary.h" // используем наши локальные ноды для кодирования/декодирования

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
    return Owner->GetLocalRole() == ROLE_AutonomousProxy || Owner->GetLocalRole() == ROLE_SimulatedProxy;
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

bool UAudioReplicatorComponent::StartBroadcastOpus(const TArray<FOpusPacket>& Packets, FOpusStreamHeader Header, FGuid& OutSessionId)
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

    FGuid SessionId = FGuid::NewGuid();
    OutSessionId = SessionId;

    FOutgoingTransfer Tr;
    Tr.SessionId = SessionId;
    Tr.Header = Header;
    Tr.Header.NumPackets = Packets.Num();
    BuildChunks(Packets, Tr.Chunks);

    Outgoing.Add(SessionId, MoveTemp(Tr));

    // Отправим заголовок сразу
    Server_StartTransfer(SessionId, Header);
    Outgoing[SessionId].bHeaderSent = true;

    return true;
}

bool UAudioReplicatorComponent::StartBroadcastFromWav(const FString& WavPath, int32 Bitrate, int32 FrameMs, FGuid& OutSessionId)
{
    TArray<FOpusPacket> Packets;
    FOpusStreamHeader Header;
    if (!EncodeWavToOpusPackets(WavPath, Bitrate, FrameMs, Packets, Header))
        return false;
    return StartBroadcastOpus(Packets, Header, OutSessionId);
}

void UAudioReplicatorComponent::CancelBroadcast(const FGuid& SessionId)
{
    if (FOutgoingTransfer* Tr = Outgoing.Find(SessionId))
    {
        // Отправим завершение, если ещё не отправлено
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
        // При желании можно очищать после чтения
        Incoming.Remove(SessionId);
#endif
    }
    return false;
}

void UAudioReplicatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!IsOwnerClient()) return;

    // Рассылаем очереди исходящих
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
        // Защита на случай рассинхронизации: инициируем пустой Header по умолчанию
        In.bStarted = true;
    }

    // Гарантируем место
    if (In.Header.NumPackets > 0 && In.Packets.Num() < In.Header.NumPackets)
        In.Packets.SetNum(In.Header.NumPackets);

    if (In.Header.NumPackets > 0 && Chunk.Index >= 0 && Chunk.Index < In.Packets.Num())
    {
        In.Packets[Chunk.Index] = Chunk.Packet;
    }
    else
    {
        // Если NumPackets неизвестно — просто пушим по порядку
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
    OnTransferEnded.Broadcast(SessionId);
}

// Реплика свойств не используем, но хук оставим на будущее
void UAudioReplicatorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}
