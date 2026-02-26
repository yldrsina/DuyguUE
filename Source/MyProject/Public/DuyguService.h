// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Sound/SoundWave.h"
#include "DuyguService.generated.h"

USTRUCT(BlueprintType)
struct FAgentProcessingTimes
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float Step1_STT_ms;

	UPROPERTY(BlueprintReadOnly)
	float Step2_AI_Response_ms;

	UPROPERTY(BlueprintReadOnly)
	float Step3_TTS_ms;

	UPROPERTY(BlueprintReadOnly)
	float Total_ms;
};

USTRUCT(BlueprintType)
struct FAgentProcessResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess;

	UPROPERTY(BlueprintReadOnly)
	FString UserText;

	UPROPERTY(BlueprintReadOnly)
	FString AssistantResponse;

	UPROPERTY(BlueprintReadOnly)
	USoundWave* ResponseAudio;

	UPROPERTY(BlueprintReadOnly)
	FAgentProcessingTimes ProcessingTimes;

	UPROPERTY(BlueprintReadOnly)
	int32 WordCount;

	UPROPERTY(BlueprintReadOnly)
	int32 ConversationLength;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProcessComplete, FAgentProcessResponse, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnProcessError, FString, ErrorMessage, int32, StatusCode);

/**
 * Duygu Service Client for processing audio with STT -> AI -> TTS pipeline
 */
UCLASS(BlueprintType)
class MYPROJECT_API UDuyguService : public UObject
{
	GENERATED_BODY()

public:
	UDuyguService();

	/**
	 * Process audio through Duygu Service (STT -> AI -> TTS)
	 * @param InputAudio - USoundWave to send to the service
	 */
	UFUNCTION(BlueprintCallable, Category = "Duygu Service")
	void ProcessAudio(USoundWave* InputAudio);

	/**
	 * Reset conversation history
	 */
	UFUNCTION(BlueprintCallable, Category = "Duygu Service")
	void ResetConversation();

	/**
	 * Check service health
	 */
	UFUNCTION(BlueprintCallable, Category = "Duygu Service")
	void CheckHealth();

	/** Called when audio processing is complete */
	UPROPERTY(BlueprintAssignable, Category = "Duygu Service")
	FOnProcessComplete OnProcessComplete;

	/** Called when an error occurs */
	UPROPERTY(BlueprintAssignable, Category = "Duygu Service")
	FOnProcessError OnProcessError;

	/** Service URL configuration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Duygu Service")
	FString ServiceURL = TEXT("http://localhost:5003");

private:
	void OnProcessResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void OnAudioDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FAgentProcessResponse ProcessResponse);
	void OnHealthCheckReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	
	USoundWave* CreateSoundWaveFromWAV(const TArray<uint8>& RawWaveData);
	TArray<uint8> SoundWaveToWAV(USoundWave* SoundWave);
	FString CreateMultipartFormData(const TArray<uint8>& AudioData, const FString& Boundary);
};
