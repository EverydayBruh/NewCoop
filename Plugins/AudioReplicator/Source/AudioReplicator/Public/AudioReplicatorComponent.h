#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OpusTypes.h"
#include "AudioReplicatorComponent.generated.h"

// �������� ��� BP
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOpusTransferStarted, FGuid, SessionId, FOpusStreamHeader, Header);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOpusChunkReceived, FGuid, SessionId, FOpusChunk, Chunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOpusTransferEnded, FGuid, SessionId);

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
    TArray<FOpusPacket> Packets; // �������� ��� ����-�������������
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

    // ������� ������ ����� �� ��� (������ �� �����)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Net")
    int32 MaxPacketsPerTick = 32;

    // �������
    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusTransferStarted OnTransferStarted;

    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusChunkReceived OnChunkReceived;

    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusTransferEnded OnTransferEnded;

    // == BP ����: ������ ��������� ==
    // 1) �� ������� Opus-������� (�������� ��� ������������)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool StartBroadcastOpus(const TArray<FOpusPacket>& Packets, FOpusStreamHeader Header, FGuid& OutSessionId);

    // 2) �� WAV-����� (������� ��������� �������, � ����� ��������)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool StartBroadcastFromWav(const FString& WavPath, int32 Bitrate, int32 FrameMs, FGuid& OutSessionId);

    // ������������� ��������� (���� ���� ��������)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    void CancelBroadcast(const FGuid& SessionId);

    // ������ � ������ ��������� ������ (��������, ����� ����� �������� ������������ � ��������� WAV)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool GetReceivedPackets(const FGuid& SessionId, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // === SERVER RPC ===
    UFUNCTION(Server, Reliable)
    void Server_StartTransfer(const FGuid& SessionId, const FOpusStreamHeader& Header);

    UFUNCTION(Server, Unreliable)
    void Server_SendChunk(const FGuid& SessionId, const FOpusChunk& Chunk);

    UFUNCTION(Server, Reliable)
    void Server_EndTransfer(const FGuid& SessionId);

    // === MULTICAST RPC ===
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_StartTransfer(const FGuid& SessionId, const FOpusStreamHeader& Header);

    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_SendChunk(const FGuid& SessionId, const FOpusChunk& Chunk);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_EndTransfer(const FGuid& SessionId);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // ������� ��������� ������ � ���������
    UPROPERTY()
    TMap<FGuid, FOutgoingTransfer> Outgoing;

    // �������� ������, �������� �� ���� ��������
    UPROPERTY()
    TMap<FGuid, FIncomingTransfer> Incoming;

    // ������: �������� ������ ������� � ����� � ���������
    static void BuildChunks(const TArray<FOpusPacket>& Packets, TArray<FOpusChunk>& OutChunks);

    // ���������������: �������� ������������ WAV -> Opus ������
    bool EncodeWavToOpusPackets(const FString& WavPath, int32 Bitrate, int32 FrameMs, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const;

    bool IsOwnerClient() const;
};
