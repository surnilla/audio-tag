#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <M5EchoBase.h>
#include <esp_task_wdt.h>
#include "flite_arduino.h"

#define AMP_ENABLE   18
#define PIN_I2C_SDA  45
#define PIN_I2C_SCL   0
#define PIN_I2S_DIN    4
#define PIN_I2S_WS     3
#define PIN_I2S_DOUT  48
#define PIN_I2S_BCLK  17
#define SAMPLE_RATE  16000
#define BTN_PIN            41   // G41 push-to-talk button (active LOW)
#define SHORT_TAP_MS       250  // press ≤ this = short tap (candidate for double-tap)
#define DOUBLE_TAP_WINDOW  400  // two short taps within this window = replay

// UUIDs matching audio-tag-android app
#define SVC_UUID   "12345678-1234-1234-1234-1234567890ab"
#define AUDIO_UUID "12345678-1234-1234-1234-1234567890ac"  // write: raw PCM from phone
#define TTS_UUID   "12345678-1234-1234-1234-1234567890ad"  // write: text → Flite TTS
#define STA_UUID   "12345678-1234-1234-1234-1234567890ae"  // notify: status
#define MIC_UUID   "12345678-1234-1234-1234-1234567890af"  // notify: mic PCM → phone STT

// Status byte values
#define STATUS_RECEIVING  0x01
#define STATUS_DONE       0x02
#define STATUS_RECORDING  0x03
#define STATUS_MIC_SEND   0x04

// Max PTT recording: 10 seconds of 16kHz mono 16-bit = 320 KB
// Falls back to 5s or 3s if PSRAM unavailable
#define MAX_REC_BYTES     (SAMPLE_RATE * sizeof(int16_t) * 10)
#define MAX_REC_BYTES_5S  (SAMPLE_RATE * sizeof(int16_t) * 5)
#define MAX_REC_BYTES_3S  (SAMPLE_RATE * sizeof(int16_t) * 3)
// BLE notify payload: conservative 244 bytes — fits within MTU 256 (negotiated
// by Samsung Galaxy). Requesting MTU 512 doesn't always succeed; if we assume
// 509 bytes per notification but only 253 are sent, half the audio is lost.
#define NOTIFY_PAYLOAD 244

static M5EchoBase echo;
static BLECharacteristic *g_sta_char = nullptr;
static BLECharacteristic *g_mic_char = nullptr;

// --- Audio (raw PCM) path: phone → Echo ---
static uint8_t  *g_audio_buf       = nullptr;
static uint32_t  g_audio_total     = 0;
static uint32_t  g_audio_received  = 0;
static bool      g_audio_receiving = false;
static volatile bool g_audio_pending = false;

// --- TTS (text) path ---
static volatile bool g_tts_pending = false;
static char          g_tts_buf[256];
static Flite        *g_flite = nullptr;

// --- PTT mic path: Echo → phone ---
static uint8_t  *g_rec_buf   = nullptr;  // mono PCM in PSRAM
static uint32_t  g_rec_bytes = 0;        // bytes recorded so far
static bool      g_btn_last  = false;    // previous button state
static volatile bool g_rec_pending = false;

// --- Replay last received message (double-tap G41) ---
static uint8_t  *g_replay_pcm_buf  = nullptr;  // saved Phone Voice PCM (PSRAM)
static uint32_t  g_replay_pcm_size = 0;
static uint8_t   g_replay_type     = 0;  // 0=none 1=PCM 2=TTS
static volatile bool g_replay_pending = false;

// ---------------------------------------------------------------

static void play_beep(int freq_hz, int duration_ms) {
    int samples = SAMPLE_RATE * duration_ms / 1000;
    int32_t *buf = (int32_t *)malloc(samples * 2 * sizeof(int32_t));  // stereo 32-bit
    if (!buf) return;
    for (int i = 0; i < samples; i++) {
        int32_t s = (int32_t)((int16_t)(28000.0f * sinf(2.0f * M_PI * freq_hz * i / SAMPLE_RATE))) << 16;
        buf[i*2+0] = s;  // L
        buf[i*2+1] = s;  // R
    }
    echo.play((const uint8_t *)buf, samples * 2 * sizeof(int32_t));
    delay(duration_ms + 200);
    free(buf);
}

static void beeps(int n) {
    for (int i = 0; i < n; i++) {
        play_beep(1000, 400);
        if (i < n - 1) delay(200);
    }
}

// Attention chime played before an incoming message.
// Two sharp high notes + a settling tone, then a 3-second pause so the
// worker is ready before the message begins.
static void play_notification() {
    play_beep(1760, 100);   // A6 — sharp attention note
    delay(80);
    play_beep(1760, 100);   // repeat
    delay(80);
    play_beep(1174, 300);   // D6 — settling tone
    delay(2500);            // pause → ~3.8 s total from chime start to message
}

static void notify_byte(uint8_t val) {
    if (!g_sta_char) return;
    g_sta_char->setValue(&val, 1);
    g_sta_char->notify();
}

// Play 16kHz mono PCM through M5EchoBase (stereo 32-bit). Chunked to avoid large alloc.
// I2S is 32-bit wide: shift each 16-bit sample to the MSB of the 32-bit word.
static void play_mono16(const int16_t *pcm, int n_samples) {
    const int CHUNK = 256;
    int32_t chunk_buf[CHUNK * 2];  // stereo 32-bit
    for (int i = 0; i < n_samples; i += CHUNK) {
        int cnt = (i + CHUNK < n_samples) ? CHUNK : (n_samples - i);
        for (int j = 0; j < cnt; j++) {
            int32_t s = (int32_t)pcm[i + j] << 16;
            chunk_buf[j*2+0] = s;  // L
            chunk_buf[j*2+1] = s;  // R
        }
        echo.play((const uint8_t *)chunk_buf, cnt * 2 * sizeof(int32_t));
    }
}

// Record one chunk of stereo audio and convert to mono into g_rec_buf.
// I2S is 32-bit stereo: each DMA frame = 8 bytes [L:4 bytes, R:4 bytes].
// ES8311_RESOLUTION_32 sends 24-bit audio MSB-aligned; top 16 bits = signal.
#define REC_CHUNK_BYTES 1280   // 160 frames × 8 bytes = 10ms at 16kHz 32-bit stereo
static uint8_t s_stereo_chunk[REC_CHUNK_BYTES];  // static — no VLA on stack

static void record_chunk(int rec_chunk_bytes) {
    echo.record(s_stereo_chunk, rec_chunk_bytes);
    int frames = rec_chunk_bytes / 8;  // 8 bytes per 32-bit stereo frame
    for (int i = 0; i < frames && g_rec_bytes + 2 <= MAX_REC_BYTES; i++) {
        int16_t L    = (int16_t)((*(int32_t *)&s_stereo_chunk[i * 8])     >> 16);
        int16_t R    = (int16_t)((*(int32_t *)&s_stereo_chunk[i * 8 + 4]) >> 16);
        int16_t mono = (int16_t)(((int32_t)L + R) / 2);
        *(int16_t *)&g_rec_buf[g_rec_bytes] = mono;
        g_rec_bytes += 2;
    }
}

// Send recorded mono PCM to phone via MIC_DATA notify.
// Protocol mirrors AUDIO_RAW write: [4-byte LE total length][PCM data...]
static void send_mic_audio() {
    if (!g_mic_char || g_rec_bytes == 0) return;
    notify_byte(STATUS_MIC_SEND);

    uint32_t total = g_rec_bytes;
    uint8_t  notify_buf[NOTIFY_PAYLOAD];

    // First notification: 4-byte header + first data chunk
    notify_buf[0] = total & 0xFF;
    notify_buf[1] = (total >> 8)  & 0xFF;
    notify_buf[2] = (total >> 16) & 0xFF;
    notify_buf[3] = (total >> 24) & 0xFF;
    uint32_t first = (total < (uint32_t)(NOTIFY_PAYLOAD - 4))
                     ? total : (NOTIFY_PAYLOAD - 4);
    memcpy(notify_buf + 4, g_rec_buf, first);
    g_mic_char->setValue(notify_buf, 4 + first);
    g_mic_char->notify();
    delay(20);

    uint32_t sent = first;
    while (sent < total) {
        uint32_t chunk = (total - sent < NOTIFY_PAYLOAD)
                         ? (total - sent) : NOTIFY_PAYLOAD;
        g_mic_char->setValue(g_rec_buf + sent, chunk);
        g_mic_char->notify();
        sent += chunk;
        delay(20);  // Coded PHY S8 ≈ 12 KB/s; 244 bytes per 20 ms fits within budget
    }

    notify_byte(STATUS_DONE);
}

// ---------------------------------------------------------------------------
// Custom Flite allocator (CST_USER_MALLOC).
// Wave buffers are 8-50 KB and cause fragmentation/exhaustion in internal SRAM
// when BLE is running.  We route allocations >= 8 KB to PSRAM so they don't
// compete with BLE, while keeping small synthesis structures in fast internal
// SRAM so the synthesis inner loops aren't slowed by PSRAM cache misses.
// ESP-IDF free() handles both internal and external (PSRAM) pointers correctly.
// ---------------------------------------------------------------------------
extern "C" {

static const int FLITE_PSRAM_THRESHOLD = 8192;

void *cst_safe_alloc(int size) {
    if (size <= 0) size = 1;
    void *p = nullptr;
    if (size >= FLITE_PSRAM_THRESHOLD) {
        p = heap_caps_calloc(1, (size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = calloc(1, (size_t)size);
    } else {
        p = calloc(1, (size_t)size);
        if (!p) p = heap_caps_calloc(1, (size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!p) abort();
    return p;
}

void *cst_safe_calloc(int size) { return cst_safe_alloc(size); }

void *cst_safe_realloc(void *p, int size) {
    if (size <= 0) size = 1;
    if (!p) return cst_safe_alloc(size);
    void *np = realloc(p, (size_t)size);
    if (!np) abort();
    return np;
}

void cst_free(void *p) { free(p); }

} // extern "C"

// ---------------------------------------------------------------

class ServerCallbacks : public BLEServerCallbacks {
    void onDisconnect(BLEServer *srv) override {
        g_audio_receiving = false;
        g_audio_pending   = false;
        if (g_audio_buf) { free(g_audio_buf); g_audio_buf = nullptr; }
        BLEDevice::getAdvertising()->start();
    }
};

class AudioCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        uint8_t *data = c->getData();
        size_t   len  = c->getLength();
        if (len == 0) return;

        if (!g_audio_receiving) {
            if (len < 4) return;
            g_audio_total = (uint32_t)data[0]
                          | ((uint32_t)data[1] << 8)
                          | ((uint32_t)data[2] << 16)
                          | ((uint32_t)data[3] << 24);
            if (g_audio_buf) { free(g_audio_buf); g_audio_buf = nullptr; }
            g_audio_buf = (uint8_t *)heap_caps_malloc(
                g_audio_total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!g_audio_buf)
                g_audio_buf = (uint8_t *)malloc(g_audio_total);
            if (!g_audio_buf) return;
            g_audio_received  = 0;
            g_audio_receiving = true;
            notify_byte(STATUS_RECEIVING);
            size_t payload = len - 4;
            if (payload > 0) {
                memcpy(g_audio_buf, data + 4, payload);
                g_audio_received = payload;
            }
        } else {
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
        notify_byte(STATUS_RECEIVING);
    }
};

// ---------------------------------------------------------------

void setup() {
    delay(500);
    Serial.begin(115200);

    // No heap_caps_malloc_extmem_enable — Flite uses the custom cst_* allocator
    // below (CST_USER_MALLOC) which routes large allocs to PSRAM explicitly.

    // WDT at 30s — Flite init and synthesis legitimately exceed the default 5s.
    esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt_cfg);

    // Init Flite BEFORE BLE — BLE consumes heap Flite needs
    g_flite = new Flite();
    g_flite->setVoice(register_cmu_us_kal());

    pinMode(BTN_PIN, INPUT_PULLUP);

    BLEDevice::setMTU(512);
    BLEDevice::init("AudioTag");
    // Request Coded PHY (S8) as the preferred PHY for all new connections.
    // Falls back to 1M if the central doesn't support Coded PHY — no error.
    ble_gap_set_prefered_default_le_phy(
        BLE_HCI_LE_PHY_CODED_PREF_MASK,
        BLE_HCI_LE_PHY_CODED_PREF_MASK
    );
    BLEServer  *srv = BLEDevice::createServer();
    srv->setCallbacks(new ServerCallbacks());
    BLEService *svc = srv->createService(BLEUUID(SVC_UUID), 20);

    BLECharacteristic *audio = svc->createCharacteristic(
        AUDIO_UUID, BLECharacteristic::PROPERTY_WRITE);
    audio->setCallbacks(new AudioCallback());

    BLECharacteristic *tts = svc->createCharacteristic(
        TTS_UUID, BLECharacteristic::PROPERTY_WRITE);
    tts->setCallbacks(new TtsCallback());

    g_sta_char = svc->createCharacteristic(
        STA_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_sta_char->addDescriptor(new BLE2902());

    g_mic_char = svc->createCharacteristic(
        MIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_mic_char->addDescriptor(new BLE2902());

    svc->start();
    BLEDevice::getAdvertising()->start();

    pinMode(AMP_ENABLE, OUTPUT);
    digitalWrite(AMP_ENABLE, HIGH);
    if (!echo.init(SAMPLE_RATE, PIN_I2C_SDA, PIN_I2C_SCL,
                   PIN_I2S_DIN, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_BCLK)) {
        return;
    }
    echo.setSpeakerVolume(85);
    echo.setMicGain(ES8311_MIC_GAIN_6DB);   // modest boost — 18dB caused clipping

    // Allocate recording buffer — try PSRAM first, fall back to heap
    g_rec_buf = (uint8_t *)heap_caps_malloc(
        MAX_REC_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_rec_buf)
        g_rec_buf = (uint8_t *)heap_caps_malloc(
            MAX_REC_BYTES_5S, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_rec_buf)
        g_rec_buf = (uint8_t *)malloc(MAX_REC_BYTES_3S);  // 3s in regular heap

    beeps(3);  // 3 beeps = audio + rec buffer OK
    if (g_rec_buf) beeps(1);  // +1 beep = PSRAM rec buffer allocated

    // Flite init deferred to first TTS use — avoids WDT timeout during boot
}

void loop() {
    // --- PTT / double-tap button handling ---
    bool btn_pressed = (digitalRead(BTN_PIN) == LOW);

    // Statics for double-tap detection
    static uint32_t s_press_start_ms      = 0;
    static bool     s_short_press_pending = false;
    static uint32_t s_short_press_time    = 0;

    if (btn_pressed && !g_btn_last) {
        s_press_start_ms = millis();
        if (s_short_press_pending &&
                (millis() - s_short_press_time < DOUBLE_TAP_WINDOW)) {
            // Second short tap within window → replay last message
            s_short_press_pending = false;
            g_replay_pending = true;
        } else {
            // Normal PTT press — start recording
            s_short_press_pending = false;
            g_rec_bytes = 0;
            notify_byte(STATUS_RECORDING);
        }
    }

    if (btn_pressed && g_rec_buf && !g_replay_pending) {
        // Held — record one chunk (~10 ms at 16 kHz 32-bit stereo)
        record_chunk(REC_CHUNK_BYTES);
    }

    if (!btn_pressed && g_btn_last && !g_replay_pending) {
        uint32_t dur = millis() - s_press_start_ms;
        if (dur < SHORT_TAP_MS) {
            // Short tap — first of a potential double-tap; discard the tiny recording
            notify_byte(STATUS_DONE);   // clear phone's "Recording…" state
            g_rec_bytes = 0;
            s_short_press_pending = true;
            s_short_press_time    = millis();
        } else {
            // Long press — PTT complete, send audio
            s_short_press_pending = false;
            if (g_rec_bytes > 0) g_rec_pending = true;
        }
    }

    // Expire the double-tap window
    if (s_short_press_pending &&
            (millis() - s_short_press_time >= DOUBLE_TAP_WINDOW)) {
        s_short_press_pending = false;
    }

    g_btn_last = btn_pressed;

    // --- Send mic audio to phone for STT ---
    if (g_rec_pending) {
        g_rec_pending = false;
        send_mic_audio();
        return;
    }

    // --- Raw PCM audio from phone → speaker ---
    if (g_audio_pending) {
        g_audio_pending = false;
        play_notification();
        int n_samples = g_audio_total / sizeof(int16_t);
        play_mono16((const int16_t *)g_audio_buf, n_samples);
        // Save for replay — transfer ownership, don't free yet
        if (g_replay_pcm_buf) { free(g_replay_pcm_buf); }
        g_replay_pcm_buf  = g_audio_buf;
        g_replay_pcm_size = g_audio_total;
        g_audio_buf       = nullptr;
        g_replay_type     = 1;
        notify_byte(STATUS_DONE);
        return;
    }

    // --- Text → Flite TTS → speaker ---
    // Runs in the Arduino loop task.  CONFIG_ARDUINO_LOOP_STACK_SIZE=32768 gives
    // Flite enough stack for multi-word synthesis without overflow.
    if (g_tts_pending) {
        g_tts_pending = false;
        play_notification();
        g_replay_type = 0;
        cst_wave *wave = g_flite ? g_flite->textToWave(g_tts_buf) : nullptr;
        if (wave && wave->num_samples > 0) {
            // Cache the synthesised samples in PSRAM for instant replay
            // (avoids re-synthesising on double-tap, which caused a 1-3 s delay).
            size_t wave_bytes = (size_t)wave->num_samples * sizeof(int16_t);
            if (g_replay_pcm_buf) { free(g_replay_pcm_buf); }
            g_replay_pcm_buf = (uint8_t *)heap_caps_malloc(
                wave_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!g_replay_pcm_buf)
                g_replay_pcm_buf = (uint8_t *)malloc(wave_bytes);
            if (g_replay_pcm_buf) {
                memcpy(g_replay_pcm_buf, wave->samples, wave_bytes);
                g_replay_pcm_size = wave_bytes;
                g_replay_type = 2;  // cached TTS PCM — 8 kHz 16-bit mono
            }

            int n = wave->num_samples;
            const int CHUNK = 256;
            int32_t chunk_buf[CHUNK * 4];  // 2× upsampled stereo 32-bit
            for (int i = 0; i < n; i += CHUNK) {
                int cnt = (i + CHUNK < n) ? CHUNK : (n - i);
                for (int j = 0; j < cnt; j++) {
                    int16_t s0  = wave->samples[i + j];
                    int16_t s1  = (i + j + 1 < n) ? wave->samples[i + j + 1] : s0;
                    int16_t mid = (int16_t)(((int32_t)s0 + s1) / 2);
                    chunk_buf[j*4+0] = (int32_t)s0  << 16;
                    chunk_buf[j*4+1] = (int32_t)s0  << 16;
                    chunk_buf[j*4+2] = (int32_t)mid << 16;
                    chunk_buf[j*4+3] = (int32_t)mid << 16;
                }
                echo.play((const uint8_t *)chunk_buf, cnt * 4 * sizeof(int32_t));
            }
            delete_wave(wave);
        }
        notify_byte(STATUS_DONE);
        return;
    }

    // --- Double-tap replay ---
    if (g_replay_pending) {
        g_replay_pending = false;
        if (g_replay_pcm_buf && g_replay_pcm_size > 0) {
            int n = (int)(g_replay_pcm_size / sizeof(int16_t));
            const int16_t *samples = (const int16_t *)g_replay_pcm_buf;
            if (g_replay_type == 1) {
                // Phone Voice: 16 kHz mono — play directly
                play_mono16(samples, n);
            } else if (g_replay_type == 2) {
                // Device Voice: cached 8 kHz Flite PCM — 2× upsample to 16 kHz
                const int CHUNK = 256;
                int32_t chunk_buf[CHUNK * 4];
                for (int i = 0; i < n; i += CHUNK) {
                    int cnt = (i + CHUNK < n) ? CHUNK : (n - i);
                    for (int j = 0; j < cnt; j++) {
                        int16_t s0  = samples[i + j];
                        int16_t s1  = (i + j + 1 < n) ? samples[i + j + 1] : s0;
                        int16_t mid = (int16_t)(((int32_t)s0 + s1) / 2);
                        chunk_buf[j*4+0] = (int32_t)s0  << 16;
                        chunk_buf[j*4+1] = (int32_t)s0  << 16;
                        chunk_buf[j*4+2] = (int32_t)mid << 16;
                        chunk_buf[j*4+3] = (int32_t)mid << 16;
                    }
                    echo.play((const uint8_t *)chunk_buf, cnt * 4 * sizeof(int32_t));
                }
            }
        }
        return;
    }

    // When idle (button not pressed), drain the RX DMA so stale audio never
    // accumulates before a recording session.
    if (!btn_pressed) {
        echo.record(s_stereo_chunk, REC_CHUNK_BYTES);  // drain + discard
    }
}
