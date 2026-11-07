#pragma once
#include <stdint.h>
#include <stdbool.h>

// Called once during setup() to initialise the EI continuous inference state.
void ww_init();

// Feed one 10-ms stereo I2S chunk (REC_CHUNK_BYTES bytes of 32-bit stereo frames
// at 16 kHz) into the wake-word detector.
// Returns true if "Hey Mobi" was just detected with enough confidence.
// The caller owns `stereo_chunk`; this function reads but does not modify it.
bool ww_feed(const uint8_t *stereo_chunk, int chunk_bytes);

// Reset the EI rolling window and cooldown — call after a WW recording session
// ends so stale features don't cause an immediate re-trigger.
void ww_reset();
