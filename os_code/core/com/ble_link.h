#pragma once
#include <stdbool.h>
#include <stdint.h>

// Start the BLE peripheral. Advertises as "Oiria".
// No-op if CONFIG_BT_ENABLED is off.
void ble_link_start(void);

// True once a central is connected.
bool ble_link_connected(void);

// Ask the link to prefer a long interval and send a heartbeat
// every `seconds`. The CPU may light-sleep between notifies
// if PM light sleep is enabled.
void ble_link_set_sleep_window(uint8_t seconds);
