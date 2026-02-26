#include "MultiAudioOutput.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Engine/Engine.h"
#include <cmath>
#include <algorithm>

#if PLATFORM_WINDOWS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#pragma comment(lib, "Ole32.lib")
#endif

UMultiAudioOutput::UMultiAudioOutput()
{
}

// Define log category
DEFINE_LOG_CATEGORY(LogMultiAudio);

// Helper: convert 16-bit PCM to float samples (interleaved)
static void DecodePCM16ToFloat(const TArray<uint8>& InPCM, TArray<float>& OutFloats, int32 InChannels)
{
    int32 bytesPerSample = 2;
    int32 totalSamples = InPCM.Num() / bytesPerSample;
    OutFloats.Reset();
    OutFloats.AddUninitialized(totalSamples);
    const int16* src = reinterpret_cast<const int16*>(InPCM.GetData());
    for (int32 i = 0; i < totalSamples; ++i)
    {
        OutFloats[i] = static_cast<float>(src[i]) / 32768.0f;
    }
}

// Helper: resample per-channel linear interpolation. Input and output are interleaved.
static void ResampleLinear(const TArray<float>& InInterleaved, TArray<float>& OutInterleaved, int32 InRate, int32 OutRate, int32 InChannels)
{
    if (InRate == OutRate)
    {
        OutInterleaved = InInterleaved;
        return;
    }

    const double ratio = static_cast<double>(InRate) / static_cast<double>(OutRate);
    int32 inTotalFrames = InInterleaved.Num() / InChannels;
    int32 outTotalFrames = static_cast<int32>(std::ceil(inTotalFrames * (static_cast<double>(OutRate) / InRate)));
    OutInterleaved.Reset();
    OutInterleaved.AddUninitialized(outTotalFrames * InChannels);

    for (int32 outFrame = 0; outFrame < outTotalFrames; ++outFrame)
    {
        double srcPos = outFrame * ratio; // position in input frames
        int32 srcIndex = static_cast<int32>(std::floor(srcPos));
        double frac = srcPos - srcIndex;

        for (int32 ch = 0; ch < InChannels; ++ch)
        {
            float s0 = 0.f;
            float s1 = 0.f;
            int32 i0 = srcIndex;
            int32 i1 = srcIndex + 1;
            if (i0 < inTotalFrames)
            {
                s0 = InInterleaved[i0 * InChannels + ch];
            }
            if (i1 < inTotalFrames)
            {
                s1 = InInterleaved[i1 * InChannels + ch];
            }
            OutInterleaved[outFrame * InChannels + ch] = static_cast<float>(s0 * (1.0 - frac) + s1 * frac);
        }
    }
}

// Helper: convert interleaved floats to target format (PCM16 or float32 interleaved)
static void ConvertFloatToTarget(const TArray<float>& InFloats, TArray<uint8>& OutPCM, int32 OutChannels, WORD OutBitsPerSample, WORD OutFormatTag)
{
    if (OutFormatTag == WAVE_FORMAT_IEEE_FLOAT && OutBitsPerSample == 32)
    {
        // copy float32
        OutPCM.Reset();
        OutPCM.AddUninitialized(InFloats.Num() * sizeof(float));
        FMemory::Memcpy(OutPCM.GetData(), InFloats.GetData(), InFloats.Num() * sizeof(float));
        return;
    }

    if (OutFormatTag == WAVE_FORMAT_PCM && OutBitsPerSample == 16)
    {
        OutPCM.Reset();
        OutPCM.AddUninitialized(InFloats.Num() * sizeof(int16));
        int16* dst = reinterpret_cast<int16*>(OutPCM.GetData());
        for (int32 i = 0; i < InFloats.Num(); ++i)
        {
            float v = InFloats[i];
            v = FMath::Clamp(v, -1.0f, 1.0f);
            dst[i] = static_cast<int16>(v * 32767.0f);
        }
        return;
    }

    // Fallback: attempt float32
    OutPCM.Reset();
    OutPCM.AddUninitialized(InFloats.Num() * sizeof(float));
    FMemory::Memcpy(OutPCM.GetData(), InFloats.GetData(), InFloats.Num() * sizeof(float));
}

// High-level conversion: takes source 16-bit PCM interleaved and outputs device-format PCM interleaved
static bool ConvertPCMToDeviceFormat(const TArray<uint8>& InPCM, int32 InSampleRate, int32 InChannels, int32 InBitsPerSample, const WAVEFORMATEX* DeviceWF, TArray<uint8>& OutPCM, UINT32& OutBytesPerFrame)
{
    if (InBitsPerSample != 16)
    {
        UE_LOG(LogMultiAudio, Warning, TEXT("ConvertPCMToDeviceFormat: only 16-bit source supported currently."));
        return false;
    }

    // Decode source to floats
    TArray<float> srcFloats;
    DecodePCM16ToFloat(InPCM, srcFloats, InChannels);

    // If channel count differs, first deinterleave-per-channel approach isn't necessary for linear resampler.
    // We'll resample interleaved and then map channels.
    // Resample
    TArray<float> resampled;
    ResampleLinear(srcFloats, resampled, InSampleRate, DeviceWF->nSamplesPerSec, InChannels);

    // Channel mapping
    int32 outChannels = DeviceWF->nChannels;
    if (InChannels != outChannels)
    {
        // simple mapping: mono->stereo duplicate, stereo->mono average, or truncate/duplicate for other cases
        int32 frames = resampled.Num() / InChannels;
        TArray<float> mapped;
        mapped.AddZeroed(frames * outChannels);
        for (int32 f = 0; f < frames; ++f)
        {
            for (int oc = 0; oc < outChannels; ++oc)
            {
                if (InChannels == 1 && outChannels >= 2)
                {
                    float s = resampled[f * InChannels + 0];
                    mapped[f * outChannels + oc] = s;
                }
                else if (InChannels >= 2 && outChannels == 1)
                {
                    // average first two channels
                    float s = 0.f;
                    for (int ic = 0; ic < InChannels; ++ic) s += resampled[f * InChannels + ic];
                    mapped[f * outChannels + oc] = s / static_cast<float>(InChannels);
                }
                else
                {
                    // map as much as possible, duplicate if needed
                    int ic = oc < InChannels ? oc : (InChannels - 1);
                    mapped[f * outChannels + oc] = resampled[f * InChannels + ic];
                }
            }
        }
        resampled = MoveTemp(mapped);
    }

    // Convert floats to target
    ConvertFloatToTarget(resampled, OutPCM, outChannels, DeviceWF->wBitsPerSample, DeviceWF->wFormatTag);
    OutBytesPerFrame = outChannels * (DeviceWF->wBitsPerSample / 8);
    return true;
}

TArray<FString> UMultiAudioOutput::GetAvailableAudioDevices()
{
    TArray<FString> Devices;
#if PLATFORM_WINDOWS
    UE_LOG(LogMultiAudio, Display, TEXT("GetAvailableAudioDevices called"));
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* pEnum = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum))))
    {
        IMMDeviceCollection* pCollection = nullptr;
        if (SUCCEEDED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection)))
        {
            UINT count = 0;
            pCollection->GetCount(&count);
            for (UINT i = 0; i < count; ++i)
            {
                IMMDevice* pDevice = nullptr;
                if (SUCCEEDED(pCollection->Item(i, &pDevice)))
                {
                    IPropertyStore* pProps = nullptr;
                    if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps)))
                    {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)))
                        {
                            if (varName.vt == VT_LPWSTR && varName.pwszVal)
                            {
                                FString Friendly(varName.pwszVal);
                                Devices.Add(Friendly);
                                UE_LOG(LogMultiAudio, Display, TEXT("Found audio device: %s"), *Friendly);
                            }
                        }
                        PropVariantClear(&varName);
                        pProps->Release();
                    }
                    pDevice->Release();
                }
            }
            pCollection->Release();
        }
        pEnum->Release();
    }
    CoUninitialize();
#endif
    return Devices;
}

bool UMultiAudioOutput::ExtractPCMData(USoundWave* SoundWave, TArray<uint8>& OutPCMData, int32& OutSampleRate, int32& OutNumChannels, int32& OutBitsPerSample)
{
    OutPCMData.Empty();
    OutSampleRate = 0;
    OutNumChannels = 0;
    OutBitsPerSample = 16;

    if (!SoundWave)
    {
        return false;
    }

    // This helper assumes the USoundWave contains raw PCM in RawPCMData
    if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
    {
        OutPCMData.AddUninitialized(SoundWave->RawPCMDataSize);
        FMemory::Memcpy(OutPCMData.GetData(), SoundWave->RawPCMData, SoundWave->RawPCMDataSize);
        OutSampleRate = SoundWave->GetSampleRateForCurrentPlatform();
        OutNumChannels = SoundWave->NumChannels;
        // We assume 16-bit PCM unless metadata present
        OutBitsPerSample = 16;
        return true;
    }

    return false;
}

bool UMultiAudioOutput::PlaySoundOnDevice(USoundWave* SoundWave, const FString& DeviceName, float Volume)
{
    if (!SoundWave || DeviceName.IsEmpty())
    {
        return false;
    }

    TArray<uint8> PCM;
    int32 SampleRate = 0;
    int32 NumChannels = 0;
    int32 BitsPerSample = 16;

    if (!ExtractPCMData(SoundWave, PCM, SampleRate, NumChannels, BitsPerSample))
    {
        return false;
    }

    // Log sound info (use Warning verbosity to show yellow in Output Log)
    UE_LOG(LogMultiAudio, Warning, TEXT("PlaySoundOnDevice called for device '%s' - SampleRate: %d, Channels: %d, BitsPerSample: %d, Bytes: %d"), *DeviceName, SampleRate, NumChannels, BitsPerSample, PCM.Num());

    // Play in background thread so we don't block game thread
    bStopRequested = false;
    TArray<uint8> MovePCM = MoveTemp(PCM);
    FString TargetDevice = DeviceName;

    Async(EAsyncExecution::Thread, [this, MovePCM = MovePCM, SampleRate, NumChannels, BitsPerSample, TargetDevice, Volume]() mutable
    {
#if PLATFORM_WINDOWS
        if (MovePCM.Num() == 0)
        {
            UE_LOG(LogMultiAudio, Warning, TEXT("No PCM data to play (MovePCM empty)."));
            return;
        }

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        IMMDeviceEnumerator* pEnum = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum))))
        {
            CoUninitialize();
            return;
        }

        IMMDeviceCollection* pCollection = nullptr;
        if (FAILED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection)))
        {
            pEnum->Release();
            CoUninitialize();
            return;
        }

        IMMDevice* pTargetDevice = nullptr;
        UINT count = 0;
        pCollection->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* pDevice = nullptr;
            if (SUCCEEDED(pCollection->Item(i, &pDevice)))
            {
                IPropertyStore* pProps = nullptr;
                if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps)))
                {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)))
                    {
                        if (varName.vt == VT_LPWSTR && varName.pwszVal)
                        {
                            FString Friendly(varName.pwszVal);
                            if (Friendly == TargetDevice)
                            {
                                pTargetDevice = pDevice; // take ownership
                                UE_LOG(LogMultiAudio, Log, TEXT("Selected target device: %s"), *Friendly);
                                pProps->Release();
                                PropVariantClear(&varName);
                                break;
                            }
                        }
                    }
                    PropVariantClear(&varName);
                    pProps->Release();
                }
                pDevice->Release();
            }
        }

        if (!pTargetDevice)
        {
            UE_LOG(LogMultiAudio, Warning, TEXT("Target device '%s' not found."), *TargetDevice);
            pCollection->Release();
            pEnum->Release();
            CoUninitialize();
            return;
        }

        IAudioClient* pAudioClient = nullptr;
        if (FAILED(pTargetDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient)))
        {
            UE_LOG(LogMultiAudio, Error, TEXT("Failed to activate IAudioClient on target device '%s'."), *TargetDevice);
            pTargetDevice->Release();
            pCollection->Release();
            pEnum->Release();
            CoUninitialize();
            return;
        }

        // Query device mix format and compare with source
        WAVEFORMATEX* pwfxDevice = nullptr;
        if (FAILED(pAudioClient->GetMixFormat(&pwfxDevice)) || !pwfxDevice)
        {
            UE_LOG(LogMultiAudio, Error, TEXT("Failed to get device mix format for '%s'."), *TargetDevice);
            pAudioClient->Release();
            pTargetDevice->Release();
            pCollection->Release();
            pEnum->Release();
            CoUninitialize();
            return;
        }

        UE_LOG(LogMultiAudio, Log, TEXT("Device mix format for '%s': SR=%u, Ch=%u, Bits=%u, FormatTag=%u"), *TargetDevice, pwfxDevice->nSamplesPerSec, pwfxDevice->nChannels, pwfxDevice->wBitsPerSample, pwfxDevice->wFormatTag);

        // Additional detailed logging: full WAVEFORMATEX fields
        UE_LOG(LogMultiAudio, Log, TEXT("Device WAVEFORMATEX: wFormatTag=%u, nChannels=%u, nSamplesPerSec=%u, nAvgBytesPerSec=%u, nBlockAlign=%u, wBitsPerSample=%u, cbSize=%u"),
            pwfxDevice->wFormatTag, pwfxDevice->nChannels, pwfxDevice->nSamplesPerSec, pwfxDevice->nAvgBytesPerSec, pwfxDevice->nBlockAlign, pwfxDevice->wBitsPerSample, pwfxDevice->cbSize);

        // Build a desired format from source to ask IsFormatSupported
        WAVEFORMATEX wfDesired = {0};
        wfDesired.wFormatTag = (BitsPerSample == 32) ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        wfDesired.nChannels = static_cast<WORD>(NumChannels);
        wfDesired.nSamplesPerSec = SampleRate;
        wfDesired.wBitsPerSample = static_cast<WORD>(BitsPerSample);
        wfDesired.nBlockAlign = (wfDesired.nChannels * wfDesired.wBitsPerSample) / 8;
        wfDesired.nAvgBytesPerSec = wfDesired.nSamplesPerSec * wfDesired.nBlockAlign;

        WAVEFORMATEX* pwfxClosest = nullptr;
        HRESULT hrIs = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &wfDesired, &pwfxClosest);
        if (hrIs == S_OK)
        {
            UE_LOG(LogMultiAudio, Log, TEXT("Desired format is supported by device '%s'"), *TargetDevice);
        }
        else if (hrIs == S_FALSE && pwfxClosest)
        {
            UE_LOG(LogMultiAudio, Warning, TEXT("Desired format NOT directly supported; closest supported format: wFormatTag=%u SR=%u Ch=%u Bits=%u"), pwfxClosest->wFormatTag, pwfxClosest->nSamplesPerSec, pwfxClosest->nChannels, pwfxClosest->wBitsPerSample);
            CoTaskMemFree(pwfxClosest);
        }
        else
        {
            UE_LOG(LogMultiAudio, Error, TEXT("IsFormatSupported failed (hr=0x%08x) for device '%s'"), hrIs, *TargetDevice);
        }

        // If formats don't match, attempt to convert source PCM to device mix format
        TArray<uint8> FinalPCM = MovePCM; // default: original
        UINT32 finalBytesPerFrame = 0;
        if (pwfxDevice->nSamplesPerSec != (UINT)SampleRate || pwfxDevice->nChannels != (UINT)NumChannels || pwfxDevice->wBitsPerSample != (UINT)BitsPerSample)
        {
            UE_LOG(LogMultiAudio, Warning, TEXT("Format mismatch: source SR=%d Ch=%d Bits=%d but device '%s' wants SR=%u Ch=%u Bits=%u. Attempting conversion..."), SampleRate, NumChannels, BitsPerSample, *TargetDevice, pwfxDevice->nSamplesPerSec, pwfxDevice->nChannels, pwfxDevice->wBitsPerSample);
            TArray<uint8> converted;
            if (!ConvertPCMToDeviceFormat(MovePCM, SampleRate, NumChannels, BitsPerSample, pwfxDevice, converted, finalBytesPerFrame))
            {
                UE_LOG(LogMultiAudio, Error, TEXT("Conversion to device format failed for device '%s'."), *TargetDevice);
                CoTaskMemFree(pwfxDevice);
                pAudioClient->Release();
                pTargetDevice->Release();
                pCollection->Release();
                pEnum->Release();
                CoUninitialize();
                return;
            }
            FinalPCM = MoveTemp(converted);
            UE_LOG(LogMultiAudio, Log, TEXT("Conversion to device format succeeded: output bytes=%d framesize=%u"), FinalPCM.Num(), finalBytesPerFrame);
        }

        // Initialize using device mix format
        REFERENCE_TIME hnsBufferDuration = 10000000; // 1s
        UINT32 bytesPerFrame = 0;
        if (FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsBufferDuration, 0, pwfxDevice, nullptr)))
        {
            UE_LOG(LogMultiAudio, Error, TEXT("IAudioClient::Initialize failed on device '%s' even with device mix format."), *TargetDevice);
            CoTaskMemFree(pwfxDevice);
            pAudioClient->Release();
            pTargetDevice->Release();
            pCollection->Release();
            pEnum->Release();
            CoUninitialize();
            return;
        }

        // compute bytesPerFrame from device format before freeing
        bytesPerFrame = pwfxDevice->nBlockAlign;
        CoTaskMemFree(pwfxDevice);

        IAudioRenderClient* pRenderClient = nullptr;
        if (FAILED(pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient)))
        {
            pAudioClient->Release();
            pTargetDevice->Release();
            pCollection->Release();
            pEnum->Release();
            CoUninitialize();
            return;
        }

        UINT32 bufferFrameCount = 0;
        pAudioClient->GetBufferSize(&bufferFrameCount);

        // Start playback
        UE_LOG(LogMultiAudio, Log, TEXT("Starting playback on device '%s'"), *TargetDevice);
        pAudioClient->Start();

        const uint8* DataPtr = FinalPCM.GetData();
        const int32 TotalBytes = FinalPCM.Num();
        if (finalBytesPerFrame != 0)
        {
            bytesPerFrame = finalBytesPerFrame;
        }
        int32 BytesWritten = 0;

        while (BytesWritten < TotalBytes && !bStopRequested)
        {
            UINT32 numFramesPadding = 0;
            pAudioClient->GetCurrentPadding(&numFramesPadding);
            UINT32 availFrames = (bufferFrameCount > numFramesPadding) ? (bufferFrameCount - numFramesPadding) : 0;
            if (availFrames == 0)
            {
                FPlatformProcess::Sleep(0.005f);
                continue;
            }

            UINT32 framesToWrite = availFrames;
            UINT32 bytesToWrite = framesToWrite * bytesPerFrame;
            int32 bytesRemaining = TotalBytes - BytesWritten;
            if ((int32)bytesToWrite > bytesRemaining)
            {
                // reduce framesToWrite to fit remaining data
                framesToWrite = bytesRemaining / bytesPerFrame;
                bytesToWrite = framesToWrite * bytesPerFrame;
            }

            BYTE* pBuffer = nullptr;
            if (SUCCEEDED(pRenderClient->GetBuffer(framesToWrite, &pBuffer)))
            {
                FMemory::Memcpy(pBuffer, DataPtr + BytesWritten, bytesToWrite);
                pRenderClient->ReleaseBuffer(framesToWrite, 0);
                BytesWritten += bytesToWrite;
            }
        }

        pAudioClient->Stop();
        UE_LOG(LogMultiAudio, Log, TEXT("Playback finished on device '%s'"), *TargetDevice);
        pRenderClient->Release();
        pAudioClient->Release();
        pTargetDevice->Release();
        pCollection->Release();
        pEnum->Release();
        CoUninitialize();
#endif
    });

    return true;
}

void UMultiAudioOutput::StopAll()
{
    bStopRequested = true;
}
