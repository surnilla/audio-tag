#pragma once
#include <stdint.h>

// Clip indices — must match order in audio_clips.cpp
typedef enum {
    CLIP_GEOFENCE_ALERT    = 0,
    CLIP_COLLISION_WARNING = 1,
    CLIP_LOW_BATTERY       = 2,
    CLIP_HELMET_ON         = 3,
    CLIP_HELMET_OFF        = 4,
    CLIP_COUNT
} audio_clip_id_t;

// Initialize clip table (call once from setup)
void audio_clips_init();

// Play a pre-stored clip by index; silently ignores out-of-range indices
void audio_clips_play(uint8_t idx);
