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
