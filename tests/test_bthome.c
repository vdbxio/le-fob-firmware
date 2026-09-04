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

	printf("\n== status frame (text object) ==\n");

	int n = bthome_build_status_adv(out, 0x02, mac, 0x2A, "USB chg");

	hexdump("status packet", out, n);

	const uint8_t *sd = out + 11;

	check(sd[1] == 0x16 && sd[2] == 0xD2 && sd[3] == 0xFC && sd[4] == 0x44,
	      "same service-data framing as the button frame");
	check(sd[5] == 0x00 && sd[6] == 0x2A, "packet id first");
	check(sd[7] == 0x53, "then the text object, 0x53");
	check(sd[8] == 7, "length byte is the text length");
	check(memcmp(sd + 9, "USB chg", 7) == 0, "UTF-8 payload verbatim");
	check(sd[0] == 1 + 2 + 1 + 2 + 2 + 7, "service data length byte");

	/* No button objects: a "none" event carries nothing, and the bytes
	 * are the whole text budget. */
	int buttons = 0;
	for (int i = 5; i < 5 + sd[0] - 1; i++)
		if (sd[i] == 0x3A) buttons++;
	check(buttons == 0 || sd[7] != 0x3A, "no 0x3A button objects in a chirp");

	/* Name survives, so discovery still works from a chirp alone. */
	const uint8_t *nm = sd + 1 + sd[0];
	check(nm[1] == 0x09 && memcmp(nm + 2, BTHOME_NAME, strlen(BTHOME_NAME)) == 0,
	      "local name still present");

	check(n - 8 <= 31, "payload within the 31-byte legacy limit");

	printf("\n== text budget ==\n");

	/* Exactly the maximum fills the payload to the byte. */
	char maxtext[BTHOME_STATUS_TEXT_MAX + 1];
	memset(maxtext, 'x', BTHOME_STATUS_TEXT_MAX); maxtext[BTHOME_STATUS_TEXT_MAX] = 0;
	n = bthome_build_status_adv(out, 0x02, mac, 1, maxtext);
	printf("  %d-char text -> payload %d of 31\n", BTHOME_STATUS_TEXT_MAX, n - 8);
	check(n - 8 == 31, "the maximum text fills the payload exactly");

	/* One more character must be truncated, not overflow. */
	char over[BTHOME_STATUS_TEXT_MAX + 6];
	memset(over, 'y', sizeof(over) - 1); over[sizeof(over) - 1] = 0;
	n = bthome_build_status_adv(out, 0x02, mac, 1, over);
	check(n - 8 == 31 && out[11 + 8] == BTHOME_STATUS_TEXT_MAX,
	      "longer text is truncated to the budget, never over 31");
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

int main(void)
{
	test_layout();
	test_status_frame();
	test_address_order();

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
