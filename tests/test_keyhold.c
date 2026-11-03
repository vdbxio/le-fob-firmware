/*
 * Host-side check of the bindkey holds (firmware/keyhold.c) and the typing
 * sequence (firmware/hidtype.c).
 *
 * The holds must never fire unasked -- the key is a secret, and a rekey
 * un-pairs the fob -- and must fire every time they are asked. The typing
 * must produce exactly the 32 characters Home Assistant's bindkey field
 * accepts, and leave the host's NumLock as it found it.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/board.h"
#include "../firmware/keyhold.h"
#include "../firmware/hidtype.h"

static int      failures;
static uint32_t now;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

#define SW1 0x01
#define SW2 0x02
#define SW3 0x04

/* Hold `mask` for `ms`; return the first event seen, counting events. */
static int events;
static keyhold_event_t hold(uint8_t mask, int usb, uint32_t ms)
{
	keyhold_event_t first = KEYHOLD_NONE;

	for (uint32_t i = 0; i <= ms; i++) {
		keyhold_event_t e = keyhold_tick(mask, usb, ++now);

		if (e != KEYHOLD_NONE) {
			events++;
			if (first == KEYHOLD_NONE)
				first = e;
		}
	}
	return first;
}

static void release(void)
{
	hold(0, 1, 20);
}

static void test_holds(void)
{
	printf("\n== holds ==\n");

	release();
	check(hold(SW2, 1, KEY_EXPORT_HOLD_MS) == KEYHOLD_EXPORT, "SW2 alone on USB: export");
	events = 0;
	hold(SW2, 1, 3 * KEY_EXPORT_HOLD_MS);
	check(events == 0, "keep holding: fires once, not again");
	release();

	check(hold(SW2, 1, KEY_EXPORT_HOLD_MS - 10) == KEYHOLD_NONE, "let go early: nothing");
	release();

	check(hold(SW2, 1, KEY_EXPORT_ARM_MS - 5) == KEYHOLD_NONE && keyhold_progress() == 0,
	      "before the arm point: no fuse yet");
	hold(SW2, 1, 100);
	check(keyhold_progress() > 0 && keyhold_arming() == KEYHOLD_EXPORT,
	      "past the arm point: export fuse running");
	release();

	/* No USB: nothing, and USB arriving mid-hold does not rescue it. */
	check(hold(SW2, 0, KEY_EXPORT_HOLD_MS) == KEYHOLD_NONE, "no USB: nothing");
	check(hold(SW2, 1, KEY_EXPORT_HOLD_MS) == KEYHOLD_NONE,
	      "held through enumeration: still nothing until released");
	release();
	check(hold(SW2, 1, KEY_EXPORT_HOLD_MS) == KEYHOLD_EXPORT, "released and held again: export");
	release();

	/* Unplug mid-hold cancels. */
	hold(SW2, 1, KEY_EXPORT_HOLD_MS / 2);
	check(hold(SW2, 0, 10) == KEYHOLD_NONE && hold(SW2, 1, KEY_EXPORT_HOLD_MS) == KEYHOLD_NONE,
	      "unplugged mid-hold: cancelled for this press");
	release();

	/* Other pads. */
	check(hold(SW1, 1, REKEY_HOLD_MS) == KEYHOLD_NONE, "SW1 (bootloader pad): never ours");
	release();
	check(hold(SW2 | SW1, 1, REKEY_HOLD_MS) == KEYHOLD_NONE, "SW1+SW2: nothing");
	release();
	hold(SW2, 1, 100);
	check(hold(SW2 | SW1, 1, REKEY_HOLD_MS) == KEYHOLD_NONE &&
	      hold(SW2, 1, KEY_EXPORT_HOLD_MS) == KEYHOLD_NONE,
	      "SW1 joins an export hold: cancelled until release");
	release();

	/* Rekey. */
	events = 0;
	check(hold(SW2 | SW3, 1, REKEY_HOLD_MS) == KEYHOLD_REKEY && events == 1,
	      "SW2+SW3 on USB: rekey, once");
	release();

	hold(SW2, 1, KEY_EXPORT_HOLD_MS - 500); /* nearly an export ... */
	check(hold(SW2 | SW3, 1, KEY_EXPORT_HOLD_MS) == KEYHOLD_NONE,
	      "SW3 joins late: no export fires on the way");
	check(hold(SW2 | SW3, 1, REKEY_HOLD_MS) == KEYHOLD_REKEY,
	      "... and the rekey clock restarted from SW3");
	release();

	hold(SW3, 1, 300);
	check(hold(SW2 | SW3, 1, REKEY_HOLD_MS) == KEYHOLD_REKEY, "SW3 first, then SW2: rekey");
	release();

	hold(SW2 | SW3, 1, REKEY_HOLD_MS / 2);
	check(hold(SW2, 1, KEY_EXPORT_HOLD_MS + 100) == KEYHOLD_NONE,
	      "SW3 let go out of a rekey hold: not an export");
	release();

	check(hold(SW2 | SW3, 1, REKEY_HOLD_MS - 10) == KEYHOLD_NONE, "rekey let go early: nothing");
	release();
}

/* Run the typing to completion; collect pressed keys in order. */
static int type_all(uint8_t *keys, int max)
{
	uint8_t rep[8];
	int     n = 0, reports = 0, releases_ok = 1;
	uint8_t last_down = 0;

	for (int t = 0; t < 10000 && hidtype_busy(); t++) {
		if (hidtype_poll(++now, rep)) {
			reports++;
			if (rep[2]) {
				if (n < max)
					keys[n++] = rep[2];
				last_down = rep[2];
			} else if (!last_down) {
				releases_ok = 0; /* release with nothing pressed */
			} else {
				last_down = 0;
			}
			if (rep[0] || rep[1] || rep[3] || rep[4] || rep[5] || rep[6] || rep[7])
				releases_ok = 0; /* no modifiers, one key at a time */
		}
	}
	return releases_ok && reports == 2 * n ? n : -1;
}

static void test_typing(void)
{
	static const uint8_t key[16] = { 0x23, 0x1d, 0x39, 0xc1, 0xd7, 0xcc, 0x1a, 0xb1,
	                                 0xae, 0xe2, 0x24, 0xcd, 0x09, 0x6d, 0xb9, 0x32 };
	const char *want = "231d39c1d7cc1ab1aee224cd096db932";
	uint8_t     keys[40];
	int         n;

	printf("\n== typing ==\n");

	uint32_t t0 = now;
	hidtype_start(key, 0, now);
	{
		uint8_t rep[8];
		check(!hidtype_poll(now + HID_LEAD_MS - 1, rep), "nothing before the lead-in");
		now = t0 + HID_LEAD_MS - 1;
	}
	n = type_all(keys, sizeof(keys));
	check(n == 32, "NumLock on: exactly 32 keys, each pressed and released");

	{
		int ok = 1;

		for (int i = 0; i < 32 && n == 32; i++)
			if (keys[i] != hidtype_code_for(want[i]))
				ok = 0;
		check(ok, "they spell the key in lowercase hex");
	}
	check(hidtype_code_for('0') == HID_KEY_KP_0 && hidtype_code_for('1') == HID_KEY_KP_1 &&
	      hidtype_code_for('9') == HID_KEY_KP_1 + 8,
	      "digits use keypad keys (layout-proof)");
	check(hidtype_code_for('a') == HID_KEY_A && hidtype_code_for('f') == HID_KEY_A + 5,
	      "a-f use letter keys");
	check(hidtype_code_for('g') == 0 && hidtype_code_for('A') == 0, "anything else: no key");

	hidtype_start(key, 1, now);
	n = type_all(keys, sizeof(keys));
	check(n == 34 && keys[0] == HID_KEY_NUMLOCK && keys[33] == HID_KEY_NUMLOCK,
	      "NumLock off: NumLock pressed before and after");

	hidtype_start(key, 0, now);
	hidtype_abort();
	check(!hidtype_busy(), "abort stops it");
}

int main(void)
{
	printf("test_keyhold\n");
	test_holds();
	test_typing();
	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures ? 1 : 0;
}
