#include <Arduino.h>
#include "ble_handler.h"
#include "i2s_audio.h"
#include "audio_clips.h"

void setup() {
    Serial.begin(115200);

    audio_clips_init();
    i2s_audio_init();
    ble_handler_init();

    Serial.println("audio-tag ready");
}

void loop() {
    ble_handler_update();
}
