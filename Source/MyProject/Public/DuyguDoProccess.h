// Minimal async process class for Duygu flow
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DuyguDoProccess.generated.h"

class IHttpRequest;
class IHttpResponse;
typedef TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> FHttpRequestPtr;
typedef TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> FHttpResponsePtr;

class USoundWave;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDuyguDoProccessCompleted, bool, bSuccess, const FString&, Message, USoundWave*, ProcessedSound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDuyguAudioImported, USoundWave*, ImportedSound);

UCLASS(Blueprintable)
class MYPROJECT_API UDuyguDoProccess : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FDuyguDoProccessCompleted OnCompleted;

    UPROPERTY(BlueprintAssignable)
    FDuyguAudioImported OnAudioImported;

    UPROPERTY()
    USoundWave* ImportedSound;

    UFUNCTION(BlueprintCallable)
    void StartProcess(const FString& AudioFilePath, const FString& ServerUrl = TEXT("http://127.0.0.1:5003/process"));

    UFUNCTION(BlueprintCallable, Category="Duygu|Audio")
    void StartProcessFromPCM(const TArray<uint8>& PCMBytes, int32 SampleRate, int32 NumChannels, int32 BitsPerSample = 16, const FString& ServerUrl = TEXT("http://127.0.0.1:5003/process"));

    UFUNCTION(BlueprintCallable, Category="Duygu|Audio")
    void StartProcessFromSoundWave(USoundWave* SoundWave, const FString& ServerUrl = TEXT("http://127.0.0.1:5003/process"));

protected:
    void OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnAudioDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    UPROPERTY()
    FString ServerUrl;

    UPROPERTY()
    FString AudioFilePath;
    
    UPROPERTY()
    FString PendingResponseMessage;
};
