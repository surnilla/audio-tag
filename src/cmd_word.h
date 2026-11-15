#pragma once
#include <stdint.h>

// Command IDs returned by cmd_feed()
#define CMD_NONE        0
#define CMD_HEY_TALKIE  1
#define CMD_OVER        2

// Initialize MultiNet6 English command recognizer from "model" flash partition.
// Must be called in setup() after the filesystem is ready.
// Returns true on success; false if the model partition is not flashed.
bool cmd_init();

// Feed one stereo 32-bit I2S chunk into the recognizer (non-blocking).
// stereo_chunk: REC_CHUNK_BYTES bytes of [L:int32][R:int32] frames at 16 kHz.
// Returns CMD_HEY_TALKIE, CMD_OVER, or CMD_NONE.
int cmd_feed(const uint8_t *stereo_chunk, int chunk_bytes);

// Free MultiNet resources.
void cmd_deinit();
