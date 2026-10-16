#pragma once
#include <stdint.h>

// Initialize I2S peripheral (call once from setup)
void i2s_audio_init();

// Play raw PCM samples from a buffer
// Assumes 16-bit signed, 16 kHz, mono unless configured otherwise
void i2s_audio_play_raw(const uint8_t *pcm_data, uint32_t len_bytes);

// Stop playback immediately
void i2s_audio_stop();

// Returns true if playback is currently in progress
bool i2s_audio_is_playing();

// Returns true exactly once when playback transitions from playing → stopped.
// Call repeatedly from loop(); resets the flag on first true return.
bool i2s_audio_just_finished();
