#include "audio_clips.h"
#include "i2s_audio.h"
#include <Arduino.h>

// ── Placeholder clip stubs ──────────────────────────────────────────────────
// Replace each extern array + length with your actual PCM data.
// Recommended format: 16-bit signed PCM, 16 kHz, mono.
// You can generate these with: sox input.wav -r 16000 -b 16 -c 1 -e signed-integer output.raw
// Then convert to a C array: xxd -i output.raw > clip_name.h

// Stub declarations — replace with actual generated headers
extern const uint8_t  clip_geofence_alert[];
extern const uint32_t clip_geofence_alert_len;

extern const uint8_t  clip_collision_warning[];
extern const uint32_t clip_collision_warning_len;

extern const uint8_t  clip_low_battery[];
extern const uint32_t clip_low_battery_len;

extern const uint8_t  clip_helmet_on[];
extern const uint32_t clip_helmet_on_len;

extern const uint8_t  clip_helmet_off[];
extern const uint32_t clip_helmet_off_len;

// ── Clip table ──────────────────────────────────────────────────────────────
struct ClipEntry {
    const uint8_t  *data;
    uint32_t        len;
    const char     *name;
};

static const ClipEntry s_clips[CLIP_COUNT] = {
    [CLIP_GEOFENCE_ALERT]    = { clip_geofence_alert,    clip_geofence_alert_len,    "Geofence Alert"    },
    [CLIP_COLLISION_WARNING] = { clip_collision_warning, clip_collision_warning_len, "Collision Warning" },
    [CLIP_LOW_BATTERY]       = { clip_low_battery,       clip_low_battery_len,       "Low Battery"       },
    [CLIP_HELMET_ON]         = { clip_helmet_on,         clip_helmet_on_len,         "Helmet On"         },
    [CLIP_HELMET_OFF]        = { clip_helmet_off,        clip_helmet_off_len,        "Helmet Off"        },
};

void audio_clips_init() {
    Serial.printf("audio_clips: %d clips registered\n", CLIP_COUNT);
}

void audio_clips_play(uint8_t idx) {
    if (idx >= CLIP_COUNT) {
        Serial.printf("audio_clips: invalid index %u\n", idx);
        return;
    }
    const ClipEntry &c = s_clips[idx];
    if (c.data == nullptr || c.len == 0) {
        Serial.printf("audio_clips: clip '%s' has no data\n", c.name);
        return;
    }
    Serial.printf("audio_clips: playing '%s' (%u bytes)\n", c.name, c.len);
    i2s_audio_play_raw(c.data, c.len);
}
