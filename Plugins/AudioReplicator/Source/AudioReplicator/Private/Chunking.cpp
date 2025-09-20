#include "Chunking.h"

namespace Chunking
{
    void PackWithLengths(const TArray<FOpusPacket>& Packets, TArray<uint8>& OutBuffer)
    {
        OutBuffer.Reset();

        int64 total = 0;
        for (const auto& P : Packets)
        {
            total += 2;
            total += P.Data.Num();
        }
        OutBuffer.Reserve((int32)FMath::Min<int64>(total, INT32_MAX));

        for (const auto& P : Packets)
        {
            const int32 n = P.Data.Num();
            if (n < 0 || n > 65535)
            {
                UE_LOG(LogTemp, Warning, TEXT("PackWithLengths: packet too large (%d bytes)"), n);
                continue;
            }

            OutBuffer.Add((uint8)(n & 0xFF));
            OutBuffer.Add((uint8)((n >> 8) & 0xFF));

            if (n > 0)
            {
                OutBuffer.Append(P.Data.GetData(), n);
            }
        }
    }

    bool UnpackWithLengths(const TArray<uint8>& Buffer, TArray<FOpusPacket>& OutPackets)
    {
        OutPackets.Reset();
        const int32 N = Buffer.Num();
        int32 i = 0;

        while (i + 2 <= N)
        {
            const uint16 len = (uint16)(Buffer[i] | (Buffer[i + 1] << 8));
            i += 2;

            if (i + (int32)len > N)
            {
                UE_LOG(LogTemp, Warning, TEXT("UnpackWithLengths: truncated buffer (need %d, have %d)"), (int32)len, N - i);
                return false;
            }

            FOpusPacket pkt;
            if (len > 0)
            {
                pkt.Data.Append(Buffer.GetData() + i, (int32)len);
                i += (int32)len;
            }
            OutPackets.Add(MoveTemp(pkt));
        }

        if (i != N)
        {
            UE_LOG(LogTemp, Warning, TEXT("UnpackWithLengths: trailing bytes (%d)"), N - i);
            return false;
        }

        return true;
    }
}
