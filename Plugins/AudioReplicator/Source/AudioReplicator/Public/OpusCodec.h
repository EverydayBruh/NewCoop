#pragma once
#include "CoreMinimal.h"

// forward-declare, чтобы не тянуть <opus.h> в публичный заголовок
struct OpusEncoder;
struct OpusDecoder;

class AUDIOREPLICATOR_API FOpusCodec
{
public:
    static TUniquePtr<FOpusCodec> Create(int32 SampleRate = AUDIO_REPL_OPUS_SR, int32 Channels = 1, int32 Bitrate = 32000);

    // PCM16 -> Opus packets
    bool EncodePcm16ToPackets(const TArray<int16>& Pcm, int32 FrameSizeSamplesPerCh, TArray<TArray<uint8>>& OutPackets);
    // Opus packets -> PCM16
    bool DecodePacketsToPcm16(const TArray<TArray<uint8>>& Packets, TArray<int16>& OutPcm);

    int32 GetSampleRate() const { return SR; }
    int32 GetChannels() const { return Ch; }

    // Должен быть публичным, чтобы TUniquePtr мог удалить объект
    ~FOpusCodec();

    // Безопасность владения
    FOpusCodec(const FOpusCodec&) = delete;
    FOpusCodec& operator=(const FOpusCodec&) = delete;

private:
    FOpusCodec(int32 InSR, int32 InCh, int32 InBitrate);

    OpusEncoder* Encoder = nullptr;
    OpusDecoder* Decoder = nullptr;
    int32 SR = 48000;
    int32 Ch = 1;
    int32 Bitrate = 32000;
};
