// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Sound/SoundWave.h"
#include "VirtualAudioOutput.h"
#include "VirtualAudioHelpers.generated.h"

/**
 * Blueprint helpers for Virtual Audio functionality
 */
UCLASS()
class MYPROJECT_API UVirtualAudioHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a new Virtual Audio Output instance
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio", meta = (WorldContext = "WorldContextObject"))
	static UVirtualAudioOutput* CreateVirtualAudioOutput(UObject* WorldContextObject);

	/**
	 * Quick function to play any USoundWave to VB-Audio Cable
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio", meta = (WorldContext = "WorldContextObject"))
	static bool PlaySoundToVBAudioCable(UObject* WorldContextObject, USoundWave* SoundWave, float Volume = 1.0f);

	/**
	 * Play sound to a specific audio device by name
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio", meta = (WorldContext = "WorldContextObject"))
	static bool PlaySoundToDevice(UObject* WorldContextObject, USoundWave* SoundWave, const FString& DeviceName, float Volume = 1.0f);

	/**
	 * Get list of all available audio output devices
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	static TArray<FString> GetAllAudioDevices();
};
