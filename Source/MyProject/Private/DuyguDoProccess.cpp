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
        OnCompleted.Broadcast(false, TEXT("Empty audio file path"));
        return;
    }

    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *AudioFilePath))
    {
        OnCompleted.Broadcast(false, FString::Printf(TEXT("Failed to read file: %s"), *AudioFilePath));
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
        OnCompleted.Broadcast(false, TEXT("Failed to start HTTP request"));
    }
}

void UDuyguDoProccess::OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        OnCompleted.Broadcast(false, TEXT("HTTP request failed or no response"));
        return;
    }

    int32 Code = Response->GetResponseCode();
    FString ResponseBody = Response->GetContentAsString();

    if (Code >= 200 && Code < 300)
    {
        // Return the raw JSON string as the message so Blueprints can parse it
        OnCompleted.Broadcast(true, ResponseBody);

        // Try to parse JSON and download audio_url if present
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
        TSharedPtr<FJsonObject> JsonObj;
        if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid() && JsonObj->HasField(TEXT("audio_url")))
        {
            FString AudioUrl = JsonObj->GetStringField(TEXT("audio_url"));
            if (!AudioUrl.IsEmpty())
            {
                // Resolve relative URL
                FString FullUrl = AudioUrl;
                if (AudioUrl.StartsWith(TEXT("/")))
                {
                    // Extract scheme+host+port from server URL
                    FString Scheme, Host, Path;
                    int32 Port = 0;
                    if (ServerUrl.Split(TEXT("://"), &Scheme, &Path))
                    {
                        if (Path.Split(TEXT("/"), &Host, nullptr))
                        {
                            // Host may still include path; remove path part
                            int32 SlashIndex;
                            if (Host.FindChar(TEXT('/'), SlashIndex))
                            {
                                Host = Host.Left(SlashIndex);
                            }
                        }
                        FString Base = Scheme + TEXT("://") + Host;
                        FullUrl = Base + AudioUrl;
                    }
                }

                // Download the audio file
                TSharedRef<IHttpRequest, ESPMode::ThreadSafe> AudioRequest = FHttpModule::Get().CreateRequest();
                AudioRequest->SetURL(FullUrl);
                AudioRequest->SetVerb(TEXT("GET"));
                AudioRequest->OnProcessRequestComplete().BindUObject(this, &UDuyguDoProccess::OnAudioDownloaded);
                AudioRequest->ProcessRequest();
            }
        }
    }
    else
    {
        OnCompleted.Broadcast(false, FString::Printf(TEXT("Server returned %d: %s"), Code, *ResponseBody));
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
    // FString Filename = FPaths::GetCleanFilename(Request->GetURL());
    // if (Filename.IsEmpty())
    // {
    //     Filename = FString::Printf(TEXT("duygu_response_%lld.wav"), FDateTime::Now().ToUnixTimestamp());
    // }

    // FString SaveDir = FPaths::ProjectSavedDir() / TEXT("DuyguDownloads");
    // IFileManager::Get().MakeDirectory(*SaveDir, true);
    // FString LocalPath = SaveDir / Filename;
    // if (!FFileHelper::SaveArrayToFile(AudioBytes, *LocalPath))
    // {
    //     return;
    // }

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

            SW = NewObject<USoundWaveProcedural>(GetTransientPackage(), NAME_None, RF_Public | RF_Standalone);
            if (SW)
            {
                SW->SetSampleRate(SampleRate);
                SW->NumChannels = NumChannels;
                SW->bLooping = false;
                SW->QueueAudio(PCM.GetData(), PCM.Num());
            }
        }
    }

    // Broadcast imported audio (may be null if parse failed) with the created SoundWave or nullptr
    OnAudioImported.Broadcast(SW);
}

