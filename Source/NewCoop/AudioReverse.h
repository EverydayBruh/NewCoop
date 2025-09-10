#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundWave.h"
#include "AudioReverse.generated.h"

UCLASS()
class NEWCOOP_API UReversedProcedural : public USoundWaveProcedural
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Audio|Reverse")
    void InitFromPCM(const TArray<uint8>& InPCMBytes, int32 InSampleRate, int32 InNumChannels, float InDurationSeconds);

    UFUNCTION(BlueprintCallable, Category = "Audio|Reverse")
    bool InitFromSoundWave(USoundWave* InSound);

protected:
    virtual int32 GeneratePCMData(uint8* OutAudio, const int32 NumBytes) override;

private:
    TArray<uint8> PCMData;
    int32 ReadOffsetBytes = 0;
};

UCLASS()
class NEWCOOP_API UAudioReverse : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Audio|Reverse")
    static USoundWaveProcedural* CreateReversedSoundWave(USoundWave* Source);
};