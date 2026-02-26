#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Sound/SoundWave.h"
#include "MultiAudioOutput.generated.h"

// Log category for multi-audio helper
DECLARE_LOG_CATEGORY_EXTERN(LogMultiAudio, Log, All);

/**
 * Minimal Windows-only multi-device audio output using WASAPI.
 * Provides device enumeration and ability to play a USoundWave to a specific device.
 */
UCLASS(BlueprintType)
class MYPROJECT_API UMultiAudioOutput : public UObject
{
    GENERATED_BODY()

public:
    UMultiAudioOutput();

    UFUNCTION(BlueprintCallable, Category = "MultiAudio")
    static TArray<FString> GetAvailableAudioDevices();

    UFUNCTION(BlueprintCallable, Category = "MultiAudio")
    bool PlaySoundOnDevice(USoundWave* SoundWave, const FString& DeviceName, float Volume = 1.0f);

    UFUNCTION(BlueprintCallable, Category = "MultiAudio")
    void StopAll();

    UFUNCTION(BlueprintCallable, Category = "MultiAudio")
    static bool ExtractPCMData(USoundWave* SoundWave, TArray<uint8>& OutPCMData, int32& OutSampleRate, int32& OutNumChannels, int32& OutBitsPerSample);

private:
    // Simple token to request stop for background tasks
    TAtomic<bool> bStopRequested{false};
};
