// Async Blueprint node (proxy) for UDuyguDoProccess
// 
// USAGE - DuyguDoProcessFromSoundWave (RECOMMENDED):
//   Input: USoundWave* (Unreal sound object)
//   Process: Automatically exports to WAV, sends to microservice, downloads response
//   Output: OnSuccess (text message), OnAudioImported (processed USoundWave*)
//
// This is a complete latent async node that handles all intermediate steps:
// 1. Converts input USoundWave to WAV format
// 2. Sends WAV to Duygu microservice (/process endpoint)
// 3. Receives transcription + AI response + audio URL
// 4. Downloads response audio file
// 5. Converts downloaded WAV back to USoundWave
// 6. Returns processed USoundWave through OnAudioImported delegate
//
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "DuyguDoProccess.h"
#include "DuyguDoProccessAsync.generated.h"

UCLASS()
class MYPROJECT_API UDuyguDoProccessAsync : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FDuyguDoProccessCompleted OnSuccess;

    UPROPERTY(BlueprintAssignable)
    FDuyguDoProccessCompleted OnFailure;

    UPROPERTY(BlueprintAssignable)
    FDuyguAudioImported OnAudioImported;

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
    static UDuyguDoProccessAsync* DuyguDoProcess(UObject* WorldContextObject, const FString& AudioFilePath, const FString& ServerUrl = TEXT("http://127.0.0.1:5003/process"));

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category="Duygu|Audio")
    static UDuyguDoProccessAsync* DuyguDoProcessFromPCM(UObject* WorldContextObject, const TArray<uint8>& PCMBytes, int32 SampleRate, int32 NumChannels, int32 BitsPerSample = 16, const FString& ServerUrl = TEXT("http://127.0.0.1:5003/process"));

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category="Duygu|Audio", DisplayName="Duygu Do Process (SoundWave)")
    static UDuyguDoProccessAsync* DuyguDoProcessFromSoundWave(UObject* WorldContextObject, USoundWave* InputSoundWave, const FString& ServerUrl = TEXT("http://127.0.0.1:5003/process"));

    virtual void Activate() override;

protected:
    UFUNCTION()
    void HandleCompleted(bool bSuccess, const FString& Message, USoundWave* ProcessedSound);

    UFUNCTION()
    void HandleAudioImported(USoundWave* InImportedSound);

    UPROPERTY()
    UDuyguDoProccess* InnerProcess;

    UPROPERTY()
    UObject* WorldContextObject;
    
    UPROPERTY()
    FString AudioFilePath;

    UPROPERTY()
    FString ServerUrl;
    
    UPROPERTY()
    TArray<uint8> PendingPCM;

    UPROPERTY()
    int32 PendingSampleRate;

    UPROPERTY()
    int32 PendingNumChannels;

    UPROPERTY()
    int32 PendingBitsPerSample;
    
    UPROPERTY()
    USoundWave* PendingSoundWave;
    
    // The imported/processed sound wave - accessible from Blueprint after OnAudioImported fires
    UPROPERTY(BlueprintReadOnly, Category="Duygu")
    USoundWave* ImportedSound;
    
    // Get the imported sound (can be called after OnAudioImported delegate)
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Duygu")
    USoundWave* GetImportedSound() const { return ImportedSound; }
};
