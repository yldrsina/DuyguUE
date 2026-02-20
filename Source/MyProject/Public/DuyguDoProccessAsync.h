// Async Blueprint node (proxy) for UDuyguDoProccess
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
    static UDuyguDoProccessAsync* DuyguDoProcess(UObject* WorldContextObject, const FString& AudioFilePath, const FString& ServerUrl = TEXT("http://127.0.0.1:5000/process"));

    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category="Duygu|Audio")
    static UDuyguDoProccessAsync* DuyguDoProcessFromPCM(UObject* WorldContextObject, const TArray<uint8>& PCMBytes, int32 SampleRate, int32 NumChannels, int32 BitsPerSample = 16, const FString& ServerUrl = TEXT("http://127.0.0.1:5000/process"));

    virtual void Activate() override;

protected:
    UFUNCTION()
    void HandleCompleted(bool bSuccess, const FString& Message);

    UFUNCTION()
    void HandleAudioImported(USoundWave* ImportedSound);

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
    
    UPROPERTY(BlueprintReadOnly)
    USoundWave* ImportedSound;
};
