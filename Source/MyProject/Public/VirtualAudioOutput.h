// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Sound/SoundWave.h"
#include "AudioDevice.h"
#include "VirtualAudioOutput.generated.h"

/**
 * Virtual Audio Output - Routes audio to VB-Audio Cable or other virtual devices
 */
UCLASS(BlueprintType)
class MYPROJECT_API UVirtualAudioOutput : public UObject
{
	GENERATED_BODY()

public:
	UVirtualAudioOutput();

	/**
	 * List available audio output devices
	 * @return Array of device names
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	TArray<FString> GetAvailableAudioDevices();

	/**
	 * Set target audio device by name (e.g., "CABLE Input (VB-Audio Virtual Cable)")
	 * @param DeviceName - Name of the audio device
	 * @return Success status
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	bool SetTargetAudioDevice(const FString& DeviceName);

	/**
	 * Play USoundWave directly to the selected virtual audio device
	 * @param SoundWave - Audio to play
	 * @param Volume - Playback volume (0.0 - 1.0)
	 * @return Success status
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	bool PlayToVirtualDevice(USoundWave* SoundWave, float Volume = 1.0f);

	/**
	 * Stop playback
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	void StopPlayback();

	/**
	 * Check if audio is currently playing
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	bool IsPlaying() const;

	/**
	 * Set master volume for virtual output
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	void SetVolume(float Volume);

	/**
	 * Extract PCM data from USoundWave (handles compressed data)
	 * @param SoundWave - Source sound wave
	 * @param OutPCMData - Output PCM data buffer
	 * @return Success status
	 */
	UFUNCTION(BlueprintCallable, Category = "Virtual Audio")
	static bool ExtractPCMData(USoundWave* SoundWave, TArray<uint8>& OutPCMData);

private:
	// Windows Core Audio API için platform-specific implementation
	void* AudioClient;
	void* RenderClient;
	void* AudioDevice_Handle;
	
	FString SelectedDeviceName;
	float MasterVolume;
	bool bIsPlaying;
	
	// Initialize Windows Audio Session
	bool InitializeWindowsAudio(const FString& DeviceName);
	
	// Cleanup audio resources
	void CleanupAudio();
	
	// Write PCM data to device
	bool WriteAudioData(const uint8* Data, int32 DataSize, int32 SampleRate, int32 NumChannels);
};
