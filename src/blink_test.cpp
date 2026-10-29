// Minimal blink test — uses direct NRF_P0 register writes to bypass the
// feather variant pin-mapping, so we directly control P0.26 (LED_RED on XIAO).
// If this blinks, the linker script + SoftDevice version + toolchain are all correct.
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// XIAO nRF52840 RGB LEDs wired to nRF P0 port (active LOW)
// We write the NRF_P0 registers directly — no variant pin-map dependency.
#define XIAO_RED_BIT   (1UL << 26)   // P0.26
#define XIAO_GREEN_BIT (1UL << 30)   // P0.30
#define XIAO_BLUE_BIT  (1UL <<  6)   // P0.06

void setup() {
    // Configure LED pins as output via NRF_P0->DIRSET
    NRF_P0->DIRSET = XIAO_RED_BIT | XIAO_GREEN_BIT | XIAO_BLUE_BIT;
    // All off (active LOW — drive HIGH)
    NRF_P0->OUTSET = XIAO_RED_BIT | XIAO_GREEN_BIT | XIAO_BLUE_BIT;

    Serial.begin(115200);
    // Don't block on Serial — XIAO will still blink even without USB open
}

void loop() {
    NRF_P0->OUTCLR = XIAO_RED_BIT;   // red ON
    delay(300);
    NRF_P0->OUTSET = XIAO_RED_BIT;   // red OFF
    delay(300);
}
