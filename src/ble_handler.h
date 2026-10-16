#pragma once
#include <stdint.h>

// Called from setup() — starts BLE advertising and registers callbacks
void ble_handler_init();

// Called from loop() — processes any pending BLE events
void ble_handler_update();
