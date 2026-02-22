// Fill out your copyright notice in the Description page of Project Settings.

#include "VirtualAudioOutput.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"

// Windows Core Audio API includes
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include "Windows/HideWindowsPlatformTypes.h"

// Link required libraries
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#endif

UVirtualAudioOutput::UVirtualAudioOutput()
	: AudioClient(nullptr)
	, RenderClient(nullptr)
	, AudioDevice_Handle(nullptr)
	, MasterVolume(1.0f)
	, bIsPlaying(false)
{
}

TArray<FString> UVirtualAudioOutput::GetAvailableAudioDevices()
{
	TArray<FString> DeviceNames;

#if PLATFORM_WINDOWS
	CoInitialize(nullptr);

	IMMDeviceEnumerator* pEnumerator = nullptr;
	IMMDeviceCollection* pCollection = nullptr;

	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), 
		nullptr,
		CLSCTX_ALL, 
		__uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator);

	if (SUCCEEDED(hr))
	{
		hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
		
		if (SUCCEEDED(hr))
		{
			UINT count;
			pCollection->GetCount(&count);

			for (UINT i = 0; i < count; i++)
			{
				IMMDevice* pDevice = nullptr;
				hr = pCollection->Item(i, &pDevice);

				if (SUCCEEDED(hr))
				{
					IPropertyStore* pProps = nullptr;
					hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);

					if (SUCCEEDED(hr))
					{
						PROPVARIANT varName;
						PropVariantInit(&varName);

						hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
						if (SUCCEEDED(hr))
						{
							FString DeviceName = FString(varName.pwszVal);
							DeviceNames.Add(DeviceName);
							
							UE_LOG(LogTemp, Log, TEXT("Found Audio Device: %s"), *DeviceName);
						}

						PropVariantClear(&varName);
						pProps->Release();
					}
					pDevice->Release();
				}
			}
			pCollection->Release();
		}
		pEnumerator->Release();
	}

	CoUninitialize();
#endif

	return DeviceNames;
}

bool UVirtualAudioOutput::SetTargetAudioDevice(const FString& DeviceName)
{
	SelectedDeviceName = DeviceName;
	
	// Cleanup audio client and render client (but keep device handle if same device)
	CleanupAudio();
	
	// If we're changing to a new device, release the old device handle
#if PLATFORM_WINDOWS
	if (AudioDevice_Handle)
	{
		static_cast<IMMDevice*>(AudioDevice_Handle)->Release();
		AudioDevice_Handle = nullptr;
	}
#endif
	
	bool bSuccess = InitializeWindowsAudio(DeviceName);
	
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Successfully set device to %s"), *DeviceName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Failed to set device to %s"), *DeviceName);
	}
	
	return bSuccess;
}

bool UVirtualAudioOutput::InitializeWindowsAudio(const FString& DeviceName)
{
#if PLATFORM_WINDOWS
	CoInitialize(nullptr);

	IMMDeviceEnumerator* pEnumerator = nullptr;
	IMMDeviceCollection* pCollection = nullptr;
	IMMDevice* pDevice = nullptr;

	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		nullptr,
		CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator);

	if (FAILED(hr))
	{
		return false;
	}

	// Find device by name
	hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
	
	if (SUCCEEDED(hr))
	{
		UINT count;
		pCollection->GetCount(&count);

		for (UINT i = 0; i < count; i++)
		{
			IMMDevice* pTempDevice = nullptr;
			hr = pCollection->Item(i, &pTempDevice);

			if (SUCCEEDED(hr))
			{
				IPropertyStore* pProps = nullptr;
				hr = pTempDevice->OpenPropertyStore(STGM_READ, &pProps);

				if (SUCCEEDED(hr))
				{
					PROPVARIANT varName;
					PropVariantInit(&varName);

					hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
					if (SUCCEEDED(hr))
					{
						FString CurrentDeviceName = FString(varName.pwszVal);
						
						if (CurrentDeviceName == DeviceName)
						{
							pDevice = pTempDevice;
							pDevice->AddRef();
						}
					}

					PropVariantClear(&varName);
					pProps->Release();
				}
				pTempDevice->Release();
			}

			if (pDevice != nullptr)
			{
				break;
			}
		}
		pCollection->Release();
	}

	if (pDevice == nullptr)
	{
		pEnumerator->Release();
		return false;
	}

	// Store the device handle (don't activate audio client yet)
	// Audio client will be created fresh for each playback
	AudioDevice_Handle = pDevice;
	
	pEnumerator->Release();
	
	return true;
#else
	return false;
#endif
}

bool UVirtualAudioOutput::PlayToVirtualDevice(USoundWave* SoundWave, float Volume)
{
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: SoundWave is null"));
		return false;
	}

	if (!AudioDevice_Handle)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: No audio device selected. Call SetTargetAudioDevice first."));
		return false;
	}

#if PLATFORM_WINDOWS
	// Stop any existing playback and cleanup
	if (bIsPlaying)
	{
		StopPlayback();
		FPlatformProcess::Sleep(0.1f); // Give some time for thread to finish
	}
	
	// Cleanup and reinitialize audio client for new playback
	if (RenderClient)
	{
		static_cast<IAudioRenderClient*>(RenderClient)->Release();
		RenderClient = nullptr;
	}
	
	if (AudioClient)
	{
		static_cast<IAudioClient*>(AudioClient)->Release();
		AudioClient = nullptr;
	}
	
	// Reinitialize audio client
	if (!AudioDevice_Handle)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: No audio device selected. Call SetTargetAudioDevice first."));
		return false;
	}
	
	IMMDevice* pDevice = static_cast<IMMDevice*>(AudioDevice_Handle);
	IAudioClient* pAudioClient = nullptr;
	
	HRESULT hr = pDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL,
		nullptr,
		(void**)&pAudioClient);
	
	if (FAILED(hr))
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Failed to activate audio client (HRESULT: 0x%08X)"), hr);
		return false;
	}
	
	AudioClient = pAudioClient;

	// Get sound wave properties
	int32 SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
	int32 NumChannels = SoundWave->NumChannels;
	
	UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: SoundWave Info - Duration: %.2f, Channels: %d, SampleRate: %d, RawPCMDataSize: %d"), 
		SoundWave->Duration, NumChannels, SampleRate, SoundWave->RawPCMDataSize);
	
	// Try to get PCM data - first check if RawPCMData is available
	const uint8* PCMData = SoundWave->RawPCMData;
	int32 PCMDataSize = SoundWave->RawPCMDataSize;
	
	// Check if this is a USoundWaveProcedural
	USoundWaveProcedural* ProceduralWave = Cast<USoundWaveProcedural>(SoundWave);
	
	// If no raw PCM data, try to get it from the bulk data or procedural buffer
	TArray<uint8> DecompressedData;
	bool bUsingDecompressedData = false;
	
	if (!PCMData || PCMDataSize == 0)
	{
		if (ProceduralWave)
		{
			// For procedural waves, we need to extract from the queued audio buffer
			UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Detected USoundWaveProcedural, extracting from buffer..."));
			
			// Calculate expected size from duration if available
			if (SoundWave->Duration > 0)
			{
				int32 NumSamples = SoundWave->Duration * SampleRate * NumChannels;
				int32 ExpectedSize = NumSamples * sizeof(int16);
				DecompressedData.SetNumUninitialized(ExpectedSize);
				
				// Try to get audio buffer from procedural wave
				// Note: This is a workaround - procedural waves stream data
				// For now, we'll create silence if we can't extract
				UE_LOG(LogTemp, Warning, TEXT("VirtualAudioOutput: USoundWaveProcedural detected but buffer extraction not fully implemented"));
				UE_LOG(LogTemp, Warning, TEXT("VirtualAudioOutput: Creating silent buffer of expected size: %d bytes"), ExpectedSize);
				
				// Fill with zeros (silence) as placeholder
				FMemory::Memzero(DecompressedData.GetData(), ExpectedSize);
				
				PCMData = DecompressedData.GetData();
				PCMDataSize = DecompressedData.Num();
				bUsingDecompressedData = true;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("VirtualAudioOutput: RawPCMData not available, attempting to extract PCM..."));
			
			// Use the helper function to extract PCM data
			if (ExtractPCMData(SoundWave, DecompressedData))
			{
				PCMData = DecompressedData.GetData();
				PCMDataSize = DecompressedData.Num();
				bUsingDecompressedData = true;
				UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Successfully extracted PCM data (%d bytes)"), PCMDataSize);
			}
		}
	}
	
	if (!PCMData || PCMDataSize == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Invalid PCM data in SoundWave (Duration: %.2f, Channels: %d, SampleRate: %d)"), 
			SoundWave->Duration, NumChannels, SampleRate);
		return false;
	}
	
	UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Playing audio - Size: %d bytes, Channels: %d, SampleRate: %d Hz"), 
		PCMDataSize, NumChannels, SampleRate);

	// Set up wave format
	WAVEFORMATEX waveFormat = {};
	waveFormat.wFormatTag = WAVE_FORMAT_PCM;
	waveFormat.nChannels = NumChannels;
	waveFormat.nSamplesPerSec = SampleRate;
	waveFormat.wBitsPerSample = 16;
	waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
	waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
	waveFormat.cbSize = 0;

	// Check if format is supported
	WAVEFORMATEX* pClosestMatch = nullptr;
	hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &waveFormat, &pClosestMatch);
	
	bool bNeedsConversion = false;
	WAVEFORMATEX finalFormat = waveFormat;
	
	if (hr == S_FALSE && pClosestMatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("VirtualAudioOutput: Exact format not supported, will convert to closest match"));
		UE_LOG(LogTemp, Warning, TEXT("  -> Source: %d Hz, %d ch, %d bit"), SampleRate, NumChannels, 16);
		UE_LOG(LogTemp, Warning, TEXT("  -> Target: %d Hz, %d ch, %d bit"), 
			pClosestMatch->nSamplesPerSec, pClosestMatch->nChannels, pClosestMatch->wBitsPerSample);
		
		finalFormat = *pClosestMatch;
		
		// Fix format tag and recalculate block align for 32-bit float
		if (finalFormat.wBitsPerSample == 32)
		{
			finalFormat.wFormatTag = 3; // WAVE_FORMAT_IEEE_FLOAT
			finalFormat.nBlockAlign = (finalFormat.nChannels * finalFormat.wBitsPerSample) / 8;
			finalFormat.nAvgBytesPerSec = finalFormat.nSamplesPerSec * finalFormat.nBlockAlign;
			finalFormat.cbSize = 0;
			UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Configured WAVE_FORMAT_IEEE_FLOAT - BlockAlign: %d, AvgBytesPerSec: %d"),
				finalFormat.nBlockAlign, finalFormat.nAvgBytesPerSec);
		}
		
		bNeedsConversion = true;
		CoTaskMemFree(pClosestMatch);
	}
	else if (FAILED(hr))
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Format not supported (HRESULT: 0x%08X)"), hr);
		if (pClosestMatch)
		{
			CoTaskMemFree(pClosestMatch);
		}
		pAudioClient->Release();
		AudioClient = nullptr;
		return false;
	}
	
	// Convert audio data if needed
	TArray<uint8> ConvertedData;
	if (bNeedsConversion)
	{
		// Resample and convert format
		int32 SourceSampleRate = SampleRate;
		int32 TargetSampleRate = finalFormat.nSamplesPerSec;
		int32 SourceChannels = NumChannels;
		int32 TargetChannels = finalFormat.nChannels;
		int32 TargetBitsPerSample = finalFormat.wBitsPerSample;
		
		int32 SourceSampleCount = PCMDataSize / (SourceChannels * 2); // 2 bytes per sample (16-bit)
		
		// Use int64 to prevent overflow during multiplication
		int64 TargetSampleCount64 = ((int64)SourceSampleCount * (int64)TargetSampleRate) / (int64)SourceSampleRate;
		int32 TargetSampleCount = (int32)TargetSampleCount64;
		
		int32 TargetBytesPerSample = TargetBitsPerSample / 8;
		int32 ConvertedDataSize = TargetSampleCount * TargetChannels * TargetBytesPerSample;
		
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Conversion parameters:"));
		UE_LOG(LogTemp, Log, TEXT("  -> Source: %d samples, %d Hz, %d ch, 16-bit"), SourceSampleCount, SourceSampleRate, SourceChannels);
		UE_LOG(LogTemp, Log, TEXT("  -> Target: %d samples, %d Hz, %d ch, %d-bit"), TargetSampleCount, TargetSampleRate, TargetChannels, TargetBitsPerSample);
		UE_LOG(LogTemp, Log, TEXT("  -> Output size: %d bytes"), ConvertedDataSize);
		
		ConvertedData.SetNumZeroed(ConvertedDataSize);
		
		// Simple nearest-neighbor resampling with channel and bit depth conversion
		for (int32 i = 0; i < TargetSampleCount; i++)
		{
			// Calculate source sample index
			int32 SourceIndex = (i * SourceSampleRate) / TargetSampleRate;
			if (SourceIndex >= SourceSampleCount) SourceIndex = SourceSampleCount - 1;
			
			// Read source sample (16-bit mono/stereo)
			int16 SourceSample = 0;
			if (SourceChannels == 1)
			{
				SourceSample = *reinterpret_cast<const int16*>(&PCMData[SourceIndex * 2]);
			}
			else
			{
				// Average channels if source is stereo
				int16 L = *reinterpret_cast<const int16*>(&PCMData[SourceIndex * 4]);
				int16 R = *reinterpret_cast<const int16*>(&PCMData[SourceIndex * 4 + 2]);
				SourceSample = (L + R) / 2;
			}
			
			// Write to target format with volume applied
			if (TargetBitsPerSample == 16)
			{
				// Apply volume and write as 16-bit
				int16 VolumedSample = (int16)(SourceSample * Volume * MasterVolume);
				for (int32 ch = 0; ch < TargetChannels; ch++)
				{
					int32 TargetIndex = (i * TargetChannels + ch) * 2;
					*reinterpret_cast<int16*>(&ConvertedData[TargetIndex]) = VolumedSample;
				}
			}
			else if (TargetBitsPerSample == 32)
			{
				// Convert 16-bit to 32-bit float (-1.0 to 1.0) with volume
				float FloatSample = (SourceSample / 32768.0f) * Volume * MasterVolume;
				for (int32 ch = 0; ch < TargetChannels; ch++)
				{
					int32 TargetIndex = (i * TargetChannels + ch) * 4;
					*reinterpret_cast<float*>(&ConvertedData[TargetIndex]) = FloatSample;
				}
			}
		}
		
		// Update pointers to use converted data
		PCMData = ConvertedData.GetData();
		PCMDataSize = ConvertedData.Num();
		NumChannels = TargetChannels;
		SampleRate = TargetSampleRate;
		bUsingDecompressedData = true; // Mark as using temporary buffer
		
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Conversion complete - New size: %d bytes"), PCMDataSize);
	}

	// Initialize audio client with final format
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		0,
		10000000, // 1 second buffer
		0,
		&finalFormat,
		nullptr);

	if (FAILED(hr))
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Failed to initialize audio client (HRESULT: 0x%08X)"), hr);
		UE_LOG(LogTemp, Error, TEXT("  -> Common errors:"));
		UE_LOG(LogTemp, Error, TEXT("  -> 0x88890008 (AUDCLNT_E_ALREADY_INITIALIZED): Client already initialized"));
		UE_LOG(LogTemp, Error, TEXT("  -> 0x88890019 (AUDCLNT_E_UNSUPPORTED_FORMAT): Format not supported"));
		UE_LOG(LogTemp, Error, TEXT("  -> 0x8889000A (AUDCLNT_E_DEVICE_IN_USE): Device in use by exclusive mode"));
		pAudioClient->Release();
		AudioClient = nullptr;
		return false;
	}

	// Get buffer size
	UINT32 bufferFrameCount;
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	if (FAILED(hr))
	{
		return false;
	}

	// Get render client
	IAudioRenderClient* pRenderClient = nullptr;
	hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
	if (FAILED(hr))
	{
		return false;
	}

	RenderClient = pRenderClient;

	// Copy PCM data if using temporary buffer (to avoid dangling pointer)
	TArray<uint8> PCMDataCopy;
	if (bUsingDecompressedData || bNeedsConversion)
	{
		// Copy either decompressed data or converted data
		if (bNeedsConversion && ConvertedData.Num() > 0)
		{
			PCMDataCopy = ConvertedData;
		}
		else
		{
			PCMDataCopy = DecompressedData;
		}
	}
	
	// Calculate frame size for lambda
	int32 BytesPerSample = finalFormat.wBitsPerSample / 8;
	int32 FrameSize = NumChannels * BytesPerSample;
	
	// Start playback on a separate thread
	bIsPlaying = true;
	
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, PCMData, PCMDataSize, Volume, SampleRate, NumChannels, pAudioClient, pRenderClient, bufferFrameCount, bUsingDecompressedData, bNeedsConversion, PCMDataCopy, FrameSize]()
	{
		HRESULT hr = pAudioClient->Start();
		
		if (FAILED(hr))
		{
			bIsPlaying = false;
			return;
		}
		
		// Use copied data if using temporary buffer
		const uint8* ActualPCMData = (bUsingDecompressedData || bNeedsConversion) ? PCMDataCopy.GetData() : PCMData;

		int32 BytesWritten = 0;

		while (bIsPlaying && BytesWritten < PCMDataSize)
		{
			// Wait for buffer to be ready
			UINT32 numFramesPadding;
			hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
			
			if (FAILED(hr))
			{
				break;
			}

			UINT32 numFramesAvailable = bufferFrameCount - numFramesPadding;

			if (numFramesAvailable > 0)
			{
				BYTE* pData = nullptr;
				hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
				
				if (SUCCEEDED(hr))
				{
					int32 BytesToWrite = FMath::Min((int32)(numFramesAvailable * FrameSize), PCMDataSize - BytesWritten);
					int32 FramesToWrite = BytesToWrite / FrameSize;

					// Copy data directly (volume already applied during conversion if needed)
					FMemory::Memcpy(pData, &ActualPCMData[BytesWritten], BytesToWrite);

					hr = pRenderClient->ReleaseBuffer(FramesToWrite, 0);
					BytesWritten += BytesToWrite;
				}
			}

			// Small sleep to prevent busy waiting
			FPlatformProcess::Sleep(0.01f);
		}

		pAudioClient->Stop();
		bIsPlaying = false;
		
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Playback completed"));
	});

	return true;
#else
	UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Not supported on this platform"));
	return false;
#endif
}

void UVirtualAudioOutput::StopPlayback()
{
	bIsPlaying = false;
}

bool UVirtualAudioOutput::IsPlaying() const
{
	return bIsPlaying;
}

void UVirtualAudioOutput::SetVolume(float Volume)
{
	MasterVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
}

void UVirtualAudioOutput::CleanupAudio()
{
#if PLATFORM_WINDOWS
	bIsPlaying = false;

	if (RenderClient)
	{
		static_cast<IAudioRenderClient*>(RenderClient)->Release();
		RenderClient = nullptr;
	}

	if (AudioClient)
	{
		static_cast<IAudioClient*>(AudioClient)->Release();
		AudioClient = nullptr;
	}

	// DON'T release AudioDevice_Handle here - we need it for subsequent playbacks
	// It will be released only when SetTargetAudioDevice is called with a new device
	// or when the object is destroyed
#endif
}

bool UVirtualAudioOutput::ExtractPCMData(USoundWave* SoundWave, TArray<uint8>& OutPCMData)
{
	OutPCMData.Empty();
	
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: SoundWave is null"));
		return false;
	}

	// First try RawPCMData
	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
	{
		OutPCMData.Append(SoundWave->RawPCMData, SoundWave->RawPCMDataSize);
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Extracted %d bytes from RawPCMData"), OutPCMData.Num());
		return true;
	}

	// Try to get compressed data and decompress it
	UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Attempting to load and decompress audio..."));
	
	// Force load the sound wave data
	SoundWave->ConditionalPostLoad();
	
	// Check RawPCMData again after loading
	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
	{
		OutPCMData.Append(SoundWave->RawPCMData, SoundWave->RawPCMDataSize);
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Extracted %d bytes after loading"), OutPCMData.Num());
		return true;
	}

	// Try accessing compressed data directly
	// Common formats: OGG, OPUS, ADPCM
	static const FName OGGFormat(TEXT("OGG"));
	static const FName OpusFormat(TEXT("OPUS"));
	static const FName ADPCMFormat(TEXT("ADPCM"));
	
	if (SoundWave->HasCompressedData(OGGFormat) || SoundWave->HasCompressedData(OpusFormat) || SoundWave->HasCompressedData(ADPCMFormat))
	{
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Has compressed data, creating decompressed buffer..."));
		
		// Calculate expected size
		int32 SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
		int32 NumChannels = FMath::Max(1, SoundWave->NumChannels);
		float Duration = FMath::Max(0.1f, SoundWave->Duration);
		int32 NumSamples = Duration * SampleRate * NumChannels;
		int32 ExpectedSize = NumSamples * sizeof(int16);
		
		UE_LOG(LogTemp, Log, TEXT("VirtualAudioOutput: Expected size: %d bytes (Duration: %.2f, SR: %d, Channels: %d)"), 
			ExpectedSize, Duration, SampleRate, NumChannels);
		
		// Pre-allocate the buffer
		OutPCMData.SetNumUninitialized(ExpectedSize);
		
		// You may need to implement custom decompression here
		// For now, return false and log that manual decompression is needed
		OutPCMData.Empty();
	}

	UE_LOG(LogTemp, Error, TEXT("VirtualAudioOutput: Failed to extract PCM data. SoundWave may need to be imported as uncompressed PCM."));
	UE_LOG(LogTemp, Error, TEXT("  -> In Unreal Editor: Right-click sound asset -> Asset Actions -> Bulk Edit via Property Matrix"));
	UE_LOG(LogTemp, Error, TEXT("  -> Set 'Loading Behavior' to 'Retain On Load' or use USoundWaveProcedural"));
	
	return false;
}
