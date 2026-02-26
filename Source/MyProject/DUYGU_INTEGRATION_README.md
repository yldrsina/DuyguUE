# Duygu AI Integration - Unreal Engine

## Genel Bakış

Bu entegrasyon, Unreal Engine'de ses tabanlı AI terapist servisi ile etkileşim kurmanıza olanak tanır. Tek bir latent async node kullanarak ses dosyalarını işleyebilir, transkripsiyon ve AI yanıtı alabilirsiniz.

## Ana Özellikler

### ✅ Tam Otomatik İşlem Akışı
1. **Giriş**: USoundWave objesi (Unreal'deki herhangi bir ses)
2. **Otomatik Dönüşüm**: SoundWave → WAV formatı
3. **Mikroservis İletişimi**: WAV dosyası → Duygu agent-service
4. **AI İşleme**: Speech-to-Text → AI Response → Text-to-Speech
5. **Otomatik İndirme**: Yanıt ses dosyası indirilir
6. **Otomatik Dönüşüm**: WAV → USoundWave
7. **Çıkış**: İşlenmiş USoundWave objesi

## Blueprint Kullanımı

### 🎯 Önerilen Yöntem: DuyguDoProcessFromSoundWave

```
Blueprint Node: "Duygu Do Process (SoundWave)"
Kategori: Duygu|Audio
```

**Parametreler:**
- `InputSoundWave` (USoundWave*): İşlenecek ses dosyası
- `ServerUrl` (String): Mikroservis URL'i (default: http://127.0.0.1:5003/process)

**Çıkış Delegateleri:**
- `OnSuccess`: İşlem başarılı → `Message` (User + Assistant metinleri)
- `OnFailure`: İşlem başarısız → Hata mesajı
- `OnAudioImported`: İşlenmiş ses → `ImportedSound` (USoundWave*)

### 📋 Blueprint Örneği

```
[Input SoundWave] → [Duygu Do Process (SoundWave)]
                            ├─→ OnSuccess → [Print Message]
                            ├─→ OnFailure → [Print Error]
                            └─→ OnAudioImported → [Play Sound]
```

## Alternatif Yöntemler

### 1. Dosya Yolu ile İşleme
```
DuyguDoProcess
- AudioFilePath: "C:/Audio/input.wav"
- ServerUrl: "http://127.0.0.1:5003/process"
```

### 2. PCM Byte Array ile İşleme
```
DuyguDoProcessFromPCM
- PCMBytes: TArray<uint8>
- SampleRate: 44100
- NumChannels: 2
- BitsPerSample: 16
- ServerUrl: "http://127.0.0.1:5003/process"
```

## Mikroservis Yanıtı

Agent-service `/process` endpoint'i şu yanıtı döner:

```json
{
  "success": true,
  "user_text": "Bugün kendimi çok iyi hissediyorum",
  "assistant_response": "Ne güzel! İyi hissetmeniz harika...",
  "audio_url": "/download/response_123456.wav",
  "processing_times": {
    "step1_stt_ms": "245.32",
    "step2_ai_response_ms": "1523.45",
    "step3_tts_ms": "876.12",
    "total_ms": "2644.89"
  },
  "conversation_length": 5
}
```

## Teknik Detaylar

### Ses Formatı
- **Input**: Herhangi bir Unreal USoundWave (otomatik dönüşüm)
- **Transfer**: WAV format (16-bit PCM)
- **Output**: USoundWaveProcedural (Unreal uyumlu)

### Hata Yönetimi
- SoundWave null kontrolü
- Audio data extraction hatası
- HTTP iletişim hatası
- JSON parsing hatası
- WAV indirme hatası
- SoundWave oluşturma hatası

### Performans
- Async işleme (non-blocking)
- Otomatik bellek yönetimi (GC friendly)
- Connection pooling (HTTP)
- Streaming download

## Servis Kurulumu

Mikroservisi başlatmak için:

```bash
cd c:\Users\sina\Desktop\dev\Duygu_Services\agent-service
python app.py
```

Default olarak `http://localhost:5003` üzerinde çalışır.

## Dosya Yapısı

```
Source/MyProject/
├── Public/
│   ├── DuyguDoProccess.h           # Ana işleme sınıfı
│   └── DuyguDoProccessAsync.h      # Blueprint async node
├── Private/
│   ├── DuyguDoProccess.cpp         # İşleme implementasyonu
│   └── DuyguDoProccessAsync.cpp    # Async node implementasyonu
└── DUYGU_INTEGRATION_README.md     # Bu dosya
```

## Sorun Giderme

### "Could not extract audio data from SoundWave"
- SoundWave'in yüklendiğinden emin olun
- Editor'da Content Browser'dan ses dosyasını test edin
- Ses dosyasının compressed olup olmadığını kontrol edin

### "HTTP request failed"
- Mikroservisin çalıştığını kontrol edin: `http://localhost:5003/health`
- Firewall ayarlarını kontrol edin
- ServerUrl'in doğru olduğunu kontrol edin

### "Failed to parse server response"
- Mikroservisin doğru versiyon olduğunu kontrol edin
- Mikroservis loglarını inceleyin
- Response JSON formatını kontrol edin

## İletişim ve Destek

Sorunlarınız için projenizin issue tracker'ını kullanabilirsiniz.

**Multi-Device Ses Çıkışı (Blueprint Kullanımı)**

- **Amaç**: `MultiAudioOutput` sınıfı, Windows üzerinde birden fazla ses çıkış cihazına aynı anda ses oynatma yeteneği sağlar. Bu özellik sayesinde örneğin sistem hoparlörüne ve bir sanal kabloya aynı anda farklı sesler gönderebilirsiniz.
- **Platform**: Yalnızca Windows (WASAPI kullanır).
- **Dosya**: Sınıf tanımı ve implementasyonu için bakınız: [Source/MyProject/Public/MultiAudioOutput.h](Source/MyProject/Public/MultiAudioOutput.h) ve [Source/MyProject/Private/MultiAudioOutput.cpp](Source/MyProject/Private/MultiAudioOutput.cpp)

- **Cihaz Listeleme**: `GetAvailableAudioDevices()` çağrısı size `TArray<FString>` döndürür. Blueprint'te bu sonucu `ForEachLoop` ile gezip kullanıcıya sunabilirsiniz.
- **Cihaz Seçimi & Çalma**: Seçilen cihaz adına aşağıdaki node ile ses gönderin:
  - `PlaySoundOnDevice(SoundWave, DeviceName, Volume)` — `SoundWave`: çalınacak `USoundWave`, `DeviceName`: listeden seçilen cihaz ismi.
- **Birden Fazla Cihazta Aynı Anda Çalma**: Blueprint içinde iki ayrı `MultiAudioOutput` örneği oluşturun (örn. iki adet "Construct Object from Class" veya iki farklı variable). Her örnek için farklı `DeviceName` verip `PlaySoundOnDevice` çağırırsanız aynı anda iki farklı cihaza ses gönderebilirsiniz.

- **Durdurma**: O anki instance'ı durdurmak için `StopAll()` node'unu çağırın.

- **Örnek Event Graph Akışı (Adım Adım)**
  - Oyun başladığında veya kullanıcı butona bastığında:
    1. `GetAvailableAudioDevices()` çağır.
    2. Dönen listeyi (`Array`) kullanıcıya göster (ör. Widget içinde Dropdown).
    3. Kullanıcı iki cihaz seçerse: `Construct Object from Class` → `MultiAudioOutput` (örnek A) ve tekrar `MultiAudioOutput` (örnek B).
    4. Örnek A için: `PlaySoundOnDevice(SoundWaveA, ChosenDeviceA, 1.0)`.
    5. Örnek B için: `PlaySoundOnDevice(SoundWaveB, ChosenDeviceB, 1.0)`.
    6. Durdurmak için: ilgili örneğe `StopAll()` çağır.

- **Notlar & Kısıtlar**:
  - `USoundWave` içinde ham PCM verisi (`RawPCMData` ve `RawPCMDataSize`) bulunmalıdır. Editor'da import edilmiş sıkıştırılmış asset'ler doğrudan çalışmayabilir. Sunucudan gelen WAV -> `USoundWave` oluşturma zinciri (`RawPCMData` doldurma) örnek projede mevcuttur.
  - `PlaySoundOnDevice` çağrısı oyun iş parçacığını bloke etmez; arka planda oynatma için asenkron iş kullanır. Ancak cihaz sürücüsü ve format uyuşmazlıkları runtime hatalarına neden olabilir.
  - Ses formatı (sample rate, kanal sayısı, bit depth) hedef cihazın desteklediği formatla uyumlu olmalıdır. Gerekirse veriyi dönüştürün veya proje içinde ortak bir format kullanın (ör. 16-bit PCM, 44100 Hz, stereo).

- **Hızlı Örnek (Blueprint özet)**
  - Event BeginPlay → `GetAvailableAudioDevices()` → Kullanıcı seçimi → `Construct Object from Class (MultiAudioOutput)` → `PlaySoundOnDevice`

