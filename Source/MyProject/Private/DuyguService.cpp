// Fill out your copyright notice in the Description page of Project Settings.

#include "DuyguService.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "AudioDevice.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWaveProcedural.h"

UDuyguService::UDuyguService()
	: VirtualAudioOutput(nullptr)
{
}

void UDuyguService::ProcessAudio(USoundWave* InputAudio)
{
	if (!InputAudio)
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: InputAudio is null"));
		OnProcessError.Broadcast(TEXT("Input audio is null"), 0);
		return;
	}

	// Convert SoundWave to WAV format
	TArray<uint8> WavData = SoundWaveToWAV(InputAudio);
	if (WavData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: Failed to convert audio to WAV"));
		OnProcessError.Broadcast(TEXT("Failed to convert audio to WAV"), 0);
		return;
	}

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ServiceURL + TEXT("/process"));
	Request->SetVerb(TEXT("POST"));

	// Create multipart form data
	FString Boundary = FString::Printf(TEXT("----UnrealEngine%d"), FMath::Rand());
	FString MultipartData = CreateMultipartFormData(WavData, Boundary);
	
	Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	Request->SetContentAsString(MultipartData);

	Request->OnProcessRequestComplete().BindUObject(this, &UDuyguService::OnProcessResponseReceived);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("DuyguService: Sent audio processing request"));
}

void UDuyguService::ResetConversation()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ServiceURL + TEXT("/reset"));
	Request->SetVerb(TEXT("POST"));
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("DuyguService: Conversation reset"));
}

void UDuyguService::CheckHealth()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ServiceURL + TEXT("/health"));
	Request->SetVerb(TEXT("GET"));
	Request->OnProcessRequestComplete().BindUObject(this, &UDuyguService::OnHealthCheckReceived);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("DuyguService: Checking health"));
}

void UDuyguService::OnProcessResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: Request failed"));
		OnProcessError.Broadcast(TEXT("Request failed"), 0);
		return;
	}

	int32 StatusCode = Response->GetResponseCode();
	if (StatusCode != 200)
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: Bad status code: %d"), StatusCode);
		OnProcessError.Broadcast(FString::Printf(TEXT("Bad status code: %d"), StatusCode), StatusCode);
		return;
	}

	// Parse JSON response
	FString ResponseString = Response->GetContentAsString();
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: Failed to parse JSON"));
		OnProcessError.Broadcast(TEXT("Failed to parse JSON response"), StatusCode);
		return;
	}

	// Extract data
	FAgentProcessResponse ProcessResponse;
	ProcessResponse.bSuccess = JsonObject->GetBoolField(TEXT("success"));
	ProcessResponse.UserText = JsonObject->GetStringField(TEXT("user_text"));
	ProcessResponse.AssistantResponse = JsonObject->GetStringField(TEXT("assistant_response"));
	ProcessResponse.WordCount = JsonObject->GetIntegerField(TEXT("word_count"));
	ProcessResponse.ConversationLength = JsonObject->GetIntegerField(TEXT("conversation_length"));

	// Parse processing times
	TSharedPtr<FJsonObject> TimesObject = JsonObject->GetObjectField(TEXT("processing_times"));
	ProcessResponse.ProcessingTimes.Step1_STT_ms = TimesObject->GetNumberField(TEXT("step1_stt_ms"));
	ProcessResponse.ProcessingTimes.Step2_AI_Response_ms = TimesObject->GetNumberField(TEXT("step2_ai_response_ms"));
	ProcessResponse.ProcessingTimes.Step3_TTS_ms = TimesObject->GetNumberField(TEXT("step3_tts_ms"));
	ProcessResponse.ProcessingTimes.Total_ms = TimesObject->GetNumberField(TEXT("total_ms"));

	// Download audio
	FString AudioURL = JsonObject->GetStringField(TEXT("audio_url"));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> AudioRequest = FHttpModule::Get().CreateRequest();
	AudioRequest->SetURL(ServiceURL + AudioURL);
	AudioRequest->SetVerb(TEXT("GET"));
	AudioRequest->OnProcessRequestComplete().BindLambda([this, ProcessResponse](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess) mutable
	{
		OnAudioDownloaded(Req, Resp, bSuccess, ProcessResponse);
	});
	AudioRequest->ProcessRequest();
}

void UDuyguService::OnAudioDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FAgentProcessResponse ProcessResponse)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: Audio download failed"));
		OnProcessError.Broadcast(TEXT("Audio download failed"), 0);
		return;
	}

	// Convert downloaded WAV to SoundWave
	const TArray<uint8>& WavData = Response->GetContent();
	USoundWave* SoundWave = CreateSoundWaveFromWAV(WavData);

	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: Failed to create SoundWave"));
		OnProcessError.Broadcast(TEXT("Failed to create SoundWave from response"), 0);
		return;
	}

	ProcessResponse.ResponseAudio = SoundWave;
	ProcessResponse.bSuccess = true;

	// Play to virtual audio device if enabled
	if (bUseVirtualAudioOutput && !VirtualAudioDeviceName.IsEmpty())
	{
		UVirtualAudioOutput* VirtualOutput = GetVirtualAudioOutput();
		if (VirtualOutput)
		{
			VirtualOutput->SetTargetAudioDevice(VirtualAudioDeviceName);
			VirtualOutput->PlayToVirtualDevice(SoundWave, 1.0f);
			UE_LOG(LogTemp, Log, TEXT("DuyguService: Playing to virtual device: %s"), *VirtualAudioDeviceName);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DuyguService: Process complete"));
	OnProcessComplete.Broadcast(ProcessResponse);
}

void UDuyguService::OnHealthCheckReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		UE_LOG(LogTemp, Log, TEXT("DuyguService: Health check OK"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DuyguService: Health check failed"));
	}
}

USoundWave* UDuyguService::CreateSoundWaveFromWAV(const TArray<uint8>& RawWaveData)
{
	if (RawWaveData.Num() == 0)
	{
		return nullptr;
	}

	USoundWave* SoundWave = NewObject<USoundWave>();
	if (!SoundWave)
	{
		return nullptr;
	}

	// Parse WAV header (assumes standard 44-byte header)
	if (RawWaveData.Num() > 44)
	{
		const uint8* WavHeader = RawWaveData.GetData();
		
		// Parse WAV format
		int32 NumChannels = *reinterpret_cast<const int16*>(&WavHeader[22]);
		int32 SampleRate = *reinterpret_cast<const int32*>(&WavHeader[24]);
		int32 BitsPerSample = *reinterpret_cast<const int16*>(&WavHeader[34]);
		int32 DataSize = RawWaveData.Num() - 44;

		// Set sound wave properties
		SoundWave->SetSampleRate(SampleRate);
		SoundWave->NumChannels = NumChannels;
		SoundWave->Duration = (float)DataSize / (SampleRate * NumChannels * (BitsPerSample / 8.0f));
		SoundWave->RawPCMDataSize = DataSize;

		// Copy raw PCM data (skip WAV header)
		SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(DataSize));
		if (SoundWave->RawPCMData)
		{
			FMemory::Memcpy(SoundWave->RawPCMData, RawWaveData.GetData() + 44, DataSize);
			
			UE_LOG(LogTemp, Log, TEXT("DuyguService: Created SoundWave - Channels: %d, SampleRate: %d, Duration: %.2f, DataSize: %d"), 
				NumChannels, SampleRate, SoundWave->Duration, DataSize);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DuyguService: Failed to allocate memory for PCM data"));
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DuyguService: WAV data too small (%d bytes)"), RawWaveData.Num());
		return nullptr;
	}

	return SoundWave;
}

TArray<uint8> UDuyguService::SoundWaveToWAV(USoundWave* SoundWave)
{
	TArray<uint8> OutWavData;

	if (!SoundWave)
	{
		return OutWavData;
	}

	// Get raw PCM data
	const uint8* RawData = SoundWave->RawPCMData;
	int32 RawDataSize = SoundWave->RawPCMDataSize;

	if (RawData && RawDataSize > 0)
	{
		// Create WAV header
		int32 NumChannels = SoundWave->NumChannels;
		int32 SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
		int32 BitsPerSample = 16; // Assuming 16-bit audio
		int32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
		int32 BlockAlign = NumChannels * BitsPerSample / 8;

		// Reserve space for header + data
		OutWavData.SetNum(44 + RawDataSize);

		// RIFF header
		FMemory::Memcpy(&OutWavData[0], "RIFF", 4);
		int32 ChunkSize = 36 + RawDataSize;
		FMemory::Memcpy(&OutWavData[4], &ChunkSize, 4);
		FMemory::Memcpy(&OutWavData[8], "WAVE", 4);

		// fmt subchunk
		FMemory::Memcpy(&OutWavData[12], "fmt ", 4);
		int32 SubChunk1Size = 16;
		FMemory::Memcpy(&OutWavData[16], &SubChunk1Size, 4);
		int16 AudioFormat = 1; // PCM
		FMemory::Memcpy(&OutWavData[20], &AudioFormat, 2);
		FMemory::Memcpy(&OutWavData[22], &NumChannels, 2);
		FMemory::Memcpy(&OutWavData[24], &SampleRate, 4);
		FMemory::Memcpy(&OutWavData[28], &ByteRate, 4);
		FMemory::Memcpy(&OutWavData[32], &BlockAlign, 2);
		FMemory::Memcpy(&OutWavData[34], &BitsPerSample, 2);

		// data subchunk
		FMemory::Memcpy(&OutWavData[36], "data", 4);
		FMemory::Memcpy(&OutWavData[40], &RawDataSize, 4);
		FMemory::Memcpy(&OutWavData[44], RawData, RawDataSize);
	}

	return OutWavData;
}

FString UDuyguService::CreateMultipartFormData(const TArray<uint8>& AudioData, const FString& Boundary)
{
	FString MultipartData;

	// Add audio file part
	MultipartData += FString::Printf(TEXT("--%s\r\n"), *Boundary);
	MultipartData += TEXT("Content-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\n");
	MultipartData += TEXT("Content-Type: audio/wav\r\n\r\n");

	// Convert binary data to string (Base64 or raw bytes)
	// Note: For proper multipart, you'd need to handle binary data properly
	// This is a simplified version - in production, use proper multipart encoding
	FString AudioBase64 = FBase64::Encode(AudioData);
	MultipartData += AudioBase64;

	MultipartData += FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary);

	return MultipartData;
}

UVirtualAudioOutput* UDuyguService::GetVirtualAudioOutput()
{
	if (!VirtualAudioOutput)
	{
		VirtualAudioOutput = NewObject<UVirtualAudioOutput>(this);
	}
	return VirtualAudioOutput;
}

TArray<FString> UDuyguService::GetAvailableAudioDevices()
{
	UVirtualAudioOutput* VirtualOutput = GetVirtualAudioOutput();
	if (VirtualOutput)
	{
		return VirtualOutput->GetAvailableAudioDevices();
	}
	return TArray<FString>();
}
