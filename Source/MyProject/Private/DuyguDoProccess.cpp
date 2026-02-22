// Implementation for minimal UDuyguDoProccess
#include "DuyguDoProccess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Json.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundWave.h"
#include "HAL/PlatformFilemanager.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

static FString GenerateBoundary()
{
    return FString::Printf(TEXT("----UEBoundary%u"), FPlatformTime::Cycles());
}

static void BuildMultipartFormData(const FString& FieldName, const FString& Filename, const TArray<uint8>& FileData, const FString& Boundary, TArray<uint8>& OutBody)
{
    FString Header = FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n"), *Boundary, *FieldName, *Filename);
    FTCHARToUTF8 HeaderUtf8(*Header);
    OutBody.Append((uint8*)HeaderUtf8.Get(), HeaderUtf8.Length());
    OutBody.Append(FileData);
    FString Ending = FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary);
    FTCHARToUTF8 EndingUtf8(*Ending);
    OutBody.Append((uint8*)EndingUtf8.Get(), EndingUtf8.Length());
}

void UDuyguDoProccess::StartProcess(const FString& InAudioFilePath, const FString& InServerUrl)
{
    // store for later (used when resolving relative audio_url)
    this->ServerUrl = InServerUrl;
    this->AudioFilePath = InAudioFilePath;

    if (AudioFilePath.IsEmpty())
    {
        OnCompleted.Broadcast(false, TEXT("Empty audio file path"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *AudioFilePath))
    {
        OnCompleted.Broadcast(false, FString::Printf(TEXT("Failed to read file: %s"), *AudioFilePath), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    FString Boundary = GenerateBoundary();
    TArray<uint8> Body;
    BuildMultipartFormData(TEXT("audio"), FPaths::GetCleanFilename(AudioFilePath), FileBytes, Boundary, Body);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(ServerUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
    Request->SetContent(Body);

    Request->OnProcessRequestComplete().BindUObject(this, &UDuyguDoProccess::OnHttpResponseReceived);
    if (!Request->ProcessRequest())
    {
        OnCompleted.Broadcast(false, TEXT("Failed to start HTTP request"), nullptr);
        OnAudioImported.Broadcast(nullptr);
    }
}

static void AppendWavHeader(TArray<uint8>& OutWav, int32 PCMDataSize, int32 SampleRate, int32 NumChannels, int32 BitsPerSample)
{
    // RIFF header
    int32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
    int16 BlockAlign = (int16)(NumChannels * BitsPerSample / 8);
    int16 NumChannels16 = (int16)NumChannels;
    int16 BitsPerSample16 = (int16)BitsPerSample;

    // 'RIFF'
    OutWav.Append((uint8*)"RIFF", 4);
    int32 ChunkSize = 36 + PCMDataSize;
    OutWav.Append((uint8*)&ChunkSize, 4);
    OutWav.Append((uint8*)"WAVE", 4);

    // fmt chunk
    OutWav.Append((uint8*)"fmt ", 4);
    int32 Subchunk1Size = 16;
    OutWav.Append((uint8*)&Subchunk1Size, 4);
    int16 AudioFormat = 1; // PCM
    OutWav.Append((uint8*)&AudioFormat, 2);
    OutWav.Append((uint8*)&NumChannels16, 2);
    OutWav.Append((uint8*)&SampleRate, 4);
    OutWav.Append((uint8*)&ByteRate, 4);
    OutWav.Append((uint8*)&BlockAlign, 2);
    OutWav.Append((uint8*)&BitsPerSample16, 2);

    // data chunk
    OutWav.Append((uint8*)"data", 4);
    OutWav.Append((uint8*)&PCMDataSize, 4);
}

void UDuyguDoProccess::StartProcessFromPCM(const TArray<uint8>& PCMBytes, int32 SampleRate, int32 NumChannels, int32 BitsPerSample, const FString& InServerUrl)
{
    this->ServerUrl = InServerUrl;
    this->AudioFilePath.Empty();

    if (PCMBytes.Num() == 0 || SampleRate <= 0 || NumChannels <= 0)
    {
        OnCompleted.Broadcast(false, TEXT("Invalid PCM data or parameters"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Build WAV bytes
    TArray<uint8> WavData;
    int32 PCMSize = PCMBytes.Num();
    AppendWavHeader(WavData, PCMSize, SampleRate, NumChannels, BitsPerSample);
    WavData.Append(PCMBytes);

    // create multipart body and upload same as file-based StartProcess
    FString Boundary = GenerateBoundary();
    TArray<uint8> Body;
    BuildMultipartFormData(TEXT("audio"), TEXT("recording.wav"), WavData, Boundary, Body);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(ServerUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
    Request->SetContent(Body);

    Request->OnProcessRequestComplete().BindUObject(this, &UDuyguDoProccess::OnHttpResponseReceived);
    if (!Request->ProcessRequest())
    {
        OnCompleted.Broadcast(false, TEXT("Failed to start HTTP request"), nullptr);
        OnAudioImported.Broadcast(nullptr);
    }
}

void UDuyguDoProccess::StartProcessFromSoundWave(USoundWave* SoundWave, const FString& InServerUrl)
{
    this->ServerUrl = InServerUrl;
    this->AudioFilePath.Empty();

    if (!SoundWave)
    {
        OnCompleted.Broadcast(false, TEXT("Invalid SoundWave"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Get audio format info
    int32 SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
    int32 NumChannels = SoundWave->NumChannels;
    
    if (SampleRate <= 0 || NumChannels <= 0)
    {
        OnCompleted.Broadcast(false, TEXT("Invalid SoundWave parameters"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Extract PCM data from SoundWave
    TArray<uint8> PCMData;
    
    // Method 1: Check if already cached in RawPCMData
    if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
    {
        PCMData.Append(static_cast<const uint8*>(SoundWave->RawPCMData), SoundWave->RawPCMDataSize);
    }
    else
    {
        // Method 2: Try to load and cache the PCM data
        // Force the sound to cache its PCM data
        FAudioDevice* AudioDevice = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
        if (AudioDevice)
        {
            // Initialize audio resources to decompress
            SoundWave->InitAudioResource(SoundWave->GetRuntimeFormat());
            
            // Try again after initialization
            if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
            {
                PCMData.Append(static_cast<const uint8*>(SoundWave->RawPCMData), SoundWave->RawPCMDataSize);
            }
        }
    }
    
    // If still no data, try to get it from the source asset (Editor only)
#if WITH_EDITORONLY_DATA
    if (PCMData.Num() == 0)
    {
        // Try to get the source file path
        if (SoundWave->AssetImportData && SoundWave->AssetImportData->GetSourceData().SourceFiles.Num() > 0)
        {
            FString SourceFilePath = SoundWave->AssetImportData->GetSourceData().SourceFiles[0].RelativeFilename;
            
            // Try to load the original file
            TArray<uint8> FileData;
            if (FFileHelper::LoadFileToArray(FileData, *SourceFilePath))
            {
                // Use the file directly instead of extracting PCM
                FString Boundary = GenerateBoundary();
                TArray<uint8> Body;
                BuildMultipartFormData(TEXT("audio"), FPaths::GetCleanFilename(SourceFilePath), FileData, Boundary, Body);

                TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
                Request->SetURL(ServerUrl);
                Request->SetVerb(TEXT("POST"));
                Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
                Request->SetContent(Body);

                Request->OnProcessRequestComplete().BindUObject(this, &UDuyguDoProccess::OnHttpResponseReceived);
                if (Request->ProcessRequest())
                {
                    return; // Success, request started
                }
            }
        }
    }
#endif

    if (PCMData.Num() == 0)
    {
        OnCompleted.Broadcast(false, TEXT("Could not extract audio data from SoundWave. Try:\n1. Set 'Loading Behavior' to 'Retain Audio Data' in Sound properties\n2. Or use DuyguDoProcessFromPCM with raw audio bytes\n3. Or use DuyguDoProcess with file path"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Build WAV file from PCM data
    TArray<uint8> WavData;
    const int32 BitsPerSample = 16;
    AppendWavHeader(WavData, PCMData.Num(), SampleRate, NumChannels, BitsPerSample);
    WavData.Append(PCMData);

    // Create multipart body and upload
    FString Boundary = GenerateBoundary();
    TArray<uint8> Body;
    BuildMultipartFormData(TEXT("audio"), TEXT("input.wav"), WavData, Boundary, Body);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(ServerUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
    Request->SetContent(Body);

    Request->OnProcessRequestComplete().BindUObject(this, &UDuyguDoProccess::OnHttpResponseReceived);
    if (!Request->ProcessRequest())
    {
        OnCompleted.Broadcast(false, TEXT("Failed to start HTTP request"), nullptr);
        OnAudioImported.Broadcast(nullptr);
    }
}

void UDuyguDoProccess::OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        OnCompleted.Broadcast(false, TEXT("HTTP request failed or no response"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    int32 Code = Response->GetResponseCode();
    FString ResponseBody = Response->GetContentAsString();

    if (Code < 200 || Code >= 300)
    {
        OnCompleted.Broadcast(false, FString::Printf(TEXT("HTTP error %d: %s"), Code, *ResponseBody), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Parse JSON response
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
    TSharedPtr<FJsonObject> JsonObj;
    
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
    {
        OnCompleted.Broadcast(false, TEXT("Failed to parse server response"), nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Check if request was successful
    bool bSuccess = JsonObj->GetBoolField(TEXT("success"));
    if (!bSuccess)
    {
        FString ErrorMsg = JsonObj->HasField(TEXT("error")) 
            ? JsonObj->GetStringField(TEXT("error")) 
            : TEXT("Server processing failed");
        OnCompleted.Broadcast(false, ErrorMsg, nullptr);
        OnAudioImported.Broadcast(nullptr);
        return;
    }

    // Get response text (user_text and assistant_response)
    FString UserText = JsonObj->HasField(TEXT("user_text")) 
        ? JsonObj->GetStringField(TEXT("user_text")) 
        : TEXT("");
    FString AssistantResponse = JsonObj->HasField(TEXT("assistant_response")) 
        ? JsonObj->GetStringField(TEXT("assistant_response")) 
        : TEXT("");

    // Create a combined message for blueprint
    FString CombinedMessage = FString::Printf(
        TEXT("User: %s\nAssistant: %s"), 
        *UserText, 
        *AssistantResponse
    );

    // Store the message for broadcasting later with the sound
    PendingResponseMessage = CombinedMessage;

    // Try to download audio_url if present
    bool bDispatchedAudio = false;
    if (JsonObj->HasField(TEXT("audio_url")))
    {
        FString AudioUrl = JsonObj->GetStringField(TEXT("audio_url"));
        if (!AudioUrl.IsEmpty())
        {
            // Resolve relative URL (e.g., "/download/response_123456.wav")
            FString FullUrl = AudioUrl;
            if (AudioUrl.StartsWith(TEXT("/")))
            {
                // Extract base URL (scheme + host + port) from ServerUrl
                FString Scheme, Rest;
                if (ServerUrl.Split(TEXT("://"), &Scheme, &Rest))
                {
                    // Find the first '/' after host:port to separate base from path
                    int32 PathStartIndex;
                    if (Rest.FindChar(TEXT('/'), PathStartIndex))
                    {
                        // Has a path component, extract just host:port
                        FString HostPort = Rest.Left(PathStartIndex);
                        FullUrl = Scheme + TEXT("://") + HostPort + AudioUrl;
                    }
                    else
                    {
                        // No path component, entire Rest is host:port
                        FullUrl = Scheme + TEXT("://") + Rest + AudioUrl;
                    }
                }
            }

            // Download the audio file
            TSharedRef<IHttpRequest, ESPMode::ThreadSafe> AudioRequest = FHttpModule::Get().CreateRequest();
            AudioRequest->SetURL(FullUrl);
            AudioRequest->SetVerb(TEXT("GET"));
            AudioRequest->OnProcessRequestComplete().BindUObject(this, &UDuyguDoProccess::OnAudioDownloaded);
            if (AudioRequest->ProcessRequest())
            {
                bDispatchedAudio = true;
            }
        }
    }

    if (!bDispatchedAudio)
    {
        // No audio to download; notify listeners with nullptr
        ImportedSound = nullptr;
        OnCompleted.Broadcast(false, TEXT("No audio URL in server response"), nullptr);
        OnAudioImported.Broadcast(nullptr);
    }
}

void UDuyguDoProccess::OnAudioDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        return;
    }

    int32 Code = Response->GetResponseCode();
    if (Code < 200 || Code >= 300)
    {
        return;
    }

    // Save to disk
    TArray<uint8> AudioBytes = Response->GetContent();
    FString Filename = FPaths::GetCleanFilename(Request->GetURL());
    if (Filename.IsEmpty())
    {
        Filename = FString::Printf(TEXT("duygu_response_%lld.wav"), FDateTime::Now().ToUnixTimestamp());
    }

    FString SaveDir = FPaths::ProjectSavedDir() / TEXT("DuyguDownloads");
    IFileManager::Get().MakeDirectory(*SaveDir, true);
    FString LocalPath = SaveDir / Filename;
    FFileHelper::SaveArrayToFile(AudioBytes, *LocalPath);

    // Try to parse WAV headers and create a SoundWaveProcedural
    USoundWaveProcedural* SW = nullptr;
    if (AudioBytes.Num() > 44)
    {
        // Basic WAV parser: find "data" chunk
        const uint8* DataPtr = AudioBytes.GetData();
        int32 Pos = 12; // skip RIFF header
        int32 DataStart = -1;
        int32 DataSize = 0;
        int32 NumChannels = 0;
        int32 SampleRate = 0;
        int32 BitsPerSample = 0;

        while (Pos + 8 <= AudioBytes.Num())
        {
            FString ChunkID = FString::Printf(TEXT("%c%c%c%c"), DataPtr[Pos], DataPtr[Pos+1], DataPtr[Pos+2], DataPtr[Pos+3]);
            int32 ChunkSize = *(int32*)(DataPtr + Pos + 4);
            if (ChunkID == TEXT("fmt "))
            {
                if (ChunkSize >= 16 && Pos + 8 + 16 <= AudioBytes.Num())
                {
                    int16 AudioFormat = *(int16*)(DataPtr + Pos + 8 + 0);
                    NumChannels = *(int16*)(DataPtr + Pos + 8 + 2);
                    SampleRate = *(int32*)(DataPtr + Pos + 8 + 4);
                    BitsPerSample = *(int16*)(DataPtr + Pos + 8 + 14);
                }
            }
            else if (ChunkID == TEXT("data"))
            {
                DataStart = Pos + 8;
                DataSize = ChunkSize;
                break;
            }

            Pos += 8 + ChunkSize;
        }

        if (DataStart > 0 && DataSize > 0 && NumChannels > 0 && SampleRate > 0 && BitsPerSample == 16)
        {
            TArray<uint8> PCM;
            PCM.Append(AudioBytes.GetData() + DataStart, DataSize);

            SW = NewObject<USoundWaveProcedural>(this, NAME_None, RF_Public | RF_Standalone);
            if (SW)
            {
                SW->SetSampleRate(SampleRate);
                SW->NumChannels = NumChannels;
                SW->bLooping = false;
                
                // Calculate and set duration
                float CalculatedDuration = (float)DataSize / (SampleRate * NumChannels * (BitsPerSample / 8.0f));
                SW->Duration = CalculatedDuration;
                
                // Store PCM data size for later reference
                SW->RawPCMDataSize = DataSize;
                
                // Allocate and copy RawPCMData for VirtualAudioOutput compatibility
                SW->RawPCMData = static_cast<uint8*>(FMemory::Malloc(DataSize));
                if (SW->RawPCMData)
                {
                    FMemory::Memcpy(SW->RawPCMData, PCM.GetData(), DataSize);
                }
                
                // Also queue audio for procedural playback
                SW->QueueAudio(PCM.GetData(), PCM.Num());
                
                UE_LOG(LogTemp, Log, TEXT("DuyguDoProccess: Created SoundWave - Channels: %d, SampleRate: %d, Duration: %.2f, DataSize: %d"), 
                    NumChannels, SampleRate, CalculatedDuration, DataSize);
            }
        }
    }

    // Store and broadcast imported audio (may be null if parse failed)
    ImportedSound = SW;
    
    // Broadcast completion with the sound
    if (ImportedSound)
    {
        OnCompleted.Broadcast(true, PendingResponseMessage, ImportedSound);
    }
    else
    {
        OnCompleted.Broadcast(false, TEXT("Failed to create SoundWave from downloaded audio"), nullptr);
    }
    
    OnAudioImported.Broadcast(ImportedSound);
}

