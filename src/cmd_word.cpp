// ESP-SR MultiNet6 English command recognition.
// Recognises "HEY TALKIE" (CMD_HEY_TALKIE=1), "OVER" (CMD_OVER=2),
// and 25 worker reply phrases (CMD_COPY_THAT=3 … CMD_EVACUATE_AREA=27).
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

// Map of command ID → display text
static const struct { int id; const char *text; } s_cmd_text[] = {
    { CMD_COPY_THAT,           "Copy that"           },
    { CMD_THANK_YOU,           "Thank you"           },
    { CMD_YES,                 "Yes"                 },
    { CMD_NO,                  "No"                  },
    { CMD_REPEAT_THAT,         "Repeat that"         },
    { CMD_STAND_BY,            "Stand by"            },
    { CMD_ON_MY_WAY,           "On my way"           },
    { CMD_ALMOST_THERE,        "Almost there"        },
    { CMD_IN_POSITION,         "In position"         },
    { CMD_HEADING_BACK,        "Heading back"        },
    { CMD_STAY_PUT,            "Stay put"            },
    { CMD_MOVE_OUT,            "Move out"            },
    { CMD_STARTING_NOW,        "Starting now"        },
    { CMD_JOB_DONE,            "Job done"            },
    { CMD_TAKING_BREAK,        "Taking break"        },
    { CMD_GATE_OPEN,           "Gate open"           },
    { CMD_GATE_CLOSED,         "Gate closed"         },
    { CMD_ROAD_CLEAR,          "Road clear"          },
    { CMD_ALL_CLEAR,           "All clear"           },
    { CMD_NEED_HELP,           "Need help"           },
    { CMD_SEND_BACKUP,         "Send backup"         },
    { CMD_STOP_WORK,           "Stop work"           },
    { CMD_DANGER_AHEAD,        "Danger ahead"        },
    { CMD_VEHICLE_APPROACHING, "Vehicle approaching" },
    { CMD_EVACUATE_AREA,       "Evacuate area"       },
};

const char *cmd_text(int id) {
    for (auto &e : s_cmd_text) {
        if (e.id == id) return e.text;
    }
    return nullptr;
}

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

    // 6000 ms inter-command timeout
    s_model = s_multinet->create(mn_name, 6000);
    if (!s_model) {
        Serial.println("[CMD] model create failed");
        return false;
    }

    // Register all commands
    esp_mn_commands_alloc(s_multinet, s_model);
    esp_mn_commands_clear();
    esp_mn_commands_add(CMD_HEY_TALKIE,          "HEY TALKIE");
    esp_mn_commands_add(CMD_OVER,                "OVER");
    esp_mn_commands_add(CMD_COPY_THAT,           "COPY THAT");
    esp_mn_commands_add(CMD_THANK_YOU,           "THANK YOU");
    esp_mn_commands_add(CMD_YES,                 "YES");
    esp_mn_commands_add(CMD_NO,                  "NO");
    esp_mn_commands_add(CMD_REPEAT_THAT,         "REPEAT THAT");
    esp_mn_commands_add(CMD_STAND_BY,            "STAND BY");
    esp_mn_commands_add(CMD_ON_MY_WAY,           "ON MY WAY");
    esp_mn_commands_add(CMD_ALMOST_THERE,        "ALMOST THERE");
    esp_mn_commands_add(CMD_IN_POSITION,         "IN POSITION");
    esp_mn_commands_add(CMD_HEADING_BACK,        "HEADING BACK");
    esp_mn_commands_add(CMD_STAY_PUT,            "STAY PUT");
    esp_mn_commands_add(CMD_MOVE_OUT,            "MOVE OUT");
    esp_mn_commands_add(CMD_STARTING_NOW,        "STARTING NOW");
    esp_mn_commands_add(CMD_JOB_DONE,            "JOB DONE");
    esp_mn_commands_add(CMD_TAKING_BREAK,        "TAKING BREAK");
    esp_mn_commands_add(CMD_GATE_OPEN,           "GATE OPEN");
    esp_mn_commands_add(CMD_GATE_CLOSED,         "GATE CLOSED");
    esp_mn_commands_add(CMD_ROAD_CLEAR,          "ROAD CLEAR");
    esp_mn_commands_add(CMD_ALL_CLEAR,           "ALL CLEAR");
    esp_mn_commands_add(CMD_NEED_HELP,           "NEED HELP");
    esp_mn_commands_add(CMD_SEND_BACKUP,         "SEND BACKUP");
    esp_mn_commands_add(CMD_STOP_WORK,           "STOP WORK");
    esp_mn_commands_add(CMD_DANGER_AHEAD,        "DANGER AHEAD");
    esp_mn_commands_add(CMD_VEHICLE_APPROACHING, "VEHICLE APPROACHING");
    esp_mn_commands_add(CMD_EVACUATE_AREA,       "EVACUATE AREA");

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

    Serial.printf("[CMD] ready: chunk=%d samples, rate=%d Hz, commands=27\n",
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
                    Serial.printf("[CMD] detected id=%d prob=%.2f text=%s\n",
                                  id, result->prob[0],
                                  cmd_text(id) ? cmd_text(id) : (id == CMD_HEY_TALKIE ? "HEY TALKIE" : "OVER"));
                    return id;
                }
            }
        }
    }
    return CMD_NONE;
}
