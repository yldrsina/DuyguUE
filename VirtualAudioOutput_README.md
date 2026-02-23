# Virtual Audio Output - VB-Audio Cable Entegrasyonu

Bu sistem, Unreal Engine 5.7'de USoundWave dosyalarını VB-Audio Cable veya diğer sanal audio cihazlara yönlendirmenizi sağlar.

## Özellikler

- ✅ Windows Core Audio API kullanarak direkt audio device kontrolü
- ✅ VB-Audio Cable, VoiceMeeter ve diğer sanal audio cihazlarla uyumlu
- ✅ USoundWave'i istediğiniz audio output'a yönlendirme
- ✅ **Hem Procedural hem de Normal SoundWave asset'lerini destekler**
- ✅ **Otomatik compressed audio decompression**
- ✅ Blueprint'ten tam kontrol
- ✅ Gerçek zamanlı ses akışı
- ✅ Volume kontrolü
- ✅ SoundWave preload ve cache yönetimi

## Kurulum

### 1. VB-Audio Cable Kurulumu
1. [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) indirin ve kurun
2. Windows Sound ayarlarında "CABLE Input" cihazını görmelisiniz

### 2. Unreal Engine'de Kullanım

#### A. Blueprint'ten Kullanım

##### Yöntem 1: DuyguService ile Otomatik Kullanım

```cpp
// DuyguService instance'ınızda:
1. "Use Virtual Audio Output" = true
2. "Virtual Audio Device Name" = "CABLE Input (VB-Audio Virtual Cable)"

// Artık ProcessAudio çağrıldığında, dönen ses otomatik olarak VB-Audio'ya gönderilir
```

##### Yöntem 2: Manuel Kullanım (Her USoundWave için)

Blueprint Node'ları:
```
1. "Get All Audio Devices" -> Mevcut tüm ses cihazlarını listeler

2. "Is Sound Wave Ready" (Pure)
   - Sound Wave: Kontrol etmek istediğiniz USoundWave
   - Return: PCM data cache'lenmiş mi?

3. "Preload Sound Wave" (Önerilen - compressed audio için)
   - Sound Wave: Cache'lemek istediğiniz USoundWave
   - Return: Başarılı mı?
   - Kullanım: Play'den ÖNCE çağırın, sorunsuz playback için

4. "Play Sound To VB Audio Cable"
   - Sound Wave: Oynatmak istediğiniz USoundWave (Procedural veya Normal)
   - Volume: 0.0 - 1.0
   - Not: Otomatik olarak PreloadSoundWave çağırır

5. "Play Sound To Device"
   - Sound Wave: USoundWave (Procedural veya Normal)
   - Device Name: "CABLE Input (VB-Audio Virtual Cable)" veya başka cihaz
   - Volume: 0.0 - 1.0
   - Not: Otomatik olarak PreloadSoundWave çağırır
```

**Önerilen Blueprint Akışı (Compressed SoundWave için):**
```
Event BeginPlay
  └─> Preload Sound Wave (MySoundAsset)
       └─> Wait/Delay (0.2 saniye - opsiyonel)
            └─> Play Sound To Device (MySoundAsset, DeviceName)
```

#### B. C++ Kullanımı

```cpp
#include "VirtualAudioOutput.h"
#include "VirtualAudioHelpers.h"

// Mevcut audio cihazlarını listele
TArray<FString> Devices = UVirtualAudioHelpers::GetAllAudioDevices();
for (const FString& Device : Devices)
{
    UE_LOG(LogTemp, Log, TEXT("Audio Device: %s"), *Device);
}

// USoundWave'i oynatmadan önce preload et (compressed audio için önerilen)
USoundWave* MySound = LoadObject<USoundWave>(...);
if (!UVirtualAudioHelpers::IsSoundWaveReady(MySound))
{
    UVirtualAudioHelpers::PreloadSoundWave(MySound);
    // Opsiyonel küçük gecikme - async decompression için
    FPlatformProcess::Sleep(0.2f);
}

// USoundWave'i VB-Audio Cable'a gönder
bool bSuccess = UVirtualAudioHelpers::PlaySoundToVBAudioCable(this, MySound, 1.0f);

// Veya başka bir cihaza gönder
bool bSuccess = UVirtualAudioHelpers::PlaySoundToDevice(
    this, 
    MySound, 
    TEXT("Speakers (Realtek High Definition Audio)"), 
    0.8f
);
```

#### C. UVirtualAudioOutput Class Direkt Kullanımı

```cpp
// Virtual audio output oluştur
UVirtualAudioOutput* VirtualOutput = NewObject<UVirtualAudioOutput>(this);

// Mevcut cihazları listele
TArray<FString> Devices = VirtualOutput->GetAvailableAudioDevices();

// Cihaz seç
VirtualOutput->SetTargetAudioDevice(TEXT("CABLE Input (VB-Audio Virtual Cable)"));

// Ses oynat
VirtualOutput->PlayToVirtualDevice(MySoundWave, 1.0f);

// Volume değiştir
VirtualOutput->SetVolume(0.5f);

// Oynatmayı durdur
VirtualOutput->StopPlayback();

// Oynatılıyor mu kontrol et
bool bPlaying = VirtualOutput->IsPlaying();
```

## SoundWave Asset Ayarları (Önemli!)

### Compressed Audio (OGG/OPUS) İçin Önerilen Ayarlar

Normal SoundWave asset'leriniz (Content Browser'da .wav/.mp3 import edilen) compressed formatında saklanır. 
Bu asset'leri virtual device'a oynatmak için **PCM data cache'lemek** gerekir.

**Seçenek 1: Asset'i Düzenle (Önerilen)**
1. Content Browser'da SoundWave asset'inize sağ tıklayın
2. **Details Panel**'de şu ayarları yapın:
   - **Loading Behavior Type**: `FORCE_INLINE` veya `RETAIN_ON_LOAD`
   - **Compression Quality**: `100` (opsiyonel - en iyi kalite için)
3. Asset'i kaydedin ve Editor'ü restart edin

**Seçenek 2: Runtime Preload Kullanın**
```cpp
// Blueprint veya C++'da
PreloadSoundWave(MySoundAsset);  // Otomatik cache
// Küçük gecikme sonrası
PlaySoundToDevice(MySoundAsset, DeviceName);
```

**Seçenek 3: Procedural SoundWave Kullanın**
- Runtime-generated audio zaten PCM formatında
- Hiçbir ek ayar gerekmez
- DuyguService'den dönen audio'lar zaten procedural

### Loading Behavior Tipleri

| Tip | Açıklama | Virtual Audio İçin |
|-----|----------|-------------------|
| **FORCE_INLINE** | PCM data her zaman RAM'de | ✅ En iyi seçenek |
| **RETAIN_ON_LOAD** | İlk oynatışta cache'lenir | ✅ İyi |
| **LOAD_ON_DEMAND** | Streaming kullanır | ⚠️ PreloadSoundWave gerekli |
| **STREAMING** | Chunk'lar halinde | ⚠️ PreloadSoundWave gerekli |

## Kullanım Senaryoları

### 1. AI Yanıtlarını VB-Audio'ya Yönlendirme
```cpp
// DuyguService'te otomatik
MyDuyguService->bUseVirtualAudioOutput = true;
MyDuyguService->VirtualAudioDeviceName = TEXT("CABLE Input (VB-Audio Virtual Cable)");
MyDuyguService->ProcessAudio(UserVoice);
// AI yanıtı otomatik olarak VB-Audio'ya gider
```

### 2. OBS'ye Ses Gönderme
VB-Audio Cable, OBS'de audio source olarak seçilebilir:
1. OBS > Settings > Audio > Mic/Auxiliary Audio
2. Device: "CABLE Output (VB-Audio Virtual Cable)"
3. Unreal'dan gönderilen tüm sesler artık OBS'de kayıt olur

### 3. Discord/Zoom'a Sanal Mikrofon Olarak
1. VB-Audio Cable'ı Discord/Zoom'da mikrofon olarak seçin
2. Unreal'dan gönderilen sesler arkadaşlarınıza/toplantı katılımcılarına iletilir

### 4. Çoklu Audio Stream'leri
```cpp
// Stream 1: Normal hoparlörler
UGameplayStatics::PlaySound2D(this, GameMusic);

// Stream 2: VB-Audio (streaming için)
UVirtualAudioHelpers::PlaySoundToVBAudioCable(this, AIVoice, 1.0f);
```

## Teknik Detaylar

### Windows Core Audio API
Sistem, Windows'un WASAPI (Windows Audio Session API) kullanır:
- **IMMDeviceEnumerator**: Ses cihazlarını listeler
- **IAudioClient**: Ses akışını yönetir
- **IAudioRenderClient**: PCM verilerini cihaza yazar

### Thread Safety
- Audio playback arka plan thread'inde çalışır
- Asenkron oynatma (non-blocking)
- Buffer yönetimi otomatik

### Desteklenen Formatlar
- **Sample Rate**: USoundWave'den otomatik algılanır
- **Channels**: Mono/Stereo desteklenir
- **Bit Depth**: 16-bit PCM
- **Format**: WAVE_FORMAT_PCM

## Sorun Giderme

### "Failed to extract PCM data" Hatası (En Yaygın!)

**Sorun**: Compressed SoundWave asset'lerinde PCM data cache'lenmemiş.

**Çözüm 1 - Asset Ayarları (Kalıcı Çözüm)**:
1. Content Browser'da SoundWave asset'inize **çift tıklayın** veya sağ tıklayıp **Edit** deyin
2. **Details Panel**'de (sağ tarafta):
   - **Loading Behavior** kategorisini bulun
   - **Loading Behavior Type**: `Force Inline` veya `Retain On Load` seçin
3. Asset'i **Save** edin (Ctrl+S)
4. Editor'ü **restart** edin (önemli!)
5. Tekrar deneyin

**Çözüm 2 - Runtime Preload (Kod Değişikliği Gerektirmez)**:
```cpp
// Blueprint'te playback'ten ÖNCE:
Preload Sound Wave (MySoundAsset)
  └─> Delay (0.2 sec) // Opsiyonel
       └─> Play Sound To Device (MySoundAsset, DeviceName)
```

**Çözüm 3 - Asset'i Yeniden Import Edin**:
1. Orjinal .wav/.mp3 dosyanızı Content Browser'a sürükleyin
2. Import Settings'de:
   - **Compression Quality**: `100`
   - Diğer ayarları default bırakın
3. Import edin

**Neden Oluyor?**: Unreal Engine varsayılan olarak audio'yu OGG/OPUS formatında compress eder. 
Virtual audio device PCM (raw) data gerektirir. `FORCE_INLINE` ayarı audio'yu decompress edilmiş halde RAM'de tutar.

### "Failed to set device" Hatası
- VB-Audio Cable'ın kurulu olduğundan emin olun
- Device ismini tam olarak kontrol edin (büyük/küçük harf duyarlı değil ama karakter hassas)
- Windows Sound ayarlarında cihazın enabled olduğunu kontrol edin

### Ses Çıkmıyor
- VB-Audio Cable'ın default device olmadığından emin olun
- Windows Volume Mixer'da "CABLE Input" volume'ünü kontrol edin
- USoundWave'in valid PCM data içerdiğinden emin olun (IsSoundWaveReady ile kontrol edin)
- Procedural SoundWave kullanıyorsanız - her zaman çalışır
- Normal asset kullanıyorsanız - yukarıdaki "Failed to extract PCM data" çözümlerini deneyin

### Performance Sorunları
- Çok fazla concurrent stream oluşturmayın
- Kullanılmayan VirtualAudioOutput instance'larını cleanup edin
- IsPlaying() ile kontrol edin, bitmiş playback'leri yeniden kullanın

## Örnek Blueprint Workflow

```
[Mikrofon Input] 
    → [Process Audio (DuyguService)]
        → [AI İşleme]
            → [TTS Üretimi]
                → (Otomatik) [VB-Audio Cable Output]
                    → [OBS/Discord/Zoom]
```

## Lisans ve Gereksinimler

- **Unreal Engine**: 5.7+
- **Platform**: Windows only (Core Audio API)
- **VB-Audio Cable**: Ücretsiz indirebilirsiniz
- **Alternatifler**: VoiceMeeter, Virtual Audio Cable, diğer WASAPI uyumlu cihazlar

## İleri Seviye Özellikler

### Özel Audio Effect Chain
```cpp
// Kendi audio processing pipeline'ınızı ekleyebilirsiniz
// VirtualAudioOutput.cpp içinde WriteAudioData() fonksiyonunda
// reverb, echo, pitch shift vb. eklenebilir
```

### Multi-Device Routing
```cpp
// Aynı anda birden fazla cihaza gönderme
UVirtualAudioOutput* Output1 = NewObject<UVirtualAudioOutput>();
Output1->SetTargetAudioDevice(TEXT("CABLE Input"));
Output1->PlayToVirtualDevice(Sound, 1.0f);

UVirtualAudioOutput* Output2 = NewObject<UVirtualAudioOutput>();
Output2->SetTargetAudioDevice(TEXT("Speakers"));
Output2->PlayToVirtualDevice(Sound, 1.0f);
```

## Destek

Sorularınız için:
- Unreal Engine Forums
- VB-Audio Documentation
- Windows WASAPI Documentation
