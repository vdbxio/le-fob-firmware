#ifndef LE_FOB_BLE_ADV_H
#define LE_FOB_BLE_ADV_H

#include <stdint.h>

/*
 * BTHome advertising over ch32fun's iSLER raw-radio layer.
 *
 * There is no BLE stack here and none is needed: a BTHome sensor is a
 * connectionless beacon, so this only ever transmits ADV_NONCONN_IND.
 */

/* Bring up the radio and work out our advertising address. */
void ble_adv_init(void);

/* Queue one button event for broadcast. This replaces any burst still in
 * flight -- the newer gesture is the one the user cares about. */
void ble_adv_send_event(uint8_t button, uint8_t event);

/*
 * Queue a status chirp: the same frame with every button reporting "none",
 * a fresh packet ID, and a shorter burst. Its job is to refresh the
 * receiver's "last seen"; later it will carry charge state too.
 */
void ble_adv_send_heartbeat(void);

/* Drive the burst. Transmits at most one round (three channels) per call
 * and only when ADV_ROUND_INTERVAL_MS has elapsed. */
void ble_adv_tick(uint32_t now);

/* Nonzero while a burst is still going out. */
int ble_adv_busy(void);

/*
 * Which pad the burst currently on air belongs to (0..NUM_BUTTONS-1), or -1
 * when the radio is idle. Drives the "on air" LED layer.
 */
int ble_adv_tx_button(void);

/* Re-initialise the radio after deep sleep, which leaves it in an odd
 * state. Harmless to call when we did not actually sleep. */
void ble_adv_resume(void);

/* The address we advertise under, in over-the-air order, for logging. */
const uint8_t *ble_adv_mac(void);

/* Nonzero if that address is a synthesised random static one rather than
 * the WCH factory address. */
int ble_adv_mac_is_random(void);

#endif /* LE_FOB_BLE_ADV_H */
