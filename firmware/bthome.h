#ifndef LE_FOB_BTHOME_H
#define LE_FOB_BTHOME_H

#include <stdint.h>
#include "board.h"

/*
 * BTHome v2 (unencrypted) advertisement builder.
 * Format per https://bthome.io/format/ — service data UUID 0xFCD2.
 */

/* Button event values for BTHome object 0x3A. */
#define BTHOME_EVT_NONE              0x00
#define BTHOME_EVT_PRESS             0x01
#define BTHOME_EVT_DOUBLE_PRESS      0x02
#define BTHOME_EVT_TRIPLE_PRESS      0x03
#define BTHOME_EVT_LONG_PRESS        0x04
#define BTHOME_EVT_LONG_DOUBLE_PRESS 0x05
#define BTHOME_EVT_LONG_TRIPLE_PRESS 0x06
#define BTHOME_EVT_HOLD_PRESS        0x80

/*
 * Resolve the address this fob advertises under, into `mac` (6 bytes, in
 * over-the-air order, least significant byte first).
 *
 * Returns the BLE PDU header byte to advertise with: 0x02 (ADV_NONCONN_IND,
 * public address) when the WCH factory address was usable, or 0x42 (the same
 * PDU with TxAdd set, i.e. a random static address) when it had to be
 * synthesised. See bthome.c.
 */
uint8_t bthome_resolve_address(uint8_t mac[6]);

/*
 * Build a complete advertisement into `out`.
 *
 * `events` holds one BTHOME_EVT_* per button; BTHOME_EVT_NONE means "this
 * button did nothing", which is how BTHome expresses an idle button while
 * still declaring it to the receiver.
 *
 * Returns the total number of bytes written, ready to hand to iSLERTX()
 * (which overwrites out[1] with the length itself).
 */
int bthome_build_adv(uint8_t *out, uint8_t pdu_header, const uint8_t mac[6],
                     const uint8_t events[NUM_BUTTONS], uint8_t packet_id);

/* Largest value bthome_build_adv() can return, for sizing buffers. */
#define BTHOME_ADV_MAX 40

#endif /* LE_FOB_BTHOME_H */
