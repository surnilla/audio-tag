// Placeholder clip data — replace with real PCM arrays generated via:
//   sox input.wav -r 16000 -b 16 -c 1 -e signed-integer output.raw
//   xxd -i output.raw > clip_name.h
// See audio_clips/README.md

#include <stdint.h>

// 1-sample silence placeholder for each clip
static const uint8_t silence[] = { 0x00, 0x00 };

extern const uint8_t  clip_geofence_alert[]    = { 0x00, 0x00 };
extern const uint32_t clip_geofence_alert_len  = 0;

extern const uint8_t  clip_collision_warning[]    = { 0x00, 0x00 };
extern const uint32_t clip_collision_warning_len  = 0;

extern const uint8_t  clip_low_battery[]    = { 0x00, 0x00 };
extern const uint32_t clip_low_battery_len  = 0;

extern const uint8_t  clip_helmet_on[]    = { 0x00, 0x00 };
extern const uint32_t clip_helmet_on_len  = 0;

extern const uint8_t  clip_helmet_off[]    = { 0x00, 0x00 };
extern const uint32_t clip_helmet_off_len  = 0;
