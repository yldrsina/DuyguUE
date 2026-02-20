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

UDuyguDoProccessAsync* UDuyguDoProccessAsync::DuyguDoProcessFromPCM(UObject* WorldContextObject, const TArray<uint8>& PCMBytes, int32 SampleRate, int32 NumChannels, int32 BitsPerSample, const FString& ServerUrl)
{
    UDuyguDoProccessAsync* Proxy = NewObject<UDuyguDoProccessAsync>(WorldContextObject ? WorldContextObject : (UObject*)GetTransientPackage());
    Proxy->WorldContextObject = WorldContextObject;
    Proxy->AudioFilePath.Empty();
    Proxy->ServerUrl = ServerUrl;
    Proxy->PendingPCM = PCMBytes;
    Proxy->PendingSampleRate = SampleRate;
    Proxy->PendingNumChannels = NumChannels;
    Proxy->PendingBitsPerSample = BitsPerSample;
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
        if (PendingPCM.Num() > 0 && PendingSampleRate > 0 && PendingNumChannels > 0)
        {
            InnerProcess->StartProcessFromPCM(PendingPCM, PendingSampleRate, PendingNumChannels, PendingBitsPerSample, ServerUrl);
        }
        else
        {
            InnerProcess->StartProcess(AudioFilePath, ServerUrl);
        }
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
}

void UDuyguDoProccessAsync::HandleAudioImported(USoundWave* ImportedSound)
{
    // store so Blueprints can access
    this->ImportedSound = ImportedSound;
    OnAudioImported.Broadcast(ImportedSound);

    // complete lifecycle and allow GC
    RemoveFromRoot();
}
