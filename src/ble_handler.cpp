#include "ble_handler.h"
#include "i2s_audio.h"
#include "audio_clips.h"
#include <Arduino.h>
#include <bluefruit.h>

// ── Service & Characteristic UUIDs ─────────────────────────────────────────
// Custom 128-bit UUIDs — replace with your own if desired
#define AUDIO_TAG_SVC_UUID   "12345678-1234-1234-1234-1234567890AB"
#define AUDIO_DATA_CHAR_UUID "12345678-1234-1234-1234-1234567890AC"  // phone → raw PCM
#define CLIP_SEL_CHAR_UUID   "12345678-1234-1234-1234-1234567890AD"  // helmet → clip index

BLEService        audioSvc(AUDIO_TAG_SVC_UUID);
BLECharacteristic audioDataChar(AUDIO_DATA_CHAR_UUID);  // Write, WriteNoRsp
BLECharacteristic clipSelChar(CLIP_SEL_CHAR_UUID);       // Write

// ── Audio data reassembly buffer ───────────────────────────────────────────
#define AUDIO_BUF_MAX (64 * 1024)  // 64 KB — enough for ~10s of 8-bit/8kHz mono
static uint8_t  s_audio_buf[AUDIO_BUF_MAX];
static uint32_t s_audio_len = 0;
static bool     s_streaming = false;

// Written by phone: raw PCM packets, multi-packet stream
// First packet carries a 4-byte little-endian total length header,
// followed by audio payload. Subsequent packets are pure payload.
static void on_audio_data_written(uint16_t conn_hdl,
                                  BLECharacteristic *chr,
                                  uint8_t *data, uint16_t len) {
    if (len == 0) return;

    if (!s_streaming) {
        // First packet: parse 4-byte total-length header
        if (len < 4) return;
        uint32_t total = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        if (total == 0 || total > AUDIO_BUF_MAX) {
            Serial.printf("BLE: invalid audio length %u\n", total);
            return;
        }
        s_audio_len  = 0;
        s_streaming  = true;
        data += 4; len -= 4;
        Serial.printf("BLE: audio stream start, expect %u bytes\n", total);
    }

    uint32_t copy = len;
    if (s_audio_len + copy > AUDIO_BUF_MAX) {
        copy = AUDIO_BUF_MAX - s_audio_len;
    }
    memcpy(s_audio_buf + s_audio_len, data, copy);
    s_audio_len += copy;

    // End-of-stream sentinel: zero-length write from phone
    // (handled below in on_audio_data_written with len==0 after streaming starts)
    // Alternatively phone can send exact byte count to trigger playback.
}

// Written by helmet tag: 1-byte clip index
static void on_clip_sel_written(uint16_t conn_hdl,
                                BLECharacteristic *chr,
                                uint8_t *data, uint16_t len) {
    if (len < 1) return;
    uint8_t idx = data[0];
    Serial.printf("BLE: clip select %u\n", idx);
    audio_clips_play(idx);
}

static void on_connect(uint16_t conn_hdl) {
    Serial.println("BLE: connected");
}

static void on_disconnect(uint16_t conn_hdl, uint8_t reason) {
    Serial.printf("BLE: disconnected (reason 0x%02X)\n", reason);
    // If streaming was in progress, play whatever we buffered
    if (s_streaming && s_audio_len > 0) {
        Serial.printf("BLE: playing buffered %u bytes\n", s_audio_len);
        i2s_audio_play_raw(s_audio_buf, s_audio_len);
    }
    s_streaming = false;
    s_audio_len = 0;
}

void ble_handler_init() {
    Bluefruit.begin();
    Bluefruit.setName("AudioTag");
    Bluefruit.Periph.setConnectCallback(on_connect);
    Bluefruit.Periph.setDisconnectCallback(on_disconnect);

    audioSvc.begin();

    audioDataChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    audioDataChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    audioDataChar.setFixedLen(0);
    audioDataChar.setWriteCallback(on_audio_data_written);
    audioDataChar.begin();

    clipSelChar.setProperties(CHR_PROPS_WRITE);
    clipSelChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    clipSelChar.setFixedLen(1);
    clipSelChar.setWriteCallback(on_clip_sel_written);
    clipSelChar.begin();

    // Start advertising
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(audioSvc);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.start(0);

    Serial.println("BLE: advertising as 'AudioTag'");
}

void ble_handler_update() {
    // Placeholder for any polling-based BLE logic
    // Bluefruit callbacks are interrupt-driven; nothing needed here yet
}
