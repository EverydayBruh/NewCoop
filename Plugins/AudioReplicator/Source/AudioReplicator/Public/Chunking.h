#pragma once
#include "CoreMinimal.h"
#include "OpusTypes.h"

namespace Chunking
{
    void PackWithLengths(const TArray<FOpusPacket>& Packets, TArray<uint8>& OutBuffer);
    bool UnpackWithLengths(const TArray<uint8>& Buffer, TArray<FOpusPacket>& OutPackets);
}
