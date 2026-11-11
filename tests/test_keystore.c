/*
 * Host-side check of firmware/keystore.c against the MVP spec's abuse
 * tests (section 18), on a fake DataFlash that can lose power part-way
 * through any write or erase.
 *
 * The property everything here comes back to: a counter value, once sent,
 * is never handed out again -- not after a reboot, a torn write, a corrupt
 * record or a rekey. A lower counter is the one failure that reopens
 * replay, and it would fail silently.
 *
 * Build and run with:  make -C tests
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/board.h"
#include "../firmware/keystore.h"
#include "stub/dataflash_fake.h"

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

static const uint8_t key_a[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const uint8_t key_b[16] = { 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
                                   0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF };

/* Small deterministic PRNG so a failure reproduces. */
static uint32_t rng = 0x1234567u;
static uint32_t rnd(uint32_t n)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng % n;
}

/* Same CRC-32 as keystore.c, to forge a record by hand. */
static uint32_t test_crc32(const uint8_t *p, int n)
{
	uint32_t c = 0xFFFFFFFFu;

	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

/* "Boot": scan the journal and open a fresh window. */
static void boot(void)
{
	g_df_cut_after = -1;
	keystore_open();
	keystore_reserve();
}

int main(void)
{
	uint32_t c = 0, prev;

	printf("test_keystore\n");

	/* --- spec test 3: erased DataFlash means no key, fresh start ------- */
	df_fake_wipe();
	check(keystore_open() == KS_EMPTY && keystore_key() == 0,
	      "erased DataFlash: no key");
	check(!keystore_next_counter(1, &c), "no key: refuses to hand out a counter");

	check(keystore_provision(key_a), "provision writes and verifies");
	check(keystore_key() && memcmp(keystore_key(), key_a, 16) == 0, "key is the one provisioned");
	check(keystore_next_counter(1, &c) && c == KS_COUNTER_START,
	      "first counter is KS_COUNTER_START");
	check(c >= 100, "never below Home Assistant's exempt band");

	/* --- survives a reboot, and resumes above everything sent ---------- */
	for (int i = 0; i < 10; i++)
		keystore_next_counter(1, &c);
	prev = c;
	boot();
	check(keystore_open() == KS_OK, "record found after reboot");
	keystore_reserve();
	check(memcmp(keystore_key(), key_a, 16) == 0, "same key after reboot");
	check(keystore_next_counter(1, &c) && c > prev, "counter resumes above the last one sent");

	/* --- the window tops itself up when writes are allowed ------------- */
	{
		int ok = 1, writes0 = g_df_writes;

		prev = c;
		for (uint32_t i = 0; i < 5 * KS_RESERVE; i++) {
			if (!keystore_next_counter(1, &c) || c != prev + 1)
				ok = 0;
			prev = c;
		}
		check(ok, "5 windows' worth handed out, strictly +1 each");
		check(g_df_writes - writes0 >= 5 && g_df_writes - writes0 <= 6,
		      "one flash write per KS_RESERVE values");
	}

	/* --- torch on: no writes; runs dry and refuses, never reuses -------- */
	{
		int handed = 0, writes0 = g_df_writes;

		while (keystore_next_counter(0, &c))
			handed++;
		check(g_df_writes == writes0, "may_write=0 never touches flash");
		check(handed > 0 && handed <= (int)KS_RESERVE, "runs out within the window");
		prev = c;
		check(keystore_next_counter(1, &c) && c == prev + 1,
		      "torch off again: tops up and carries on");
	}

	/* --- exposure count ------------------------------------------------- */
	check(keystore_exposures() == 0, "exposure count starts at 0");
	keystore_bump_exposure();
	keystore_bump_exposure();
	boot();
	check(keystore_exposures() == 2, "exposure count persists across reboot");

	/* --- rekey: new key, exposure 0, counter keeps climbing ------------- */
	keystore_next_counter(1, &prev);
	check(keystore_rekey(key_b), "rekey writes");
	check(memcmp(keystore_key(), key_b, 16) == 0 && keystore_exposures() == 0,
	      "rekey: new key, exposure back to 0");
	keystore_next_counter(1, &c);
	check(c > prev, "rekey: counter does not go back");
	boot();
	check(memcmp(keystore_key(), key_b, 16) == 0, "rekeyed key survives reboot");

	/* --- spec test 2: a corrupt newest record recovers, and high -------- */
	{
		keystore_next_counter(1, &prev);
		/* Find the newest record and flip a bit in it. */
		int newest = -1;
		uint32_t best = 0;

		for (int s = 0; s < (int)(KS_PAGES * 8); s++) {
			uint8_t *r = g_df + KS_FLASH_OFFSET + s * 32;
			uint32_t seq = r[0] | r[1] << 8 | r[2] << 16 | (uint32_t)r[3] << 24;

			if (seq != 0xFFFFFFFFu && (newest < 0 || seq > best)) {
				best   = seq;
				newest = s;
			}
		}
		g_df[KS_FLASH_OFFSET + newest * 32 + 5] ^= 0x10;
		boot();
		check(keystore_key() != 0, "corrupt newest record: falls back, still has a key");
		check(keystore_next_counter(1, &c) && c > prev,
		      "corrupt newest record: counter still above last sent");
	}

	/* --- spec test 1: yank power mid-write, many times ------------------ */
	{
		uint32_t sent_max = 0;
		int      ok = 1, cuts = 0;

		boot();
		for (int trial = 0; trial < 500; trial++) {
			/* Send a random run, with the power going at a random
			 * byte of some write along the way. */
			g_df_cut_after = (int)rnd(40);
			for (uint32_t n = rnd(3 * KS_RESERVE); n; n--) {
				if (!keystore_next_counter(1, &c))
					break; /* refused: nothing sent */
				if (c <= sent_max && sent_max)
					ok = 0;
				sent_max = c;
			}
			/* Also cut during the occasional exposure bump. */
			if (rnd(4) == 0)
				keystore_bump_exposure();
			if (g_df_cut_after == 0)
				cuts++;
			boot();
			if (!keystore_key())
				ok = 0;
		}
		keystore_next_counter(1, &c);
		check(cuts > 50, "power actually cut mid-write in many trials");
		check(ok && c > sent_max, "500 torn writes: never reused a counter, key kept");
	}

	/* --- wear: erases spread across the ring ---------------------------- */
	{
		int min = 1 << 30, max = 0;

		df_fake_wipe();
		keystore_open();
		keystore_provision(key_a);
		for (int i = 0; i < 2000; i++)
			keystore_bump_exposure();
		for (int p = KS_FLASH_OFFSET / 256; p < (int)(KS_FLASH_OFFSET / 256 + KS_PAGES); p++) {
			if (g_df_erase_count[p] < min) min = g_df_erase_count[p];
			if (g_df_erase_count[p] > max) max = g_df_erase_count[p];
		}
		check(max - min <= 1 && max <= 2000 / 8 / (int)KS_PAGES + 1,
		      "2000 writes: every page erased evenly, once per 8 records");
		check(g_df_erase_count[KS_FLASH_OFFSET / 256 + KS_PAGES] == 0,
		      "nothing outside the journal is erased");
	}

	/* --- the ceiling: stops short of 2^32 rather than wrapping ---------- */
	{
		/* Forge a record whose window sits 10 values under the ceiling. */
		uint8_t r[32];
		uint32_t lim = 0xFFFFFF00u - 10;
		int handed = 0;

		df_fake_wipe();
		memset(r, 0xFF, sizeof(r));
		r[0] = 1; r[1] = r[2] = r[3] = 0;
		memcpy(r + 4, key_a, 16);
		r[20] = (uint8_t)lim; r[21] = (uint8_t)(lim >> 8);
		r[22] = (uint8_t)(lim >> 16); r[23] = (uint8_t)(lim >> 24);
		r[24] = r[25] = 0;
		r[26] = 1;
		uint32_t crc = test_crc32(r, 28);
		r[28] = (uint8_t)crc; r[29] = (uint8_t)(crc >> 8);
		r[30] = (uint8_t)(crc >> 16); r[31] = (uint8_t)(crc >> 24);
		memcpy(g_df + KS_FLASH_OFFSET, r, 32);

		boot();
		while (keystore_next_counter(1, &c) && handed < 100)
			handed++;
		check(handed == 10 && c == 0xFFFFFF00u - 1,
		      "near 2^32: hands out what is left, then stops");
		boot();
		check(!keystore_next_counter(1, &c), "and stays stopped across a reboot");
		check(keystore_rekey(key_b) && !keystore_next_counter(1, &c),
		      "rekey alone does not restart a spent counter");
	}

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures != 0;
}
