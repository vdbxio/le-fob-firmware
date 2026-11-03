#include "ch32fun.h"
#include "board.h"
#include "bthome.h"
#include "temp.h"
#include "ccm.h"
#include <string.h>

/* BTHome service data UUID, 0xFCD2, sent little-endian. */
#define BTHOME_UUID_LO 0xD2
#define BTHOME_UUID_HI 0xFC

/*
 * Device information byte.
 *   bit 0    encryption      0 = unencrypted
 *   bit 2    trigger based   1 = irregular, event-driven updates
 *   bits 5-7 BTHome version  0b010 = v2
 *
 * The trigger-based bit matters for how a receiver treats us: without it
 * Home Assistant expects updates on a regular cadence and will mark the
 * device unavailable between button presses. A fob that only speaks when
 * poked is exactly what that bit is for.
 */
#define BTHOME_DEVICE_INFO 0x44

#define BTHOME_OBJ_PACKET_ID 0x00
#define BTHOME_OBJ_TEMP      0x02  /* sint16, 0.01 C */
#define BTHOME_OBJ_VOLTAGE   0x0C  /* uint16, 0.001 V */
#define BTHOME_OBJ_BATT_LOW  0x15  /* binary, 1 = low */
#define BTHOME_OBJ_CHARGING  0x16  /* binary, 1 = charging */
#define BTHOME_OBJ_BUTTON    0x3A
#define BTHOME_OBJ_COUNT16   0x3D  /* uint16 count */
#define BTHOME_OBJ_TEXT      0x53  /* [id][len][utf-8...] */

/* WCH's assigned OUI, used to work out which way round the factory address
 * is stored. */
#define WCH_OUI_0 0x84
#define WCH_OUI_1 0xC2
#define WCH_OUI_2 0xE4

uint8_t bthome_resolve_address(uint8_t mac[6])
{
	const uint8_t *rom = LEFOB_ROM_MAC_ADDR;
	uint8_t        raw[6];

	for (int i = 0; i < 6; i++)
		raw[i] = rom[i];

	/*
	 * The factory address lives at 0x3F018, but WCH's documentation does
	 * not pin down its byte order, and a BLE address goes out least
	 * significant byte first. Rather than guess, detect: WCH's OUI is the
	 * *most* significant half of the address, so whichever end of this
	 * buffer holds 84:C2:E4 tells us the order we were given.
	 */
	if (raw[5] == WCH_OUI_0 && raw[4] == WCH_OUI_1 && raw[3] == WCH_OUI_2) {
		/* Already in over-the-air order. */
		memcpy(mac, raw, 6);
		return 0x02; /* ADV_NONCONN_IND, public address */
	}

	if (raw[0] == WCH_OUI_0 && raw[1] == WCH_OUI_1 && raw[2] == WCH_OUI_2) {
		/* Stored human-readable; reverse it. */
		for (int i = 0; i < 6; i++)
			mac[i] = raw[5 - i];
		return 0x02;
	}

	/*
	 * No recognisable factory address (blank part, or a batch WCH did not
	 * program). Synthesise a random static address from whatever is in the
	 * ROM config word anyway, so it is at least stable across reboots for
	 * this particular chip, and flag it as random.
	 *
	 * A random static address must have its two most significant bits set.
	 */
	for (int i = 0; i < 6; i++)
		mac[i] = raw[i] ^ (uint8_t)(0xA5 + i);
	mac[5] |= 0xC0;

	return 0x42; /* ADV_NONCONN_IND, TxAdd = 1 (random static address) */
}

int bthome_build_adv(uint8_t *out, uint8_t pdu_header, const uint8_t mac[6],
                     const uint8_t events[NUM_BUTTONS], uint8_t packet_id)
{
	int n = 0;

	/* --- BLE advertising PDU header --- */
	out[n++] = pdu_header;
	out[n++] = 0; /* payload length; iSLERTX() fills this in */

	/* Advertiser address, least significant byte first. */
	for (int i = 0; i < 6; i++)
		out[n++] = mac[i];

	/* --- AD structure: Flags --- */
	out[n++] = 0x02;
	out[n++] = 0x01;
	out[n++] = 0x06; /* LE General Discoverable, BR/EDR not supported */

	/* --- AD structure: Service Data, 16-bit UUID --- */
	/*
	 * Length is filled in after the objects are written. BTHome requires
	 * object IDs in ascending order, which is why the packet ID (0x00)
	 * precedes the button objects (0x3A).
	 */
	int len_at = n++;
	out[n++] = 0x16; /* Service Data - 16-bit UUID */
	out[n++] = BTHOME_UUID_LO;
	out[n++] = BTHOME_UUID_HI;
	out[n++] = BTHOME_DEVICE_INFO;

	out[n++] = BTHOME_OBJ_PACKET_ID;
	out[n++] = packet_id;

	/*
	 * One 0x3A object per physical button. BTHome maps repeated objects of
	 * the same type to "Button 1", "Button 2", ... in the order they appear,
	 * so all four are always present and the three that did nothing carry
	 * BTHOME_EVT_NONE. Sending only the button that fired would renumber
	 * the entities on every press.
	 */
	for (int i = 0; i < NUM_BUTTONS; i++) {
		out[n++] = BTHOME_OBJ_BUTTON;
		out[n++] = events[i];
	}

	out[len_at] = (uint8_t)(n - len_at - 1);

	/* --- AD structure: Complete Local Name --- */
	const char *name    = BTHOME_NAME;
	int         namelen = (int)strlen(name);

	out[n++] = (uint8_t)(namelen + 1);
	out[n++] = 0x09; /* Complete Local Name */
	for (int i = 0; i < namelen; i++)
		out[n++] = (uint8_t)name[i];

	return n;
}

int bthome_build_status_adv(uint8_t *out, uint8_t pdu_header,
                            const uint8_t mac[6], uint8_t packet_id,
                            const bthome_status_t *st)
{
	int n = 0;

	out[n++] = pdu_header;
	out[n++] = 0; /* filled in by iSLERTX() */
	for (int i = 0; i < 6; i++)
		out[n++] = mac[i];

	out[n++] = 0x02;
	out[n++] = 0x01;
	out[n++] = 0x06;

	int len_at = n++;
	out[n++] = 0x16;
	out[n++] = BTHOME_UUID_LO;
	out[n++] = BTHOME_UUID_HI;
	out[n++] = BTHOME_DEVICE_INFO;

	/* Ascending object ids: 0x00, 0x02, 0x0C, 0x15, 0x16, 0x53. No 0x3A
	 * objects -- a button reporting "none" tells the receiver nothing. */
	out[n++] = BTHOME_OBJ_PACKET_ID;
	out[n++] = packet_id;

	/* Left out, rather than sent as a wrong number, when the sensor did
	 * not answer. This brings the full frame to exactly 31 bytes. */
	if (st->temp_c100 != TEMP_INVALID) {
		out[n++] = BTHOME_OBJ_TEMP;
		out[n++] = (uint8_t)((uint16_t)st->temp_c100 & 0xFF);
		out[n++] = (uint8_t)((uint16_t)st->temp_c100 >> 8);
	}

	out[n++] = BTHOME_OBJ_VOLTAGE;
	out[n++] = (uint8_t)(st->supply_mv & 0xFF);
	out[n++] = (uint8_t)(st->supply_mv >> 8);

	out[n++] = BTHOME_OBJ_BATT_LOW;
	out[n++] = st->batt_low ? 1 : 0;

	out[n++] = BTHOME_OBJ_CHARGING;
	out[n++] = st->charging ? 1 : 0;

	const char *text = st->text ? st->text : "";
	int         tlen = (int)strlen(text);

	if (tlen > BTHOME_STATUS_TEXT_MAX)
		tlen = BTHOME_STATUS_TEXT_MAX;

	out[n++] = BTHOME_OBJ_TEXT;
	out[n++] = (uint8_t)tlen;
	for (int i = 0; i < tlen; i++)
		out[n++] = (uint8_t)text[i];

	out[len_at] = (uint8_t)(n - len_at - 1);

	return n;
}

/* ------------------------------------------------------------------ */
/* Encrypted frames. See bthome.h.                                    */
/* ------------------------------------------------------------------ */

static void frame_begin(bthome_frame_t *f, uint8_t pdu_header,
                        const uint8_t mac[6], uint8_t packet_id)
{
	uint8_t *o = f->buf;
	int      n = 0;

	o[n++] = pdu_header;
	o[n++] = 0; /* filled in by iSLERTX() */
	for (int i = 0; i < 6; i++)
		o[n++] = mac[i];

	o[n++] = 0x02;
	o[n++] = 0x01;
	o[n++] = 0x06;

	f->svc_len_at = n++;
	o[n++] = 0x16;
	o[n++] = BTHOME_UUID_LO;
	o[n++] = BTHOME_UUID_HI;
	o[n++] = BTHOME_DEVICE_INFO_ENC;
	f->payload_at = n;

	/* The packet id is the first object, and encrypted like the rest. */
	o[n++] = BTHOME_OBJ_PACKET_ID;
	o[n++] = packet_id;
	f->len = n;
}

static void frame_end(bthome_frame_t *f)
{
	f->buf[f->svc_len_at] = (uint8_t)(f->len - f->svc_len_at - 1);
}

void bthome_build_event_frame(bthome_frame_t *f, uint8_t pdu_header,
                              const uint8_t mac[6],
                              const uint8_t events[NUM_BUTTONS],
                              uint8_t packet_id)
{
	frame_begin(f, pdu_header, mac, packet_id);
	/* All four, always: see bthome_build_adv(). */
	for (int i = 0; i < NUM_BUTTONS; i++) {
		f->buf[f->len++] = BTHOME_OBJ_BUTTON;
		f->buf[f->len++] = events[i];
	}
	frame_end(f);
}

void bthome_build_power_frame(bthome_frame_t *f, uint8_t pdu_header,
                              const uint8_t mac[6], uint8_t packet_id,
                              const bthome_status_t *st)
{
	uint8_t *o = f->buf;

	frame_begin(f, pdu_header, mac, packet_id);

	/* Ascending object ids: 0x00, 0x02, 0x0C, 0x15, 0x16, 0x3D. */
	if (st->temp_c100 != TEMP_INVALID) {
		o[f->len++] = BTHOME_OBJ_TEMP;
		o[f->len++] = (uint8_t)((uint16_t)st->temp_c100 & 0xFF);
		o[f->len++] = (uint8_t)((uint16_t)st->temp_c100 >> 8);
	}
	o[f->len++] = BTHOME_OBJ_VOLTAGE;
	o[f->len++] = (uint8_t)(st->supply_mv & 0xFF);
	o[f->len++] = (uint8_t)(st->supply_mv >> 8);
	o[f->len++] = BTHOME_OBJ_BATT_LOW;
	o[f->len++] = st->batt_low ? 1 : 0;
	o[f->len++] = BTHOME_OBJ_CHARGING;
	o[f->len++] = st->charging ? 1 : 0;
	/*
	 * How many times the bindkey has been typed out. After setup it reads
	 * 1, or 2 with a saved copy; if it ever rises on its own, rekey. Home
	 * Assistant shows it as a count sensor to alert on (MVP spec 14).
	 */
	o[f->len++] = BTHOME_OBJ_COUNT16;
	o[f->len++] = (uint8_t)st->exposures;
	o[f->len++] = (uint8_t)(st->exposures >> 8);
	frame_end(f);
}

void bthome_build_text_frame(bthome_frame_t *f, uint8_t pdu_header,
                             const uint8_t mac[6], uint8_t packet_id,
                             const char *text)
{
	const char *t    = text ? text : "";
	int         tlen = (int)strlen(t);

	if (tlen > BTHOME_TEXT_FRAME_MAX)
		tlen = BTHOME_TEXT_FRAME_MAX;

	frame_begin(f, pdu_header, mac, packet_id);
	f->buf[f->len++] = BTHOME_OBJ_TEXT;
	f->buf[f->len++] = (uint8_t)tlen;
	for (int i = 0; i < tlen; i++)
		f->buf[f->len++] = (uint8_t)t[i];
	frame_end(f);
}

int bthome_seal(const bthome_frame_t *f, uint8_t *out, const uint8_t mac[6],
                const uint8_t key[16], uint32_t counter)
{
	uint8_t nonce[CCM_NONCE_LEN];
	int     n = f->len;

	memcpy(out, f->buf, (size_t)n);

	bthome_nonce(nonce, mac, BTHOME_DEVICE_INFO_ENC, counter);
	ccm_encrypt(key, nonce, out + f->payload_at, n - f->payload_at, out + n + 4);

	out[n++] = (uint8_t)counter;
	out[n++] = (uint8_t)(counter >> 8);
	out[n++] = (uint8_t)(counter >> 16);
	out[n++] = (uint8_t)(counter >> 24);
	n += CCM_MIC_LEN; /* written in place by ccm_encrypt() above */

	out[f->svc_len_at] = (uint8_t)(f->buf[f->svc_len_at] + 4 + CCM_MIC_LEN);
	return n;
}
