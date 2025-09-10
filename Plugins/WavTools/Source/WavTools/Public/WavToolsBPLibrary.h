#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WavToolsBPLibrary.generated.h"

UCLASS()
class WAVTOOLS_API UWavToolsBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Path: "Voice/record.wav" (relative to Saved/) or full path "C:/.../Saved/Voice/record.wav"
    UFUNCTION(BlueprintCallable, Category = "WavTools")
    static bool InspectWavAtSavedPath(const FString& RelativeOrFullPath);
    // Path: "Voice/record.wav" (relative to Saved/) or full path "C:/.../Saved/Voice/record.wav"
    UFUNCTION(BlueprintCallable, Category = "WavTools")
    static bool ReverseWavAtSavedPath(const FString& RelativeOrFullPath, FString& OutReversedFullPath, bool bOverwriteExisting /*= true*/);

};
