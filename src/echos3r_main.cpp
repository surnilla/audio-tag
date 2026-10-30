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

#define SVC_UUID  "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define TTS_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STA_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a9"

static M5EchoBase echo;
static BLECharacteristic *g_sta_char = nullptr;

static volatile bool g_tts_pending = false;
static char          g_tts_buf[256];

static Flite *g_flite = nullptr;

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

static void notify_status(const char *msg) {
    if (!g_sta_char) return;
    g_sta_char->setValue((uint8_t *)msg, strlen(msg));
    g_sta_char->notify();
}

class TtsCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0) return;
        int len = val.length();
        if (len > (int)sizeof(g_tts_buf) - 1) len = sizeof(g_tts_buf) - 1;
        memcpy(g_tts_buf, val.c_str(), len);
        g_tts_buf[len] = '\0';
        g_tts_pending = true;
        notify_status("RECEIVED");
    }
};

void setup() {
    delay(500);

    BLEDevice::init("AudioTag");
    BLEServer  *srv = BLEDevice::createServer();
    BLEService *svc = srv->createService(SVC_UUID);

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

    beeps(3);  // 3 beeps = audio OK, about to init Flite

    g_flite = new Flite();
    g_flite->setVoice(register_cmu_us_kal());

    beeps(1);  // 1 beep = Flite init OK
}

void loop() {
    if (!g_tts_pending) {
        delay(50);
        return;
    }
    g_tts_pending = false;
    notify_status("PLAYING");

    // Synthesize to wave — kal voice: 8kHz mono
    cst_wave *wave = g_flite->textToWave(g_tts_buf);
    if (wave && wave->num_samples > 0) {
        int n = wave->num_samples;
        // Process in 256-sample chunks — avoids large heap allocation.
        // Convert 8kHz mono → 16kHz stereo via linear interpolation (4× per input sample).
        const int CHUNK = 256;
        int16_t chunk_buf[CHUNK * 4];
        for (int i = 0; i < n; i += CHUNK) {
            int cnt = (i + CHUNK < n) ? CHUNK : (n - i);
            for (int j = 0; j < cnt; j++) {
                int16_t s0 = wave->samples[i + j];
                int16_t s1 = (i + j + 1 < n) ? wave->samples[i + j + 1] : s0;
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

    notify_status("DONE");
}
