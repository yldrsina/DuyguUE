// Minimal async process class for Duygu flow
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DuyguDoProccess.generated.h"

class IHttpRequest;
class IHttpResponse;
typedef TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> FHttpRequestPtr;
typedef TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> FHttpResponsePtr;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDuyguDoProccessCompleted, bool, bSuccess, const FString&, Message);
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

    UFUNCTION(BlueprintCallable)
    void StartProcess(const FString& AudioFilePath, const FString& ServerUrl = TEXT("http://127.0.0.1:5000/process"));

protected:
    void OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnAudioDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    UPROPERTY()
    FString ServerUrl;

    UPROPERTY()
    FString AudioFilePath;
};
