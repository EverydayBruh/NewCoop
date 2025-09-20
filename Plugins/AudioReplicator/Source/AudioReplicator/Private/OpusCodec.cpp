#include "OpusCodec.h"
#include <opus.h> // ThirdParty/Opus/Include

namespace
{
    constexpr int32 MaxPacketSize = 4000; // с большим запасом на пакет
}

FOpusCodec::FOpusCodec(int32 InSR, int32 InCh, int32 InBitrate)
    : SR(InSR), Ch(InCh), Bitrate(InBitrate)
{
    int Err = 0;

    Encoder = opus_encoder_create(SR, Ch, OPUS_APPLICATION_AUDIO, &Err);
    if (!Encoder || Err != OPUS_OK)
    {
        Encoder = nullptr;
    }
    else
    {
        opus_encoder_ctl(Encoder, OPUS_SET_BITRATE(Bitrate));
        opus_encoder_ctl(Encoder, OPUS_SET_VBR(1));
        opus_encoder_ctl(Encoder, OPUS_SET_COMPLEXITY(8));
    }

    Decoder = opus_decoder_create(SR, Ch, &Err);
    if (!Decoder || Err != OPUS_OK)
    {
        Decoder = nullptr;
    }
}

FOpusCodec::~FOpusCodec()
{
    if (Encoder) { opus_encoder_destroy(Encoder); Encoder = nullptr; }
    if (Decoder) { opus_decoder_destroy(Decoder); Decoder = nullptr; }
}

TUniquePtr<FOpusCodec> FOpusCodec::Create(int32 SampleRate, int32 Channels, int32 Bitrate)
{
    TUniquePtr<FOpusCodec> Ptr(new FOpusCodec(SampleRate, Channels, Bitrate));
    if (!Ptr->Encoder || !Ptr->Decoder)
    {
        return nullptr;
    }
    return Ptr;
}

bool FOpusCodec::EncodePcm16ToPackets(const TArray<int16>& Pcm, int32 FrameSizeSamplesPerCh, TArray<TArray<uint8>>& OutPackets)
{
    if (!Encoder || FrameSizeSamplesPerCh <= 0) return false;

    const int32 SamplesPerFrameTotal = FrameSizeSamplesPerCh * Ch;
    int32 Offset = 0;
    OutPackets.Reset();

    while (Offset + SamplesPerFrameTotal <= Pcm.Num())
    {
        const int16* FramePtr = Pcm.GetData() + Offset;

        TArray<uint8> Packet;
        Packet.SetNumUninitialized(MaxPacketSize);

        const int EncBytes = opus_encode(
            Encoder,
            FramePtr,
            FrameSizeSamplesPerCh,
            Packet.GetData(),
            Packet.Num()
        );
        if (EncBytes < 0)
        {
            return false;
        }

        Packet.SetNum(EncBytes);
        OutPackets.Add(MoveTemp(Packet));

        Offset += SamplesPerFrameTotal;
    }

    return true;
}

bool FOpusCodec::DecodePacketsToPcm16(const TArray<TArray<uint8>>& Packets, TArray<int16>& OutPcm)
{
    if (!Decoder) return false;

    OutPcm.Reset();

    for (const TArray<uint8>& Packet : Packets)
    {
        TArray<int16> FramePcm;
        FramePcm.SetNumZeroed(5760 * Ch); // максимально безопасный буфер

        const int DecSamplesPerCh = opus_decode(
            Decoder,
            Packet.GetData(),
            Packet.Num(),
            FramePcm.GetData(),
            FramePcm.Num() / Ch,
            0
        );
        if (DecSamplesPerCh < 0) return false;

        FramePcm.SetNum(DecSamplesPerCh * Ch);
        OutPcm.Append(FramePcm);
    }
    return true;
}
