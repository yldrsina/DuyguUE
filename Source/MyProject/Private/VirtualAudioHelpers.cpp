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

	UE_LOG(LogTemp, Log, TEXT("VirtualAudioHelpers: Attempting to play sound to device: %s"), *DeviceName);

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
