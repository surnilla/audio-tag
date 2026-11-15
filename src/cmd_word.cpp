// ESP-SR MultiNet6 English command recognition.
// Recognises "HEY TALKIE" (CMD_HEY_TALKIE=1) and "OVER" (CMD_OVER=2).
// No separate wake word or AFE — MultiNet runs continuously on raw mic audio.
//
// Usage:
//   cmd_init()  — once in setup()
//   cmd_feed(stereo_chunk, chunk_bytes)  — once per 10-ms I2S DMA chunk

#include "cmd_word.h"
#include <Arduino.h>
#include <stdlib.h>
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "esp_mn_speech_commands.h"
#include "esp_heap_caps.h"

static esp_mn_iface_t     *s_multinet = nullptr;
static model_iface_data_t *s_model    = nullptr;
static int16_t            *s_buf      = nullptr;
static int                 s_buf_idx  = 0;
static int                 s_chunk    = 0;   // samples per detect() call (~480 = 30ms)

bool cmd_init() {
    // Mount "model" partition (SPIFFS, contains srmodels.bin flashed by PlatformIO)
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        Serial.println("[CMD] esp_srmodel_init failed — model partition not flashed?");
        return false;
    }

    // Pick the first MultiNet English model found in the partition
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        Serial.println("[CMD] no MultiNet English model in partition");
        return false;
    }
    Serial.printf("[CMD] model: %s\n", mn_name);

    s_multinet = esp_mn_handle_from_name(mn_name);
    if (!s_multinet) {
        Serial.println("[CMD] esp_mn_handle_from_name failed");
        return false;
    }

    // 6000 ms inter-command timeout — MultiNet returns ESP_MN_STATE_TIMEOUT
    // if no command is recognised within this window after the last detection.
    s_model = s_multinet->create(mn_name, 6000);
    if (!s_model) {
        Serial.println("[CMD] model create failed");
        return false;
    }

    // Register commands; command_id values map to CMD_HEY_TALKIE / CMD_OVER
    esp_mn_commands_alloc(s_multinet, s_model);
    esp_mn_commands_clear();
    esp_mn_commands_add(CMD_HEY_TALKIE, "HEY TALKIE");
    esp_mn_commands_add(CMD_OVER, "OVER");
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err && err->num > 0) {
        for (int i = 0; i < err->num; i++) {
            Serial.printf("[CMD] phrase rejected: %s\n", err->phrases[i]->string);
        }
    }

    s_chunk = s_multinet->get_samp_chunksize(s_model);  // typically 480 samples
    s_buf   = (int16_t *)heap_caps_malloc(
                  s_chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_buf) s_buf = (int16_t *)malloc(s_chunk * sizeof(int16_t));
    s_buf_idx = 0;

    Serial.printf("[CMD] ready: chunk=%d samples, rate=%d Hz\n",
                  s_chunk, s_multinet->get_samp_rate(s_model));
    return s_buf != nullptr;
}

void cmd_deinit() {
    if (s_model && s_multinet) {
        s_multinet->destroy(s_model);
        s_model = nullptr;
    }
    esp_mn_commands_free();
    free(s_buf);
    s_buf = nullptr;
}

int cmd_feed(const uint8_t *stereo_chunk, int chunk_bytes) {
    if (!s_model || !s_buf) return CMD_NONE;

    // Unpack 32-bit stereo I2S → 16-bit mono; sample is in upper 16 bits of each word.
    int frames = chunk_bytes / 8;
    for (int i = 0; i < frames; i++) {
        int16_t L = (int16_t)((*(const int32_t *)(stereo_chunk + i * 8))     >> 16);
        int16_t R = (int16_t)((*(const int32_t *)(stereo_chunk + i * 8 + 4)) >> 16);
        s_buf[s_buf_idx++] = (int16_t)(((int32_t)L + R) / 2);

        if (s_buf_idx >= s_chunk) {
            s_buf_idx = 0;
            esp_mn_state_t state = s_multinet->detect(s_model, s_buf);
            if (state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *result = s_multinet->get_results(s_model);
                if (result && result->num > 0) {
                    int id = result->command_id[0];
                    Serial.printf("[CMD] detected id=%d prob=%.2f\n",
                                  id, result->prob[0]);
                    return id;
                }
            }
        }
    }
    return CMD_NONE;
}
