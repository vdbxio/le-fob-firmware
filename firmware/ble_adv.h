#ifndef LE_FOB_BLE_ADV_H
#define LE_FOB_BLE_ADV_H

#include <stdint.h>
#include "bthome.h"

/*
 * BTHome advertising over ch32fun's iSLER raw-radio layer.
 *
 * There is no BLE stack here and none is needed: a BTHome sensor is a
 * connectionless beacon, so this only ever transmits ADV_NONCONN_IND.
 *
 * With FOB_ENCRYPT every round is sealed under the next value of the
 * keystore's replay counter, and nothing at all goes out without a key and
 * a counter -- a fob that cannot persist its counter stays silent rather
 * than transmit from a value it might repeat.
 */

/* Bring up the radio and work out our advertising address. */
void ble_adv_init(void);

/* The bindkey to seal with (encrypted builds), or 0 to send nothing. */
void ble_adv_set_key(const uint8_t *key);

/*
 * Whether a counter top-up may write DataFlash right now. The main loop
 * clears this while the torch is on: flash programming must not share a
 * sagging rail with it. With writes held off the radio runs on what is
 * left of the reserved window, then goes quiet until they are allowed.
 */
void ble_adv_allow_flash_writes(int ok);

/* Queue one button event for broadcast. This replaces any burst still in
 * flight -- the newer gesture is the one the user cares about. */
void ble_adv_send_event(uint8_t button, uint8_t event);

/*
 * Queue a status chirp: packet id plus a BTHome text object carrying
 * `status_text`, no button objects, a shorter burst. Refreshes the
 * receiver's "last seen" and shows it a line a human can read.
 */
void ble_adv_send_heartbeat(const bthome_status_t *status);

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
