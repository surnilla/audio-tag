#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <M5EchoBase.h>
#include "flite_arduino.h"

#define AMP_ENABLE   18
#define PIN_I2C_SDA  45
#define PIN_I2C_SCL   0
#define PIN_I2S_DIN    4
#define PIN_I2S_WS     3
#define PIN_I2S_DOUT  48
#define PIN_I2S_BCLK  17
#define SAMPLE_RATE  16000

// UUIDs matching audio-tag-android app
#define SVC_UUID   "12345678-1234-1234-1234-1234567890ab"
#define AUDIO_UUID "12345678-1234-1234-1234-1234567890ac"  // write: raw PCM from phone
#define TTS_UUID   "12345678-1234-1234-1234-1234567890ad"  // write: text → Flite TTS
#define STA_UUID   "12345678-1234-1234-1234-1234567890ae"  // notify: status

static M5EchoBase echo;
static BLECharacteristic *g_sta_char = nullptr;

// --- Audio (raw PCM) path ---
// Phone sends: [4-byte LE length header][PCM data chunks...]
// PCM format: 16kHz mono 16-bit little-endian
static uint8_t  *g_audio_buf      = nullptr;
static uint32_t  g_audio_total    = 0;
static uint32_t  g_audio_received = 0;
static bool      g_audio_receiving = false;
static volatile bool g_audio_pending = false;

// --- TTS (text) path ---
static volatile bool g_tts_pending = false;
static char          g_tts_buf[256];
static Flite        *g_flite = nullptr;

// ---------------------------------------------------------------

static void play_beep(int freq_hz, int duration_ms) {
    int samples = SAMPLE_RATE * duration_ms / 1000;
    int16_t *buf = (int16_t *)malloc(samples * sizeof(int16_t));
    if (!buf) return;
    for (int i = 0; i < samples; i++)
        buf[i] = (int16_t)(28000.0f * sinf(2.0f * M_PI * freq_hz * i / SAMPLE_RATE));
    echo.play((const uint8_t *)buf, samples * sizeof(int16_t));
    delay(duration_ms + 200);
    free(buf);
}

static void beeps(int n) {
    for (int i = 0; i < n; i++) {
        play_beep(1000, 400);
        if (i < n - 1) delay(200);
    }
}

static void notify_byte(uint8_t val) {
    if (!g_sta_char) return;
    g_sta_char->setValue(&val, 1);
    g_sta_char->notify();
}

// Play 16kHz mono PCM buffer through M5EchoBase (which is stereo).
// Converts mono → stereo in 256-sample chunks to avoid large allocations.
static void play_mono16(const int16_t *pcm, int n_samples) {
    const int CHUNK = 256;
    int16_t chunk_buf[CHUNK * 2];  // stereo
    for (int i = 0; i < n_samples; i += CHUNK) {
        int cnt = (i + CHUNK < n_samples) ? CHUNK : (n_samples - i);
        for (int j = 0; j < cnt; j++) {
            chunk_buf[j*2+0] = pcm[i + j];  // L
            chunk_buf[j*2+1] = pcm[i + j];  // R
        }
        echo.play((const uint8_t *)chunk_buf, cnt * 2 * sizeof(int16_t));
    }
}

// ---------------------------------------------------------------

class ServerCallbacks : public BLEServerCallbacks {
    void onDisconnect(BLEServer *srv) override {
        // Reset receive state in case transfer was mid-flight
        g_audio_receiving = false;
        g_audio_pending   = false;
        if (g_audio_buf) { free(g_audio_buf); g_audio_buf = nullptr; }
        // Restart advertising so the app can reconnect
        BLEDevice::getAdvertising()->start();
    }
};

class AudioCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        uint8_t *data = c->getData();
        size_t   len  = c->getLength();
        if (len == 0) return;

        if (!g_audio_receiving) {
            // First chunk: 4-byte LE length header
            if (len < 4) return;
            g_audio_total = (uint32_t)data[0]
                          | ((uint32_t)data[1] << 8)
                          | ((uint32_t)data[2] << 16)
                          | ((uint32_t)data[3] << 24);
            if (g_audio_buf) { free(g_audio_buf); g_audio_buf = nullptr; }
            // Try PSRAM first, fall back to heap
            g_audio_buf = (uint8_t *)heap_caps_malloc(
                g_audio_total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!g_audio_buf)
                g_audio_buf = (uint8_t *)malloc(g_audio_total);
            if (!g_audio_buf) return;  // out of memory
            g_audio_received = 0;
            g_audio_receiving = true;
            notify_byte(0x01);  // 0x01 = receiving

            // Copy payload after header
            size_t payload = len - 4;
            if (payload > 0) {
                memcpy(g_audio_buf, data + 4, payload);
                g_audio_received = payload;
            }
        } else {
            // Subsequent chunks: raw PCM data
            uint32_t space = g_audio_total - g_audio_received;
            uint32_t copy  = (len < space) ? len : space;
            memcpy(g_audio_buf + g_audio_received, data, copy);
            g_audio_received += copy;
        }

        if (g_audio_received >= g_audio_total) {
            g_audio_receiving = false;
            g_audio_pending   = true;
        }
    }
};

class TtsCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0) return;
        int len = val.length();
        if (len > (int)sizeof(g_tts_buf) - 1) len = sizeof(g_tts_buf) - 1;
        memcpy(g_tts_buf, val.c_str(), len);
        g_tts_buf[len] = '\0';
        g_tts_pending = true;
        notify_byte(0x01);  // 0x01 = receiving
    }
};

// ---------------------------------------------------------------

void setup() {
    delay(500);

    BLEDevice::setMTU(512);
    BLEDevice::init("AudioTag");
    BLEServer  *srv = BLEDevice::createServer();
    srv->setCallbacks(new ServerCallbacks());
    BLEService *svc = srv->createService(BLEUUID(SVC_UUID), 16);

    BLECharacteristic *audio = svc->createCharacteristic(
        AUDIO_UUID, BLECharacteristic::PROPERTY_WRITE);
    audio->setCallbacks(new AudioCallback());

    BLECharacteristic *tts = svc->createCharacteristic(
        TTS_UUID, BLECharacteristic::PROPERTY_WRITE);
    tts->setCallbacks(new TtsCallback());

    g_sta_char = svc->createCharacteristic(
        STA_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_sta_char->addDescriptor(new BLE2902());

    svc->start();
    BLEDevice::getAdvertising()->start();

    pinMode(AMP_ENABLE, OUTPUT);
    digitalWrite(AMP_ENABLE, HIGH);
    if (!echo.init(SAMPLE_RATE, PIN_I2C_SDA, PIN_I2C_SCL,
                   PIN_I2S_DIN, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_BCLK)) {
        return;
    }
    echo.setSpeakerVolume(80);

    beeps(3);  // 3 beeps = audio OK

    g_flite = new Flite();
    g_flite->setVoice(register_cmu_us_kal());

    beeps(1);  // 1 beep = Flite OK, ready
}

void loop() {
    // --- Raw PCM audio from phone ---
    if (g_audio_pending) {
        g_audio_pending = false;
        int n_samples = g_audio_total / sizeof(int16_t);
        play_mono16((const int16_t *)g_audio_buf, n_samples);
        free(g_audio_buf);
        g_audio_buf = nullptr;
        notify_byte(0x02);  // 0x02 = done (matches Android app expectation)
        return;
    }

    // --- Text → Flite TTS ---
    if (g_tts_pending) {
        g_tts_pending = false;
        cst_wave *wave = g_flite->textToWave(g_tts_buf);
        if (wave && wave->num_samples > 0) {
            // kal voice: 8kHz mono → upsample 2× + stereo for M5EchoBase
            int n = wave->num_samples;
            const int CHUNK = 256;
            int16_t chunk_buf[CHUNK * 4];
            for (int i = 0; i < n; i += CHUNK) {
                int cnt = (i + CHUNK < n) ? CHUNK : (n - i);
                for (int j = 0; j < cnt; j++) {
                    int16_t s0  = wave->samples[i + j];
                    int16_t s1  = (i + j + 1 < n) ? wave->samples[i + j + 1] : s0;
                    int16_t mid = (int16_t)(((int32_t)s0 + s1) / 2);
                    chunk_buf[j*4+0] = s0;
                    chunk_buf[j*4+1] = s0;
                    chunk_buf[j*4+2] = mid;
                    chunk_buf[j*4+3] = mid;
                }
                echo.play((const uint8_t *)chunk_buf, cnt * 4 * sizeof(int16_t));
            }
            delete_wave(wave);
        }
        notify_byte(0x02);  // done
        return;
    }

    delay(50);
}
