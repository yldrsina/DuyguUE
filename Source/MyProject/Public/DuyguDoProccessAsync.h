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
};
