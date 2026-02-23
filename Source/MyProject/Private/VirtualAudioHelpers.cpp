// Fill out your copyright notice in the Description page of Project Settings.

#include "VirtualAudioHelpers.h"
#include "Engine/World.h"

UVirtualAudioOutput* UVirtualAudioHelpers::CreateVirtualAudioOutput(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UVirtualAudioOutput* VirtualOutput = NewObject<UVirtualAudioOutput>(WorldContextObject);
	return VirtualOutput;
}

bool UVirtualAudioHelpers::PlaySoundToVBAudioCable(UObject* WorldContextObject, USoundWave* SoundWave, float Volume)
{
	return PlaySoundToDevice(WorldContextObject, SoundWave, TEXT("CABLE Input (VB-Audio Virtual Cable)"), Volume);
}

bool UVirtualAudioHelpers::PlaySoundToDevice(UObject* WorldContextObject, USoundWave* SoundWave, const FString& DeviceName, float Volume)
{
	if (!WorldContextObject || !SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Invalid parameters (WorldContext: %s, SoundWave: %s)"),
			WorldContextObject ? TEXT("Valid") : TEXT("NULL"),
			SoundWave ? TEXT("Valid") : TEXT("NULL"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Attempting to play sound '%s' to device: %s"), *SoundWave->GetName(), *DeviceName);

	// Auto-preload if PCM data is not ready
	if (!IsSoundWaveReady(SoundWave))
	{
		UE_LOG(LogTemp, Warning, TEXT("VirtualAudioHelpers: SoundWave PCM data not cached, attempting to preload..."));
		if (!PreloadSoundWave(SoundWave))
		{
			UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Failed to preload SoundWave. Playback may fail."));
			// Continue anyway - VirtualAudioOutput will try its own extraction methods
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: SoundWave has %d bytes of cached PCM data"), SoundWave->RawPCMDataSize);
	}

	UVirtualAudioOutput* VirtualOutput = CreateVirtualAudioOutput(WorldContextObject);
	if (!VirtualOutput)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Failed to create VirtualAudioOutput instance"));
		return false;
	}

	// First, list available devices to help with debugging
	TArray<FString> AvailableDevices = VirtualOutput->GetAvailableAudioDevices();
	UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Found %d audio devices:"), AvailableDevices.Num());
	for (const FString& Device : AvailableDevices)
	{
		UE_LOG(LogTemp, Log, TEXT("  - %s"), *Device);
	}

	bool bDeviceSet = VirtualOutput->SetTargetAudioDevice(DeviceName);
	if (!bDeviceSet)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Failed to set device: %s"), *DeviceName);
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Please check the device name matches exactly one from the list above"));
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Common VB-Audio Cable names:"));
		UE_LOG(LogTemp, Error, TEXT("  - CABLE Input (VB-Audio Virtual Cable)"));
		UE_LOG(LogTemp, Error, TEXT("  - VB-Audio Virtual Cable"));
		UE_LOG(LogTemp, Error, TEXT("  - CABLE-A Input (VB-Audio Cable A)"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Device set successfully, starting playback..."));

	bool bSuccess = VirtualOutput->PlayToVirtualDevice(SoundWave, Volume);
	
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Playback started successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Playback failed"));
	}
	
	return bSuccess;
}

TArray<FString> UVirtualAudioHelpers::GetAllAudioDevices()
{
	UVirtualAudioOutput* TempOutput = NewObject<UVirtualAudioOutput>();
	TArray<FString> Devices = TempOutput->GetAvailableAudioDevices();
	return Devices;
}

bool UVirtualAudioHelpers::PreloadSoundWave(USoundWave* SoundWave)
{
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioHelpers: Cannot preload null SoundWave"));
		return false;
	}

	// Check if already loaded
	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: SoundWave already has cached PCM data (%d bytes)"), SoundWave->RawPCMDataSize);
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Preloading SoundWave '%s'..."), *SoundWave->GetName());

	// Try multiple methods to cache PCM data
	
	// Method 1: ConditionalPostLoad
	SoundWave->ConditionalPostLoad();
	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Successfully preloaded %d bytes (ConditionalPostLoad)"), SoundWave->RawPCMDataSize);
		return true;
	}

	// Method 2: Initialize audio resource
	if (GEngine)
	{
		FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw();
		if (AudioDevice)
		{
			SoundWave->InitAudioResource(SoundWave->GetRuntimeFormat());
			
			if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Successfully preloaded %d bytes (InitAudioResource)"), SoundWave->RawPCMDataSize);
				return true;
			}

			// Method 3: Precache on audio device
			AudioDevice->Precache(SoundWave, true, true);
			FPlatformProcess::Sleep(0.15f); // Wait for async operation
			
			if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Successfully preloaded %d bytes (Precache)"), SoundWave->RawPCMDataSize);
				return true;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("VirtualAudioHelpers: Failed to preload SoundWave. Check asset's Loading Behavior setting."));
	UE_LOG(LogTemp, Warning, TEXT("  -> Recommended: Set 'Loading Behavior Type' to 'FORCE_INLINE' or 'RETAIN_ON_LOAD'"));
	return false;
}

bool UVirtualAudioHelpers::IsSoundWaveReady(USoundWave* SoundWave)
{
	if (!SoundWave)
	{
		return false;
	}

	return (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0);
}
