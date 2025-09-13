#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OpusTypes.h"
#include "AudioReplicatorComponent.generated.h"

// Делегаты для BP
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
    TArray<FOpusPacket> Packets; // Собираем для пост-декодирования
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

    // Сколько чанков слать за тик (защита от спама)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AudioReplicator|Net")
    int32 MaxPacketsPerTick = 32;

    // События
    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusTransferStarted OnTransferStarted;

    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusChunkReceived OnChunkReceived;

    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Net")
    FOnOpusTransferEnded OnTransferEnded;

    // == BP НОДЫ: запуск трансфера ==
    // 1) Из готовых Opus-пакетов (локально уже закодировали)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool StartBroadcastOpus(const TArray<FOpusPacket>& Packets, FOpusStreamHeader Header, FGuid& OutSessionId);

    // 2) Из WAV-файла (вызовет локальный энкодер, а затем отправку)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    bool StartBroadcastFromWav(const FString& WavPath, int32 Bitrate, int32 FrameMs, FGuid& OutSessionId);

    // Принудительно завершить (если надо оборвать)
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Net")
    void CancelBroadcast(const FGuid& SessionId);

    // Доступ к буферу принятого потока (например, чтобы потом локально декодировать и сохранить WAV)
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
    // Очереди исходящих сессий у владельца
    UPROPERTY()
    TMap<FGuid, FOutgoingTransfer> Outgoing;

    // Входящие сессии, собираем на всех клиентах
    UPROPERTY()
    TMap<FGuid, FIncomingTransfer> Incoming;

    // Помощь: нарезать массив пакетов в чанки с индексами
    static void BuildChunks(const TArray<FOpusPacket>& Packets, TArray<FOpusChunk>& OutChunks);

    // Вспомогательное: локально закодировать WAV -> Opus пакеты
    bool EncodeWavToOpusPackets(const FString& WavPath, int32 Bitrate, int32 FrameMs, TArray<FOpusPacket>& OutPackets, FOpusStreamHeader& OutHeader) const;

    bool IsOwnerClient() const;
};
