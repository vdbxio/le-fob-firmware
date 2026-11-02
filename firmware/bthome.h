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

/* What the periodic chirp reports. */
typedef struct {
	int16_t     temp_c100;  /* 0x02 temperature, 0.01 C; TEMP_INVALID omits it */
	uint16_t    supply_mv;  /* 0x0C voltage: the 3V3 rail, see battery.h */
	uint8_t     batt_low;   /* 0x15 battery low                          */
	uint8_t     charging;   /* 0x16 battery charging                     */
	const char *text;       /* 0x53 text: charger state, or why not      */
} bthome_status_t;

/*
 * Build a status advertisement: packet id, die temperature, supply voltage,
 * battery low, charging, and a text object, with no button objects. Used for the
 * periodic chirp, where every button would be "none" and the bytes are
 * better spent on state.
 *
 * There is no local name in this frame: the 31-byte legacy payload does not
 * have room for it and the battery objects. Button frames carry it, and the
 * receiver keeps the last name it saw for an address.
 *
 * Text longer than BTHOME_STATUS_TEXT_MAX is truncated.
 */
int bthome_build_status_adv(uint8_t *out, uint8_t pdu_header,
                            const uint8_t mac[6], uint8_t packet_id,
                            const bthome_status_t *st);

#define BTHOME_STATUS_TEXT_MAX 9

/* Largest value either builder can return, for sizing buffers. */
#define BTHOME_ADV_MAX 40

#endif /* LE_FOB_BTHOME_H */
