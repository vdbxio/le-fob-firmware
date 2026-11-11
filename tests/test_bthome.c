/*
 * Host-side check of the BTHome v2 packet builder.
 *
 * Verifies the byte layout against the worked example in the BTHome spec
 * (https://bthome.io/format/) rather than against what bthome.c happens to
 * emit: lengths, the service-data UUID and device-info byte, ascending
 * object IDs, and the advertiser-address byte order logic.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/board.h"
#include "../firmware/bthome.h"
#include "../firmware/temp.h"
#include "../firmware/ccm.h"

/* Storage that LEFOB_ROM_MAC_ADDR is redirected at (see tests/Makefile). */
uint8_t g_fake_rom[6];

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

static void hexdump(const char *label, const uint8_t *p, int n)
{
	printf("%s (%d bytes):\n  ", label, n);
	for (int i = 0; i < n; i++)
		printf("%02X%s", p[i], (i % 16 == 15) ? "\n  " : " ");
	printf("\n");
}

static void test_layout(void)
{
	uint8_t out[64];
	uint8_t mac[6]    = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
	uint8_t events[4] = { BTHOME_EVT_NONE, BTHOME_EVT_DOUBLE_PRESS,
	                      BTHOME_EVT_NONE, BTHOME_EVT_NONE };

	printf("\n== advertisement layout ==\n");

	int n = bthome_build_adv(out, 0x02, mac, events, 0x2A);

	hexdump("packet", out, n);

	check(n <= BTHOME_ADV_MAX, "fits in BTHOME_ADV_MAX");
	check(out[0] == 0x02, "PDU header is ADV_NONCONN_IND");

	/* Advertiser address, least significant byte first. */
	check(memcmp(out + 2, mac, 6) == 0, "address copied in over-the-air order");

	/* Flags AD structure. */
	check(out[8] == 0x02 && out[9] == 0x01 && out[10] == 0x06,
	      "flags AD = 02 01 06");

	/* Service data AD structure starts at 11. */
	const uint8_t *sd = out + 11;

	check(sd[1] == 0x16, "service data AD type 0x16");
	check(sd[2] == 0xD2 && sd[3] == 0xFC, "UUID 0xFCD2, little-endian");
	check(sd[4] == 0x44,
	      "device info 0x44 (v2, unencrypted, trigger based)");

	/* Objects: packet id first (0x00), then four button objects (0x3A). */
	check(sd[5] == 0x00 && sd[6] == 0x2A, "object 0x00 carries packet id");

	int ok_objects = 1;
	for (int i = 0; i < 4; i++) {
		if (sd[7 + i * 2] != 0x3A)
			ok_objects = 0;
		if (sd[8 + i * 2] != events[i])
			ok_objects = 0;
	}
	check(ok_objects, "four 0x3A button objects, in button order");

	/* Length byte covers everything after itself in this AD structure. */
	int sd_len = sd[0];
	check(sd_len == 1 + 2 + 1 + 2 + 8,
	      "service data length byte counts type+uuid+info+objects");

	/* Name AD structure follows the service data. */
	const uint8_t *nm = sd + 1 + sd_len;

	check(nm[1] == 0x09, "name AD type 0x09 (complete local name)");
	check(nm[0] == strlen(BTHOME_NAME) + 1, "name length byte");
	check(memcmp(nm + 2, BTHOME_NAME, strlen(BTHOME_NAME)) == 0,
	      "name payload");

	/* The advertising payload (everything after the 6-byte address) has to
	 * fit the 31-byte legacy limit. */
	int payload = n - 8;

	printf("  advertising payload: %d of 31 bytes\n", payload);
	check(payload <= 31, "payload within the 31-byte legacy limit");

	/* Objects must ascend, which is why packet id (0x00) precedes 0x3A. */
	check(sd[5] < sd[7], "object IDs ascend");
}

static void test_status_frame(void)
{
	uint8_t out[64];
	uint8_t mac[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
	bthome_status_t st = { .temp_c100 = 2345, .supply_mv = 3297,
	                       .batt_low = 1, .charging = 0, .text = "USB chg" };

	printf("\n== status frame ==\n");

	int n = bthome_build_status_adv(out, 0x02, mac, 0x2A, &st);

	hexdump("status packet", out, n);

	const uint8_t *sd = out + 11;

	check(sd[1] == 0x16 && sd[2] == 0xD2 && sd[3] == 0xFC && sd[4] == 0x44,
	      "same service-data framing as the button frame");
	check(sd[5] == 0x00 && sd[6] == 0x2A, "packet id first");
	check(sd[7] == 0x02 && (int16_t)(sd[8] | sd[9] << 8) == 2345,
	      "then temperature, 0x02, sint16 0.01 C little-endian");
	check(sd[10] == 0x0C && (sd[11] | sd[12] << 8) == 3297,
	      "then voltage, 0x0C, uint16 mV little-endian");
	check(sd[13] == 0x15 && sd[14] == 1, "then battery low, 0x15");
	check(sd[15] == 0x16 && sd[16] == 0, "then charging, 0x16");
	check(sd[17] == 0x53, "then the text object, 0x53");
	check(sd[18] == 7, "length byte is the text length");
	check(memcmp(sd + 19, "USB chg", 7) == 0, "UTF-8 payload verbatim");
	check(sd[0] == 4 + 2 + 3 + 3 + 2 + 2 + 2 + 7, "service data length byte");
	check(sd[5] < sd[7] && sd[7] < sd[10] && sd[10] < sd[13] &&
	      sd[13] < sd[15] && sd[15] < sd[17], "object IDs ascend");

	/* No button objects: a "none" event carries nothing. */
	int buttons = 0;
	for (int i = 5; i < 1 + sd[0]; i += (sd[i] == 0x53) ? 2 + sd[i + 1] :
	                                    (sd[i] == 0x02 || sd[i] == 0x0C) ? 3 : 2)
		if (sd[i] == 0x3A) buttons++;
	check(buttons == 0, "no 0x3A button objects in a chirp");

	/* The service data is the last AD structure: no room for the name. */
	check(11 + 1 + sd[0] == n, "service data ends the payload");
	check(n - 8 <= 31, "payload within the 31-byte legacy limit");

	/* Negative temperatures survive the two's-complement packing. */
	st.temp_c100 = -1250;
	bthome_build_status_adv(out, 0x02, mac, 1, &st);
	check((int16_t)(sd[8] | sd[9] << 8) == -1250, "-12.50 C packs as sint16");

	/* No reading: the object is left out and everything shifts up. */
	st.temp_c100 = TEMP_INVALID;
	int n_notemp = bthome_build_status_adv(out, 0x02, mac, 1, &st);
	check(sd[7] == 0x0C && n_notemp == n - 3,
	      "an invalid temperature is omitted, not sent");
	st.temp_c100 = 2345;

	printf("\n== text budget ==\n");

	char maxtext[BTHOME_STATUS_TEXT_MAX + 1];
	memset(maxtext, 'x', BTHOME_STATUS_TEXT_MAX); maxtext[BTHOME_STATUS_TEXT_MAX] = 0;
	st.text = maxtext;
	n = bthome_build_status_adv(out, 0x02, mac, 1, &st);
	printf("  %d-char text -> payload %d of 31\n", BTHOME_STATUS_TEXT_MAX, n - 8);
	check(n - 8 == 31, "the maximum text fills the payload exactly");

	char over[BTHOME_STATUS_TEXT_MAX + 6];
	memset(over, 'y', sizeof(over) - 1); over[sizeof(over) - 1] = 0;
	st.text = over;
	int n2 = bthome_build_status_adv(out, 0x02, mac, 1, &st);
	check(n2 == n && sd[18] == BTHOME_STATUS_TEXT_MAX,
	      "longer text is truncated to the budget, never over 31");

	st.text = 0;
	bthome_build_status_adv(out, 0x02, mac, 1, &st);
	check(sd[18] == 0, "a missing text is sent as empty");
}

static void test_address_order(void)
{
	uint8_t mac[6];
	uint8_t hdr;

	printf("\n== advertiser address resolution ==\n");

	/* Factory address already in over-the-air order (OUI at the top end). */
	uint8_t ota[6] = { 0x11, 0x22, 0x33, 0xE4, 0xC2, 0x84 };

	memcpy(g_fake_rom, ota, 6);
	hdr = bthome_resolve_address(mac);
	check(hdr == 0x02 && memcmp(mac, ota, 6) == 0,
	      "OTA-order factory address used as-is, public");

	/* Same address stored human-readable; must come back reversed. */
	uint8_t human[6] = { 0x84, 0xC2, 0xE4, 0x33, 0x22, 0x11 };

	memcpy(g_fake_rom, human, 6);
	hdr = bthome_resolve_address(mac);
	check(hdr == 0x02 && memcmp(mac, ota, 6) == 0,
	      "human-order factory address reversed to OTA order");

	/* Blank part: expect a synthesised random static address. */
	memset(g_fake_rom, 0xFF, 6);
	hdr = bthome_resolve_address(mac);
	check(hdr == 0x42, "blank ROM falls back to random static");
	check((mac[5] & 0xC0) == 0xC0,
	      "random static address has both top bits set");

	/* And it must be stable for a given chip. */
	uint8_t first[6];

	memcpy(first, mac, 6);
	bthome_resolve_address(mac);
	check(memcmp(first, mac, 6) == 0, "synthesised address is stable");
}

/*
 * Decrypt a sealed frame the way a receiver would -- nonce from the MAC,
 * the device-info byte and the counter it carries -- and hand back the
 * plaintext objects. Returns their length, or -1 if the MIC failed.
 */
static int unseal(const uint8_t *out, int n, const uint8_t mac[6],
                  const uint8_t key[16], uint8_t *plain, uint32_t *counter)
{
	/* Service-data AD starts after header, MAC and flags. */
	int svc = 2 + 6 + 3;
	int ad_end = svc + 1 + out[svc];
	int info_at = svc + 4;
	int payload_at = info_at + 1;
	int cipher_len = ad_end - payload_at - 8;
	const uint8_t *ctr = out + ad_end - 8;
	uint8_t nonce[CCM_NONCE_LEN];

	if (ad_end != n || cipher_len < 0)
		return -1;
	*counter = (uint32_t)ctr[0] | (uint32_t)ctr[1] << 8 |
	           (uint32_t)ctr[2] << 16 | (uint32_t)ctr[3] << 24;
	memcpy(plain, out + payload_at, (size_t)cipher_len);
	bthome_nonce(nonce, mac, out[info_at], *counter);
	if (!ccm_decrypt(key, nonce, plain, cipher_len, out + ad_end - 4))
		return -1;
	return cipher_len;
}

static void test_encrypted_frames(void)
{
	static const uint8_t key[16] = { 0x23, 0x1d, 0x39, 0xc1, 0xd7, 0xcc, 0x1a, 0xb1,
	                                 0xae, 0xe2, 0x24, 0xcd, 0x09, 0x6d, 0xb9, 0x32 };
	uint8_t mac[6] = { 0xA5, 0x80, 0x8F, 0xE6, 0x48, 0x54 };
	uint8_t events[4] = { BTHOME_EVT_NONE, BTHOME_EVT_LONG_PRESS,
	                      BTHOME_EVT_NONE, BTHOME_EVT_NONE };
	bthome_frame_t f;
	uint8_t out[BTHOME_ADV_MAX], plain[32];
	uint32_t ctr;
	int n, p;

	printf("\n== encrypted frames ==\n");

	/* Event frame */
	bthome_build_event_frame(&f, 0x02, mac, events, 0x2A);
	n = bthome_seal(&f, out, mac, key, 1000);
	hexdump("event, sealed", out, n);
	check(n - 8 <= 31, "event frame: AD payload within 31 bytes");
	check(out[2 + 6 + 3 + 4] == 0x45, "device info 0x45: v2, trigger, encrypted");
	p = unseal(out, n, mac, key, plain, &ctr);
	{
		const uint8_t want[] = { 0x00, 0x2A, 0x3A, 0, 0x3A, 4, 0x3A, 0, 0x3A, 0 };
		check(p == (int)sizeof(want) && memcmp(plain, want, sizeof(want)) == 0 && ctr == 1000,
		      "event frame decrypts to packet id + four buttons");
	}
	check(memcmp(out + 2 + 6 + 3 + 5, plain, 4) != 0, "payload on air is not the plaintext");

	/* Same frame, next counter: different bytes, still decrypts. */
	{
		uint8_t out2[BTHOME_ADV_MAX];
		int n2 = bthome_seal(&f, out2, mac, key, 1001);

		check(n2 == n && memcmp(out, out2, (size_t)n) != 0,
		      "resealed under the next counter: different bytes");
		check(unseal(out2, n2, mac, key, plain, &ctr) > 0 && ctr == 1001,
		      "and it decrypts with counter 1001");
	}

	/* Wrong key: rejected. */
	{
		uint8_t bad[16];

		memcpy(bad, key, 16);
		bad[0] ^= 1;
		check(unseal(out, n, mac, bad, plain, &ctr) < 0, "wrong key: MIC fails");
	}

	/* Power frame, worst case (temperature present). */
	bthome_status_t st = { .temp_c100 = 2345, .supply_mv = 3300, .batt_low = 1,
	                       .charging = 1, .text = "USB chg", .exposures = 0x0102 };

	bthome_build_power_frame(&f, 0x02, mac, 7, &st);
	n = bthome_seal(&f, out, mac, key, 5000);
	hexdump("power, sealed", out, n);
	check(n - 8 <= 31, "power frame: AD payload within 31 bytes");
	p = unseal(out, n, mac, key, plain, &ctr);
	{
		const uint8_t want[] = { 0x00, 7, 0x02, 0x29, 0x09, 0x0C, 0xE4, 0x0C,
		                         0x15, 1, 0x16, 1, 0x3D, 0x02, 0x01 };
		check(p == (int)sizeof(want) && memcmp(plain, want, sizeof(want)) == 0,
		      "power frame: id, temp, volt, low, chg, exposure count");
	}
	check(n - 8 == 31, "power frame with temperature is exactly 31 bytes");

	/* Text frame at the full encrypted length, and one over. */
	bthome_build_text_frame(&f, 0x02, mac, 8, "0123456789AB");
	n = bthome_seal(&f, out, mac, key, 5001);
	hexdump("text, sealed", out, n);
	check(n - 8 <= 31, "text frame: AD payload within 31 bytes");
	p = unseal(out, n, mac, key, plain, &ctr);
	check(p == 4 + BTHOME_TEXT_FRAME_MAX && plain[2] == 0x53 &&
	      plain[3] == BTHOME_TEXT_FRAME_MAX &&
	      memcmp(plain + 4, "0123456789A", BTHOME_TEXT_FRAME_MAX) == 0,
	      "text frame: truncated to 11 characters");
	check(n - 8 == 31, "text frame at 11 characters is exactly 31 bytes");
}

int main(void)
{
	test_layout();
	test_encrypted_frames();
	test_status_frame();
	test_address_order();

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
