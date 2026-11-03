#ifndef LE_FOB_BTHOME_H
#define LE_FOB_BTHOME_H

#include <stdint.h>
#include "board.h"

/*
 * BTHome v2 advertisement builder: the plaintext frames first (what an
 * ENCRYPT=0 build sends), then the encrypted ones further down.
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
	uint16_t    exposures;  /* 0x3D count: bindkey readouts (encrypted only) */
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

/* ------------------------------------------------------------------ */
/* Encrypted frames                                                   */
/* ------------------------------------------------------------------ */

/*
 * An encrypted advertisement is built in two steps. A builder lays out the
 * frame with its payload still in plaintext; bthome_seal() then encrypts a
 * copy under a given counter, ready for the radio. The radio side keeps
 * the plaintext frame and seals it afresh for every round, so each repeat
 * goes out under its own counter value.
 *
 * Encryption costs 8 bytes of the 31-byte legacy payload (4 counter + 4
 * MIC), so the layouts differ from the plaintext ones above:
 *
 *   event   flags, packet id, four buttons. No local name: it no longer
 *           fits, and Home Assistant names the device by its MAC suffix.
 *   power   packet id, temperature, voltage, battery low, charging,
 *           bindkey exposure count (0x3D) -- exactly 31 bytes
 *   text    packet id, text of up to BTHOME_TEXT_FRAME_MAX characters
 *
 * The status chirp sends power and text as two frames with two packet
 * ids; Home Assistant merges both into the one device. The power frame is
 * full, so a fuel gauge on a later board gets a third frame of its own --
 * the radio side already queues frames in sequence.
 */

typedef struct {
	uint8_t buf[BTHOME_ADV_MAX];
	int     len;         /* bytes used, plaintext payload included */
	int     svc_len_at;  /* the service-data AD's length byte */
	int     payload_at;  /* first byte after the device-info byte */
} bthome_frame_t;

/* Characters a text frame carries once encrypted. */
#define BTHOME_TEXT_FRAME_MAX 11

/* Device-info byte for the encrypted frames: v2, trigger based, encrypted. */
#define BTHOME_DEVICE_INFO_ENC 0x45

void bthome_build_event_frame(bthome_frame_t *f, uint8_t pdu_header,
                              const uint8_t mac[6],
                              const uint8_t events[NUM_BUTTONS],
                              uint8_t packet_id);

void bthome_build_power_frame(bthome_frame_t *f, uint8_t pdu_header,
                              const uint8_t mac[6], uint8_t packet_id,
                              const bthome_status_t *st);

void bthome_build_text_frame(bthome_frame_t *f, uint8_t pdu_header,
                             const uint8_t mac[6], uint8_t packet_id,
                             const char *text);

/*
 * Encrypt a copy of `f` into `out` under `key` and `counter`, appending the
 * counter and MIC. Returns the length to hand to iSLERTX().
 */
int bthome_seal(const bthome_frame_t *f, uint8_t *out, const uint8_t mac[6],
                const uint8_t key[16], uint32_t counter);

#endif /* LE_FOB_BTHOME_H */
