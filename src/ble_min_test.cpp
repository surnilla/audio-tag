// BLE minimal test — reverted to last known working state.
// begin() confirmed returning true in previous session.
// RED x1 at setup entry = this exact firmware version.
#include <Arduino.h>
#include <bluefruit.h>
#include <nrf.h>
#include <nrfx_power.h>

#define LED_RED_BIT  (1UL << 26)
#define LED_BLUE_BIT (1UL <<  6)

static void busy_blink(uint32_t bit, uint32_t n) {
    NRF_P0->DIRSET = bit;
    NRF_P0->OUTSET = bit;
    for (volatile uint32_t d = 0; d < 200000; d++) {}
    for (uint32_t i = 0; i < n; i++) {
        NRF_P0->OUTCLR = bit;
        for (volatile uint32_t j = 0; j < 500000; j++) {}
        NRF_P0->OUTSET = bit;
        for (volatile uint32_t j = 0; j < 500000; j++) {}
    }
    for (volatile uint32_t j = 0; j < 1500000; j++) {}
}

extern "C" void HardFault_Handler(void) {
    while (true) {
        busy_blink(LED_RED_BIT,  8);
        busy_blink(LED_BLUE_BIT, 8);
    }
}

__attribute__((constructor(101)))
static void early_diag() { busy_blink(LED_RED_BIT, 3); }

__attribute__((constructor(65534)))
static void late_diag() { busy_blink(LED_BLUE_BIT, 3); }

static void slow_blink(uint32_t bit, int n) {
    NRF_P0->DIRSET = bit;
    for (int i = 0; i < n; i++) {
        NRF_P0->OUTCLR = bit; delay(400);
        NRF_P0->OUTSET = bit; delay(400);
    }
    delay(800);
}

void setup() {
    Serial.begin(115200);
    // Wait for host to open CDC port (up to 5 s); drop-through if no host
    { uint32_t t0 = millis(); while (!Serial && (millis() - t0 < 5000)) delay(10); }
    delay(200);

    // Print reset reason + GPREGRET (persists across watchdog resets)
    uint32_t resetreas = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFF;
    uint32_t gpregret = NRF_POWER->GPREGRET;
    NRF_POWER->GPREGRET = gpregret + 1;  // increment each boot — tells us if bootloader clears it
    Serial.print("RESETREAS=0x"); Serial.println(resetreas, HEX);
    Serial.print("GPREGRET=0x");  Serial.println(gpregret, HEX);
    Serial.flush();

    Serial.println("SETUP_ENTERED");
    Serial.flush();
    slow_blink(LED_RED_BIT, 1);   // RED x1 = this firmware version

    // Uninit nrfx_power before SD enable (same as usb_softdevice_pre_enable)
    // Avoids POWER peripheral conflict that causes SD fault after enable
    nrfx_power_usbevt_disable();
    nrfx_power_usbevt_uninit();
    nrfx_power_uninit();

    Serial.println("PRE_SD_ENABLE");
    Serial.flush();

    // Custom fault handler: store fault id in GPREGRET, then blink
    // GPREGRET survives watchdog reset so we can read it next boot
    static auto sd_fault = [](uint32_t id, uint32_t pc, uint32_t info) {
        NRF_POWER->GPREGRET = 0xA0 | (id & 0x0F);  // 0xA0 = marker, low nibble = fault id
        while (true) {
            busy_blink(LED_RED_BIT,  6);
            busy_blink(LED_BLUE_BIT, 6);
        }
    };

    // Direct sd_softdevice_enable() with POWER peripheral properly released
    nrf_clock_lf_cfg_t clk = {
        .source       = NRF_CLOCK_LF_SRC_RC,
        .rc_ctiv      = 16,
        .rc_temp_ctiv = 2,
        .accuracy     = NRF_CLOCK_LF_ACCURACY_250_PPM
    };
    uint32_t err = sd_softdevice_enable(&clk, sd_fault);
    // sd_softdevice_enable() returned — blink x7 FAST (no FreeRTOS needed)
    // RED x7 = err==0 (SD ok),  BLUE x7 = err!=0 (SD returned error)
    if (err == 0) {
        busy_blink(LED_RED_BIT,  7);   // FAST RED x7 = SD enable returned OK
        while (true) busy_blink(LED_RED_BIT, 7);
    } else {
        busy_blink(LED_BLUE_BIT, 7);   // FAST BLUE x7 = SD enable returned error
        while (true) busy_blink(LED_BLUE_BIT, 7);
    }
}

void loop() {}
