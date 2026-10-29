// Stub I2S implementation — replaces i2s_audio.cpp while we verify BLE path.
// Swap back to the real i2s_audio.cpp once BLE end-to-end is confirmed working.
#include "i2s_audio.h"
#include <Arduino.h>

void i2s_audio_init()                                           {}
void i2s_audio_play_raw(const uint8_t *, uint32_t len_bytes)   {
    Serial.printf("I2S stub: play %u bytes (no output)\n", len_bytes);
}
void i2s_audio_stop()                                           {}
bool i2s_audio_is_playing()                                     { return false; }
bool i2s_audio_just_finished()                                  { return false; }
