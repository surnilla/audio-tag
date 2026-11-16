#pragma once
#include <stdint.h>

// Command IDs returned by cmd_feed()
#define CMD_NONE               0
#define CMD_HEY_TALKIE         1
#define CMD_OVER               2

// Worker reply commands (sent via REPLY BLE characteristic)
#define CMD_COPY_THAT          3
#define CMD_THANK_YOU          4
#define CMD_YES                5
#define CMD_NO                 6
#define CMD_REPEAT_THAT        7
#define CMD_STAND_BY           8
#define CMD_ON_MY_WAY          9
#define CMD_ALMOST_THERE      10
#define CMD_IN_POSITION       11
#define CMD_HEADING_BACK      12
#define CMD_STAY_PUT          13
#define CMD_MOVE_OUT          14
#define CMD_STARTING_NOW      15
#define CMD_JOB_DONE          16
#define CMD_TAKING_BREAK      17
#define CMD_GATE_OPEN         18
#define CMD_GATE_CLOSED       19
#define CMD_ROAD_CLEAR        20
#define CMD_ALL_CLEAR         21
#define CMD_NEED_HELP         22
#define CMD_SEND_BACKUP       23
#define CMD_STOP_WORK         24
#define CMD_DANGER_AHEAD      25
#define CMD_VEHICLE_APPROACHING 26
#define CMD_EVACUATE_AREA     27

// Initialize MultiNet6 English command recognizer from "model" flash partition.
// Must be called in setup() after the filesystem is ready.
// Returns true on success; false if the model partition is not flashed.
bool cmd_init();

// Feed one stereo 32-bit I2S chunk into the recognizer (non-blocking).
// stereo_chunk: REC_CHUNK_BYTES bytes of [L:int32][R:int32] frames at 16 kHz.
// Returns a CMD_* value or CMD_NONE.
int cmd_feed(const uint8_t *stereo_chunk, int chunk_bytes);

// Free MultiNet resources.
void cmd_deinit();

// Returns true if cmd_id is a worker reply command (not HEY_TALKIE / OVER).
inline bool cmd_is_reply(int id) { return id >= CMD_COPY_THAT; }

// Returns the display text for a reply command, or nullptr for non-reply IDs.
const char *cmd_text(int id);
