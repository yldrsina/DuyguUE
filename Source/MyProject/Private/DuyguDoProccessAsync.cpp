// Implementation of the async proxy that bridges UDuyguDoProccess to Blueprints
#include "DuyguDoProccessAsync.h"
#include "UObject/Package.h"

UDuyguDoProccessAsync* UDuyguDoProccessAsync::DuyguDoProcess(UObject* WorldContextObject, const FString& AudioFilePath, const FString& ServerUrl)
{
    UDuyguDoProccessAsync* Proxy = NewObject<UDuyguDoProccessAsync>(WorldContextObject ? WorldContextObject : (UObject*)GetTransientPackage());
    Proxy->WorldContextObject = WorldContextObject;
    Proxy->AudioFilePath = AudioFilePath;
    Proxy->ServerUrl = ServerUrl;
    Proxy->AddToRoot();
    return Proxy;
}

void UDuyguDoProccessAsync::Activate()
{
    InnerProcess = NewObject<UDuyguDoProccess>(this);
    if (InnerProcess)
    {
        InnerProcess->OnCompleted.AddDynamic(this, &UDuyguDoProccessAsync::HandleCompleted);
        InnerProcess->OnAudioImported.AddDynamic(this, &UDuyguDoProccessAsync::HandleAudioImported);
        InnerProcess->StartProcess(AudioFilePath, ServerUrl);
    }
    else
    {
        HandleCompleted(false, TEXT("Failed to create inner process"));
    }
}

void UDuyguDoProccessAsync::HandleCompleted(bool bSuccess, const FString& Message)
{
    if (bSuccess)
    {
        OnSuccess.Broadcast(bSuccess, Message);
    }
    else
    {
        OnFailure.Broadcast(bSuccess, Message);
    }

    RemoveFromRoot();
}

void UDuyguDoProccessAsync::HandleAudioImported(USoundWave* ImportedSound)
{
    OnAudioImported.Broadcast(ImportedSound);
}
