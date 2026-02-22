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

UDuyguDoProccessAsync* UDuyguDoProccessAsync::DuyguDoProcessFromSoundWave(UObject* WorldContextObject, USoundWave* InputSoundWave, const FString& ServerUrl)
{
    UDuyguDoProccessAsync* Proxy = NewObject<UDuyguDoProccessAsync>(WorldContextObject ? WorldContextObject : (UObject*)GetTransientPackage());
    Proxy->WorldContextObject = WorldContextObject;
    Proxy->AudioFilePath.Empty();
    Proxy->ServerUrl = ServerUrl;
    Proxy->PendingSoundWave = InputSoundWave;
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
        
        if (PendingSoundWave)
        {
            // Process from USoundWave
            InnerProcess->StartProcessFromSoundWave(PendingSoundWave, ServerUrl);
        }
        else if (PendingPCM.Num() > 0 && PendingSampleRate > 0 && PendingNumChannels > 0)
        {
            // Process from PCM bytes
            InnerProcess->StartProcessFromPCM(PendingPCM, PendingSampleRate, PendingNumChannels, PendingBitsPerSample, ServerUrl);
        }
        else
        {
            // Process from file path
            InnerProcess->StartProcess(AudioFilePath, ServerUrl);
        }
    }
    else
    {
        HandleCompleted(false, TEXT("Failed to create inner process"), nullptr);
    }
}

void UDuyguDoProccessAsync::HandleCompleted(bool bSuccess, const FString& Message, USoundWave* ProcessedSound)
{
    // Store the sound
    ImportedSound = ProcessedSound;
    
    if (bSuccess)
    {
        // Success - broadcast with the sound
        OnSuccess.Broadcast(bSuccess, Message, ProcessedSound);
    }
    else
    {
        // Failure - broadcast with nullptr
        OnFailure.Broadcast(bSuccess, Message, nullptr);
        // Also trigger audio imported with nullptr and cleanup immediately
        OnAudioImported.Broadcast(nullptr);
        RemoveFromRoot();
    }
}

void UDuyguDoProccessAsync::HandleAudioImported(USoundWave* InImportedSound)
{
    // store so Blueprints can access
    ImportedSound = InImportedSound;
    
    // Broadcast the audio imported event
    OnAudioImported.Broadcast(ImportedSound);

    // complete lifecycle and allow GC
    RemoveFromRoot();
}
