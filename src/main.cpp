#include <Arduino.h>
#include "ble_handler.h"
#include "i2s_audio.h"
#include "audio_clips.h"

// XIAO nRF52840 red LED on P0.26, active LOW
#define LED_RED_BIT (1UL << 26)

static void blink_led(int times) {
    NRF_P0->DIRSET = LED_RED_BIT;
    for (int i = 0; i < times; i++) {
        NRF_P0->OUTCLR = LED_RED_BIT; delay(200);
        NRF_P0->OUTSET = LED_RED_BIT; delay(200);
    }
}

void setup() {
    Serial.begin(115200);
    blink_led(1);          // 1 blink = setup started

    audio_clips_init();
    blink_led(2);          // 2 blinks = past audio_clips_init

    i2s_audio_init();
    blink_led(3);          // 3 blinks = past i2s_audio_init

    ble_handler_init();
    blink_led(4);          // 4 blinks = fully initialized

    Serial.println("audio-tag ready");
}

void loop() {
    ble_handler_update();
}
